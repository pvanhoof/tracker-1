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

#include <string.h>

#include <libtracker-common/tracker-log.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-parser.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-dbus.h"
#include "tracker-dbus-search.h"
#include "tracker-rdf-query.h"
#include "tracker-query-tree.h"
#include "tracker-indexer.h"
#include "tracker-marshal.h"

#define DEFAULT_SEARCH_MAX_HITS 1024

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_DBUS_SEARCH, TrackerDBusSearchPriv))

typedef struct {
	DBusGProxy      *fd_proxy;
	DBConnection    *db_con;
	TrackerConfig   *config;
	TrackerLanguage *language;
        Indexer         *file_index;
        Indexer         *email_index;
} TrackerDBusSearchPriv;

enum {
	PROP_0,
	PROP_DB_CONNECTION,
	PROP_CONFIG,
	PROP_LANGUAGE,
	PROP_FILE_INDEX,
	PROP_EMAIL_INDEX
};

static void dbus_search_finalize     (GObject      *object);
static void dbus_search_set_property (GObject      *object,
                                      guint         param_id,
                                      const GValue *value,
                                      GParamSpec   *pspec);

G_DEFINE_TYPE(TrackerDBusSearch, tracker_dbus_search, G_TYPE_OBJECT)

static void
tracker_dbus_search_class_init (TrackerDBusSearchClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = dbus_search_finalize;
	object_class->set_property = dbus_search_set_property;

	g_object_class_install_property (object_class,
					 PROP_DB_CONNECTION,
					 g_param_spec_pointer ("db-connection",
							       "DB connection",
							       "Database connection to use in transactions",
							       G_PARAM_WRITABLE));
	g_object_class_install_property (object_class,
					 PROP_CONFIG,
					 g_param_spec_object ("config",
							      "Config",
							      "TrackerConfig object",
							      tracker_config_get_type (),
							      G_PARAM_WRITABLE));
	g_object_class_install_property (object_class,
					 PROP_LANGUAGE,
					 g_param_spec_object ("language",
							      "Language",
							      "Language",
							      tracker_language_get_type (),
							      G_PARAM_WRITABLE));
	g_object_class_install_property (object_class,
					 PROP_FILE_INDEX,
					 g_param_spec_pointer ("file-index",
							       "File index",
							       "File index",
							       G_PARAM_WRITABLE));
	g_object_class_install_property (object_class,
					 PROP_EMAIL_INDEX,
					 g_param_spec_pointer ("email-index",
							       "Email index",
							       "Email index",
							       G_PARAM_WRITABLE));

	g_type_class_add_private (object_class, sizeof (TrackerDBusSearchPriv));
}

static void
tracker_dbus_search_init (TrackerDBusSearch *object)
{
}

static void
dbus_search_finalize (GObject *object)
{
	TrackerDBusSearchPriv *priv;
	
	priv = GET_PRIV (object);

	if (priv->fd_proxy) {
		g_object_unref (priv->fd_proxy);
	}

	G_OBJECT_CLASS (tracker_dbus_search_parent_class)->finalize (object);
}

