/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2008, Nokia (urho.konttori@nokia.com)
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

#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include <libtracker-common/tracker-field.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-db-manager.h"
#include "tracker-db-interface-sqlite.h"

typedef enum {
        TRACKER_DB_LOCATION_DATA_DIR,
        TRACKER_DB_LOCATION_USER_DATA_DIR,
        TRACKER_DB_LOCATION_SYS_TMP_ROOT_DIR,
} TrackerDBLocation;

typedef struct {
        TrackerDB          db;
        TrackerDBLocation  location;
        const gchar       *file;
        const gchar       *name;
        gchar             *abs_filename;
        gint               cache_size;
        gint               page_size;
        gboolean           add_functions;
} TrackerDBDefinition;

static TrackerDBDefinition dbs[] = {
        { TRACKER_DB_COMMON, 
          TRACKER_DB_LOCATION_USER_DATA_DIR, 
          "common.db",
          "common",
          NULL,
          32, 
          TRACKER_DB_PAGE_SIZE_DEFAULT, 
          FALSE },
        { TRACKER_DB_CACHE, 
          TRACKER_DB_LOCATION_SYS_TMP_ROOT_DIR,
          "cache.db",
          "cache",
          NULL, 
          128,                          
          TRACKER_DB_PAGE_SIZE_DONT_SET, 
          FALSE },
        { TRACKER_DB_FILE_METADATA,
          TRACKER_DB_LOCATION_DATA_DIR,
          "file-meta.db",
          NULL,
          NULL,
          512,                          
          TRACKER_DB_PAGE_SIZE_DEFAULT, 
          TRUE },
        { TRACKER_DB_FILE_CONTENTS,
          TRACKER_DB_LOCATION_DATA_DIR,
          "file-contents.db",
          NULL,
          NULL,
          1024,
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          FALSE },
        { TRACKER_DB_EMAIL_METADATA,
          TRACKER_DB_LOCATION_DATA_DIR,
          "email-meta.db",
          NULL,
          NULL,
          512, 
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          TRUE },
        { TRACKER_DB_EMAIL_CONTENTS,
          TRACKER_DB_LOCATION_DATA_DIR,
          "email-contents.db",
          NULL,
          NULL,
          512,
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          FALSE },
        { TRACKER_DB_XESAM,
          TRACKER_DB_LOCATION_DATA_DIR,
          "xesam.db",
          NULL,
          NULL,
          512,
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          TRUE },
};

static gboolean    initialized;
static gchar      *services_dir;
static gchar      *sql_dir;
static GHashTable *prepared_queries;

static const gchar * 
location_to_directory (TrackerDBLocation  location,
                       const gchar       *data_dir,
                       const gchar       *user_data_dir,
                       const gchar       *sys_tmp_root_dir)
{
        switch (location) {
        case TRACKER_DB_LOCATION_DATA_DIR:
                return data_dir;
        case TRACKER_DB_LOCATION_USER_DATA_DIR:
                return user_data_dir;
        case TRACKER_DB_LOCATION_SYS_TMP_ROOT_DIR:
                return sys_tmp_root_dir;
        };

	return NULL;
}

void 
configure_database_description (const gchar *data_dir,
                                const gchar *user_data_dir,
                                const gchar *sys_tmp_root_dir)
{
        const gchar *dir;
        guint        i;

        for (i = 0; i < G_N_ELEMENTS (dbs); i++) {
                /* Fill absolute path for the database */
                dir = location_to_directory (dbs[i].location,
                                             data_dir, 
                                             user_data_dir, 
                                             sys_tmp_root_dir);
                
                dbs[i].abs_filename = g_build_filename (dir, dbs[i].file, NULL);
        }
}

