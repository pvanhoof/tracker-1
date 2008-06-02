/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>

#include <xdgmime/xdgmime.h>

#include "tracker-log.h"
#include "tracker-os-dependant.h"
#include "tracker-file-utils.h"

#define TEXT_SNIFF_SIZE 4096

gint
tracker_file_open (const gchar *uri, 
		   gboolean     readahead)
{
	gint fd;

#if defined(__linux__)
	fd = open (uri, O_RDONLY | O_NOATIME);

	if (fd == -1) {
		fd = open (uri, O_RDONLY); 
	}
#else
	fd = open (uri, O_RDONLY); 
#endif

	if (fd == -1) { 
		return -1;
	}
	
#ifdef HAVE_POSIX_FADVISE
	if (readahead) {
		posix_fadvise (fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	} else {
		posix_fadvise (fd, 0, 0, POSIX_FADV_RANDOM);
	}
#endif

	return fd;
}

void
tracker_file_close (gint     fd, 
		    gboolean no_longer_needed)
{

#ifdef HAVE_POSIX_FADVISE
	if (no_longer_needed) {
		posix_fadvise (fd, 0, 0, POSIX_FADV_DONTNEED);
	}
#endif

	close (fd);
}

gboolean
tracker_file_unlink (const gchar *uri)
{
	gchar    *str;
	gboolean  result;

	str = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);
	result = g_unlink (str) == 0;
	g_free (str);

	return result;
}

guint32
tracker_file_get_size (const gchar *uri)
{
	struct stat finfo;
	
	if (g_lstat (uri, &finfo) == -1) {
		return 0;
	} else {
                return (guint32) finfo.st_size;
        }
}

static inline gboolean
is_utf8 (const gchar *buffer,
	 gint         buffer_length)
{
	gchar *end;

	/* Code in this function modified from gnome-vfs */
	if (g_utf8_validate ((gchar*) buffer, 
			     buffer_length, 
			     (const gchar**) &end)) {
		return TRUE;
	} else {
		/* Check whether the string was truncated in the middle of
		 * a valid UTF8 char, or if we really have an invalid
		 * UTF8 string.
     		 */
		gunichar validated;
		gint     remaining_bytes;

		remaining_bytes  = buffer_length;
		remaining_bytes -= end - ((gchar *) buffer);

		if (remaining_bytes > 4) {
			return FALSE;
		}

		validated = g_utf8_get_char_validated (end, (gsize) remaining_bytes);

 		if (validated == (gunichar) - 2) {
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
is_text_file (const gchar *uri)
{
	gchar  	 buffer[TEXT_SNIFF_SIZE];
	gint     buffer_length = 0;
	gint     fd;
	gboolean result = FALSE;

	fd = tracker_file_open (uri, FALSE);
	buffer_length = read (fd, buffer, TEXT_SNIFF_SIZE);

	/* Don't allow embedded zeros in textfiles. */
	if (buffer_length > 2 && 
	    memchr (buffer, 0, buffer_length) == NULL) {
		if (is_utf8 (buffer, buffer_length)) {
			result = TRUE;
		} else {
			GError *error = NULL;
			gchar  *tmp;
			
			tmp = g_locale_to_utf8 (buffer, 
						buffer_length, 
						NULL, 
						NULL, 
						&error);
			g_free (tmp);
			
			if (error) {
				gboolean result = FALSE;
				
				if (error->code != G_CONVERT_ERROR_ILLEGAL_SEQUENCE && 
				    error->code != G_CONVERT_ERROR_FAILED && 
				    error->code != G_CONVERT_ERROR_NO_CONVERSION) {
					result = TRUE;
				}
				
				g_error_free (error);
			}
		}
	}

	tracker_file_close (fd, !result);

	return result;
}

gboolean
tracker_file_is_valid (const gchar *uri)
{
	gchar	 *str;
	gboolean  is_valid = TRUE;

	str = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);

	if (!str) {
		g_warning ("URI:'%s' could not be converted to locale format",
			   uri);
		return FALSE;
	}

	/* g_file_test (file,G_FILE_TEST_EXISTS) uses the access ()
	 * system call and so needs locale filenames.
	 */
	is_valid &= tracker_check_uri (uri);
	is_valid &= g_file_test (str, 
				 G_FILE_TEST_IS_REGULAR | 
				 G_FILE_TEST_IS_DIR | 
				 G_FILE_TEST_IS_SYMLINK);

	g_free (str);

	return is_valid;
}

gboolean
tracker_file_is_directory (const gchar *uri)
{
	gchar    *str;
	gboolean  is_directory;

	str = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);

	if (!str) {
		g_warning ("URI:'%s' could not be converted to locale format",
			   str);
		return FALSE;
	}

	is_directory = g_file_test (str, G_FILE_TEST_IS_DIR);
	g_free (str);

	return is_directory;
}

gboolean
tracker_file_is_indexable (const gchar *uri)
{
	gchar       *str;
	struct stat  finfo;
	gboolean     is_indexable;

	g_return_val_if_fail (uri != NULL, FALSE);

	str = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);

	if (!str) {
		g_warning ("URI:'%s' could not be converted to locale format",
			   str);
		return FALSE;
	}

	g_lstat (str, &finfo);
	g_free (str);

	is_indexable  = TRUE;
	is_indexable &= !S_ISDIR (finfo.st_mode);
	is_indexable &= S_ISREG (finfo.st_mode);

	g_debug ("URI:'%s' %s indexable", 
		 uri,
		 is_indexable ? "is" : "is not");
		 
	return is_indexable;
}