static void
dbus_search_set_property (GObject      *object,
                          guint 	param_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	TrackerDBusSearchPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_DB_CONNECTION:
		tracker_dbus_search_set_db_connection (TRACKER_DBUS_SEARCH (object),
                                                       g_value_get_pointer (value));
		break;
	case PROP_CONFIG:
		tracker_dbus_search_set_config (TRACKER_DBUS_SEARCH (object),
						g_value_get_object (value));
		break;
	case PROP_LANGUAGE:
		tracker_dbus_search_set_language (TRACKER_DBUS_SEARCH (object),
						  g_value_get_object (value));
		break;
	case PROP_FILE_INDEX:
		tracker_dbus_search_set_file_index (TRACKER_DBUS_SEARCH (object),
                                                    g_value_get_pointer (value));
		break;
	case PROP_EMAIL_INDEX:
		tracker_dbus_search_set_email_index (TRACKER_DBUS_SEARCH (object),
						     g_value_get_pointer (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

TrackerDBusSearch *
tracker_dbus_search_new (DBConnection *db_con)
{
	TrackerDBusSearch *object;

	object = g_object_new (TRACKER_TYPE_DBUS_SEARCH, 
			       "db-connection", db_con,
			       NULL);
	
	return object;
}

void
tracker_dbus_search_set_db_connection (TrackerDBusSearch *object,
                                       DBConnection      *db_con)
{
	TrackerDBusSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_SEARCH (object));
	g_return_if_fail (db_con != NULL);

	priv = GET_PRIV (object);

	priv->db_con = db_con;
	
	g_object_notify (G_OBJECT (object), "db-connection");
}

void
tracker_dbus_search_set_config (TrackerDBusSearch *object,
				TrackerConfig     *config)
{
	TrackerDBusSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_SEARCH (object));
	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = GET_PRIV (object);

	if (config) {
		g_object_ref (config);
	}

	if (priv->config) {
		g_object_unref (priv->config);
	}

	priv->config = config;

	g_object_notify (G_OBJECT (object), "config");
}

void
tracker_dbus_search_set_language (TrackerDBusSearch *object,
				  TrackerLanguage   *language)
{
	TrackerDBusSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_SEARCH (object));
	g_return_if_fail (language != NULL);

	priv = GET_PRIV (object);

	if (language) {
		g_object_ref (language);
	}

	if (priv->language) {
		g_object_unref (priv->language);
	}

	priv->language = language;
	
	g_object_notify (G_OBJECT (object), "language");
}

void
tracker_dbus_search_set_file_index (TrackerDBusSearch *object,
                                    Indexer           *file_index)
{
	TrackerDBusSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_SEARCH (object));
	g_return_if_fail (file_index != NULL);

	priv = GET_PRIV (object);

	priv->file_index = file_index;
	
	g_object_notify (G_OBJECT (object), "file-index");
}

void
tracker_dbus_search_set_email_index (TrackerDBusSearch *object,
				     Indexer           *email_index)
{
	TrackerDBusSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_SEARCH (object));
	g_return_if_fail (email_index != NULL);

	priv = GET_PRIV (object);

	priv->email_index = email_index;
	
	g_object_notify (G_OBJECT (object), "email-index");
}

/*
 * Functions
 */
static gint
dbus_search_sanity_check_max_hits (gint max_hits)
{
        if (max_hits < 1) {
                return DEFAULT_SEARCH_MAX_HITS;
        }

        return max_hits;
}

static const gchar *
dbus_search_utf8_p_from_offset_skipping_decomp (const gchar *str,
						gint         offset)
{
	const gchar *p, *q;
	gchar       *casefold, *normal;

	g_return_val_if_fail (str != NULL, NULL);

	p = str;

	while (offset > 0) {
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
dbus_search_utf8_strcasestr_array (const gchar  *haystack, 
				   gchar       **needles)
{
	gsize         needle_len;
	gsize         haystack_len;
	const gchar  *ret = NULL;
	const gchar  *needle;
	gchar       **array;
	gchar        *p;
	gchar        *casefold;
	gchar        *caseless_haystack;
	gint          i;

	g_return_val_if_fail (haystack != NULL, NULL);

	casefold = g_utf8_casefold (haystack, -1);
	caseless_haystack = g_utf8_normalize (casefold, -1, G_NORMALIZE_NFC);
	g_free (casefold);

	if (!caseless_haystack) {
		return NULL;
	}

	haystack_len = g_utf8_strlen (caseless_haystack, -1);

	for (array = needles; *array; array++) {
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
				ret = dbus_search_utf8_p_from_offset_skipping_decomp (haystack, i);
				goto done;
			}

			p = g_utf8_next_char (p);
			i++;
		}
	}

done:
	g_free (caseless_haystack);

	return ret;
}

