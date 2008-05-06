/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2007, Michal Pryc (Michal.Pryc@Sun.Com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */
#include "config.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <zlib.h>
#include <math.h>

#ifdef OS_WIN32
#include <conio.h>
#include "mingw-compat.h"
#else
#include <sys/resource.h>
#endif

#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <glib/gpattern.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-os-dependant.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-utils.h>


#include "tracker-dbus.h"
#include "tracker-utils.h"
#include "tracker-indexer.h"
#include "tracker-process-files.h"

extern Tracker	*tracker;

#define ZLIBBUFSIZ 8192
#define MAX_INDEX_FILE_SIZE 2000000000

static int info_allocated = 0;
static int info_deallocated = 0;

char *
tracker_get_radix_by_suffix (const char *str, const char *suffix)
{
	g_return_val_if_fail (str, NULL);
	g_return_val_if_fail (suffix, NULL);

	if (g_str_has_suffix (str, suffix)) {
		return g_strndup (str, g_strrstr (str, suffix) - str);
	} else {
		return NULL;
	}
}


char *
tracker_escape_metadata (const char *in)
{
	if (!in) {
		return NULL;
	}

	GString *gs = g_string_new ("");

	for(; *in; in++) {
		if (*in == '|') {
			g_string_append_c (gs, 30);
		} else {
			g_string_append_c (gs, *in);
		}
	}

	return g_string_free (gs, FALSE);
}



char *
tracker_unescape_metadata (const char *in)
{
	if (!in) {
		return NULL;
	}

	GString *gs = g_string_new ("");

	for(; *in; in++) {
		if (*in == 30) {
			g_string_append_c (gs, '|');
		} else {
			g_string_append_c (gs, *in);
		}
	}

	return g_string_free (gs, FALSE);
}

char *
tracker_format_search_terms (const char *str, gboolean *do_bool_search)
{
	char *def_prefix;
	char **terms;

	*do_bool_search = FALSE;

	def_prefix = "+";

	if (strlen (str) < 3) {
		return g_strdup (str);
	}

	/* if already has quotes then do nothing */
	if (strchr (str, '"') || strchr (str, '*')) {
		*do_bool_search = TRUE;
		return g_strdup (str);
	}

	if (strstr (str, " or ")) {
		def_prefix = " ";
	}

	terms = g_strsplit (str, " ", -1);

	if (terms) {
		GString *search_term;
		char	**st;
		char	*prefix;

		search_term = g_string_new (" ");

		for (st = terms; *st; st++) {

			if (*st[0] == '-') {
				prefix = " ";
			} else {
				prefix = def_prefix;
			}

			if ((*st[0] != '-') && strchr (*st, '-')) {
				char *s;

				*do_bool_search = TRUE;

				s = g_strconcat ("\"", *st, "\"", NULL);

				g_string_append (search_term, s);

				g_free (s);

			} else {
				g_string_append_printf (search_term, " %s%s ", prefix, *st);
			}
		}

		g_strfreev (terms);

		return g_string_free (search_term, FALSE);
	}

	return g_strdup (str);
}


void
tracker_print_object_allocations (void)
{
	tracker_log ("Total allocations = %d, total deallocations = %d", info_allocated, info_deallocated);
}

void
tracker_throttle (int multiplier)
{
	gint throttle;

	throttle = tracker_config_get_throttle (tracker->config);

	if (throttle < 1) {
		return;
	}

 	throttle *= multiplier;

	if (throttle > 0) {
  		g_usleep (throttle);
	}
}


void
tracker_notify_file_data_available (void)
{
	if (!tracker->is_running) {
		return;
	}

	/* if file thread is asleep then we just need to wake it up! */
	if (g_mutex_trylock (tracker->files_signal_mutex)) {
		g_cond_signal (tracker->files_signal_cond);
		g_mutex_unlock (tracker->files_signal_mutex);
		return;
	}

	/* if busy - check if async queue has new stuff as we do not need to notify then */
	if (g_async_queue_length (tracker->file_process_queue) > 1) {
		return;
	}

	/* if file thread not in check phase then we need do nothing */
	if (g_mutex_trylock (tracker->files_check_mutex)) {
		g_mutex_unlock (tracker->files_check_mutex);
		return;
	}

	int revs = 0;

	/* we are in check phase - we need to wait until either check_mutex is unlocked or file thread is asleep then awaken it */
	while (revs < 100000) {

		if (g_mutex_trylock (tracker->files_check_mutex)) {
			g_mutex_unlock (tracker->files_check_mutex);
			return;
		}

		if (g_mutex_trylock (tracker->files_signal_mutex)) {
			g_cond_signal (tracker->files_signal_cond);
			g_mutex_unlock (tracker->files_signal_mutex);
			return;
		}

		g_thread_yield ();
		g_usleep (10);
		revs++;
	}
}


void
tracker_notify_meta_data_available (void)
{
	if (!tracker->is_running) {
		return;
	}

	/* if metadata thread is asleep then we just need to wake it up! */
	if (g_mutex_trylock (tracker->metadata_signal_mutex)) {
		g_cond_signal (tracker->metadata_signal_cond);
		g_mutex_unlock (tracker->metadata_signal_mutex);
		return;
	}

	/* if busy - check if async queue has new stuff as we do not need to notify then */
	if (g_async_queue_length (tracker->file_metadata_queue) > 1) {
		return;
	}

	/* if metadata thread not in check phase then we need do nothing */
	if (g_mutex_trylock (tracker->metadata_check_mutex)) {
		g_mutex_unlock (tracker->metadata_check_mutex);
		return;
	}

	/* we are in check phase - we need to wait until either check_mutex is unlocked or until metadata thread is asleep then we awaken it */
	while (TRUE) {

		if (g_mutex_trylock (tracker->metadata_check_mutex)) {
			g_mutex_unlock (tracker->metadata_check_mutex);
			return;
		}

		if (g_mutex_trylock (tracker->metadata_signal_mutex)) {
			g_cond_signal (tracker->metadata_signal_cond);
			g_mutex_unlock (tracker->metadata_signal_mutex);
			return;
		}

		g_thread_yield ();
		g_usleep (10);
		tracker_debug ("in check phase");
	}
}

char *
tracker_compress (const char *ptr, int size, int *compressed_size)
{
	z_stream zs;
	char *buf, *swap;
	unsigned char obuf[ZLIBBUFSIZ];
	int rv, asiz, bsiz, osiz;
	if (size < 0) size = strlen (ptr);
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	if (deflateInit2 (&zs, 6, Z_DEFLATED, 15, 6, Z_DEFAULT_STRATEGY) != Z_OK) {
		return NULL;
	}

	asiz = size + 16;
	if (asiz < ZLIBBUFSIZ) asiz = ZLIBBUFSIZ;
	if (!(buf = malloc (asiz))) {
		deflateEnd (&zs);
		return NULL;
	}
	bsiz = 0;
	zs.next_in = (unsigned char *)ptr;
	zs.avail_in = size;
	zs.next_out = obuf;
	zs.avail_out = ZLIBBUFSIZ;
	while ( (rv = deflate (&zs, Z_FINISH)) == Z_OK) {
		osiz = ZLIBBUFSIZ - zs.avail_out;
		if (bsiz + osiz > asiz) {
			asiz = asiz * 2 + osiz;
			if (!(swap = realloc (buf, asiz))) {
				free (buf);
				deflateEnd (&zs);
				return NULL;
			}
			buf = swap;
		}
		memcpy (buf + bsiz, obuf, osiz);
		bsiz += osiz;
		zs.next_out = obuf;
		zs.avail_out = ZLIBBUFSIZ;
	}
	if (rv != Z_STREAM_END) {
		free (buf);
		deflateEnd (&zs);
		return NULL;
	}

	osiz = ZLIBBUFSIZ - zs.avail_out;

	if (bsiz + osiz + 1 > asiz) {
		asiz = asiz * 2 + osiz;

		if (!(swap = realloc (buf, asiz))) {
			free (buf);
			deflateEnd (&zs);
			return NULL;
		}
		buf = swap;
	}

	memcpy (buf + bsiz, obuf, osiz);
	bsiz += osiz;
	buf[bsiz] = '\0';

	*compressed_size = bsiz;

	deflateEnd (&zs);

	return buf;
}


char *
tracker_uncompress (const char *ptr, int size, int *uncompressed_size)
{
	z_stream zs;
	char *buf, *swap;
	unsigned char obuf[ZLIBBUFSIZ];
	int rv, asiz, bsiz, osiz;
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	if (inflateInit2 (&zs, 15) != Z_OK) return NULL;

	asiz = size * 2 + 16;
	if (asiz < ZLIBBUFSIZ) asiz = ZLIBBUFSIZ;
	if (!(buf = malloc (asiz))) {
		inflateEnd (&zs);
		return NULL;
	}
	bsiz = 0;
	zs.next_in = (unsigned char *)ptr;
	zs.avail_in = size;
	zs.next_out = obuf;
	zs.avail_out = ZLIBBUFSIZ;
	while ( (rv = inflate (&zs, Z_NO_FLUSH)) == Z_OK) {
		osiz = ZLIBBUFSIZ - zs.avail_out;
		if (bsiz + osiz >= asiz) {
			asiz = asiz * 2 + osiz;
			if (!(swap = realloc (buf, asiz))) {
				free (buf);
				inflateEnd (&zs);
				return NULL;
			}
			buf = swap;
		}
		memcpy (buf + bsiz, obuf, osiz);
		bsiz += osiz;
		zs.next_out = obuf;
		zs.avail_out = ZLIBBUFSIZ;
	}
	if (rv != Z_STREAM_END) {
		free (buf);
		inflateEnd (&zs);
		return NULL;
	}
	osiz = ZLIBBUFSIZ - zs.avail_out;
	if (bsiz + osiz >= asiz) {
		asiz = asiz * 2 + osiz;
		if (!(swap = realloc (buf, asiz))) {
			free (buf);
			inflateEnd (&zs);
			return NULL;
		}
		buf = swap;
	}
	memcpy (buf + bsiz, obuf, osiz);
	bsiz += osiz;
	buf[bsiz] = '\0';
	*uncompressed_size = bsiz;
	inflateEnd (&zs);
	return buf;
}


static inline gboolean
is_match (const char *a, const char *b)
{
	int len = strlen (b);

	char *str1 = g_utf8_casefold (a, len);
        char *str2 = g_utf8_casefold (b, len);

	char *normal1 = g_utf8_normalize (str1, -1, G_NORMALIZE_NFC);
	char *normal2 = g_utf8_normalize (str2, -1, G_NORMALIZE_NFC);

	gboolean result = (strcmp (normal1, normal2) == 0);

	g_free (str1);
	g_free (str2);
	g_free (normal1);
	g_free (normal2);

	return result;
}


static const gchar *
pointer_from_offset_skipping_decomp (const gchar *str, gint offset)
{
	gchar *casefold, *normal;
	const gchar *p, *q;

	g_return_val_if_fail (str != NULL, NULL);

	p = str;
	while (offset > 0)
	{
		q = g_utf8_next_char (p);
		casefold = g_utf8_casefold (p, q - p);
		normal = g_utf8_normalize (casefold, -1, G_NORMALIZE_NFC);
		offset -= g_utf8_strlen (normal, -1);
		g_free (casefold);
		g_free (normal);
		p = q;
	}
	return p;
}


static const char *
g_utf8_strcasestr_array (const gchar *haystack, gchar **needles)
{
	gsize needle_len;
	gsize haystack_len;
	const char *ret = NULL, *needle;
	char **array;
	char *p;
	char *casefold;
	char *caseless_haystack;
	int i;

	g_return_val_if_fail (haystack != NULL, NULL);

	casefold = g_utf8_casefold (haystack, -1);
	caseless_haystack = g_utf8_normalize (casefold, -1, G_NORMALIZE_NFC);
	g_free (casefold);

	if (!caseless_haystack) {
		return NULL;
	}

	haystack_len = g_utf8_strlen (caseless_haystack, -1);

	for (array=needles; *array; array++)
	{
		needle = *array;
		needle_len = g_utf8_strlen (needle, -1);

		if (needle_len == 0) {
			continue;
		}

		if (haystack_len < needle_len) {
			continue;
		}

		p = (gchar *) caseless_haystack;
		needle_len = strlen (needle);
		i = 0;

		while (*p) {

			if ((strncmp (p, needle, needle_len) == 0)) {
				ret = pointer_from_offset_skipping_decomp (haystack, i);
				goto finally_1;
			}

			p = g_utf8_next_char (p);
			i++;
		}
	}

finally_1:
	g_free (caseless_haystack);

	return ret;
}


const char *
substring_utf8 (const char *a, const char *b)
{
        const char  *ptr, *found_ptr;
	gunichar c, lower, upper;
	int len;
	gboolean got_match = FALSE;

	len = strlen (b);

	c = g_utf8_get_char (b);

	lower = g_unichar_tolower (c);
	upper = g_unichar_toupper (c);

	ptr = a;
	found_ptr = a;

	/* check lowercase first */
	while (found_ptr) {

		found_ptr = g_utf8_strchr (ptr, -1, lower);

		if (found_ptr) {
			ptr = g_utf8_find_next_char (found_ptr, NULL);
			if (is_match (found_ptr, b)) {
				got_match = TRUE;
				break;
			}
		} else {
			break;
		}
	}

	if (!got_match) {
		ptr = a;
		found_ptr = a;
		while (found_ptr) {

			found_ptr = g_utf8_strchr (ptr, -1, upper);

			if (found_ptr) {
				ptr = g_utf8_find_next_char (found_ptr, NULL);
				if (is_match (found_ptr, b)) {
					break;
				}
			} else {

			}
		}
	}

	return found_ptr;
}


static int
get_word_break (const char *a)
{
	char **words = g_strsplit_set (a, "\t\n\v\f\r !\"#$%&'()*/<=>?[\\]^`{|}~+,.:;@\"[]" , -1);

	if (!words) return 0;

	int ret = strlen (words[0]);

	g_strfreev  (words);

	return ret;
}


static gboolean
is_word_break (const char a)
{
	const char *breaks = "\t\n\v\f\r !\"#$%&'()*/<=>?[\\]^`{|}~+,.:;@\"[]";
	int i;

	for (i = 0; breaks[i]; i++) {
		if (a == breaks[i]) {
			return TRUE;
		}
	}

	return FALSE;
}


static char *
highlight_terms (const char *str, char **terms)
{
	const char *ptr;
	char *txt;
	GString *st;
	int term_length;

	if (!str || !terms) {
		return NULL;
	}

	char **array;
	txt = g_strdup (str);

	for (array = terms; *array; array++) {
		char **single_term;

		single_term = g_new( char *, 2);
		single_term[0] = g_strdup (*array);
		single_term[1] = NULL;

		st = g_string_new ("");

		const char *ptxt = txt;

		while ((ptr = g_utf8_strcasestr_array  (ptxt, single_term))) {
			char *pre_snip, *term;

			pre_snip = g_strndup (ptxt, (ptr - ptxt));

			term_length = get_word_break (ptr);

			term = g_strndup (ptr, term_length);

			ptxt = ptr + term_length;

			g_string_append_printf (st, "%s<b>%s</b>", pre_snip, term);

			g_free (pre_snip);
			g_free (term);
		}

		if (ptxt) {
			g_string_append (st, ptxt);

		}

		g_strfreev  (single_term);
		g_free (txt);

		txt = g_string_free (st, FALSE);
	}

	return txt;
}


char *
tracker_get_snippet (const char *txt, char **terms, int length)
{
	const char *ptr = NULL, *end_ptr,  *tmp;
	int i, txt_len;

	if (!txt || !terms) {
		return NULL;
	}

	txt_len = strlen (txt);

	ptr = g_utf8_strcasestr_array (txt, terms);

	if (ptr) {
		tmp = ptr;

		i = 0;

		/* get snippet before  the matching term */
		while ((ptr = g_utf8_prev_char (ptr)) && (ptr >= txt) && (i < length)) {

			if (*ptr == '\n') {
				break;
			}
			i++;
		}

		/* try to start beginning of snippet on a word break */
		if ((*ptr != '\n') && (ptr > txt)) {
			i=0;
			while (!is_word_break (*ptr) && (i<(length/2))) {
				ptr = g_utf8_next_char (ptr);
				i++;
			}
		}

		ptr = g_utf8_next_char (ptr);

		if (!ptr || ptr < txt) {
			return NULL;
		}

		end_ptr = tmp;
		i = 0;

		/* get snippet after match */
		while ((end_ptr = g_utf8_next_char (end_ptr)) && (end_ptr <= txt_len + txt) && (i < length)) {
			i++;
			if (*end_ptr == '\n') {
				break;
			}
		}

		while (end_ptr > txt_len + txt) {
			end_ptr = g_utf8_prev_char (end_ptr);
		}

		/* try to end snippet on a word break */
		if ((*end_ptr != '\n') && (end_ptr < txt_len + txt)) {
			i=0;
			while (!is_word_break (*end_ptr) && (i<(length/2))) {
				end_ptr = g_utf8_prev_char (end_ptr);
				i++;
			}
		}

		if (!end_ptr || !ptr) {
			return NULL;
		}

		char *snip, *esc_snip,  *highlight_snip;

		snip = g_strndup (ptr,  end_ptr - ptr);

		i = strlen (snip);

		esc_snip = g_markup_escape_text (snip, i);

		g_free (snip);

		highlight_snip = highlight_terms (esc_snip, terms);

		g_free (esc_snip);

		return highlight_snip;
	}

	ptr = txt;
	i = 0;
	while ((ptr = g_utf8_next_char (ptr)) && (ptr <= txt_len + txt) && (i < length)) {
		i++;
		if (*ptr == '\n') {
			break;
		}
	}

	if (ptr > txt_len + txt) {
		ptr = g_utf8_prev_char (ptr);
	}

	if (ptr) {
		char *snippet = g_strndup (txt, ptr - txt);
		char *esc_snippet = g_markup_escape_text (snippet, ptr - txt);
		char *highlight_snip = highlight_terms (esc_snippet, terms);

		g_free (snippet);
		g_free (esc_snippet);

		return highlight_snip;
	} else {
		return NULL;
	}
}

void
tracker_add_metadata_to_table (GHashTable *meta_table, const gchar *key, const gchar *value)
{
	GSList *list = g_hash_table_lookup (meta_table, (gchar *) key);

	list = g_slist_prepend (list, (gchar *) value);

	g_hash_table_steal (meta_table, key);

	g_hash_table_insert (meta_table, (gchar *) key, list);
}


void
tracker_free_metadata_field (FieldData *field_data)
{
	g_return_if_fail (field_data);

	if (field_data->alias) {
		g_free (field_data->alias);
	}

	if (field_data->where_field) {
		g_free (field_data->where_field);
	}

	if (field_data->field_name) {
		g_free (field_data->field_name);
	}

	if (field_data->select_field) {
		g_free (field_data->select_field);
	}

	if (field_data->table_name) {
		g_free (field_data->table_name);
	}

	if (field_data->id_field) {
		g_free (field_data->id_field);
	}

	g_free (field_data);
}




int 
tracker_get_memory_usage (void)
{


#if defined(__linux__)
	int  fd, length, mem = 0;
	char buffer[8192];

	char *stat_file = g_strdup_printf ("/proc/%d/stat", tracker->pid);

	fd = open (stat_file, O_RDONLY); 

	g_free (stat_file);

	if (fd ==-1) {
		return 0;
	}

	
	length = read (fd, buffer, 8192);

	buffer[length] = 0;

	close (fd);

	char **terms = g_strsplit (buffer, " ", -1);

	
	if (terms) {

		int i;
		for (i=0; i < 24; i++) {
			if (!terms[i]) {
				break;
			}		

			if (i==23) mem = 4 * atoi (terms[23]);
		}
	}


	g_strfreev (terms);

	return mem;	
	
#endif
	return 0;
}

void
tracker_add_io_grace (const gchar *uri)
{
	if (g_str_has_prefix (uri, tracker->xesam_dir)) {
		return;
	}

	tracker_log ("file changes to %s is pausing tracker", uri);

	tracker->grace_period++;
}

gboolean
tracker_pause_on_battery (void)
{
        if (!tracker->pause_battery) {
                return FALSE;
        }

	if (tracker->first_time_index) {
		return tracker_config_get_disable_indexing_on_battery_init (tracker->config);
	}

        return tracker_config_get_disable_indexing_on_battery (tracker->config);
}


gboolean
tracker_low_diskspace (void)
{
	struct statvfs st;
        gint           low_disk_space_limit;

        low_disk_space_limit = tracker_config_get_low_disk_space_limit (tracker->config);

	if (low_disk_space_limit < 1)
		return FALSE;

	if (statvfs (tracker->data_dir, &st) == -1) {
		static gboolean reported = 0;
		if (! reported) {
			reported = 1;
			tracker_error ("Could not statvfs %s", tracker->data_dir);
		}
		return FALSE;
	}

	if (((long long) st.f_bavail * 100 / st.f_blocks) <= low_disk_space_limit) {
		tracker_error ("Disk space is low!");
		return TRUE;
	}

	return FALSE;
}



gboolean
tracker_index_too_big ()
{

	
	char *file_index = g_build_filename (tracker->data_dir, "file-index.db", NULL);
	if (tracker_file_get_size (file_index) > MAX_INDEX_FILE_SIZE) {
		tracker_error ("file index is too big - discontinuing index");
		g_free (file_index);
		return TRUE;	
	}
	g_free (file_index);


	char *email_index = g_build_filename (tracker->data_dir, "email-index.db", NULL);
	if (tracker_file_get_size (email_index) > MAX_INDEX_FILE_SIZE) {
		tracker_error ("email index is too big - discontinuing index");
		g_free (email_index);
		return TRUE;	
	}
	g_free (email_index);


	char *file_meta = g_build_filename (tracker->data_dir, "file-meta.db", NULL);
	if (tracker_file_get_size (file_meta) > MAX_INDEX_FILE_SIZE) {
		tracker_error ("file metadata is too big - discontinuing index");
		g_free (file_meta);
		return TRUE;	
	}
	g_free (file_meta);


	char *email_meta = g_build_filename (tracker->data_dir, "email-meta.db", NULL);
	if (tracker_file_get_size (email_meta) > MAX_INDEX_FILE_SIZE) {
		tracker_error ("email metadata is too big - discontinuing index");
		g_free (email_meta);
		return TRUE;	
	}
	g_free (email_meta);

	return FALSE;

}

gboolean
tracker_pause (void)
{
	return tracker->pause_manual || tracker_pause_on_battery () || tracker_low_diskspace () || tracker_index_too_big ();
}


gchar*
tracker_unique_key (void)
{
	/* This function is hardly cryptographically random but should be
	 "good enough" */
	static guint serial = 0;
	gchar* key;
	guint t, ut, p, u, r;
	GTimeVal tv;

	g_get_current_time(&tv);

	t = tv.tv_sec;
	ut = tv.tv_usec;

	p = getpid();

	#ifdef HAVE_GETUID
	u = getuid();
	#else
	u = 0;
	#endif

	/* don't bother to seed; if it's based on the time or any other
	 changing info we can get, we may as well just use that changing
	 info. since we don't seed we'll at least get a different number
	 on every call to this function in the same executable. */
	r = rand();

	/* The letters may increase uniqueness by preventing "melds"
	 i.e. 01t01k01 and 0101t0k1 are not the same */
	key = g_strdup_printf("%ut%uut%uu%up%ur%uk%u",
			      /* Duplicate keys must be generated
			       by two different program instances */
			      serial,
			      /* Duplicate keys must be generated
			       in the same microsecond */
			      t,
			      ut,
			      /* Duplicate keys must be generated by
			       the same user */
			      u,
			      /* Duplicate keys must be generated by
			       two programs that got the same PID */
			      p,
			      /* Duplicate keys must be generated with the
			       same random seed and the same index into
			       the series of pseudorandom values */
			      r,
			      /* Duplicate keys must result from running
			       this function at the same stack location */
			      GPOINTER_TO_UINT(&key));

	++serial;

	return key;
}