static void
load_sql_file (TrackerDBInterface *iface,
	       const gchar        *file,
	       const gchar        *delimiter)
{
	gchar *path, *content, **queries;
	gint i;

	path = tracker_db_manager_get_sql_file (file);

	if (!delimiter) {
		delimiter = ";";
	}

	if (!g_file_get_contents (path, &content, NULL, NULL)) {
		g_critical ("Cannot read SQL file:'%s', please reinstall tracker"
			    " or check read permissions on the file if it exists", file);
		g_assert_not_reached ();
	}

	queries = g_strsplit (content, delimiter, -1);

	for (i = 0; queries[i]; i++) {
		tracker_db_interface_execute_query (iface, NULL, queries[i]);
	}

	g_message ("Loaded SQL file:'%s'", file);

	g_strfreev (queries);
	g_free (content);
	g_free (path);
}

static void
load_metadata_file (TrackerDBInterface *iface,
		    const gchar        *filename)
{
	GKeyFile      *key_file = NULL;
	gchar         *service_file, *str_id;
	gchar        **groups, **keys;
	TrackerField  *def;
	gint           id, i, j;

	key_file = g_key_file_new ();
	service_file = tracker_db_manager_get_service_file (filename);

	if (!g_key_file_load_from_file (key_file, service_file, G_KEY_FILE_NONE, NULL)) {
		g_free (service_file);
		g_key_file_free (key_file);
		return;
	}

	groups = g_key_file_get_groups (key_file, NULL);

	for (i = 0; groups[i]; i++) {
		def = tracker_ontology_get_field_def (groups[i]);

		if (!def) {
			tracker_db_interface_execute_procedure (iface, 
                                                                NULL, 
                                                                "InsertMetadataType", 
                                                                groups[i], 
                                                                NULL);
			id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));
		} else {
			id = atoi (tracker_field_get_id (def));
			g_error ("Duplicated metadata description %s", groups[i]);
		}

		str_id = tracker_uint_to_string (id);
		keys = g_key_file_get_keys (key_file, groups[i], NULL, NULL);

		for (j = 0; keys[j]; j++) {
			gchar *value, *new_value;

			value = g_key_file_get_locale_string (key_file, groups[i], keys[j], NULL, NULL);

			if (!value) {
				continue;
			}

			new_value = tracker_boolean_as_text_to_number (value);
			g_free (value);

			if (strcasecmp (keys[j], "Parent") == 0) {
				tracker_db_interface_execute_procedure (iface, 
                                                                        NULL, 
                                                                        "InsertMetaDataChildren",
									str_id, 
                                                                        new_value, 
                                                                        NULL);
			} else if (strcasecmp (keys[j], "DataType") == 0) {
				GEnumValue *enum_value;

				enum_value = g_enum_get_value_by_nick (g_type_class_peek (TRACKER_TYPE_FIELD_TYPE), new_value);

				if (enum_value) {
					tracker_db_interface_execute_query (iface, NULL,
									    "update MetaDataTypes set DataTypeID = %d where ID = %d",
									    enum_value->value, id);
				}
			} else {
				gchar *esc_value;

				esc_value = tracker_escape_string (new_value);
				tracker_db_interface_execute_query (iface, NULL,
								    "update MetaDataTypes set  %s = '%s' where ID = %d",
								    keys[j], esc_value, id);
				g_free (esc_value);
			}

			g_free (new_value);
		}

		g_free (str_id);
		g_strfreev (keys);
	}

	g_strfreev (groups);
	g_free (service_file);
	g_key_file_free (key_file);
}