static gint
dbus_search_get_word_break (const char *a)
{
	gchar **words;
	gint    value;

	words = g_strsplit_set (a, "\t\n\v\f\r !\"#$%&'()*/<=>?[\\]^`{|}~+,.:;@\"[]" , -1);

	if (!words) { 
		return 0;
	}

	value = strlen (words[0]);
	g_strfreev (words);

	return value;
}


static gboolean
dbus_search_is_word_break (const char a)
{
	const gchar *breaks = "\t\n\v\f\r !\"#$%&'()*/<=>?[\\]^`{|}~+,.:;@\"[]";
	gint         i;

	for (i = 0; breaks[i]; i++) {
		if (a == breaks[i]) {
			return TRUE;
		}
	}

	return FALSE;
}

static char *
dbus_search_highlight_terms (const gchar  *text, 
			     gchar       **terms)
{
	GStrv         p;
	GString      *s;
	const gchar  *str;
	gchar        *text_copy;
	gint          term_len;

	if (!text || !terms) {
		return NULL;
	}

	text_copy = g_strdup (text);

	for (p = terms; *p; p++) {
		const gchar  *text_p;
		gchar       **single_term;

		single_term = g_new (gchar*, 2);
		single_term[0] = g_strdup (*p);
		single_term[1] = NULL;

		s = g_string_new ("");
		text_p = text_copy;

		while ((str = dbus_search_utf8_strcasestr_array (text_p, single_term))) {
			gchar *pre_snip;
			gchar *term;

			pre_snip = g_strndup (text_p, (str - text_p));
			term_len = dbus_search_get_word_break (str);
			term = g_strndup (str, term_len);

			text_p = str + term_len;
			g_string_append_printf (s, "%s<b>%s</b>", pre_snip, term);

			g_free (pre_snip);
			g_free (term);
		}

		if (text_p) {
			g_string_append (s, text_p);
		}

		g_strfreev (single_term);
	}

	g_free (text_copy);
	text_copy = g_string_free (s, FALSE);

	return text_copy;
}

gchar *
dbus_search_get_snippet (const gchar  *text, 
			 gchar       **terms, 
			 gint          length)
{
	const gchar *ptr = NULL;
	const gchar *end_ptr;
	const gchar *tmp;
	gint         i;
	gint         text_len;

	if (!text || !terms) {
		return NULL;
	}

	text_len = strlen (text);
	ptr = dbus_search_utf8_strcasestr_array (text, terms);

	if (ptr) {
		gchar *snippet;
		gchar *snippet_escaped;
		gchar *snippet_highlighted;

		tmp = ptr;
		i = 0;

		/* Get snippet before  the matching term */
		while ((ptr = g_utf8_prev_char (ptr)) && ptr >= text && i < length) {
			if (*ptr == '\n') {
				break;
			}

			i++;
		}

		/* Try to start beginning of snippet on a word break */
		if (*ptr != '\n' && ptr > text) {
			i = 0;

			while (!dbus_search_is_word_break (*ptr) && i < (length / 2)) {
				ptr = g_utf8_next_char (ptr);
				i++;
			}
		}

		ptr = g_utf8_next_char (ptr);

		if (!ptr || ptr < text) {
			return NULL;
		}

		end_ptr = tmp;
		i = 0;

		/* Get snippet after match */
		while ((end_ptr = g_utf8_next_char (end_ptr)) && 
		       end_ptr <= text_len + text && 
		       i < length) {
			i++;

			if (*end_ptr == '\n') {
				break;
			}
		}

		while (end_ptr > text_len + text) {
			end_ptr = g_utf8_prev_char (end_ptr);
		}

		/* Try to end snippet on a word break */
		if (*end_ptr != '\n' && end_ptr < text_len + text) {
			i=0;
			while (!dbus_search_is_word_break (*end_ptr) && i < (length / 2)) {
				end_ptr = g_utf8_prev_char (end_ptr);
				i++;
			}
		}

		if (!end_ptr || !ptr) {
			return NULL;
		}

		snippet = g_strndup (ptr, end_ptr - ptr);
		i = strlen (snippet);
		snippet_escaped = g_markup_escape_text (snippet, i);
		g_free (snippet);

		snippet_highlighted = dbus_search_highlight_terms (snippet_escaped, terms);
		g_free (snippet_escaped);

		return snippet_highlighted;
	}

	ptr = text;
	i = 0;

	while ((ptr = g_utf8_next_char (ptr)) && ptr <= text_len + text && i < length) {
		i++;

		if (*ptr == '\n') {
			break;
		}
	}

	if (ptr > text_len + text) {
		ptr = g_utf8_prev_char (ptr);
	}

	if (ptr) {
		gchar *snippet;
		gchar *snippet_escaped;
		gchar *snippet_highlighted;

		snippet = g_strndup (text, ptr - text);
		snippet_escaped = g_markup_escape_text (snippet, ptr - text);
		snippet_highlighted = dbus_search_highlight_terms (snippet_escaped, terms);

		g_free (snippet);
		g_free (snippet_escaped);

		return snippet_highlighted;
	} else {
		return NULL;
	}
}