gint32
tracker_file_get_mtime (const gchar *uri)
{
	struct stat  finfo;
	gchar 	    *str;

	str = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);

	if (str) {
		if (g_lstat (str, &finfo) == -1) {
			g_free (str);
			return 0;
		}
	} else {
		g_warning ("URI:'%s' could not be converted to locale format",
			   uri);
		return 0;
	}

	g_free (str);

	return (gint32) finfo.st_mtime;
}

gchar *
tracker_file_get_mime_type (const gchar *uri)
{
	struct stat  finfo;
	gchar	    *str;
	const gchar *result;
	gchar       *mime_type;

	if (!tracker_file_is_valid (uri)) {
		g_message ("URI:'%s' is no longer valid", 
			   uri);
		return g_strdup ("unknown");
	}

	str = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);

	if (!str) {
		g_warning ("URI:'%s' could not be converted to locale format",
			   uri);
		return g_strdup ("unknown");
	}

	g_lstat (str, &finfo);

	if (S_ISLNK (finfo.st_mode) && S_ISDIR (finfo.st_mode)) {
	        g_free (str);
		return g_strdup ("symlink");
	}

	/* Handle iso files as they can be mistaken for video files */
	if (g_str_has_suffix (uri, ".iso")) {
		return g_strdup ("application/x-cd-image");
	}

	result = xdg_mime_get_mime_type_for_file (uri, NULL);

	if (!result || result == XDG_MIME_TYPE_UNKNOWN) {
		if (is_text_file (str)) {
			mime_type = g_strdup ("text/plain");
		} else {
			mime_type = g_strdup ("unknown");
		}
	} else {
		mime_type = g_strdup (result);
	}

	g_free (str);

	return mime_type;
}

gchar *
tracker_file_get_vfs_path (const gchar *uri)
{
	gchar *p;

	if (!uri || !strchr (uri, G_DIR_SEPARATOR)) {
		return NULL;
	}

	p = (gchar*) uri + strlen (uri) - 1;
	
	/* Skip trailing slash */
	if (p != uri && *p == G_DIR_SEPARATOR) {
		p--;
	}
	
	/* Search backwards to the next slash. */
	while (p != uri && *p != G_DIR_SEPARATOR) {
		p--;
	}
	
	if (p[0] != '\0') {
		gchar *new_uri_text;
		gint   length;
		
		length = p - uri;
		
		if (length == 0) {
			new_uri_text = g_strdup (G_DIR_SEPARATOR_S);
		} else {
			new_uri_text = g_malloc (length + 1);
			memcpy (new_uri_text, uri, length);
			new_uri_text[length] = '\0';
		}
		
		return new_uri_text;
	} else {
		return g_strdup (G_DIR_SEPARATOR_S);
	}
}

gchar *
tracker_file_get_vfs_name (const gchar *uri)
{
	gchar *p, *res, *tmp, *result;

	if (!uri || !strchr (uri, G_DIR_SEPARATOR)) {
		return g_strdup (" ");
	}

	tmp = g_strdup (uri);
	p = tmp + strlen (uri) - 1;

	/* Skip trailing slash */
	if (p != tmp && *p == G_DIR_SEPARATOR) {
		*p = '\0';
	}

	/* Search backwards to the next slash.  */
	while (p != tmp && *p != G_DIR_SEPARATOR) {
		p--;
	}
	
	res = p + 1;
	
	if (res && res[0] != '\0') {
		result = g_strdup (res);
		g_free (tmp);
	
		return result;
	}
	
	g_free (tmp);

	return g_strdup (" ");
}

void
tracker_path_remove (const gchar *uri)
{
	GQueue *dirs;
	GSList *dirs_to_remove = NULL;

	g_return_if_fail (uri != NULL);

	dirs = g_queue_new ();

	g_queue_push_tail (dirs, g_strdup (uri));

	while (!g_queue_is_empty (dirs)) {
		GDir  *p;
		gchar *dir;

		dir = g_queue_pop_head (dirs);
		dirs_to_remove = g_slist_prepend (dirs_to_remove, dir);

		if ((p = g_dir_open (dir, 0, NULL))) {
			const gchar *file;

			while ((file = g_dir_read_name (p))) {
				gchar *full_filename;

				full_filename = g_build_filename (dir, file, NULL);

				if (g_file_test (full_filename, G_FILE_TEST_IS_DIR)) {
					g_queue_push_tail (dirs, full_filename);
				} else {
					g_unlink (full_filename);
					g_free (full_filename);
				}
			}

			g_dir_close (p);
		}
	}

	g_queue_free (dirs);

	/* Remove directories (now they are empty) */
	g_slist_foreach (dirs_to_remove, (GFunc) g_remove, NULL);
	g_slist_foreach (dirs_to_remove, (GFunc) g_free, NULL);
	g_slist_free (dirs_to_remove);
}