static void
load_service_file (TrackerDBInterface *iface, 
		   const gchar        *filename)
{
	TrackerService  *service;
	GKeyFile 	*key_file = NULL;
	gchar 		*service_file, *str_id;
	gchar          **groups, **keys;
	gint             i, j, id;

	service_file = tracker_db_manager_get_service_file (filename);

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, service_file, G_KEY_FILE_NONE, NULL)) {
		g_free (service_file);
		g_key_file_free (key_file);
		return;
	}

	groups = g_key_file_get_groups (key_file, NULL);

	for (i = 0; groups[i]; i++) {
		g_message ("Trying to obtain service:'%s' in cache", groups[i]);
		service = tracker_ontology_get_service_type_by_name (groups[i]);

		if (!service) {
			tracker_db_interface_execute_procedure (iface,
								NULL, 
								"InsertServiceType", 
								groups[i], 
								NULL);
			id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));
		} else {
			id = tracker_service_get_id (service);
		}

		str_id = tracker_uint_to_string (id);

		keys = g_key_file_get_keys (key_file, groups[i], NULL, NULL);

		for (j = 0; keys[j]; j++) {
			if (strcasecmp (keys[j], "TabularMetadata") == 0) {
				gchar **tab_array;
				gint    k;

				tab_array = g_key_file_get_string_list (key_file, 
									groups[i], 
									keys[j], 
									NULL, 
									NULL);

				for (k = 0; tab_array[k]; k++) {
					tracker_db_interface_execute_procedure (iface,
										NULL,
										"InsertServiceTabularMetadata",
										str_id, 
										tab_array[k], 
										NULL);
				}

				g_strfreev (tab_array);
			} else if (strcasecmp (keys[j], "TileMetadata") == 0) {
				gchar **tab_array;
				gint    k;

				tab_array = g_key_file_get_string_list (key_file, 
									groups[i], 
									keys[j], 
									NULL, 
									NULL);

				for (k = 0; tab_array[k]; k++) {
					tracker_db_interface_execute_procedure (iface, 
										NULL,
										"InsertServiceTileMetadata",
										str_id, 
										tab_array[k], 
										NULL);
				}

				g_strfreev (tab_array);
			} else if (strcasecmp (keys[j], "Mimes") == 0) {
				gchar **tab_array;
				gint    k;

				tab_array = g_key_file_get_string_list (key_file, 
									groups[i], 
									keys[j], 
									NULL, 
									NULL);

				for (k = 0; tab_array[k]; k++) {
					tracker_db_interface_execute_procedure (iface, NULL,
										"InsertMimes",
										tab_array[k], 
										NULL);
					tracker_db_interface_execute_query (iface, 
									    NULL,
									    "update FileMimes set ServiceTypeID = %s where Mime = '%s'",
									    str_id, 
									    tab_array[k]);
				}

				g_strfreev (tab_array);
			} else if (strcasecmp (keys[j], "MimePrefixes") == 0) {
				gchar **tab_array;
				gint    k;

				tab_array = g_key_file_get_string_list (key_file, 
									groups[i], 
									keys[j], 
									NULL, 
									NULL);

				for (k = 0; tab_array[k]; k++) {
					tracker_db_interface_execute_procedure (iface, 
										NULL,
										"InsertMimePrefixes",
										tab_array[k],
										NULL);
					tracker_db_interface_execute_query (iface, 
									    NULL,
									    "update FileMimePrefixes set ServiceTypeID = %s where MimePrefix = '%s'",
									    str_id, 
									    tab_array[k]);
				}

				g_strfreev (tab_array);
			} else {
				gchar *value, *new_value, *esc_value;

				value = g_key_file_get_string (key_file, groups[i], keys[j], NULL);
				new_value = tracker_boolean_as_text_to_number (value);
				esc_value = tracker_escape_string (new_value);

				tracker_db_interface_execute_query (iface, 
								    NULL,
								    "update ServiceTypes set  %s = '%s' where TypeID = %s",
								    keys[j], 
								    esc_value, 
								    str_id);
				g_free (esc_value);
				g_free (value);
				g_free (new_value);
			}
		}

		g_free (str_id);
		g_strfreev (keys);
	}

	g_key_file_free (key_file);
	g_strfreev (groups);
	g_free (service_file);
}