gboolean
tracker_dbus_search_get_hit_count (TrackerDBusSearch  *object,
                                   const gchar        *service,
                                   const gchar        *search_text,
                                   gint               *value,
                                   GError            **error)
{
	TrackerDBusSearchPriv  *priv;
	TrackerQueryTree       *tree;
	GArray                 *array;
	guint                   request_id;
	DBConnection           *db_con;
	gint                    services[12];
        gint                    count = 0;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
                                  "DBus request to get hit count, "
				  "service:'%s', search text:'%s'",
                                  service,
                                  search_text);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (search_text)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "No search term was specified");
		return FALSE;
        }

	/* Check we have the right database connection */
	db_con = tracker_db_get_service_connection (db_con, service);

	services[count++] = tracker_ontology_get_id_for_service_type (service);

	if (strcmp (service, "Files") == 0) {
		services[count++] = tracker_ontology_get_id_for_service_type ("Folders");
		services[count++] = tracker_ontology_get_id_for_service_type ("Documents");
		services[count++] = tracker_ontology_get_id_for_service_type ("Images");
		services[count++] = tracker_ontology_get_id_for_service_type ("Videos");
		services[count++] = tracker_ontology_get_id_for_service_type ("Music");
		services[count++] = tracker_ontology_get_id_for_service_type ("Text");
		services[count++] = tracker_ontology_get_id_for_service_type ("Development");
		services[count++] = tracker_ontology_get_id_for_service_type ("Other");
	} else if (strcmp (service, "Emails") == 0) {
		services[count++] = tracker_ontology_get_id_for_service_type ("EvolutionEmails");
		services[count++] = tracker_ontology_get_id_for_service_type ("KMailEmails");
		services[count++] = tracker_ontology_get_id_for_service_type ("ThunderbirdEmails");
 	} else if (strcmp (service, "Conversations") == 0) {
		services[count++] = tracker_ontology_get_id_for_service_type ("GaimConversations");
	}

	services[count] = 0;

	array = g_array_new (TRUE, TRUE, sizeof (gint));
	g_array_append_vals (array, services, G_N_ELEMENTS (services));
	tree = tracker_query_tree_new (search_text, 
				       db_con->word_index, 
				       priv->config,
				       priv->language,
				       array);
	*value = tracker_query_tree_get_hit_count (tree);
	g_object_unref (tree);
        g_array_free (array, TRUE);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_get_hit_count_all (TrackerDBusSearch  *object,
                                       const gchar        *search_text,
                                       GPtrArray         **values,
                                       GError            **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set = NULL;
        TrackerQueryTree      *tree;
        GArray                *hit_counts;
	GArray                *mail_hit_counts;
	guint                  request_id;
	DBConnection          *db_con;
	guint                  i;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
                                  "DBus request to get search hit count for all, "
                                  "search text:'%s'",
                                  search_text);

        if (tracker_is_empty_string (search_text)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "No search term was specified");
		return FALSE;
        }

        tree = tracker_query_tree_new (search_text, 
				       db_con->word_index, 
				       priv->config,
				       priv->language,
				       NULL);

        hit_counts = tracker_query_tree_get_hit_counts (tree);
        tracker_query_tree_set_indexer (tree, priv->email_index);
        mail_hit_counts = tracker_query_tree_get_hit_counts (tree);
        g_array_append_vals (hit_counts, mail_hit_counts->data, mail_hit_counts->len);
        g_array_free (mail_hit_counts, TRUE);

	for (i = 0; i < hit_counts->len; i++) {
		TrackerHitCount count;
		GValue          value = { 0, };

		if (G_UNLIKELY (!result_set)) {
			result_set = _tracker_db_result_set_new (2);
		}

		count = g_array_index (hit_counts, TrackerHitCount, i);
		_tracker_db_result_set_append (result_set);

		g_value_init (&value, G_TYPE_STRING);
		g_value_take_string (&value, 
				     tracker_ontology_get_service_type_by_id (count.service_type_id));
		_tracker_db_result_set_set_value (result_set, 0, &value);
		g_value_unset (&value);

		g_value_init (&value, G_TYPE_INT);
		g_value_set_int (&value, count.count);
		_tracker_db_result_set_set_value (result_set, 1, &value);
		g_value_unset (&value);
	}

	*values = tracker_dbus_query_result_to_ptr_array (result_set);

	if (result_set) {
		tracker_db_result_set_rewind (result_set);
		g_object_unref (result_set);
	}

        g_array_free (hit_counts, TRUE);
        g_object_unref (tree);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_text (TrackerDBusSearch   *object,
                          gint                 live_query_id,
                          const gchar         *service,
                          const gchar         *search_text,
                          gint                 offset,
                          gint                 max_hits,
                          gchar             ***values,
                          GError             **error)
{
	TrackerDBusSearchPriv  *priv;
	TrackerDBResultSet     *result_set;
	guint                   request_id;
	DBConnection           *db_con;
        gchar                 **strv = NULL;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
                                  "DBus request to search text, "
				  "query id:%d, service:'%s', search text:'%s', "
				  "offset:%d, max hits:%d",
				  live_query_id,
                                  service,
                                  search_text,
                                  offset,
                                  max_hits);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (search_text)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "No search term was specified");
		return FALSE;
        }

	/* Check we have the right database connection */
	db_con = tracker_db_get_service_connection (db_con, service);

	result_set = tracker_db_search_text (db_con, 
					     service, 
					     search_text, 
					     offset, 
					     dbus_search_sanity_check_max_hits (max_hits), 
					     FALSE, 
					     FALSE);

	if (result_set) {
		gchar    *prefix, *name;
		gboolean  valid = TRUE;
		gint      row_count;
		gint      i;

		row_count = tracker_db_result_set_get_n_rows (result_set) + 1;
		strv = g_new (gchar*, row_count);
		i = 0;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   0, &prefix,
						   1, &name,
						   -1);

			strv[i++] = g_build_filename (prefix, name, NULL);
			valid = tracker_db_result_set_iter_next (result_set);

			g_free (prefix);
			g_free (name);
		}

		strv[i] = NULL;

		g_object_unref (result_set);
	}
 
        if (!strv) {
                strv = g_new (gchar*, 1);
		strv[0] = NULL;

                tracker_dbus_request_comment (request_id,
					      "Search found no results");
	}

	*values = strv;

	tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_search_text_detailed (TrackerDBusSearch  *object,
                                   gint                live_query_id,
                                   const gchar        *service,
                                   const gchar        *search_text,
                                   gint                offset,
                                   gint                max_hits,
                                   GPtrArray         **values,
                                   GError            **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
        tracker_dbus_request_new (request_id,
                                  "DBus request to search text detailed, "
				  "query id:%d, service:'%s', search text:'%s', "
				  "offset:%d, max hits:%d",
				  live_query_id,
                                  service,
                                  search_text,
                                  offset,
                                  max_hits);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (search_text)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "No search term was specified");
		return FALSE;
        }

	/* Check we have the right database connection */
	db_con = tracker_db_get_service_connection (db_con, service);

	result_set = tracker_db_search_text (db_con, 
					     service, 
					     search_text, 
					     offset, 
					     dbus_search_sanity_check_max_hits (max_hits), 
					     FALSE, 
					     TRUE);

        *values = tracker_dbus_query_result_to_ptr_array (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_get_snippet (TrackerDBusSearch  *object,
                                 const gchar        *service,
                                 const gchar        *id,
                                 const gchar        *search_text,
                                 gchar             **values,
                                 GError            **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;
        gchar                 *snippet = NULL;
        gchar                 *service_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (id != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
        tracker_dbus_request_new (request_id,
                                  "DBus request to get snippet, "
				  "service:'%s', search text:'%s', id:'%s'",
                                  service,
                                  search_text,
                                  id);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (search_text)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "No search term was specified");
		return FALSE;
        }

	/* Check we have the right database connection */
	db_con = tracker_db_get_service_connection (db_con, service);

	service_id = tracker_db_get_id (db_con, service, id);
        if (!service_id) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service URI '%s' not found", 
                                             id);
                return FALSE;
        }
             
	result_set = tracker_exec_proc (db_con->blob, 
					"GetAllContents", 
					service_id, 
					NULL);
        g_free (service_id);

	if (result_set) {
		gchar **strv;
		gchar  *text;

		tracker_db_result_set_get (result_set, 0, &text, -1);
		strv = tracker_parser_text_into_array (text, 
						       priv->language,
						       tracker_config_get_max_word_length (priv->config),
						       tracker_config_get_min_word_length (priv->config));

		if (strv && strv[0]) {
			snippet = dbus_search_get_snippet (text, strv, 120);
		}

		g_strfreev (strv);
		g_free (text);
		g_object_unref (result_set);
	}

	/* Sanity check snippet, using NULL will crash */
	if (!snippet || !g_utf8_validate (snippet, -1, NULL) ) {
		snippet = g_strdup (" ");
	}

        *values = snippet;

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_files_by_text (TrackerDBusSearch  *object,
                                   gint                live_query_id,
                                   const gchar        *search_text,
                                   gint                offset,
                                   gint                max_hits,
                                   gboolean            group_results,
                                   GHashTable        **values,
                                   GError            **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;

        tracker_dbus_request_new (request_id,
                                  "DBus request to search files by text, "
				  "query id:%d, search text:'%s', offset:%d"
                                  "max hits:%d, group results:'%s'",
				  live_query_id,
                                  search_text,
                                  offset,
                                  max_hits,
                                  group_results ? "yes" : "no");

	result_set = tracker_db_search_files_by_text (db_con, 
						      search_text, 
						      offset, 
						      dbus_search_sanity_check_max_hits (max_hits),
						      group_results);

	*values = tracker_dbus_query_result_to_hash_table (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_metadata (TrackerDBusSearch   *object,
                              const gchar         *service,
                              const gchar         *field,
                              const gchar         *search_text,
                              gint                 offset,
                              gint                 max_hits,
                              gchar             ***values,
                              GError             **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con; 

        /* FIXME: This function is completely redundant */

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (field != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;

        tracker_dbus_request_new (request_id,
                                  "DBus request to search metadata, "
				  "service:'%s', search text:'%s', field:'%s', "
				  "offset:%d, max hits:%d",
                                  service,
                                  search_text,
                                  field,
                                  offset,
                                  max_hits);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

	/* result_set = tracker_db_search_metadata (db_con,  */
	/* 					 service,  */
	/* 					 field,  */
	/* 					 text,  */
	/* 					 offset,  */
	/* 					 dbus_search_sanity_check_max_hits (max_hits)); */

        result_set = NULL;

	*values = tracker_dbus_query_result_to_strv (result_set, NULL);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_matching_fields (TrackerDBusSearch   *object,
                                     const gchar         *service,
                                     const gchar         *id,
                                     const gchar         *search_text,
                                     GHashTable         **values,
                                     GError             **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (id != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
        tracker_dbus_request_new (request_id,
                                  "DBus request to search matching fields, "
				  "service:'%s', search text:'%s', id:'%s'",
                                  service,
                                  search_text,
                                  id);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (id)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "ID field must have a value");
		return FALSE;
        }

	/* Check we have the right database connection */
	db_con = tracker_db_get_service_connection (db_con, service);

	result_set = tracker_db_search_matching_metadata (db_con, 
							  service, 
							  id, 
							  search_text);
	*values = tracker_dbus_query_result_to_hash_table (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_query (TrackerDBusSearch  *object,
                           gint                live_query_id,
                           const gchar        *service,
                           gchar             **fields,
                           const gchar        *search_text,
                           const gchar        *keyword,
                           const gchar        *query_condition,
                           gboolean            sort_by_service,
                           gint                offset,
                           gint                max_hits,
                           GPtrArray         **values,
                           GError            **error)
{
	TrackerDBusSearchPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (fields != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (keyword != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (query_condition != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;

        tracker_dbus_request_new (request_id,
                                  "DBus request to search query, "
				  "query id:%d, service:'%s', search text '%s', "
				  "keyword:'%s', query condition:'%s', offset:%d, "
				  "max hits:%d, sort by service:'%s'",
				  live_query_id,
                                  service,
                                  search_text,
                                  keyword,
                                  query_condition,
                                  offset,
                                  max_hits, 
                                  sort_by_service ? "yes" : "no");
	
	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

	result_set = NULL;

	if (query_condition) {
		GError *query_error = NULL;
		gchar  *query_translated;

                tracker_dbus_request_comment (request_id,
					      "Executing RDF query:'%s' with search "
					      "term:'%s' and keyword:'%s'",
					      query_condition,
					      search_text,
					      keyword);
	
		query_translated = tracker_rdf_query_to_sql (db_con, 
                                                             query_condition, 
                                                             service, 
                                                             fields, 
                                                             g_strv_length (fields), 
                                                             search_text, 
                                                             keyword, 
                                                             sort_by_service, 
                                                             offset, 
                                                             dbus_search_sanity_check_max_hits (max_hits), 
                                                             query_error);
               
		if (query_error) {
                        tracker_dbus_request_failed (request_id,
						     error, 
                                                     "Invalid rdf query produced following error: %s", 
                                                     query_error->message);
                        g_error_free (query_error);

                        return FALSE;
                } else if (!query_translated) {
                        tracker_dbus_request_failed (request_id,
						     error, 
                                                     "Invalid rdf query, no error given");
                        return FALSE;
		}

                tracker_dbus_request_comment (request_id,
					      "Translated RDF query:'%s'",
					      query_translated);

		db_con = tracker_db_get_service_connection (db_con, service);

		if (!tracker_is_empty_string (search_text)) {
			tracker_db_search_text (db_con, 
                                                service, 
                                                search_text, 
                                                0, 
                                                999999, 
                                                TRUE, 
                                                FALSE);
		}

		result_set = tracker_db_interface_execute_query (db_con->db, NULL, query_translated);
		g_free (query_translated);
	}

        *values = tracker_dbus_query_result_to_ptr_array (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_search_suggest (TrackerDBusSearch  *object,
                             const gchar        *search_text,
                             gint                max_dist,
                             gchar             **value,
                             GError            **error)
{
        TrackerDBusSearchPriv *priv;
	guint                  request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (search_text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);
        
        tracker_dbus_request_new (request_id,
                                  "DBus request to for suggested words, "
				  "term:'%s', max dist:%d",
				  search_text,
				  max_dist);

        *value = tracker_indexer_get_suggestion (priv->file_index, search_text, max_dist);

        if (!(*value)) {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "Possible data error in index, no suggestions given for '%s'",
					     search_text);
		return FALSE;
        }
	
	tracker_dbus_request_comment (request_id,
				      "Suggested spelling for '%s' is '%s'",
				      search_text, *value);


        return TRUE;
}