static gboolean
load_prepared_queries (void)
{
	GTimer      *t;
	GError      *error = NULL;
	GMappedFile *mapped_file;
	GStrv        queries;
	gchar       *sql_filename;
	gdouble      secs;

	g_message ("Loading prepared queries...");

	sql_filename = tracker_db_manager_get_sql_file ("sqlite-stored-procs.sql");

	t = g_timer_new ();

	mapped_file = g_mapped_file_new (sql_filename, FALSE, &error);

	if (error || !mapped_file) {
		g_warning ("Could not get contents of SQL file:'%s', %s",
			   sql_filename,
			   error ? error->message : "no error given");

		if (mapped_file) {
			g_mapped_file_free (mapped_file);
		}

		g_timer_destroy (t);
		g_free (sql_filename);

		return FALSE;
	}

	g_message ("Loaded prepared queries file:'%s' size:%" G_GSIZE_FORMAT " bytes",
		   sql_filename,
		   g_mapped_file_get_length (mapped_file));

	queries = g_strsplit (g_mapped_file_get_contents (mapped_file), "\n", -1);
	g_free (sql_filename);

	if (queries) {
		GStrv p;

		for (p = queries; *p; p++) {
			GStrv details;

			details = g_strsplit (*p, " ", 2);

			if (!details) {
				continue;
			}

			if (!details[0] || !details[1]) {
				g_strfreev (details);
				continue;
			}

			g_message ("  Adding query:'%s'", details[0]);

			g_hash_table_insert (prepared_queries,
					     g_strdup (details[0]),
					     g_strdup (details[1]));
			g_strfreev (details);
		}

		g_strfreev (queries);
	}

	secs = g_timer_elapsed (t, NULL);
	g_timer_destroy (t);
	g_mapped_file_free (mapped_file);

	g_message ("Found %d prepared queries in %4.4f seconds",
		   g_hash_table_size (prepared_queries),
		   secs);

	return TRUE;
}

static TrackerField *
db_manager_row_to_field_def (TrackerDBResultSet *result_set) 
{
        TrackerField     *field_def;
	TrackerFieldType  field_type;
	gchar            *field_name, *name;
	gint              weight, id;
	gboolean          embedded, multiple_values, delimited, filtered, store_metadata;

	field_def = tracker_field_new ();

	tracker_db_result_set_get (result_set,
				   0, &id,
				   1, &name,
				   2, &field_type,
				   3, &field_name,
				   4, &weight,
				   5, &embedded,
				   6, &multiple_values,
				   7, &delimited,
				   8, &filtered,
				   9, &store_metadata,
				   -1);

	tracker_field_set_id (field_def, tracker_int_to_string (id));
	tracker_field_set_name (field_def, name);
	tracker_field_set_data_type (field_def, field_type);
	tracker_field_set_field_name (field_def, field_name);
	tracker_field_set_weight (field_def, weight);
	tracker_field_set_embedded (field_def, embedded);
	tracker_field_set_multiple_values (field_def, multiple_values);
	tracker_field_set_delimited (field_def, delimited);
	tracker_field_set_filtered (field_def, filtered);
	tracker_field_set_store_metadata (field_def, store_metadata);

	g_free (field_name);
	g_free (name);

	return field_def;
}

static TrackerService *
db_manager_row_to_service (TrackerDBResultSet *result_set)
{
        TrackerService *service;
        GSList         *new_list = NULL;
        gint            id, i;
	gchar          *name, *parent, *content_metadata;
	gboolean        enabled, embedded, has_metadata, has_fulltext;
	gboolean        has_thumbs, show_service_files, show_service_directories;

        service = tracker_service_new ();

	tracker_db_result_set_get (result_set,
				   0, &id,
				   1, &name,
				   2, &parent,
				   3, &enabled,
				   4, &embedded,
				   5, &has_metadata,
				   6, &has_fulltext,
				   7, &has_thumbs,
				   8, &content_metadata,
				   10, &show_service_files,
				   11, &show_service_directories,
				   -1);

        tracker_service_set_id (service, id);
        tracker_service_set_name (service, name);
        tracker_service_set_parent (service, parent);
        tracker_service_set_enabled (service, enabled);
        tracker_service_set_embedded (service, embedded);
        tracker_service_set_has_metadata (service, has_metadata);
        tracker_service_set_has_full_text (service, has_fulltext);
        tracker_service_set_has_thumbs (service, has_thumbs);
	tracker_service_set_content_metadata (service, content_metadata);

#if 0
        if (g_str_has_prefix (name, "Email") ||
            g_str_has_suffix (name, "Emails")) {
                tracker_service_set_db_type (service, TRACKER_DB_TYPE_EMAIL);

                if (tracker->email_service_min == 0 || 
                    id < tracker->email_service_min) {
                        tracker->email_service_min = id;
                }

                if (tracker->email_service_max == 0 || 
                    id > tracker->email_service_max) {
                        tracker->email_service_max = id;
                }
        } else {
                tracker_service_set_db_type (service, TRACKER_DB_TYPE_DATA);
        }
#endif

        tracker_service_set_show_service_files (service, show_service_files);
        tracker_service_set_show_service_directories (service, show_service_directories);

        for (i = 12; i < 23; i++) {
		gchar *metadata;

		tracker_db_result_set_get (result_set, i, &metadata, -1);

		if (metadata) {
			new_list = g_slist_prepend (new_list, metadata);
		}
        }

	/* FIXME: is this necessary? */
#if 0
        /* Hack to prevent db change late in the cycle, check the
         * service name matches "Applications", then add some voodoo.
         */
        if (strcmp (name, "Applications") == 0) {
                /* These strings should be definitions at the top of
                 * this file somewhere really.
                 */
                new_list = g_slist_prepend (new_list, g_strdup ("App:DisplayName"));
                new_list = g_slist_prepend (new_list, g_strdup ("App:Exec"));
                new_list = g_slist_prepend (new_list, g_strdup ("App:Icon"));
        }
#endif

        new_list = g_slist_reverse (new_list);

        tracker_service_set_key_metadata (service, new_list);
	g_slist_foreach (new_list, (GFunc) g_free, NULL);
        g_slist_free (new_list);

        return service;
}

static GSList *
db_manager_mime_query (TrackerDBInterface *iface,
                       const gchar        *stored_proc,
                       gint                service_id)
{
	TrackerDBResultSet *result_set;
	GSList             *result = NULL;
	gchar              *service_id_str;

	service_id_str = g_strdup_printf ("%d", service_id);
	result_set = tracker_db_interface_execute_procedure (iface, 
                                                             NULL, 
                                                             stored_proc, 
                                                             service_id_str, 
                                                             NULL);
	g_free (service_id_str);

	if (result_set) {
		gchar    *str;
		gboolean  valid = TRUE;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &str, -1);
			result = g_slist_prepend (result, str);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	return result;
}

static GSList *
db_manager_get_mimes_for_service_id (TrackerDBInterface *iface,
                                     gint                service_id)
{
	return db_manager_mime_query (iface, "GetMimeForServiceId", service_id);
}

static GSList *
db_manager_get_mime_prefixes_for_service_id (TrackerDBInterface *iface,
                                             gint                service_id)
{
	return db_manager_mime_query (iface, "GetMimePrefixForServiceId", service_id);
}

/* Sqlite utf-8 user defined collation sequence */
static gint
db_manager_utf8_collation_func (gchar *str1,
                                gint   len1,
                                gchar *str2,
                                int    len2)
{
	gchar *word1, *word2;
	gint   result;

	/* Collate words */
	word1 = g_utf8_collate_key_for_filename (str1, len1);
	word2 = g_utf8_collate_key_for_filename (str2, len2);

	result = strcmp (word1, word2);

	g_free (word1);
	g_free (word2);

	return result;
}

/* Converts date/time in UTC format to ISO 8160 standardised format for display */
static GValue
function_date_to_str (TrackerDBInterface *interface,
		      gint                argc,
		      GValue              values[])
{
	GValue  result = { 0, };
	gchar  *str;

	str = tracker_date_to_string (g_value_get_double (&values[0]));
	g_value_init (&result, G_TYPE_STRING);
	g_value_take_string (&result, str);

	return result;
}

static GValue
function_regexp (TrackerDBInterface *interface,
		 gint                argc,
		 GValue              values[])
{
	GValue  result = { 0, };
	regex_t	regex;
	int	ret;

	if (argc != 2) {
		g_critical ("Invalid argument count");
		return result;
	}

	ret = regcomp (&regex,
		       g_value_get_string (&values[0]),
		       REG_EXTENDED | REG_NOSUB);

	if (ret != 0) {
		g_critical ("Error compiling regular expression");
		return result;
	}

	ret = regexec (&regex,
		       g_value_get_string (&values[1]),
		       0, NULL, 0);

	g_value_init (&result, G_TYPE_INT);
	g_value_set_int (&result, (ret == REG_NOMATCH) ? 0 : 1);
	regfree (&regex);

	return result;
}

static GValue
function_get_service_name (TrackerDBInterface *interface,
			   gint                argc,
			   GValue              values[])
{
	GValue  result = { 0, };
	gchar  *str;

	str = tracker_ontology_get_service_type_by_id (g_value_get_int (&values[0]));
	g_value_init (&result, G_TYPE_STRING);
	g_value_take_string (&result, str);

	return result;
}

static GValue
function_get_service_type (TrackerDBInterface *interface,
			   gint                argc,
			   GValue              values[])
{
	GValue result = { 0, };
	gint   id;

	id = tracker_ontology_get_id_for_service_type (g_value_get_string (&values[0]));
	g_value_init (&result, G_TYPE_INT);
	g_value_set_int (&result, id);

	return result;
}

static GValue
function_get_max_service_type (TrackerDBInterface *interface,
			       gint                argc,
			       GValue              values[])
{
	GValue result = { 0, };
	gint   id;

	id = tracker_ontology_get_id_for_service_type (g_value_get_string (&values[0]));
	g_value_init (&result, G_TYPE_INT);
	g_value_set_int (&result, id);

	return result;
}

static void
db_manager_set_params (TrackerDBInterface *iface,
		       gint                cache_size,
		       gint                page_size,
		       gboolean            add_functions)
{
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA synchronous = NORMAL;");
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA count_changes = 0;");
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA temp_store = FILE;");
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA encoding = \"UTF-8\"");
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA auto_vacuum = 0;");

	if (page_size != TRACKER_DB_PAGE_SIZE_DONT_SET) {
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA page_size = %d", page_size);
	}

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA cache_size = %d", cache_size);

	if (add_functions) {
		if (!tracker_db_interface_sqlite_set_collation_function (TRACKER_DB_INTERFACE_SQLITE (iface),
									 "UTF8", 
									 db_manager_utf8_collation_func)) {
			g_critical ("Collation sequence failed");
		}

		/* Create user defined functions that can be used in sql */
		tracker_db_interface_sqlite_create_function (iface, 
							     "FormatDate", 
							     function_date_to_str, 
							     1);
		tracker_db_interface_sqlite_create_function (iface, 
							     "GetServiceName", 
							     function_get_service_name, 
							     1);
		tracker_db_interface_sqlite_create_function (iface, 
							     "GetServiceTypeID", 
							     function_get_service_type, 
							     1);
		tracker_db_interface_sqlite_create_function (iface, 
							     "GetMaxServiceTypeID", 
							     function_get_max_service_type, 
							     1);
		tracker_db_interface_sqlite_create_function (iface, 
							     "REGEXP",
							     function_regexp,
							     2);
	}
}

static void
db_manager_get_static_data (TrackerDBInterface *iface)
{
	TrackerDBResultSet *result_set;

	/* Get static metadata info */
	result_set = tracker_db_interface_execute_procedure (iface, 
							     NULL, 
							     "GetMetadataTypes", 
							     NULL);

	if (result_set) {
		gboolean valid = TRUE;
		gint     id;

		while (valid) {
			TrackerDBResultSet *result_set2;
			TrackerField       *def;
			GSList             *child_ids = NULL;

			def = db_manager_row_to_field_def (result_set);

			result_set2 = tracker_db_interface_execute_procedure (iface, 
									      NULL,
									      "GetMetadataAliases",
									      tracker_field_get_id (def), 
									      NULL);

			if (result_set2) {
				valid = TRUE;

				while (valid) {
					tracker_db_result_set_get (result_set2, 1, &id, -1);
					child_ids = g_slist_prepend (child_ids,
								     tracker_int_to_string (id));

					valid = tracker_db_result_set_iter_next (result_set2);
				}

				tracker_field_set_child_ids (def, child_ids);
				g_object_unref (result_set2);
			}

			g_message ("Loading metadata def:'%s' with weight:%d",
				   tracker_field_get_name (def),
				   tracker_field_get_weight (def));

			tracker_ontology_add_field (def);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	/* Get static service info */
	result_set = tracker_db_interface_execute_procedure (iface,
							     NULL, 
							     "GetAllServices", 
							     NULL);

	if (result_set) {
		gboolean valid = TRUE;

		while (valid) {
			TrackerService *service;
			GSList          *mimes, *mime_prefixes;
			const gchar     *name;
			gint             id;

                        service = db_manager_row_to_service (result_set);

                        if (!service) {
                                continue;
			}

                        id = tracker_service_get_id (service);
                        name = tracker_service_get_name (service);

                        mimes = db_manager_get_mimes_for_service_id (iface, id);
                        mime_prefixes = db_manager_get_mime_prefixes_for_service_id (iface, id);

                        g_message ("Adding service:'%s' with id:%d and mimes:%d",
				   name,
				   id,
				   g_slist_length (mimes));

                        tracker_ontology_add_service_type (service,
							   mimes,
							   mime_prefixes);

                        g_slist_free (mimes);
                        g_slist_free (mime_prefixes);
                        g_object_unref (service);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}
}

TrackerDBInterface *
db_manager_get_common (void)
{
	TrackerDBInterface *interface;
	const gchar        *path;
	gboolean            create = FALSE;

	path = tracker_db_manager_get_file (TRACKER_DB_COMMON);

	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		create = TRUE;
	}

	interface = tracker_db_interface_sqlite_new (path);
	tracker_db_interface_set_procedure_table (interface, prepared_queries);

	if (create) {
		/* Create tables */
		load_sql_file (interface, "sqlite-tracker.sql", NULL);
		load_sql_file (interface, "sqlite-metadata.sql", NULL);
		load_sql_file (interface, "sqlite-service-types.sql", NULL);

		/* Load services info */
		load_service_file (interface, "default.service");

		/* Load metadata info */
		load_metadata_file (interface, "default.metadata");
		load_metadata_file (interface, "file.metadata");
		load_metadata_file (interface, "audio.metadata");
		load_metadata_file (interface, "application.metadata");
		load_metadata_file (interface, "document.metadata");
		load_metadata_file (interface, "email.metadata");
		load_metadata_file (interface, "image.metadata");
		load_metadata_file (interface, "video.metadata");
	}

	/* Load static data into tracker ontology */
	db_manager_get_static_data (interface);

	return interface;
}

TrackerDBInterface *
db_manager_get_file_metadata (void)
{
	TrackerDBInterface *interface;
	const gchar *path;
	gboolean create = FALSE;

	path = tracker_db_manager_get_file (TRACKER_DB_FILE_METADATA);

	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		create = TRUE;
	}

	interface = tracker_db_interface_sqlite_new (path);
	tracker_db_interface_set_procedure_table (interface, prepared_queries);

	db_manager_set_params (interface,
			       tracker_db_manager_get_cache_size (TRACKER_DB_FILE_METADATA),
			       tracker_db_manager_get_page_size (TRACKER_DB_FILE_METADATA),
			       tracker_db_manager_get_add_functions (TRACKER_DB_FILE_METADATA));

	if (create) {
		load_sql_file (interface, "sqlite-service.sql", NULL);
		load_sql_file (interface, "sqlite-service-triggers.sql", "!");
	}

	return interface;
}

const gchar *
db_manager_db_type_to_string (TrackerDB db)
{
        GType       type;
        GEnumClass *enum_class;
        GEnumValue *enum_value;

        type = tracker_db_get_type ();
        enum_class = G_ENUM_CLASS (g_type_class_peek (type));
        enum_value = g_enum_get_value (enum_class, db);
        
        if (!enum_value) {
                return "unknown";
        }

        return enum_value->value_nick;
}

GType
tracker_db_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			{ TRACKER_DB_COMMON, 
			  "TRACKER_DB_COMMON", 
			  "common" },
			{ TRACKER_DB_CACHE, 
			  "TRACKER_DB_CACHE", 
			  "cache" },
			{ TRACKER_DB_FILE_METADATA, 
			  "TRACKER_DB_FILE_METADATA", 
			  "file-meta" },
			{ TRACKER_DB_FILE_CONTENTS, 
			  "TRACKER_DB_FILE_CONTENTS", 
			  "file-contents" },
			{ TRACKER_DB_EMAIL_METADATA, 
			  "TRACKER_DB_EMAIL_METADATA", 
			  "email-meta" },
			{ TRACKER_DB_EMAIL_CONTENTS, 
			  "TRACKER_DB_EMAIL_CONTENTS", 
			  "email-contents" },
			{ TRACKER_DB_XESAM, 
			  "TRACKER_DB_XESAM", 
			  "xesam" },
			{ 0, NULL, NULL }
		};

		etype = g_enum_register_static ("TrackerDBType", values);
	}

	return etype;
}

void
tracker_db_manager_init (const gchar *data_dir, 
                         const gchar *user_data_dir,
                         const gchar *sys_tmp_dir) 
{
        g_return_if_fail (data_dir != NULL);
        g_return_if_fail (user_data_dir != NULL);
        g_return_if_fail (sys_tmp_dir != NULL);

        if (initialized) {
		return;
	}

	services_dir = g_build_filename (SHAREDIR, 
					 "tracker", 
					 "services", 
					 NULL);
	sql_dir = g_build_filename (SHAREDIR, 
				    "tracker", 
				    NULL);
	
	configure_database_description (data_dir, 
					user_data_dir, 
					sys_tmp_dir);
	
	prepared_queries = g_hash_table_new_full (g_str_hash,
						  g_str_equal,
						  g_free,
						  g_free);
	
	load_prepared_queries ();
	
	initialized = TRUE;
}

void
tracker_db_manager_shutdown (void) 
{
        guint i;

        if (!initialized) {
                return;
        }

	g_hash_table_unref (prepared_queries);
	prepared_queries = NULL;

        for (i = 0; i < G_N_ELEMENTS (dbs); i++) {
                if (dbs[i].abs_filename) {
                        g_free (dbs[i].abs_filename);
                }
        }

        g_free (services_dir);
        g_free (sql_dir);

        initialized = FALSE;
}

const gchar *
tracker_db_manager_get_file (TrackerDB db) 
{
        return dbs[db].abs_filename;
}

gboolean
tracker_db_manager_file_exists (TrackerDB db) 
{
        return g_file_test (dbs[db].abs_filename, G_FILE_TEST_IS_REGULAR);
}

gchar *
tracker_db_manager_get_service_file (const gchar *service_file)
{
        g_return_val_if_fail (service_file != NULL, NULL);

        return g_build_filename (services_dir, service_file, NULL);
}

gchar *
tracker_db_manager_get_sql_file (const gchar *sql_file) 
{
        g_return_val_if_fail (sql_file != NULL, NULL);

        return g_build_filename (sql_dir, sql_file, NULL);
}

gint         
tracker_db_manager_get_cache_size (TrackerDB db)
{
        return dbs[db].cache_size;
}

gint         
tracker_db_manager_get_page_size (TrackerDB db)
{
        return dbs[db].page_size;
}

gboolean     
tracker_db_manager_get_add_functions (TrackerDB db)
{
        return dbs[db].add_functions;
}

const gchar *
tracker_db_manager_get_name (TrackerDB db)
{
        return dbs[db].name;
}

TrackerDBInterface *
tracker_db_manager_get_db_interface (TrackerDB db)
{
	switch (db) {
        case TRACKER_DB_COMMON:
		return db_manager_get_common ();

        case TRACKER_DB_CACHE:
        case TRACKER_DB_FILE_METADATA:
		return db_manager_get_file_metadata ();

        case TRACKER_DB_FILE_CONTENTS:
        case TRACKER_DB_EMAIL_METADATA:
        case TRACKER_DB_EMAIL_CONTENTS:
	case TRACKER_DB_XESAM:
		g_critical ("This TrackerDB type:%d->'%s' has no interface set up yet!!",
			    db,
			    db_manager_db_type_to_string (db));
		break;
	}

	return NULL;
}
