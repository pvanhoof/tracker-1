/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include <fcntl.h>
#include <regex.h>
#include <zlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-utils.h>
#include <libtracker-common/tracker-nfs-lock.h>

#include <libtracker-db/tracker-db-interface-sqlite.h>

#include "tracker-db-sqlite.h"
#include "tracker-db-manager.h"
#include "tracker-indexer.h"
#include "tracker-cache.h"
#include "tracker-metadata.h"
#include "tracker-utils.h"
#include "tracker-watch.h"
#include "tracker-ontology.h"
#include "tracker-query-tree.h"
#include "tracker-xesam.h"
#include "tracker-main.h"

#define MAX_INDEX_TEXT_LENGTH 1048576
#define MAX_TEXT_BUFFER       65567
#define MAX_COMPRESS_BUFFER   65565
#define ZLIBBUFSIZ            8192

extern Tracker *tracker;

static GHashTable *prepared_queries;
//static GMutex *sequence_mutex;

typedef struct {
	guint32		service_id;
	int		service_type_id;
} ServiceTypeInfo;


/* sqlite utf-8 user defined collation sequence */

static int 
utf8_collation_func (gchar *str1, gint len1, gchar *str2, int len2)
{
	char *word1, *word2;
	int result;

	/* collate words */

	word1 = g_utf8_collate_key_for_filename (str1, len1);
	word2 = g_utf8_collate_key_for_filename (str2, len2);
	
	result = strcmp (word1, word2);
	
	g_free (word1);
	g_free (word2);

	return result;
}
 	



/* sqlite user defined functions for use in sql */

/* converts date/time in UTC format to ISO 8160 standardised format for display */
static GValue
function_date_to_str (TrackerDBInterface *interface,
		      gint                argc,
		      GValue              values[])
{
	GValue result = { 0, };
	gchar *str;

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
	GValue result = { 0, };
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

static gchar *
function_compress_string (const gchar *ptr, 
			  gint         size, 
			  gint        *compressed_size)
{
	z_stream       zs;
	gchar         *buf, *swap;
	unsigned char  obuf[ZLIBBUFSIZ];
	gint           rv, asiz, bsiz, osiz;

	if (size < 0) { 
		size = strlen (ptr);
	}

	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	if (deflateInit2 (&zs, 6, Z_DEFLATED, 15, 6, Z_DEFAULT_STRATEGY) != Z_OK) {
		return NULL;
	}

	asiz = size + 16;

	if (asiz < ZLIBBUFSIZ) {
		asiz = ZLIBBUFSIZ;
	}

	if (!(buf = malloc (asiz))) {
		deflateEnd (&zs);
		return NULL;
	}

	bsiz = 0;
	zs.next_in = (unsigned char *)ptr;
	zs.avail_in = size;
	zs.next_out = obuf;
	zs.avail_out = ZLIBBUFSIZ;

	while ((rv = deflate (&zs, Z_FINISH)) == Z_OK) {
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

static gchar *
function_uncompress_string (const gchar *ptr, 
			    gint         size, 
			    gint        *uncompressed_size)
{
	z_stream       zs;
	gchar         *buf, *swap;
	unsigned char  obuf[ZLIBBUFSIZ];
	gint           rv, asiz, bsiz, osiz;

	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	if (inflateInit2 (&zs, 15) != Z_OK) {
		return NULL;
	}

	asiz = size * 2 + 16;

	if (asiz < ZLIBBUFSIZ) {
		asiz = ZLIBBUFSIZ;
	}

	if (!(buf = malloc (asiz))) {
		inflateEnd (&zs);
		return NULL;
	}

	bsiz = 0;
	zs.next_in = (unsigned char *)ptr;
	zs.avail_in = size;
	zs.next_out = obuf;
	zs.avail_out = ZLIBBUFSIZ;

	while ((rv = inflate (&zs, Z_NO_FLUSH)) == Z_OK) {
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

/* unzips data */
static GValue
function_uncompress (TrackerDBInterface *interface,
		     gint                argc,
		     GValue              values[])
{
	GByteArray *array;
	GValue result = { 0, };
	gchar *output;
	gint len;

	array = g_value_get_boxed (&values[0]);

	if (!array) {
		return result;
	}

	output = function_uncompress_string ((const gchar *) array->data, array->len, &len);

	if (!output) {
		g_warning ("Uncompress failed");
		return result;
	}

	g_value_init (&result, G_TYPE_STRING);
	g_value_take_string (&result, output);

	return result;
}

static GValue
function_get_service_name (TrackerDBInterface *interface,
			   gint                argc,
			   GValue              values[])
{
	GValue result = { 0, };
	gchar *str;

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
	gint id;

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
	gint id;

	id = tracker_ontology_get_id_for_service_type (g_value_get_string (&values[0]));
	g_value_init (&result, G_TYPE_INT);
	g_value_set_int (&result, id);

	return result;
}

static void
load_generic_sql_file (TrackerDBInterface *iface, const gchar *sql_file, const gchar *delimiter) { 

	char *filename, *query;
	
	filename = tracker_db_manager_get_sql_file (sql_file);

	if (!g_file_get_contents (filename, &query, NULL, NULL)) {
		tracker_error ("ERROR: Tracker cannot read required file %s - Please reinstall tracker or check read permissions on the file if it exists", sql_file);
		g_assert (FALSE);
	} else {
		char **queries, **queries_p ;

		queries = g_strsplit_set (query, delimiter, -1);

		for (queries_p = queries; *queries_p; queries_p++) {
			tracker_db_exec_no_reply (iface, *queries_p);
		}
		g_strfreev (queries);
		g_free (query);
		tracker_log ("loaded sql file %s", sql_file);
	}

	g_free (filename);
}

static void
load_sql_file (TrackerDBInterface *iface, const char *sql_file)
{
	load_generic_sql_file (iface, sql_file, ";");
}

static void
load_sql_trigger (TrackerDBInterface *iface, const char *sql_file)
{
	load_generic_sql_file (iface, sql_file, "!");
}

static void
load_service_file (TrackerDBInterface *iface, const gchar *filename) 
{
	GKeyFile 		*key_file = NULL;
	const gchar * const 	*locale_array;
	gchar 			*service_file, *str_id;
	gchar                  **groups, **keys;
	gchar                  **group, **key;
	TrackerService          *service;
	gint                    id;

	service_file = tracker_db_manager_get_service_file (filename);

	locale_array = g_get_language_names ();

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, service_file, G_KEY_FILE_NONE, NULL)) {
		g_free (service_file);
		g_key_file_free (key_file);
		return;
	}
	
	groups = g_key_file_get_groups (key_file, NULL);

	for (group = groups; *group; group++) {

				
		tracker_log ("Trying to obtain service %s in cache", *group);
		service = tracker_ontology_get_service_type_by_name (*group);

		if (!service) {
			tracker_db_exec_proc (iface, "InsertServiceType", *group, NULL);
			id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));
		} else {
			id = tracker_service_get_id (service);
		}

		str_id = tracker_uint_to_string (id);

		keys = g_key_file_get_keys (key_file, *group, NULL, NULL);
		
		for (key = keys; *key; key++) {

			gchar *value = g_key_file_get_locale_string (key_file, *group, *key, locale_array[0], NULL);

			if (!value) {
				continue;
			}
			
			gchar *new_value = tracker_boolean_as_text_to_number (value);
			g_free (value);


			if (strcasecmp (*key, "TabularMetadata") == 0) {

				char **tab_array = g_key_file_get_string_list (key_file, *group, *key, NULL, NULL);

				char **tmp;
				for (tmp = tab_array; *tmp; tmp++) { 			

					tracker_db_exec_proc (iface, "InsertServiceTabularMetadata", str_id, *tmp, NULL);
								
				}

				g_strfreev (tab_array);



			} else if (strcasecmp (*key, "TileMetadata") == 0) {

				char **tab_array = g_key_file_get_string_list (key_file, *group, *key, NULL, NULL);

				char **tmp;
				for (tmp = tab_array; *tmp; tmp++) { 			

					tracker_db_exec_proc (iface, "InsertServiceTileMetadata", str_id, *tmp, NULL);
				}

				g_strfreev (tab_array);

			} else if (strcasecmp (*key, "Mimes") == 0) {

				char **tab_array = g_key_file_get_string_list (key_file, *group, *key, NULL, NULL);

				char **tmp;
				for (tmp = tab_array; *tmp; tmp++) { 			
					tracker_db_exec_proc (iface, "InsertMimes", *tmp, NULL);
							
					tracker_db_exec_no_reply (iface,
								  "update FileMimes set ServiceTypeID = %s where Mime = '%s'",
								  str_id, *tmp);
				}

				g_strfreev (tab_array);

			} else if (strcasecmp (*key, "MimePrefixes") == 0) {

				char **tab_array = g_key_file_get_string_list (key_file, *group, *key, NULL, NULL);

				char **tmp;
				for (tmp = tab_array; *tmp; tmp++) { 			
					tracker_db_exec_proc (iface, "InsertMimePrefixes", *tmp, NULL);

					tracker_db_exec_no_reply (iface,
								  "update FileMimePrefixes set ServiceTypeID = %s where MimePrefix = '%s'",
								  str_id, *tmp);
				}

				g_strfreev (tab_array);


			} else {
				char *esc_value = tracker_escape_string (new_value);

				tracker_db_exec_no_reply (iface,
							  "update ServiceTypes set  %s = '%s' where TypeID = %s",
							  *key, esc_value, str_id);
				g_free (esc_value);
			}
			g_free (new_value);


		}
		g_free (str_id);
		g_strfreev (keys);
	}
	g_strfreev (groups);
	g_free (service_file);
}

static void
load_metadata_file (TrackerDBInterface *iface, const gchar *filename) 
{
	GKeyFile 		*key_file = NULL;
	const gchar * const 	*locale_array;
	gchar 			*service_file, *str_id;
	gchar                  **groups, **keys;
	gchar                  **group, **key;
	const TrackerField      *def;
	gint                     id;
	gchar                    *DataTypeArray[11] = {"Keyword", "Indexable", "CLOB", 
						      "String", "Integer", "Double", 
						      "DateTime", "BLOB", "Struct", 
						      "Link", NULL};

	service_file = tracker_db_manager_get_service_file (filename);

	locale_array = g_get_language_names ();

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, service_file, G_KEY_FILE_NONE, NULL)) {
		g_free (service_file);
		g_key_file_free (key_file);
		return;
	}
	
	groups = g_key_file_get_groups (key_file, NULL);

	for (group = groups; *group; group++) {

		def = tracker_ontology_get_field_def (*group);

		if (!def) {
			tracker_db_exec_proc (iface, "InsertMetadataType", *group, NULL);
			id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));
		} else {
			id = atoi (tracker_field_get_id (def));
			g_error ("Duplicated metadata description %s", *group);
		}

		str_id = tracker_uint_to_string (id);

		keys = g_key_file_get_keys (key_file, *group, NULL, NULL);
		
		for (key = keys; *key; key++) {

			gchar *value = g_key_file_get_locale_string (key_file, *group, *key, locale_array[0], NULL);

			if (!value) {
				continue;
			}
			
			gchar *new_value = tracker_boolean_as_text_to_number (value);
			g_free (value);


			if (strcasecmp (*key, "Parent") == 0) {
				
				tracker_db_exec_proc (iface, "InsertMetaDataChildren", str_id, new_value, NULL);
				
			} else if (strcasecmp (*key, "DataType") == 0) {
				
				int data_id = tracker_string_in_string_list (new_value, DataTypeArray);
				
				if (data_id != -1) {
					tracker_db_exec_no_reply (iface,
								  "update MetaDataTypes set DataTypeID = %d where ID = %s",
								  data_id, str_id);
				}
				
				
			} else {
				char *esc_value = tracker_escape_string (new_value);
				
				tracker_db_exec_no_reply (iface,
							  "update MetaDataTypes set  %s = '%s' where ID = %s",
							  *key, esc_value, str_id);
				g_free (esc_value);
			}
			g_free (new_value);
		}
		g_free (str_id);
		g_strfreev (keys);
	}
	g_strfreev (groups);
	g_free (service_file);
}

static void
load_extractor_file (TrackerDBInterface *iface, const gchar *filename)
{
	GKeyFile 		*key_file = NULL;
	const gchar * const 	*locale_array;
	gchar 			*service_file, *str_id;
	gchar                  **groups, **keys;
	gchar                  **group, **key;
	gint                     id;

	service_file = tracker_db_manager_get_service_file (filename);

	locale_array = g_get_language_names ();

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, service_file, G_KEY_FILE_NONE, NULL)) {
		g_free (service_file);
		g_key_file_free (key_file);
		return;
	}
	
	groups = g_key_file_get_groups (key_file, NULL);

	for (group = groups; *group; group++) {

		/* Obtain last id */
		id = 0;
		str_id = tracker_uint_to_string (id);

		keys = g_key_file_get_keys (key_file, *group, NULL, NULL);
		
		for (key = keys; *key; key++) {

			gchar *value = g_key_file_get_locale_string (key_file, *group, *key, locale_array[0], NULL);

			if (!value) {
				continue;
			}
			
			gchar *new_value = tracker_boolean_as_text_to_number (value);
			g_free (value);

			/* to do - support extractors */

			g_free (new_value);
		}
		g_free (str_id);
		g_strfreev (keys);
	}
	g_strfreev (groups);
	g_free (service_file);
}

static gboolean
load_service_description_file (TrackerDBInterface *iface, const gchar* filename)
{

	if (g_str_has_suffix (filename, ".metadata")) {
		load_metadata_file (iface, filename);

	} else if (g_str_has_suffix (filename, ".service")) {
		load_service_file (iface, filename);

	} else if (g_str_has_suffix (filename, ".extractor")) {
		load_extractor_file (iface, filename);

	} else {
		return FALSE;
	} 

	return TRUE;
}


gboolean
tracker_db_load_prepared_queries (void)
{
	GTimer      *t;
	GError      *error = NULL;
	GMappedFile *mapped_file;
	GStrv        queries;
	gchar       *sql_filename;
	gdouble      secs;

	tracker_log ("Loading prepared queries...");

	prepared_queries = g_hash_table_new_full (g_str_hash, 
						  g_str_equal, 
						  g_free, 
						  g_free);

	sql_filename = tracker_db_manager_get_sql_file ("sqlite-stored-procs.sql");

	t = g_timer_new ();

	mapped_file = g_mapped_file_new (sql_filename, FALSE, &error);

	if (error || !mapped_file) {
		tracker_debug ("Could not get contents of SQL file:'%s', %s",
			       sql_filename,
			       error ? error->message : "no error given");

		if (mapped_file) {
			g_mapped_file_free (mapped_file);
		}

		g_timer_destroy (t);
		g_free (sql_filename);

		return FALSE;
	}

	tracker_debug ("Opened prepared queries file:'%s' size:%" G_GSIZE_FORMAT " bytes", 
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
				continue;
			}

			tracker_debug ("  Adding query:'%s'", details[0]);

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
	
	tracker_log ("Found %d prepared queries in %4.4f seconds", 
		     g_hash_table_size (prepared_queries), 
		     secs);

	return TRUE;
}

void
tracker_db_close (TrackerDBInterface *iface)
{
	if (iface) {
		g_object_unref (iface);
	}
	tracker_debug ("Database closed");
}



static void
set_params (TrackerDBInterface *iface, int cache_size, int page_size, gboolean add_functions)
{
	tracker_db_exec_no_reply (iface, "PRAGMA synchronous = NORMAL;");
	tracker_db_exec_no_reply (iface, "PRAGMA count_changes = 0;");
	tracker_db_exec_no_reply (iface, "PRAGMA temp_store = FILE;");
	tracker_db_exec_no_reply (iface, "PRAGMA encoding = \"UTF-8\"");
	tracker_db_exec_no_reply (iface, "PRAGMA auto_vacuum = 0;");

	if (page_size != TRACKER_DB_PAGE_SIZE_DONT_SET) {
		tracker_db_exec_no_reply (iface, "PRAGMA page_size = %d", page_size);
	}

	if (tracker_config_get_low_memory_mode (tracker->config)) {
		cache_size /= 2;
	}

	tracker_db_exec_no_reply (iface, "PRAGMA cache_size = %d", cache_size);

	if (add_functions) {

		if (!tracker_db_interface_sqlite_set_collation_function (TRACKER_DB_INTERFACE_SQLITE (iface),
									 "UTF8", utf8_collation_func)) {
			tracker_error ("ERROR: collation sequence failed");
		}

		/* create user defined functions that can be used in sql */
		tracker_db_interface_sqlite_create_function (iface, "FormatDate", function_date_to_str, 1);
		tracker_db_interface_sqlite_create_function (iface, "GetServiceName", function_get_service_name, 1);
		tracker_db_interface_sqlite_create_function (iface, "GetServiceTypeID", function_get_service_type, 1);
		tracker_db_interface_sqlite_create_function (iface, "GetMaxServiceTypeID", function_get_max_service_type, 1);
		tracker_db_interface_sqlite_create_function (iface, "REGEXP", function_regexp, 2);
	}
}


/*
 * If the file doesnt exist, creates a new file of size 0
 */
static TrackerDBInterface *
open_db_interface (TrackerDatabase database)
{
	TrackerDBInterface *iface;
	const gchar *dbname;


	dbname = tracker_db_manager_get_file (database);

	/* We pass a GThreadPool here, it should be the same pool for all opened
	 * SQLite databases */
	iface = tracker_db_interface_sqlite_new (dbname);
	tracker_db_interface_set_procedure_table (iface, prepared_queries);


	set_params (iface,
		    tracker_db_manager_get_cache_size (database),
		    tracker_db_manager_get_page_size (database),
		    tracker_db_manager_get_add_functions (database));
	return iface;

}


DBConnection *
tracker_db_connect_common (void)
{

	DBConnection *db_con;

	db_con = g_new0 (DBConnection, 1);

	db_con->db = open_db_interface (TRACKER_DB_COMMON);

	db_con->cache = NULL;
	db_con->emails = NULL;
	db_con->blob = NULL;
	db_con->common = db_con;

	return db_con;
}

void
tracker_db_attach_db (DBConnection *db_con, TrackerDatabase database)
{
	if (database != TRACKER_DB_COMMON && database != TRACKER_DB_CACHE) {
		tracker_error ("Attaching invalid db");
		return;
	}

	tracker_db_exec_no_reply (db_con->db, 
				  "ATTACH '%s' as %s",
				  tracker_db_manager_get_file (database),
				  tracker_db_manager_get_name (database));
}

static inline void
free_db_con (DBConnection *db_con)
{
	g_free (db_con);
	db_con = NULL;
}

/* convenience function for process files thread */
DBConnection *
tracker_db_connect_all ()
{

	DBConnection *db_con;
	DBConnection *blob_db_con = NULL;
	Indexer *word_index_db_con = NULL;

	DBConnection *common_db_con = NULL;

	DBConnection *emails_blob_db_con = NULL;
	DBConnection *emails_db_con= NULL;
	Indexer *email_word_index_db_con= NULL;

	db_con = tracker_db_connect_file_meta ();
	emails_db_con = tracker_db_connect_email_meta ();

	blob_db_con = tracker_db_connect_file_content ();
	emails_blob_db_con = tracker_db_connect_email_content ();
	common_db_con  = tracker_db_connect_common ();

	word_index_db_con = tracker->file_index;
	email_word_index_db_con = tracker->email_index;

	db_con->cache = tracker_db_connect_cache ();

	db_con->blob = blob_db_con;
	db_con->data = db_con;
	db_con->emails = emails_db_con;
	db_con->common = common_db_con;
	db_con->word_index = word_index_db_con;

	emails_db_con->common = common_db_con;
	emails_db_con->blob = emails_blob_db_con;
	emails_db_con->data = db_con;
	emails_db_con->word_index = email_word_index_db_con;
	emails_db_con->cache = db_con->cache;

	tracker_db_attach_db (db_con, TRACKER_DB_COMMON);
	tracker_db_attach_db (db_con, TRACKER_DB_CACHE);
	
	return db_con;

}



/* convenience function for process files thread */
DBConnection *
tracker_db_connect_xesam ()
{

	DBConnection *db_con;
	DBConnection *blob_db_con = NULL;
	Indexer *word_index_db_con = NULL;

	DBConnection *common_db_con = NULL;

	DBConnection *emails_blob_db_con = NULL;
	DBConnection *emails_db_con= NULL;
	Indexer *email_word_index_db_con= NULL;

	db_con = tracker_db_connect_file_meta ();
	emails_db_con = tracker_db_connect_email_meta ();

	blob_db_con = tracker_db_connect_file_content ();
	emails_blob_db_con = tracker_db_connect_email_content ();
	common_db_con  = tracker_db_connect_common ();

	word_index_db_con = tracker->file_index;
	email_word_index_db_con = tracker->email_index;

	db_con->cache = tracker_db_connect_cache ();

	db_con->blob = blob_db_con;
	db_con->data = db_con;
	db_con->emails = emails_db_con;
	db_con->common = common_db_con;
	db_con->word_index = word_index_db_con;

	emails_db_con->common = common_db_con;
	emails_db_con->blob = emails_blob_db_con;
	emails_db_con->data = db_con;
	emails_db_con->word_index = email_word_index_db_con;
	emails_db_con->cache = db_con->cache;

	tracker_db_attach_db (db_con, TRACKER_DB_COMMON);
	tracker_db_attach_db (db_con, TRACKER_DB_CACHE);
	
	return db_con;

}

void
tracker_db_close_all (DBConnection *db_con)
{

	DBConnection *email_db_con = db_con->emails;
	DBConnection *email_blob_db_con = email_db_con->blob;

	DBConnection *common_db_con = db_con->common;
	DBConnection *cache_db_con = db_con->cache;

	DBConnection *file_blob_db_con = db_con->blob;


	/* close emails */
	if (email_blob_db_con) {
		tracker_db_close (email_blob_db_con->db);
		free_db_con (email_blob_db_con);
	}
		
	if (email_db_con) {
		tracker_db_close (email_db_con->db);
		g_free (email_db_con);
	}


	/* close files */
	if (file_blob_db_con) {
		tracker_db_close (file_blob_db_con->db);
		free_db_con (file_blob_db_con);
	}

	tracker_db_close (db_con->db);
	g_free (db_con);


	/* close others */
	if (common_db_con) {
		tracker_db_close (common_db_con->db);
		free_db_con (common_db_con);
	}

	
	if (cache_db_con) {
		tracker_db_close (cache_db_con->db);
		free_db_con (cache_db_con);
	}


}

gboolean
tracker_db_is_in_transaction (DBConnection *db_con) 
{
	gboolean in_transaction;

	g_object_get (db_con->db, "in-transaction", &in_transaction, NULL);
	
	return in_transaction;
}

void
tracker_db_start_index_transaction (DBConnection *db_con)
{
	DBConnection *email_db_con = db_con->emails;

	tracker_db_interface_start_transaction (db_con->common->db);

	/* files */
	tracker_db_interface_start_transaction (db_con->db);
	tracker_db_interface_start_transaction (db_con->blob->db);

	/* emails */
	tracker_db_interface_start_transaction (email_db_con->db);
	tracker_db_interface_start_transaction (email_db_con->blob->db);
}



void
tracker_db_end_index_transaction (DBConnection *db_con)
{
	DBConnection *email_db_con = db_con->emails;

	tracker_db_interface_end_transaction (db_con->common->db);

	/* files */
	tracker_db_interface_end_transaction (db_con->db);
	tracker_db_interface_end_transaction (db_con->blob->db);

	/* emails */
	tracker_db_interface_end_transaction (email_db_con->db);
	tracker_db_interface_end_transaction (email_db_con->blob->db);
}


DBConnection *
tracker_db_connect (void)
{
	DBConnection *db_con;
	gboolean create_table = FALSE;

	create_table = !tracker_db_manager_file_exists (TRACKER_DB_FILE_META);

	db_con = tracker_db_connect_file_meta ();

	db_con->data = db_con;
	
	if (create_table) {
		tracker_log ("Creating file database... %s",
			     tracker_db_manager_get_file (TRACKER_DB_FILE_META));
		load_sql_file (db_con->db, "sqlite-service.sql");
		load_sql_trigger (db_con->db, "sqlite-service-triggers.sql");

		load_sql_file (db_con->db, "sqlite-metadata.sql");
	
		load_service_description_file (db_con->db, "default.metadata");
		load_service_description_file (db_con->db, "file.metadata");
		load_service_description_file (db_con->db, "audio.metadata");
		load_service_description_file (db_con->db, "application.metadata");
		load_service_description_file (db_con->db, "document.metadata");
		load_service_description_file (db_con->db, "email.metadata");
		load_service_description_file (db_con->db, "image.metadata");	
		load_service_description_file (db_con->db, "video.metadata");	

		load_sql_file (db_con->db, "sqlite-xesam.sql");

		tracker_db_load_xesam_service_file (db_con, "xesam.metadata");
		tracker_db_load_xesam_service_file (db_con, "xesam-convenience.metadata");
		tracker_db_load_xesam_service_file (db_con, "xesam-virtual.metadata");
		tracker_db_load_xesam_service_file (db_con, "xesam.service");
		tracker_db_load_xesam_service_file (db_con, "xesam-convenience.service");
		tracker_db_load_xesam_service_file (db_con, "xesam-service.smapping");
		tracker_db_load_xesam_service_file (db_con, "xesam-metadata.mmapping");

		tracker_db_create_xesam_lookup(db_con);

		tracker_db_exec_no_reply (db_con->db, "ANALYZE");
	}

	// TODO: move tables Events and XesamLiveSearches from sqlite-service.sql
	// to TEMPORARY tables in sqlite-temp-tables.sql:

	// load_sql_file (db_con->db, "sqlite-temp-tables.sql");

	tracker_db_attach_db (db_con, TRACKER_DB_COMMON);
	tracker_db_attach_db (db_con, TRACKER_DB_CACHE);

	/* this is not needed, it's the root db
	tracker_db_attach_db (db_con, TRACKER_DB_FILE_META); */

	db_con->cache = db_con;
	db_con->common = db_con;

	return db_con;
}

static inline void
open_file_db (DBConnection *db_con)
{
	db_con->db = open_db_interface (TRACKER_DB_FILE_META);
}

DBConnection *
tracker_db_connect_file_meta (void)
{
	DBConnection *db_con;

	db_con = g_new0 (DBConnection, 1);

	db_con->db = open_db_interface (TRACKER_DB_FILE_META);

	return db_con;
}


static inline void
open_email_db (DBConnection *db_con)
{
	db_con->db = open_db_interface (TRACKER_DB_EMAIL_META);
}

DBConnection *
tracker_db_connect_email_meta (void)
{
	DBConnection *db_con;

	db_con = g_new0 (DBConnection, 1);

	db_con->emails = db_con;

	open_email_db (db_con);

	return db_con;
}


static inline void
open_file_content_db (DBConnection *db_con)
{
	gboolean create;

	create = !tracker_db_manager_file_exists (TRACKER_DB_FILE_CONTENTS);

	db_con->db = open_db_interface (TRACKER_DB_FILE_CONTENTS);

	if (create) {
		load_sql_file (db_con->db, "sqlite-contents.sql");
		tracker_log ("Creating db: %s",
			     tracker_db_manager_get_file (TRACKER_DB_FILE_CONTENTS));
	}

	tracker_db_interface_sqlite_create_function (db_con->db, "uncompress", function_uncompress, 1);
}

DBConnection *
tracker_db_connect_file_content (void)
{
	DBConnection *db_con;

	db_con = g_new0 (DBConnection, 1);

	db_con->blob = db_con;

	open_file_content_db (db_con);

	return db_con;
}


static inline void
open_email_content_db (DBConnection *db_con)
{
	gboolean create_table;

	create_table = !tracker_db_manager_file_exists (TRACKER_DB_EMAIL_CONTENTS);

	db_con->db = open_db_interface (TRACKER_DB_EMAIL_CONTENTS);

	if (create_table) {
		load_sql_file (db_con->db, "sqlite-contents.sql");
		tracker_log ("Creating db: %s",
			     tracker_db_manager_get_file (TRACKER_DB_EMAIL_CONTENTS));
	}

	tracker_db_interface_sqlite_create_function (db_con->db, "uncompress", function_uncompress, 1);
}

DBConnection *
tracker_db_connect_email_content (void)
{
	DBConnection *db_con;

	db_con = g_new0 (DBConnection, 1);

	db_con->blob = db_con;

	open_email_content_db (db_con);

	return db_con;
}


void
tracker_db_refresh_all (DBConnection *db_con)
{
	gboolean cache_trans = FALSE;
	DBConnection *cache = db_con->cache;
	DBConnection *emails = db_con->emails;

	if (cache && tracker_db_interface_end_transaction (cache->db)) {
		cache_trans = TRUE;
	}

	/* close and reopen all databases */	
	tracker_db_close (db_con->db);	
	tracker_db_close (db_con->blob->db);

	tracker_db_close (emails->blob->db);
	tracker_db_close (emails->common->db);
	tracker_db_close (emails->db);

	open_file_db (db_con);
	open_file_content_db (db_con->blob);

	open_email_content_db (emails->blob);

	emails->common->db = open_db_interface (TRACKER_DB_COMMON);

	open_email_db (emails);
		
	if (cache_trans) {
		tracker_db_interface_start_transaction (cache->db);
	}


}

void
tracker_db_refresh_email (DBConnection *db_con)
{
	gboolean cache_trans = FALSE;
	DBConnection *cache = db_con->cache;

	if (cache && tracker_db_interface_end_transaction (cache->db)) {
		cache_trans = TRUE;
	}

	/* close email DBs and reopen them */

	DBConnection *emails = db_con->emails;

	tracker_db_close (emails->blob->db);
	tracker_db_close (emails->common->db);
	tracker_db_close (emails->db);

	open_email_content_db (emails->blob);

	emails->common->db = open_db_interface (TRACKER_DB_COMMON);

	open_email_db (emails);

	if (cache_trans) {
		tracker_db_interface_start_transaction (cache->db);
	}
}

DBConnection *
tracker_db_connect_cache (void)
{
	gboolean     create_table;
	DBConnection *db_con;

	create_table = !tracker_db_manager_file_exists (TRACKER_DB_CACHE);

	db_con = g_new0 (DBConnection, 1);

	db_con->db = open_db_interface (TRACKER_DB_CACHE);

	/* Original cache_size: 
	 *   Normal 128     Low memory mode 32
	 * Using set_params...:
	 *   Normal 128     Low memory mode 64
	 */

	if (create_table) {
		load_sql_file (db_con->db, "sqlite-cache.sql");
		tracker_db_exec_no_reply (db_con->db, "ANALYZE");
		tracker_log ("Creating db: %s",
			     tracker_db_manager_get_file (TRACKER_DB_CACHE));
	}

	return db_con;
}


DBConnection *
tracker_db_connect_emails (void)
{
	gboolean     create_table;
	DBConnection *db_con;
	
	create_table = !tracker_db_manager_file_exists (TRACKER_DB_EMAIL_META);

	db_con = g_new0 (DBConnection, 1);

	db_con->db = open_db_interface (TRACKER_DB_EMAIL_META);
	//db_con->db = open_db (tracker->data_dir, TRACKER_INDEXER_EMAIL_META_DB_FILENAME, &create_table);
	/* Old: always 8    Now: normal 8  low battery 4 */
//set_params (db_con->db, 8, TRACKER_DB_PAGE_SIZE_DEFAULT, TRUE);

	db_con->emails = db_con;

	if (create_table) {
		tracker_log ("Creating email database...");
		load_sql_file (db_con->db, "sqlite-service.sql");
		load_sql_trigger (db_con->db, "sqlite-service-triggers.sql");
		load_sql_file (db_con->db, "sqlite-email.sql");

		tracker_db_exec_no_reply (db_con->db, "ANALYZE");
	}

	tracker_db_attach_db (db_con, TRACKER_DB_COMMON);
	tracker_db_attach_db (db_con, TRACKER_DB_CACHE);

	return db_con;
}


gboolean
tracker_db_exec_no_reply (TrackerDBInterface *iface, const char *query, ...)
{
	TrackerDBResultSet *result_set;
	va_list args;

	tracker_nfs_lock_obtain ();

	va_start (args, query);
	result_set = tracker_db_interface_execute_vquery (iface, NULL, query, args);
	va_end (args);
	if (result_set)
		g_object_unref (result_set);

	tracker_nfs_lock_release ();

	return TRUE;
}


TrackerDBResultSet *
tracker_db_exec (TrackerDBInterface *iface, const char *query, ...)
{
	va_list args;
	TrackerDBResultSet *result_set;

	tracker_nfs_lock_obtain ();

	va_start (args, query);
	result_set = tracker_db_interface_execute_vquery (iface, NULL, query, args);
	va_end (args);

	tracker_nfs_lock_release ();

	return result_set;
}

TrackerDBResultSet *
tracker_exec_proc (DBConnection *db_con, const char *procedure, ...)
{
	TrackerDBResultSet *result_set;
	va_list args;

	va_start (args, procedure);
	result_set = tracker_db_interface_execute_vprocedure (db_con->db, NULL, procedure, args);
	va_end (args);

	return result_set;
}

TrackerDBResultSet *
tracker_db_exec_proc (TrackerDBInterface *iface, const char *procedure, ...)
{
	TrackerDBResultSet *result_set;
	va_list args;

	va_start (args, procedure);
	result_set = tracker_db_interface_execute_vprocedure (iface, NULL, procedure, args);
	va_end (args);

	return result_set;
}


static gboolean
tracker_exec_proc_no_reply (TrackerDBInterface *iface, const char *procedure, ...)
{
	TrackerDBResultSet *result_set;
	va_list args;

	va_start (args, procedure);
	result_set = tracker_db_interface_execute_vprocedure (iface, NULL, procedure, args);
	va_end (args);
	if (result_set)
		g_object_unref (result_set);

	return TRUE;
}


void
tracker_create_common_db (void)
{
	DBConnection *db_con;

	tracker_log ("Creating tracker database...");

	
	/* create common db first */

	db_con = tracker_db_connect_common ();
	
	load_sql_file (db_con->db, "sqlite-tracker.sql");
	load_sql_file (db_con->db, "sqlite-service-types.sql");
	load_sql_file (db_con->db, "sqlite-metadata.sql");
	load_sql_trigger (db_con->db, "sqlite-tracker-triggers.sql");

	load_service_description_file (db_con->db, "default.metadata");
	load_service_description_file (db_con->db, "file.metadata");
	load_service_description_file (db_con->db, "audio.metadata");
	load_service_description_file (db_con->db, "application.metadata");
	load_service_description_file (db_con->db, "document.metadata");
	load_service_description_file (db_con->db, "email.metadata");
	load_service_description_file (db_con->db, "image.metadata");	
	load_service_description_file (db_con->db, "video.metadata");	

	load_service_description_file (db_con->db, "default.service");

	load_sql_file (db_con->db, "sqlite-xesam.sql");

	tracker_db_load_xesam_service_file (db_con, "xesam.metadata");
	tracker_db_load_xesam_service_file (db_con, "xesam-convenience.metadata");
	tracker_db_load_xesam_service_file (db_con, "xesam-virtual.metadata");
	tracker_db_load_xesam_service_file (db_con, "xesam.service");
	tracker_db_load_xesam_service_file (db_con, "xesam-convenience.service");
	tracker_db_load_xesam_service_file (db_con, "xesam-service.smapping");
	tracker_db_load_xesam_service_file (db_con, "xesam-metadata.mmapping");

	tracker_db_create_xesam_lookup(db_con);	

	tracker_db_exec_no_reply (db_con->db, "ANALYZE");
	
	tracker_db_close (db_con->db);

	g_free (db_con);
}

static gboolean
file_exists (const gchar *dir, const char *name)
{
	gboolean is_present = FALSE;
	
	char *dbname = g_build_filename (dir, name, NULL);

	if (g_file_test (dbname, G_FILE_TEST_IS_REGULAR)) {
		is_present = TRUE;
	}

	g_free (dbname);

	return is_present;
}


gboolean
tracker_db_needs_setup (void)
{
	return (!tracker_db_manager_file_exists (TRACKER_DB_FILE_META) ||
		!file_exists (tracker->data_dir, TRACKER_INDEXER_FILE_INDEX_DB_FILENAME) ||
		!tracker_db_manager_file_exists (TRACKER_DB_FILE_CONTENTS));
}


gboolean 
tracker_db_common_need_build ()
{
	/* TODO: Check here also if it is up to date! */
	return !tracker_db_manager_file_exists (TRACKER_DB_COMMON);
}

static gint
tracker_metadata_is_key (const gchar *service, const gchar *meta_name)
{
	return tracker_ontology_metadata_key_in_service (service, meta_name);
}


static inline gboolean
is_equal (const char *s1, const char *s2)
{
	return (strcasecmp (s1, s2) == 0);
}

/* Replace with tracker_ontology_get_field_column_in_services */
char *
tracker_db_get_field_name (const char *service, const char *meta_name)
{
	int key_field = tracker_metadata_is_key (service, meta_name);

	if (key_field > 0) {
		return g_strdup_printf ("KeyMetadata%d", key_field);

	} 

	if (is_equal (meta_name, "File:Path")) return g_strdup ("Path");
	if (is_equal (meta_name, "File:Name")) return g_strdup ("Name");
	if (is_equal (meta_name, "File:Mime")) return g_strdup ("Mime");
	if (is_equal (meta_name, "File:Size")) return g_strdup ("Size");
	if (is_equal (meta_name, "File:Rank")) return g_strdup ("Rank");
	if (is_equal (meta_name, "File:Modified")) return g_strdup ("IndexTime");

	return NULL;

}

GHashTable *
tracker_db_get_file_contents_words (DBConnection *db_con, guint32 id, GHashTable *old_table)
{
	TrackerDBResultSet *result_set;
	char *str_file_id;
	gboolean valid = TRUE;

	str_file_id = tracker_uint_to_string (id);

	result_set = tracker_db_interface_execute_procedure (db_con->db, NULL, "GetAllContents", str_file_id, NULL);

	g_free (str_file_id);

	if (!result_set)
		return NULL;

	while (valid) {
		gchar *st;

		tracker_db_result_set_get (result_set, 0, &st, -1);
		old_table = tracker_parser_text (old_table, st, 1,
						 tracker->language, 
 						 tracker_config_get_max_words_to_index (tracker->config),
 						 tracker_config_get_max_word_length (tracker->config),
 						 tracker_config_get_min_word_length (tracker->config),
						 TRUE, FALSE);

		valid = tracker_db_result_set_iter_next (result_set);
		g_free (st);
	}

	g_object_unref (result_set);

	return old_table;
}


GHashTable *
tracker_db_get_indexable_content_words (DBConnection *db_con, guint32 id, GHashTable *table, gboolean embedded_only)
{
	TrackerDBResultSet *result_set;
	char *str_id;

	str_id = tracker_uint_to_string (id);

	if (embedded_only) {
		result_set = tracker_exec_proc (db_con, "GetAllIndexable", str_id, "1", NULL);
	} else {
		result_set = tracker_exec_proc (db_con, "GetAllIndexable", str_id, "0", NULL);
	}

	if (result_set) {
		gboolean valid = TRUE;
		gchar *value;
		gint weight;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   0, &value,
						   1, &weight,
						   -1);

			table = tracker_parser_text_fast (table, value, weight);

			g_free (value);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	if (embedded_only) {
		result_set = tracker_exec_proc (db_con, "GetAllIndexableKeywords", str_id, "1", NULL);
	} else {
		result_set = tracker_exec_proc (db_con, "GetAllIndexableKeywords", str_id, "0", NULL);
	}

	if (result_set) {
		gboolean valid = TRUE;
		gboolean filtered, delimited;
		gchar *value;
		gint weight;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   0, &value,
						   1, &weight,
						   2, &filtered,
						   3, &delimited
						   -1);

			table = tracker_parser_text (table, value, weight, 
						     tracker->language, 
						     tracker_config_get_max_words_to_index (tracker->config),
						     tracker_config_get_max_word_length (tracker->config),
						     tracker_config_get_min_word_length (tracker->config),
						     filtered, delimited);
			valid = tracker_db_result_set_iter_next (result_set);
			g_free (value);
		}

		g_object_unref (result_set);
	}

	g_free (str_id);

	return table;
}

static void
save_full_text_bytes (DBConnection *blob_db_con, const char *str_file_id, GByteArray *byte_array)
{
	const gchar *id;

	id = tracker_ontology_get_field_id ("File:Contents");

	if (!id) {
		tracker_error ("WARNING: metadata not found for type %s", "File:Contents");
		return;
	}

	tracker_db_interface_execute_procedure_len (blob_db_con->db,
						    NULL,
						    "SaveServiceContents",
						    str_file_id, -1,
						    id, -1,
						    byte_array->data, byte_array->len,
						    NULL);
}


static void
save_full_text (DBConnection *blob_db_con, const char *str_file_id, const char *text, int length)
{
	gchar *compressed, *value = NULL;
	gint bytes_compressed;
	const gchar *field_id;

	compressed = function_compress_string (text, length, &bytes_compressed);

	if (compressed) {
		tracker_debug ("compressed full text size of %d to %d", length, bytes_compressed);
		value = compressed;
	} else {
		tracker_error ("WARNING: compression has failed");
		value = g_strdup (text);
		bytes_compressed = length;
	}


	field_id = tracker_ontology_get_field_id ("File:Contents");

	if (!field_id) {
		tracker_error ("WARNING: metadata not found for type %s", "File:Contents");
		g_free (value);
		return;
	}

	tracker_db_interface_execute_procedure_len (blob_db_con->db,
						    NULL,
						    "SaveServiceContents",
						    str_file_id, -1,
						    field_id, -1,
						    value, bytes_compressed,
						    NULL);
	g_free (value);
}


void
tracker_db_save_file_contents (DBConnection *db_con, GHashTable *index_table, GHashTable *old_table, const char *file_name, TrackerDBFileInfo *info)
{
	char 		buffer[MAX_TEXT_BUFFER], out[MAX_COMPRESS_BUFFER];
	int  		fd, bytes_read = 0, bytes_compressed = 0, flush;
	guint32		buffer_length;
	char		*str_file_id;
	z_stream 	strm;
	GByteArray	*byte_array;
	gboolean	finished = FALSE;
	int 		max_iterations = 10000;

	DBConnection *blob_db_con = db_con->blob;

	fd = tracker_file_open (file_name, TRUE);

	if (fd ==-1) {
		tracker_error ("ERROR: could not open file %s", file_name);
		return;
	}

 	strm.zalloc = Z_NULL;
    	strm.zfree = Z_NULL;
    	strm.opaque = Z_NULL;
    		
	if (deflateInit (&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
		tracker_error ("ERROR: could not initialise zlib");
		close (fd);
		return;
	}

	str_file_id = g_strdup_printf ("%d", info->file_id);

	if (!index_table) {
		index_table = g_hash_table_new (g_str_hash, g_str_equal);
	}

	byte_array = g_byte_array_sized_new (MAX_TEXT_BUFFER);

	while (!finished) {

		char *value = NULL;
		gboolean use_buffer = TRUE;

		max_iterations--;

		if (max_iterations < 0) break;

		buffer_length = read (fd, buffer, MAX_TEXT_BUFFER-1);

		if (buffer_length == 0) {
			finished = TRUE;
			break;
		} 

		bytes_read += buffer_length;

		buffer[buffer_length] = '\0';
								
		if (buffer_length == (MAX_TEXT_BUFFER - 1)) {

			/* seek beck to last line break so we get back a clean string for utf-8 validation */
			char *end = strrchr (buffer, '\n');			

			if (!end) {
				tracker_log ("Could not find line break in text chunk..exiting");
				break;
			}

			int bytes_backtracked = strlen (end) * -1;

			buffer_length += bytes_backtracked;

			buffer[buffer_length] = '\0';

			if (lseek (fd, bytes_backtracked, SEEK_CUR) == -1) {
				tracker_error ("Could not seek to line break in text chunk");
				break;
			}

		} else {
			finished = TRUE;
		}
		
	

		if (!g_utf8_validate (buffer, buffer_length, NULL)) {

			value = g_locale_to_utf8 (buffer, buffer_length, NULL, &buffer_length, NULL);

			if (!value) {
				finished = FALSE;
				tracker_info ("could not convert text to valid utf8");
				break;
			}

			use_buffer = FALSE;

		} 
		
		if (use_buffer) {
			index_table = tracker_parser_text (index_table, buffer, 1, 
							   tracker->language, 
							   tracker_config_get_max_words_to_index (tracker->config),
							   tracker_config_get_max_word_length (tracker->config),
							   tracker_config_get_min_word_length (tracker->config),
							   TRUE, FALSE);
		} else {
			index_table = tracker_parser_text (index_table, value, 1,
							   tracker->language, 
							   tracker_config_get_max_words_to_index (tracker->config),
							   tracker_config_get_max_word_length (tracker->config),
							   tracker_config_get_min_word_length (tracker->config),
							   TRUE, FALSE);
		}

		strm.avail_in = buffer_length;

		if (use_buffer) {
			strm.next_in = (unsigned char *) buffer;
		} else {
			strm.next_in = (unsigned char *) value;	
		}

            	strm.avail_out = MAX_COMPRESS_BUFFER;
            	strm.next_out = (unsigned char *) out;
			
		/* set upper limit on text we read in */
		if (finished || bytes_read >= MAX_INDEX_TEXT_LENGTH) {
			finished = TRUE;
			flush = Z_FINISH;
		} else {
			flush = Z_NO_FLUSH;
		}


		/* compress */
       	        do {
               		int ret = deflate (&strm, flush);   
 
            		if (ret == Z_STREAM_ERROR) {
				finished = FALSE;
				tracker_error ("compression failed");
				if (!use_buffer) g_free (value);
				break;
			}

		        bytes_compressed = 65565 - strm.avail_out;

			byte_array =  g_byte_array_append (byte_array, (guint8 *) out, bytes_compressed);

			max_iterations--;

			if (max_iterations < 0) break;

              	} while (strm.avail_out == 0);

		if (!use_buffer) g_free (value);

                gint throttle;

                throttle = tracker_config_get_throttle (tracker->config);
		if (throttle > 9) {
			tracker_throttle (throttle * 100);
		}

	}        	

  
	deflateEnd(&strm);

	/* flush cache for file as we wont touch it again */
	tracker_file_close (fd, TRUE);

	if (finished && max_iterations > 0) {
		if (bytes_read > 2) {
			save_full_text_bytes (blob_db_con, str_file_id, byte_array);
		}
	} else {
		tracker_info ("An error prevented full text extraction");
	}
 
	g_byte_array_free (byte_array, TRUE);

	g_free (str_file_id);

	
}

TrackerDBResultSet *
tracker_db_search_text (DBConnection *db_con, const char *service, const char *search_string, int offset, int limit, gboolean save_results, gboolean detailed)
{
	TrackerQueryTree *tree;
	TrackerDBResultSet *result_set, *result;
	char 		**array;
	GArray          *hits;
	int 		count;
	gboolean	detailed_emails = FALSE, detailed_apps = FALSE;
	int		service_array[255];
	const gchar     *procedure;
	GArray          *services = NULL;
	GSList          *duds = NULL;
	guint           i = 0;

	array = tracker_parser_text_into_array (search_string,
						tracker->language,
						tracker_config_get_max_word_length (tracker->config),
						tracker_config_get_min_word_length (tracker->config));

	result_set = tracker_exec_proc (db_con, "GetRelatedServiceIDs", service, service, NULL);

	if (result_set) {
		gboolean valid = TRUE;
		gint type_id;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &type_id, -1);
			service_array[i] = type_id;
			i++;

			valid = tracker_db_result_set_iter_next (result_set);
		}

		service_array[i] = 0;
		services = g_array_new (TRUE, TRUE, sizeof (gint));
		g_array_append_vals (services, service_array, i);

		g_object_unref (result_set);
	}

	tree = tracker_query_tree_new (search_string, 
				       db_con->word_index, 
				       tracker->config,
				       tracker->language,
				       services);
	hits = tracker_query_tree_get_hits (tree, offset, limit);
	result = NULL;

	if (!save_results) {
		count = hits->len;

		if (count > limit) count = limit;
	} else {
		tracker_db_interface_start_transaction (db_con->db);
		tracker_exec_proc (db_con, "DeleteSearchResults1", NULL);
	}

	count = 0;

	for (i = 0; i < hits->len; i++) {
		TrackerSearchHit hit;
		char	  *str_id;

		if (count >= limit) break;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);

		/* we save results into SearchResults table instead of returing an array of array of strings */
		if (save_results) {
			char *str_score;

			str_score = tracker_int_to_string (hit.score);

			tracker_exec_proc (db_con, "InsertSearchResult1", str_id, str_score, NULL);

			g_free (str_id);
			g_free (str_score);

			continue;
		}

		if (detailed) {
			if (strcmp (service, "Emails") == 0) {
				detailed_emails = TRUE;
				procedure = "GetEmailByID";
			} else if (strcmp (service, "Applications") == 0) {
				detailed_apps = TRUE;
				procedure = "GetApplicationByID";
			} else {
				procedure = "GetFileByID2";
			}
		} else {
			procedure = "GetFileByID";
		}

		result_set = tracker_exec_proc (db_con, procedure, str_id, NULL);
		g_free (str_id);

		if (result_set) {
			gchar *path;

			tracker_db_result_set_get (result_set, 0, &path, -1);

			if (!detailed || detailed_emails || detailed_apps ||
			    (detailed && g_file_test (path, G_FILE_TEST_EXISTS))) {
				guint columns, i;

				columns = tracker_db_result_set_get_n_columns (result_set);

				if (G_UNLIKELY (!result)) {
					guint columns;

					columns = tracker_db_result_set_get_n_columns (result_set);
					result = _tracker_db_result_set_new (columns);
				}

				_tracker_db_result_set_append (result);

				for (i = 0; i < columns; i++) {
					GValue value = { 0, };

					_tracker_db_result_set_get_value (result_set, i, &value);
					_tracker_db_result_set_set_value (result, i, &value);
					g_value_unset (&value);
				}
			}

			g_free (path);
			g_object_unref (result_set);
		} else {
			tracker_log ("dud hit for search detected");
			/* add to dud list */
			duds = g_slist_prepend (duds, &hit);
		}

	}

	if (save_results) {
		tracker_db_interface_end_transaction (db_con->db);
	}

	/* delete duds */
	if (duds) {
		GSList *words, *w;
		Indexer *indexer;

		words = tracker_query_tree_get_words (tree);
		indexer = tracker_query_tree_get_indexer (tree);

		for (w = words; w; w = w->next) {
			tracker_remove_dud_hits (indexer, (const gchar *) w->data, duds);
		}

		g_slist_free (words);
	}

	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result)
		return NULL;

	if (tracker_db_result_set_get_n_rows (result) == 0) {
		g_object_unref (result);
		return NULL;
	}

	tracker_db_result_set_rewind (result);

	return result;
}

TrackerDBResultSet *
tracker_db_search_files_by_text (DBConnection *db_con, const char *text, int offset, int limit, gboolean sort)
{
	return NULL;
}


TrackerDBResultSet *
tracker_db_search_metadata (DBConnection *db_con, const char *service, const char *field, const char *text, int offset, int limit)
{
	const TrackerField *def;
	TrackerDBResultSet *result_set;

	g_return_val_if_fail ((service && field && text), NULL);

	def = tracker_ontology_get_field_def (field);

	if (!def) {
		tracker_error ("ERROR: metadata not found for type %s", field);
		return NULL;
	}

	/* FIXME This method was broken: Using wrong tables!?!?!?!?!? */
	switch (tracker_field_get_data_type (def)) {

		case TRACKER_FIELD_TYPE_KEYWORD: 
		case TRACKER_FIELD_TYPE_INDEX: 
			result_set = tracker_exec_proc (db_con, "SearchMetadata", tracker_field_get_id (def), text, NULL); 
			break;

		case TRACKER_FIELD_TYPE_FULLTEXT:
		case TRACKER_FIELD_TYPE_STRING: 
			result_set = tracker_exec_proc (db_con, "SearchMetadataNumeric", tracker_field_get_id (def), text, NULL); 
			break;

		case TRACKER_FIELD_TYPE_INTEGER: 
			result_set = tracker_exec_proc (db_con, "SearchMetadataKeywords", tracker_field_get_id (def), text, NULL); 
			break;

		default: 
			tracker_error ("ERROR: metadata could not be retrieved as type %d is not supported", tracker_field_get_data_type (def)); 
			result_set = NULL;
	}

	return result_set;
}


TrackerDBResultSet *
tracker_db_search_matching_metadata (DBConnection *db_con, const char *service, const char *id, const char *text)
{
	g_return_val_if_fail (id, NULL);

	return NULL;
}

TrackerDBResultSet *
tracker_db_get_metadata (DBConnection *db_con, const char *service, const char *id, const char *key)
{
	TrackerDBResultSet *result_set;
	const TrackerField *def;

	g_return_val_if_fail (id, NULL);

	def = tracker_ontology_get_field_def (key);

	if (!def) {
		tracker_error ("ERROR: metadata not found for id %s and type %s", id, key);
		return NULL;
	}

	switch (tracker_field_get_data_type (def)) {
		case TRACKER_FIELD_TYPE_INDEX:
		case TRACKER_FIELD_TYPE_STRING:
		case TRACKER_FIELD_TYPE_DOUBLE:
			result_set = tracker_exec_proc (db_con, "GetMetadata", id, tracker_field_get_id (def), NULL);
			break;
		case TRACKER_FIELD_TYPE_INTEGER:
		case TRACKER_FIELD_TYPE_DATE:
			result_set = tracker_exec_proc (db_con, "GetMetadataNumeric", id, tracker_field_get_id (def), NULL);
			break;
		case TRACKER_FIELD_TYPE_FULLTEXT:
			result_set = tracker_exec_proc (db_con, "GetContents", id, tracker_field_get_id (def), NULL);
			break;
		case TRACKER_FIELD_TYPE_KEYWORD:
			result_set = tracker_exec_proc (db_con, "GetMetadataKeyword", id, tracker_field_get_id (def), NULL);
			break;

		default:
			tracker_error ("ERROR: metadata could not be retrieved as type %d is not supported", tracker_field_get_data_type (def)); 
			result_set = NULL;
	}

	return result_set;
}


/* gets specified metadata value as a single row (multple values for a metadata type are returned delimited by  "|" ) */
char *	
tracker_db_get_metadata_delimited (DBConnection *db_con, const char *service, const char *id, const char *key)
{
	GString *gstr = NULL;
	TrackerDBResultSet *result_set;

	result_set = tracker_db_get_metadata (db_con, service, id, key);

	if (result_set) {
		gboolean valid = TRUE;
		gchar *str;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &str, -1);

			if (gstr) {
				g_string_append_printf (gstr, "|%s", str);
			} else {
				gstr = g_string_new (str);
			}

			g_free (str);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	if (gstr) {
		return g_string_free (gstr, FALSE);
	} else {
		return NULL;
	}
}


static void
update_metadata_index (const char *id, const char *service, const TrackerField *def, const char *old_value, const char *new_value) 
{
	GHashTable *old_table, *new_table;
	gint        sid;

	if (!def) {
		tracker_error ("ERROR: cannot find details for metadata type");
		return;
	}


	old_table = NULL;
	new_table = NULL;

	if (old_value) {
		old_table = tracker_parser_text (old_table, 
						 old_value, 
						 tracker_field_get_weight (def), 
						 tracker->language, 
 						 tracker_config_get_max_words_to_index (tracker->config),
 						 tracker_config_get_max_word_length (tracker->config),
 						 tracker_config_get_min_word_length (tracker->config),
						 tracker_field_get_filtered (def), 
						 tracker_field_get_delimited (def));
	}

	/* parse new metadata value */
	if (new_value) {
		new_table = tracker_parser_text (new_table, 
						 new_value, 
						 tracker_field_get_weight (def), 
						 tracker->language, 
 						 tracker_config_get_max_words_to_index (tracker->config),
 						 tracker_config_get_max_word_length (tracker->config),
 						 tracker_config_get_min_word_length (tracker->config),
						 tracker_field_get_filtered (def), 
						 tracker_field_get_delimited (def));
	}

	/* we only do differential updates so only changed words scores are updated */
	sid = tracker_ontology_get_id_for_service_type (service);
	tracker_db_update_differential_index (old_table, new_table, id, sid);

	tracker_parser_text_free (old_table);
	tracker_parser_text_free (new_table);
}



char *
tracker_get_related_metadata_names (DBConnection *db_con, const char *name)
{
	TrackerDBResultSet *result_set;

	result_set = tracker_exec_proc (db_con, "GetMetadataAliasesForName", name, name, NULL);

	if (result_set) {
		GString *gstr = NULL;
		gboolean valid = TRUE;
		gint id;

		while (valid) {
			tracker_db_result_set_get (result_set, 1, &id, -1);

			if (gstr) {
				g_string_append_printf (gstr, ", %d", id);
			} else {
				gstr = g_string_new ("");
				g_string_append_printf (gstr, "%d", id);
			}

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);

		return g_string_free (gstr, FALSE);
	}

	return NULL;
}


TrackerDBResultSet *
tracker_get_xesam_metadata_names (DBConnection *db_con, const char *name)
{
	TrackerDBResultSet  *result_set;

	result_set = tracker_exec_proc (db_con, "GetXesamMetaDataLookups", name, NULL);

	return result_set;
}

TrackerDBResultSet *
tracker_get_xesam_service_names (DBConnection *db_con, const char *name)
{
	TrackerDBResultSet  *result_set;

	result_set = tracker_exec_proc (db_con, "GetXesamServiceLookups", name, NULL);

	return result_set;
}


char *
tracker_get_metadata_table (TrackerFieldType type)
{
	switch (type) {

		case TRACKER_FIELD_TYPE_INDEX:
		case TRACKER_FIELD_TYPE_STRING:
		case TRACKER_FIELD_TYPE_DOUBLE:
			return g_strdup ("ServiceMetaData");
		
		case TRACKER_FIELD_TYPE_INTEGER:
		case TRACKER_FIELD_TYPE_DATE:
			return g_strdup ("ServiceNumericMetaData");

		case TRACKER_FIELD_TYPE_BLOB: 
			return g_strdup("ServiceBlobMetaData");

		case TRACKER_FIELD_TYPE_KEYWORD: 
			return g_strdup("ServiceKeywordMetaData");

		default: 
			return NULL;
	}

	return NULL;
}


static char *
format_date (const char *avalue)
{

	char *dvalue;

	dvalue = tracker_date_format (avalue);

	if (dvalue) {
		time_t time;

		time = tracker_string_to_date (dvalue);

		g_free (dvalue);

		if (time != -1) {
			return (tracker_int_to_string (time));
		} 
	}

	return NULL;

}


/* fast insert of embedded metadata. Table parameter is used to build up a unique word list of indexable contents */ 
void
tracker_db_insert_single_embedded_metadata (DBConnection *db_con, const char *service, const char *id, const char *key, const char *value, GHashTable *table)
{
	char *array[1];

	array[0] = (char *)value;
		
	tracker_db_insert_embedded_metadata (db_con, service, id, key, array, 1, table);
}

void
tracker_db_insert_embedded_metadata (DBConnection *db_con, const gchar *service, const gchar *id, const gchar *key, gchar **values, gint length, GHashTable *table)
{
	gint	key_field = 0;
	const TrackerField *def;

	if (!service || !id || !key || !values || !values[0]) {
		return;
	}

	def = tracker_ontology_get_field_def (key);

	if (!def) {
		tracker_error ("ERROR: metadata %s not found", key);
		return;
	}

	g_return_if_fail (tracker_field_get_embedded (def));

	if (length == -1) {
		length = 0;
		while (values[length] != NULL) {
			length++;
		}
	}
	
        key_field = tracker_ontology_metadata_key_in_service (service, key);

	switch (tracker_field_get_data_type (def)) {

                case TRACKER_FIELD_TYPE_KEYWORD: {
                        gint i;
			for (i = 0; i < length; i++) {
                                if (!values[i] || !values[i][0]) {
                                        continue;
                                }

				if (table) {
					gchar *mvalue;

					mvalue = tracker_parser_text_to_string (values[i],
										tracker->language,
										tracker_config_get_max_word_length (tracker->config),
										tracker_config_get_min_word_length (tracker->config),
										FALSE, 
										FALSE, 
										FALSE);
					table = tracker_parser_text_fast (table, mvalue, tracker_field_get_weight (def));

					g_free (mvalue);
				}
	
				tracker_exec_proc (db_con, "SetMetadataKeyword", id, tracker_field_get_id (def), values[i], NULL);
			}

			break;
                }
                case TRACKER_FIELD_TYPE_INDEX: {
                        gint i;
			for (i = 0; i < length; i++) {
                                gchar *mvalue;

                                if (!values[i] || !values[i][0]) {
                                        continue;
                                }

				mvalue = tracker_parser_text_to_string (values[i], 
									tracker->language,
									tracker_config_get_max_word_length (tracker->config),
									tracker_config_get_min_word_length (tracker->config),
									tracker_field_get_filtered (def), 
									tracker_field_get_filtered (def), 
									tracker_field_get_delimited (def));

				if (table) {
					table = tracker_parser_text_fast (table, mvalue, tracker_field_get_weight (def));
				}
				
				tracker_exec_proc (db_con, "SetMetadata", id, tracker_field_get_id (def), mvalue, values[i], NULL);
				
				g_free (mvalue);
			}

			break;
                }
                case TRACKER_FIELD_TYPE_FULLTEXT: {
                        gint i;
			for (i = 0; i < length; i++) {
                                if (!values[i] || !values[i][0]) {
                                        continue;
                                }

				if (table) {
					table = tracker_parser_text (table, 
								     values[i], 
								     tracker_field_get_weight (def), 
								     tracker->language, 
								     tracker_config_get_max_words_to_index (tracker->config),
								     tracker_config_get_max_word_length (tracker->config),
								     tracker_config_get_min_word_length (tracker->config),
								     tracker_field_get_filtered (def), 
								     tracker_field_get_delimited (def));
				}
	
				save_full_text (db_con->blob, id, values[i], strlen (values[i]));
			}

			break;
                }
                case TRACKER_FIELD_TYPE_DOUBLE: {
                        gint i;
			for (i = 0; i < length; i++) {
                                if (!values[i]) {
                                        continue;
                                }

				tracker_exec_proc (db_con, "SetMetadata", id, tracker_field_get_id (def), " ", values[i], NULL);
			}

                        break;
                }
                case TRACKER_FIELD_TYPE_STRING: {
                        gint i;
			for (i = 0; i < length; i++) {
				gchar *mvalue;

                                if (!values[i]) {
                                        continue;
                                }

				mvalue = tracker_parser_text_to_string (values[i], 
									tracker->language,
									tracker_config_get_max_word_length (tracker->config),
									tracker_config_get_min_word_length (tracker->config),
									tracker_field_get_filtered (def),  
									tracker_field_get_filtered (def), 
									tracker_field_get_delimited (def));
				tracker_exec_proc (db_con, "SetMetadata", id, tracker_field_get_id (def), mvalue, values[i], NULL);

				g_free (mvalue);
			}

			break;
                }
                case TRACKER_FIELD_TYPE_INTEGER: {
                        gint i;
			for (i = 0; i < length; i++) {
                                if (!values[i]) {
                                        continue;
                                }

				tracker_exec_proc (db_con, "SetMetadataNumeric", id, tracker_field_get_id (def), values[i], NULL);
			}

			break;
                }
                case TRACKER_FIELD_TYPE_DATE: {
                        gint i;
			for (i = 0; i < length; i++) {
                                if (!values[i]) {
                                        continue;
                                }

				gchar *mvalue = format_date (values[i]);

				if (!mvalue) {
					tracker_debug ("Could not format date %s", values[i]);
					continue;
				}

				tracker_exec_proc (db_con, "SetMetadataNumeric", id, tracker_field_get_id (def), mvalue, NULL);

				g_free (mvalue);
			}

			break;
                }
                default: {
			tracker_error ("ERROR: metadata could not be set as type %d for metadata %s is not supported",
                                       tracker_field_get_data_type (def), key);
			break;
                }
	}

	if (key_field > 0) {

		if (values[0]) {
			gchar *esc_value = NULL;

			if (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_DATE) {
				esc_value = format_date (values[0]);

				if (!esc_value) {
                                        return;
                                }

			} else {
				gchar *my_val = tracker_string_list_to_string (values, length, '|');
			
				esc_value = tracker_escape_string (my_val);
				g_free (my_val);
			}

			tracker_db_exec_no_reply (db_con->db,
						  "update Services set KeyMetadata%d = '%s' where id = %s",
						  key_field, esc_value, id);

			g_free (esc_value);
		}
	}
}


static char *
get_backup_id (DBConnection *db_con, const char *id)
{
	TrackerDBResultSet *result_set;
	char *backup_id = NULL;

	result_set = tracker_exec_proc (db_con, "GetBackupServiceByID", id, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &backup_id, -1);
		g_object_unref (result_set);
	}

	if (!backup_id) {
		gint64 id;

		tracker_exec_proc (db_con, "InsertBackupService", id, NULL);
		id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (db_con->db));
		backup_id = tracker_int_to_string (id);
	}

	return backup_id;
}


static inline void
backup_non_embedded_metadata (DBConnection *db_con, const char *id, const char *key_id, const char *value)
{

	char *backup_id = get_backup_id (db_con, id);

	if (backup_id) {
		tracker_exec_proc (db_con->common, "SetBackupMetadata", backup_id, key_id, value, NULL);
		g_free (backup_id);
	}

}



static inline void
backup_delete_non_embedded_metadata_value (DBConnection *db_con, const char *id, const char *key_id, const char *value)
{

	char *backup_id = get_backup_id (db_con, id);

	if (backup_id) {
		tracker_exec_proc (db_con->common, "DeleteBackupMetadataValue", backup_id, key_id, value, NULL);
		g_free (backup_id);
	}

}

static inline void
backup_delete_non_embedded_metadata (DBConnection *db_con, const char *id, const char *key_id)
{

	char *backup_id = get_backup_id (db_con, id);

	if (backup_id) {
		tracker_exec_proc (db_con->common, "DeleteBackupMetadata", backup_id, key_id, NULL);
		g_free (backup_id);
	}

}


void
tracker_db_set_single_metadata (DBConnection *db_con, const char *service, const char *id, const char *key, const char *value, gboolean do_backup)
{
	char *array[1];

	array[0] = (char *)value;

	tracker_db_set_metadata (db_con, service, id, key, array, 1, do_backup);


}


gchar *
tracker_db_set_metadata (DBConnection *db_con, const char *service, const gchar *id, const gchar *key, gchar **values, gint length, gboolean do_backup)
{
	const TrackerField *def;
	gchar 		   *old_value = NULL, *new_value = NULL;
	gboolean 	    update_index;
	gint		    key_field = 0;
	gint 		    i;
	GString 	   *str = NULL;
	gchar 		   *res_service;
	

	g_return_val_if_fail (id && values && key && service, NULL);

	if (strcmp (id, "0") == 0) {
		return NULL;
	}

	def = tracker_ontology_get_field_def (key);

	if (!def) {
		tracker_error ("metadata type %s not found", key);
		return NULL;

	}
	
	res_service = tracker_db_get_service_for_entity (db_con, id);

	if (!res_service) {
		tracker_error ("ERROR: service not found for id %s", id);
		return NULL;
	}
	
	if (tracker_field_get_multiple_values (def) && length > 1) {
		str = g_string_new ("");
	}



	key_field = tracker_ontology_metadata_key_in_service (res_service, key);
	update_index = (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_INDEX 
			|| tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_KEYWORD 
			|| tracker_field_get_data_type (def) ==  TRACKER_FIELD_TYPE_FULLTEXT);

	
	if (update_index) {
		old_value = tracker_db_get_metadata_delimited (db_con, service, id, key);
	}

	/* delete old value if metadata does not support multiple values */
	if (!tracker_field_get_multiple_values (def)) {
		tracker_db_delete_metadata (db_con, service, id, key, FALSE);
	}


	switch (tracker_field_get_data_type (def)) {

		case TRACKER_FIELD_TYPE_KEYWORD:

			for (i=0; i<length; i++) {

				if (!values[i] || !values[i][0]) continue;

				tracker_exec_proc (db_con, "SetMetadataKeyword", id, tracker_field_get_id (def), values[i], NULL);

				/* backup non-embedded data for embedded services */
				if (do_backup && 
                                    !tracker_field_get_embedded (def) && 
                                    tracker_ontology_service_type_has_embedded (service)) {
					backup_non_embedded_metadata (db_con, id, tracker_field_get_id (def), values[i]);
				}

				if (str) {
					g_string_append_printf (str, " %s ", values[i]);
				} else {
					new_value = values[i];					
				}

				tracker_log ("saving keyword %s", values[i]);
			}

			break;

		case TRACKER_FIELD_TYPE_INDEX:
			
			for (i = 0; i < length; i++) {
				gchar *mvalue;

				if (!values[i] || !values[i][0]) {
					continue;
				}

				if (str) {
					g_string_append_printf (str, " %s ", values[i]);
				} else {
					new_value = values[i];					
				}

				/* backup non-embedded data for embedded services */
				if (do_backup &&
                                    !tracker_field_get_embedded (def) && 
                                    tracker_ontology_service_type_has_embedded (service)) {
					backup_non_embedded_metadata (db_con, id, tracker_field_get_id (def), values[i]);
				}

				mvalue = tracker_parser_text_to_string (values[i], 
									tracker->language,
									tracker_config_get_max_word_length (tracker->config),
									tracker_config_get_min_word_length (tracker->config),
									tracker_field_get_filtered (def),  
									tracker_field_get_filtered (def), 
									tracker_field_get_delimited (def));
				tracker_exec_proc (db_con, "SetMetadata", id, tracker_field_get_id (def), mvalue, values[i], NULL);

				g_free (mvalue);

			}

			break;

		case TRACKER_FIELD_TYPE_FULLTEXT:
	
			/* we do not support multiple values for fulltext clobs */
						
			if (!values[0]) break;

			save_full_text (db_con->blob, id, values[0], strlen (values[0]));
			new_value = values[0];

			break;


		case TRACKER_FIELD_TYPE_STRING:

			for (i = 0; i < length; i++) {
				gchar *mvalue;

				if (!values[i] || !values[i][0]) {
					continue;
				}

				/* backup non-embedded data for embedded services */
				if (do_backup && 
                                    !tracker_field_get_embedded (def) && 
                                    tracker_ontology_service_type_has_embedded (service)) {
					backup_non_embedded_metadata (db_con, id, tracker_field_get_id (def), values[i]);
				}

				mvalue = tracker_parser_text_to_string (values[i], 
									tracker->language,
									tracker_config_get_max_word_length (tracker->config),
									tracker_config_get_min_word_length (tracker->config),
									tracker_field_get_filtered (def),  
									tracker_field_get_filtered (def), 
									tracker_field_get_delimited (def));
				tracker_exec_proc (db_con, "SetMetadata", id, tracker_field_get_id (def), mvalue, values[i], NULL);

				g_free (mvalue);
			}
			break;

		case TRACKER_FIELD_TYPE_DOUBLE:

			
			for (i=0; i<length; i++) {

				if (!values[i] || !values[i][0]) continue;

				/* backup non-embedded data for embedded services */
				if (do_backup && 
                                    !tracker_field_get_embedded (def) && 
                                    tracker_ontology_service_type_has_embedded (service)) {
					backup_non_embedded_metadata (db_con, id, tracker_field_get_id (def), values[i]);
				}


				tracker_exec_proc (db_con, "SetMetadata", id, tracker_field_get_id (def), " ", values[i], NULL);

			}
			break;

		

		case TRACKER_FIELD_TYPE_INTEGER:
	
			for (i=0; i<length; i++) {
				if (!values[i] || !values[i][0]) continue;

				/* backup non-embedded data for embedded services */
				if (do_backup && 
                                    !tracker_field_get_embedded (def) && 
                                    tracker_ontology_service_type_has_embedded (service)) {
					backup_non_embedded_metadata (db_con, id, tracker_field_get_id (def), values[i]);
				}


				tracker_exec_proc (db_con, "SetMetadataNumeric", id, tracker_field_get_id (def), values[i], NULL);
			}

			break;

		case TRACKER_FIELD_TYPE_DATE:

			for (i=0; i<length; i++) {

				if (!values[i] || !values[i][0]) continue;

				char *mvalue = format_date (values[i]);

				if (!mvalue) {
					tracker_debug ("Could not format date %s", values[i]);
					continue;

				}

				tracker_exec_proc (db_con, "SetMetadataNumeric", id, tracker_field_get_id (def), mvalue, NULL);

				/* backup non-embedded data for embedded services */
				if (do_backup && 
                                    !tracker_field_get_embedded (def) && 
                                    tracker_ontology_service_type_has_embedded (service)) {
					backup_non_embedded_metadata (db_con, id, tracker_field_get_id (def), mvalue);
				}


				g_free (mvalue);
			}

			break;

		default :
			
			tracker_error ("ERROR: metadata could not be set as type %d for metadata %s is not supported", tracker_field_get_data_type (def), key);
			break;

		

		
	}

	if (key_field > 0) {



		if (values[0]) {
			char *esc_value = NULL;

			if (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_DATE) {
				esc_value = format_date (values[0]);

				if (!esc_value) return NULL;

			} else {

				char *my_val = tracker_string_list_to_string (values, length, '|');
			
				esc_value = tracker_escape_string (my_val);
				g_free (my_val);

			}

			tracker_db_exec_no_reply (db_con->db,
						  "update Services set KeyMetadata%d = '%s' where id = %s",
						  key_field, esc_value, id);

			g_free (esc_value);
		}

	}


	

	/* update fulltext index differentially with current and new values */
	if (update_index) {

		if (str) {
			update_metadata_index (id, res_service, def, old_value, str->str);
			g_string_free (str, TRUE);
		} else {
			update_metadata_index (id, res_service, def, old_value, new_value);	
		}
	}

	g_free (old_value);
	g_free (res_service);

	return NULL;

}

static char *
remove_value (const char *str, const char *del_str) 
{
	char **tmp, **array = g_strsplit (str, "|", -1);

	GString *s = NULL;

	for (tmp = array; *tmp; tmp++) {

		if (tracker_is_empty_string (*tmp)) {
			continue;
		}

		if (strcmp (del_str, *tmp) != 0) {
			
			if (!s) {
				s = g_string_new (*tmp);
			} else {
				g_string_append_printf (s, "%s%s", "|", *tmp);
			}
		}
	}

	g_strfreev (array);

	if (!s) {
		return NULL;
	}

	return g_string_free (s, FALSE);

}

void 
tracker_db_delete_metadata_value (DBConnection *db_con, const char *service, const char *id, const char *key, const char *value) 
{

	char 		   *old_value = NULL, *new_value = NULL, *mvalue;
	const TrackerField *def;
	gboolean 	    update_index;

	g_return_if_fail (id && key && service && db_con);

	/* get type details */
	def = tracker_ontology_get_field_def (key);

	if (!def) {
		return;
	}


	if (!tracker_field_get_embedded (def) && 
            tracker_ontology_service_type_has_embedded (service)) {
		backup_delete_non_embedded_metadata_value (db_con, id, tracker_field_get_id (def), value);
	}


	char *res_service = tracker_db_get_service_for_entity (db_con, id);

	if (!res_service) {
		tracker_error ("ERROR: entity not found");
		return;
	}

	int key_field = tracker_metadata_is_key (res_service, key);

	update_index = (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_INDEX 
			|| tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_KEYWORD);

	if (update_index) {

		/* get current value and claculate the new value */	

		old_value = tracker_db_get_metadata_delimited (db_con, service, id, key);
	
		if (old_value) {
			new_value = remove_value (old_value, value);
		} else {
			g_free (res_service);
			return;
		}

	}


	/* perform deletion */
	switch (tracker_field_get_data_type (def)) {

		case TRACKER_FIELD_TYPE_INDEX:
		case TRACKER_FIELD_TYPE_STRING:
			mvalue = tracker_parser_text_to_string (value, 
								tracker->language,
								tracker_config_get_max_word_length (tracker->config),
								tracker_config_get_min_word_length (tracker->config),
								tracker_field_get_filtered (def),  
								tracker_field_get_filtered (def), 
								tracker_field_get_delimited (def));
			tracker_exec_proc (db_con, "DeleteMetadataValue", id, tracker_field_get_id (def), mvalue, NULL);
			g_free (mvalue);
			break;


		case TRACKER_FIELD_TYPE_DOUBLE:
			tracker_exec_proc (db_con, "DeleteMetadataValue", id, tracker_field_get_id (def), value, NULL);
			break;

		
		case TRACKER_FIELD_TYPE_INTEGER:
		case TRACKER_FIELD_TYPE_DATE:

			tracker_exec_proc (db_con, "DeleteMetadataNumericValue", id, tracker_field_get_id (def), value, NULL);
			break;

		
		case TRACKER_FIELD_TYPE_KEYWORD:
			
			tracker_exec_proc (db_con, "DeleteMetadataKeywordValue", id, tracker_field_get_id (def), value, NULL);
			break;
		
		default:	
			tracker_error ("ERROR: metadata could not be deleted as type %d for metadata %s is not supported", tracker_field_get_data_type (def), key);
			break;


	}

	if (key_field > 0) {
		TrackerDBResultSet *result_set;
		gchar *value;

		result_set = tracker_db_get_metadata (db_con, service, id, key);

		if (result_set) {
			tracker_db_result_set_get (result_set, 0, &value, -1);

			if (value) {
				char *esc_value = tracker_escape_string (value);

				tracker_db_exec_no_reply (db_con->db,
							 "update Services set KeyMetadata%d = '%s' where id = %s",
							  key_field, esc_value, id);

				g_free (esc_value);
				g_free (value);
			} else {
				tracker_db_exec_no_reply (db_con->db,
							  "update Services set KeyMetadata%d = NULL where id = %s",
							  key_field, id);
			}

			g_object_unref (result_set);
		} else {
			tracker_db_exec_no_reply (db_con->db,
						  "update Services set KeyMetadata%d = NULL where id = %s",
						  key_field, id);
		}
	}


	/* update fulltext index differentially with old and new values */
	if (update_index) {
		update_metadata_index (id, service, def, old_value, new_value);
	}

	g_free (new_value);
	g_free (old_value);

	g_free (res_service);
	
}


void 
tracker_db_delete_metadata (DBConnection *db_con, const char *service, const char *id, const char *key, gboolean update_indexes) 
{
	char 		*old_value = NULL;
	const TrackerField	*def;
	gboolean 	update_index;

	g_return_if_fail (id && key && service && db_con);


	/* get type details */
	def = tracker_ontology_get_field_def(key);

	if (!def) {
		return;
	}
	
	if (!tracker_field_get_embedded (def) && 
            tracker_ontology_service_type_has_embedded (service)) {
		backup_delete_non_embedded_metadata (db_con, id, tracker_field_get_id (def));
	}


	char *res_service = tracker_db_get_service_for_entity (db_con, id);

	if (!res_service) {
		tracker_error ("ERROR: entity not found");
		return;
	}


	int key_field = tracker_metadata_is_key (res_service, key);

	update_index = update_indexes && (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_INDEX || tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_KEYWORD);


	if (update_index) {
		/* get current value */	
		old_value = tracker_db_get_metadata_delimited (db_con, service, id, key);
		tracker_debug ("old value is %s", old_value);
		
	}


	
	if (key_field > 0) {
		tracker_db_exec_no_reply (db_con->db,
					  "update Services set KeyMetadata%d = NULL where id = %s",
					  key_field, id);
	}
	
	
	/* perform deletion */
	switch (tracker_field_get_data_type (def)) {

		case TRACKER_FIELD_TYPE_INDEX:
		case TRACKER_FIELD_TYPE_STRING:
		case TRACKER_FIELD_TYPE_DOUBLE:
			tracker_exec_proc (db_con, "DeleteMetadata", id, tracker_field_get_id (def), NULL);
			break;

		case TRACKER_FIELD_TYPE_INTEGER:
		case TRACKER_FIELD_TYPE_DATE:
			tracker_exec_proc (db_con, "DeleteMetadataNumeric", id, tracker_field_get_id (def), NULL);
			break;

		
		case TRACKER_FIELD_TYPE_KEYWORD:
			tracker_exec_proc (db_con, "DeleteMetadataKeyword", id, tracker_field_get_id (def), NULL);
			break;

		case TRACKER_FIELD_TYPE_FULLTEXT:

			tracker_exec_proc (db_con, "DeleteContent", id, tracker_field_get_id (def), NULL);
			break;

		default:
			tracker_error ("ERROR: metadata could not be deleted as this operation is not supported by type %d for metadata %s", tracker_field_get_data_type (def), key);
			break;

	}

	
	/* update fulltext index differentially with old values and NULL */
	if (update_index && old_value) {
		update_metadata_index (id, service, def, old_value, " ");
	}

	
	g_free (old_value);
	g_free (res_service);


}

TrackerDBResultSet* 
tracker_db_get_live_search_hit_count (DBConnection *db_con, const gchar *search_id)
{
	tracker_debug ("GetLiveSearchHitCount");

	/* SELECT count(*) 
	 * FROM LiveSearches 
	 * WHERE SearchID = ? */

	return tracker_exec_proc (db_con->cache, "GetLiveSearchHitCount", search_id, NULL);
}


TrackerDBResultSet* 
tracker_db_get_live_search_deleted_ids (DBConnection *db_con, const gchar *search_id)
{
	tracker_debug ("GetLiveSearchDeletedIDs");

	/* SELECT E.ServiceID 
	 * FROM Events as E, LiveSearches as X 
	 * WHERE E.ServiceID = X.ServiceID 
	 * AND X.SearchID = ? 
	 * AND E.EventType IS 'Delete' */

	return tracker_exec_proc (db_con->cache, "GetLiveSearchDeletedIDs", search_id, NULL);
}

void
tracker_db_stop_live_search (DBConnection *db_con, const gchar *search_id)
{
	tracker_debug ("LiveSearchStopSearch");

	/* DELETE 
	 * FROM LiveSearches as X 
	 * WHERE E.SearchID = ? */

	tracker_exec_proc_no_reply (db_con->cache->db, "LiveSearchStopSearch", search_id, NULL);
}


void
tracker_db_start_live_search (DBConnection *db_con, const gchar *from_query, const gchar *where_query, const gchar *search_id)
{
	/* INSERT
	 * INTO LiveSearches
	 * SELECT ID, SEARCH_ID FROM_QUERY WHERE_QUERY */

	tracker_db_exec_no_reply (db_con->db,  NULL,
		"INSERT INTO LiveSearches SELECT ID, '%s' %s %s",
			search_id, from_query, where_query);
}

TrackerDBResultSet* 
tracker_db_get_live_search_new_ids (DBConnection *db_con, const gchar *search_id, const gchar *columns, const gchar *from_query, const gchar *where_query)
{
	// todo: this is a query for ottela to review

	/* Contract, in @result:
	 * ServiceID is #1
	 * EventType is #2 */

	/**
	 * SELECT E.ServiceID, E.EventType, COLUMNS
	 * FROM_QUERY XesamLiveSearches as X, Events as E
	 * WHERE_QUERY"
	 * AND X.ServiceID = E.ServiceID
	 * AND X.SearchID = SEARCH_ID
	 * AND (X.EventType IS 'Create' 
	 *      OR X.EventType IS 'Update')
	 **/

	tracker_debug ("LiveSearchUpdateQuery");

	return tracker_db_exec (db_con->db,
	/* COLUMNS */      "SELECT E.ServiceID, E.EventType%s%s "
	/* FROM_QUERY */   "%s%s LiveSearches as X, Events as E "
	/* WHERE_QUERY */  "%s"
	/* AND or space */ "%sX.ServiceID = E.ServiceID "
			   "AND X.SearchID = '%s' " /* search_id arg */
			   "AND (X.EventType IS 'Create' "
			   "OR X.EventType IS 'Update') ",

			   columns?", ":"", 
			   columns?columns:"", 
			   from_query?from_query:"FROM",
			   from_query?",":"",
			   where_query?where_query:"WHERE",
			   where_query?"AND":" ", 
			   search_id);
}

TrackerDBResultSet *
tracker_db_get_live_search_get_hit_data (DBConnection *db_con, const gchar *search_id)
{
	tracker_debug ("tracker_db_get_live_search_get_hit_data");

	return tracker_db_exec (db_con->db, 
				"SELECT * FROM LiveSearches as X "
				"WHERE X.SearchID = '%s'", 
				search_id);
}

TrackerDBResultSet* 
tracker_db_get_events (DBConnection *db_con)
{
	tracker_debug ("SetEventsBeingHandled");
	tracker_exec_proc_no_reply (db_con->cache->db, "SetEventsBeingHandled", NULL);
	tracker_debug ("GetEvents");
	return tracker_exec_proc (db_con->cache, "GetEvents", NULL);
}


void 
tracker_db_delete_handled_events (DBConnection *db_con, TrackerDBResultSet *events)
{
	tracker_debug ("DeleteHandledEvents");
	tracker_exec_proc_no_reply (db_con->cache->db, "DeleteHandledEvents", NULL);
}

static guint32
tracker_db_create_event (DBConnection *db_con, const gchar *service_id_str, const gchar *type)
{
	TrackerDBResultSet *result_set;
	char	   *eid;
	int	   i;
	guint32	   id = 0;

	result_set = tracker_exec_proc (db_con->common, "GetNewEventID", NULL);

	if (!result_set) {
		tracker_error ("ERROR: could not create event - GetNewEventID failed");
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &eid, -1);
	i = atoi (eid);
	g_free (eid);
	i++;
	eid = tracker_int_to_string (i);

	result_set = tracker_exec_proc (db_con->common, "UpdateNewEventID", eid, NULL);
	if (result_set)
		g_object_unref (result_set);

	/* Uses the Events table */
	tracker_debug ("CreateEvent %s", eid);

	result_set = tracker_exec_proc (db_con->cache, "CreateEvent", eid, service_id_str, type, NULL);
	id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (db_con->db));
	if (result_set)
		g_object_unref (result_set);

	tracker_xesam_wakeup (id);

	g_free (eid);

	return id;
}

guint32
tracker_db_create_service (DBConnection *db_con, const char *service, TrackerDBFileInfo *info)
{
	TrackerDBResultSet *result_set, *b;
	int	   i;
	guint32	   id = 0;
	char	   *sid;
	char	   *str_mtime;
	const char *str_is_dir, *str_is_link;
	char	   *str_filesize, *str_offset, *str_aux;
	int	   service_type_id;
	char	   *str_service_type_id, *path, *name;

	if (!info || !info->uri || !info->uri[0] || !service || !db_con) {
		tracker_error ("ERROR: cannot create service");
		return 0;

	}

	if (info->uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (info->uri);
		path = g_path_get_dirname (info->uri);
	} else {
		name = tracker_file_get_vfs_name (info->uri);
		path = tracker_file_get_vfs_path (info->uri);
	}


	/* get a new unique ID for the service - use mutex to prevent race conditions */

	result_set = tracker_exec_proc (db_con->common, "GetNewID", NULL);

	if (!result_set) {
		tracker_error ("ERROR: could not create service - GetNewID failed");
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &sid, -1);
	i = atoi (sid);
	g_free (sid);
	i++;

	sid = tracker_int_to_string (i);
	b = tracker_exec_proc (db_con->common, "UpdateNewID", sid, NULL);

	if (b)
		g_object_unref (b);

	if (result_set)
		g_object_unref (result_set);

	if (info->is_directory) {
		str_is_dir = "1";
	} else {
		str_is_dir = "0";
	}

	if (info->is_link) {
		str_is_link = "1";
	} else {
		str_is_link = "0";
	}

	str_filesize = tracker_guint32_to_string (info->file_size);
	str_mtime = tracker_gint32_to_string (info->mtime);
	str_offset = tracker_gint32_to_string (info->offset);

	service_type_id = tracker_ontology_get_id_for_service_type (service);

	if (info->mime) {
		tracker_debug ("service id for %s is %d and sid is %s with mime %s", service, service_type_id, sid, info->mime);
	} else {
		tracker_debug ("service id for %s is %d and sid is %s", service, service_type_id, sid);
        }

	str_service_type_id = tracker_int_to_string (service_type_id);

	str_aux = tracker_int_to_string (info->aux_id);

	if (service_type_id != -1) {
		gchar *parent;

		b = tracker_exec_proc (db_con, "CreateService", sid, path, name,
                                   str_service_type_id, info->mime, str_filesize,
                                   str_is_dir, str_is_link, str_offset, str_mtime, str_aux, NULL);

		if (b) {
			g_object_unref (b);
		} 
		/*
		  Undetectable error
			tracker_error ("ERROR: CreateService uri is %s/%s", path, name);
			g_free (name);
			g_free (path);
			g_free (str_aux);
			g_free (str_service_type_id);
			g_free (sid);
			g_free (str_filesize);
			g_free (str_mtime);
			g_free (str_offset);
			g_static_rec_mutex_unlock (&events_table_lock);
			return 0;
		*/
		id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (db_con->db));

		if (info->is_hidden) {
			tracker_db_exec_no_reply (db_con->db,
						  "Update services set Enabled = 0 where ID = %d",
						  (int) id);
		}

		b = tracker_exec_proc (db_con->common, "IncStat", service, NULL);

		if (b)
			g_object_unref (b);

                parent = tracker_ontology_get_parent_service (service);
		
		if (parent) {
			b = tracker_exec_proc (db_con->common, "IncStat", parent, NULL);
			if (b)
				g_object_unref (b);
			g_free (parent);
		}

		if (tracker_config_get_enable_xesam (tracker->config))
			tracker_db_create_event (db_con, sid, "Create");
	}

	g_free (name);
	g_free (path);
	g_free (str_aux);
	g_free (str_service_type_id);
	g_free (sid);
	g_free (str_filesize);
	g_free (str_mtime);
	g_free (str_offset);

	return id;
}



/*
static void
delete_index_data (gpointer key,
		   gpointer value,
		   gpointer user_data)
{
	char	*word;
	guint32	id;

	word = (char *) key;

	id = GPOINTER_TO_UINT (user_data);
g_hash_table_foreach (table, delete_index_data, GUINT_TO_POINTER (id));
	tracker_indexer_update_word (tracker->file_indexer, word, id, 0, 1, TRUE);
}
*/

static void
delete_index_for_service (DBConnection *db_con, guint32 id)
{
	char	   *str_file_id;

	str_file_id = tracker_uint_to_string (id);

	tracker_exec_proc (db_con->blob, "DeleteAllContents", str_file_id, NULL);

	g_free (str_file_id);

	/* disable deletion of words in index for performance reasons - we can filter out deletes when we search
	GHashTable *table;

	table = get_indexable_content_words (db_con, id, table);

	table = get_file_contents_words (blob_db_con, id);

	if (table) {
		g_hash_table_foreach (table, delete_index_data, GUINT_TO_POINTER (id));
		g_hash_table_destroy (table);
	}

	*/

}




/*
static gint
delete_id_from_list (gpointer         key,
		     gpointer         value,
		     gpointer         data)
{
	
  	GSList *list, *l;
	WordDetails *wd;
	guint32 file_id = GPOINTER_TO_UINT (data);

	list = value;

	if (!list) {
		return 1;
	}
	
	for (l=list;l;l=l->next) {
		wd = l->data;
		if (wd->id == file_id) {
			
			list = g_slist_remove (list, l->data);
			g_slice_free (WordDetails, wd);
			value = list;
			tracker->word_detail_count--;

			if (g_slist_length (list) == 0) {
				tracker->word_count--;
			}

			return 1;
		}
	}

  	return 1;
}
*/

/*
static void
delete_cache_words (guint32 file_id)
{
  	g_hash_table_foreach (tracker->cached_table, (GHFunc) delete_id_from_list, GUINT_TO_POINTER (file_id));	
}
*/

static void
dec_stat (DBConnection *db_con, int id)
{
	gchar *service;
        
        service = tracker_ontology_get_service_type_by_id (id);

	if (service) {
		gchar *parent;

		tracker_exec_proc (db_con->common, "DecStat", service, NULL);

                parent = tracker_ontology_get_parent_service (service);
		
		if (parent) {
			tracker_exec_proc (db_con->common, "DecStat", parent, NULL);
			g_free (parent);
		}

		g_free (service);
	
		
	} else {
		tracker_debug ("could not dec stat for service ID %d", id);
	}

}


char *
tracker_db_get_id (DBConnection *db_con, const char *service, const char *uri)
{
	gint    service_id;
	guint32	id;

	service_id = tracker_ontology_get_id_for_service_type (service);

	if (service_id == -1) {
		return NULL;
	}

	id = tracker_db_get_file_id (db_con, uri);

	if (id > 0) {
		return tracker_uint_to_string (id);
	}

	return NULL;
}



void
tracker_db_delete_file (DBConnection *db_con, guint32 file_id)
{
	char *str_file_id, *name = NULL, *path;
	TrackerDBResultSet *result_set;
	gint id;

	delete_index_for_service (db_con, file_id);

	str_file_id = tracker_uint_to_string (file_id);

	result_set = tracker_exec_proc (db_con, "GetFileByID3", str_file_id, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set,
					   0, &name,
					   1, &path,
					   3, &id,
					   -1);

		if (name && path) {
			dec_stat (db_con, id);

			tracker_exec_proc (db_con, "DeleteService1", str_file_id, NULL);
			tracker_exec_proc (db_con->common, "DeleteService6", path, name, NULL);
			tracker_exec_proc (db_con->common, "DeleteService7", path, name, NULL);
			tracker_exec_proc (db_con->common, "DeleteService9", path, name, NULL);

			tracker_db_create_event (db_con, str_file_id, "Delete");

			g_free (name);
			g_free (path);
		}

		g_object_unref (result_set);
	}

	g_free (str_file_id);
}


void
tracker_db_delete_directory (DBConnection *db_con, guint32 file_id, const char *uri)
{
	TrackerDBResultSet *result_set;
	char *str_file_id, *uri_prefix;

	str_file_id = tracker_uint_to_string (file_id);

	uri_prefix = g_strconcat (uri, G_DIR_SEPARATOR_S, "*", NULL);

	delete_index_for_service (db_con, file_id);

	/* get all file id's for all files recursively under directory amd delete them */
	result_set = tracker_exec_proc (db_con, "SelectSubFileIDs", uri, uri_prefix, NULL);

	if (result_set) {
		gboolean valid = TRUE;
		gint id;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &id, -1);
			tracker_db_delete_file (db_con, id);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}


	/* delete all files underneath directory 
	tracker_db_interface_start_transaction (db_con->db);
	tracker_exec_proc (db_con, "DeleteService2", uri, NULL);
	tracker_exec_proc (db_con, "DeleteService3", uri_prefix, NULL);
	tracker_exec_proc (db_con, "DeleteService4", uri, NULL);
	tracker_exec_proc (db_con, "DeleteService5", uri_prefix, NULL);
	tracker_exec_proc (db_con, "DeleteService8", uri, uri_prefix, NULL);
	tracker_exec_proc (db_con, "DeleteService10", uri, uri_prefix, NULL);
	tracker_db_interface_end_transaction (db_con->db);
	*/

	/* delete directory */
	tracker_db_delete_file (db_con, file_id);


	g_free (uri_prefix);
	g_free (str_file_id);
}

void
tracker_db_delete_service (DBConnection *db_con, guint32 id, const char *uri)
{
	tracker_db_delete_directory (db_con, id, uri);

}



void
tracker_db_update_file (DBConnection *db_con, TrackerDBFileInfo *info)
{
	char *str_file_id;
	char *str_service_type_id;
	char *str_size;
	char *str_mtime;
	char *str_offset;
	char *name, *path;

	str_file_id = tracker_uint_to_string (info->file_id);
	str_service_type_id = tracker_int_to_string (info->service_type_id);
	str_size = tracker_int_to_string (info->file_size);
	str_mtime = tracker_int_to_string (info->mtime);
	str_offset = tracker_int_to_string (info->offset);

	name = g_path_get_basename (info->uri);
	path = g_path_get_dirname (info->uri);

	tracker_exec_proc (db_con, "UpdateFile", str_service_type_id, path, name, info->mime, str_size, str_mtime, str_offset, str_file_id, NULL);

	tracker_db_create_event (db_con, str_file_id, "Update");

	g_free (str_service_type_id);
	g_free (str_size);
	g_free (str_offset);
	g_free (name);
 	g_free (path);
	g_free (str_file_id);
	g_free (str_mtime);
}


gboolean
tracker_db_has_pending_files (DBConnection *db_con)
{
	TrackerDBResultSet *result_set;
	gboolean has_pending;

	if (!tracker->is_running) {
		return FALSE;
	}

	has_pending = FALSE;

	result_set = tracker_exec_proc (db_con->cache, "ExistsPendingFiles", NULL);

	if (result_set) {
		gint pending_file_count;

		tracker_db_result_set_get (result_set, 0, &pending_file_count, -1);
		has_pending = (pending_file_count > 0);

		g_object_unref (result_set);
	}

	return has_pending;
}


TrackerDBResultSet *
tracker_db_get_pending_files (DBConnection *db_con)
{
	time_t time_now;

	if (!tracker->is_running) {
		return NULL;
	}

	time (&time_now);

	tracker_db_exec_no_reply (db_con->cache->db, "DELETE FROM FileTemp");

	tracker_db_exec_no_reply (db_con->cache->db,
				  "INSERT INTO FileTemp (ID, FileID, Action, FileUri, MimeType,"
				  " IsDir, IsNew, RefreshEmbedded, RefreshContents, ServiceTypeID) "
				  "SELECT ID, FileID, Action, FileUri, MimeType,"
				  " IsDir, IsNew, RefreshEmbedded, RefreshContents, ServiceTypeID "
				  "FROM FilePending WHERE (PendingDate < %d) AND (Action <> 20) LIMIT 250",
				  (gint) time_now);

	tracker_db_exec_no_reply (db_con->cache->db, "DELETE FROM FilePending WHERE ID IN (SELECT ID FROM FileTemp)");

	return tracker_db_interface_execute_query (db_con->cache->db, NULL, "SELECT FileID, FileUri, Action, MimeType, IsDir, IsNew, RefreshEmbedded, RefreshContents, ServiceTypeID FROM FileTemp ORDER BY ID");
}


void
tracker_db_remove_pending_files (DBConnection *db_con)
{
	tracker_db_exec_no_reply (db_con->cache->db, "DELETE FROM FileTemp");
}


void
tracker_db_insert_pending (DBConnection *db_con, const char *id, const char *action, const char *counter, const char *uri, const char *mime, gboolean is_dir, gboolean is_new, int service_type_id)
{
	time_t	   time_now;
	char 	   *time_str;
	int	   i;
	const char *str_new;
	char	   *str_service_type_id;

	time (&time_now);

	i = atoi (counter);

	if (i == 0) {
		time_str = tracker_int_to_string (i);
	} else {
		time_str = tracker_int_to_string (time_now + i);
	}

	if (is_new) {
		str_new = "1";
	} else {
		str_new = "0";
	}

	str_service_type_id = tracker_int_to_string (service_type_id);

	if (is_dir) {
		tracker_exec_proc (db_con->cache, "InsertPendingFile", id, action, time_str, uri, mime, "1", str_new, "1", "1", str_service_type_id, NULL);
	} else {
		tracker_exec_proc (db_con->cache, "InsertPendingFile", id, action, time_str, uri, mime, "0", str_new, "1", "1", str_service_type_id, NULL);
	}

	g_free (str_service_type_id);
	g_free (time_str);
}


void
tracker_db_update_pending (DBConnection *db_con, const char *counter, const char *action, const char *uri)
{
	time_t  time_now;
	char 	*time_str;
	int	i;

	time (&time_now);

	i = atoi (counter);

	time_str = tracker_int_to_string (time_now + i);

	tracker_exec_proc (db_con->cache, "UpdatePendingFile", time_str, action, uri, NULL);

	g_free (time_str);
}


TrackerDBResultSet *
tracker_db_get_files_by_service (DBConnection *db_con, const char *service, int offset, int limit)
{
	TrackerDBResultSet *result_set;
	char *str_limit, *str_offset;

	str_limit = tracker_int_to_string (limit);
	str_offset = tracker_int_to_string (offset);

	result_set = tracker_exec_proc (db_con, "GetByServiceType", service, service, str_offset, str_limit, NULL);

	g_free (str_offset);
	g_free (str_limit);

	return result_set;
}

TrackerDBResultSet *
tracker_db_get_files_by_mime (DBConnection *db_con, char **mimes, int n, int offset, int limit, gboolean vfs)
{
	TrackerDBResultSet *result_set;
	int	i;
	char *service;
	char	*query;
	GString	*str;

	g_return_val_if_fail (mimes, NULL);

	if (vfs) {
		service = "VFS";
	} else {
		service = "Files";
	}

	str = g_string_new ("SELECT  DISTINCT F.Path || '/' || F.Name AS uri FROM Services F INNER JOIN ServiceKeywordMetaData M ON F.ID = M.ServiceID WHERE M.MetaDataID = (SELECT ID FROM MetaDataTypes WHERE MetaName ='File:Mime') AND (M.MetaDataValue IN ");

	g_string_append_printf (str, "('%s'", mimes[0]);

	for (i = 1; i < n; i++) {
		g_string_append_printf (str, ", '%s'", mimes[i]);

	}

	g_string_append_printf (str, ")) AND (F.ServiceTypeID in (select TypeId from ServiceTypes where TypeName = '%s' or Parent = '%s')) LIMIT %d,%d", service, service, offset, limit);

	query = g_string_free (str, FALSE);

	tracker_debug ("getting files with mimes using sql %s", query);

	result_set = tracker_db_interface_execute_query (db_con->db, NULL, query);

	g_free (query);

	return result_set;
}

TrackerDBResultSet *
tracker_db_search_text_mime (DBConnection *db_con, const char *text, char **mime_array)
{
	TrackerQueryTree *tree;
	TrackerDBResultSet *result;
	GArray       *hits;
	GSList 	     *result_list;
	guint        i;
	int 	     count;
	gint         service_array[8];
	GArray       *services;

	result = NULL;
	result_list = NULL;

	service_array[0] = tracker_ontology_get_id_for_service_type ("Files");
	service_array[1] = tracker_ontology_get_id_for_service_type ("Folders");
	service_array[2] = tracker_ontology_get_id_for_service_type ("Documents");
	service_array[3] = tracker_ontology_get_id_for_service_type ("Images");
	service_array[4] = tracker_ontology_get_id_for_service_type ("Music");
	service_array[5] = tracker_ontology_get_id_for_service_type ("Videos");
	service_array[6] = tracker_ontology_get_id_for_service_type ("Text");
	service_array[7] = tracker_ontology_get_id_for_service_type ("Other");

	services = g_array_new (TRUE, TRUE, sizeof (gint));
	g_array_append_vals (services, service_array, 8);

	tree = tracker_query_tree_new (text, 
				       db_con->word_index, 
				       tracker->config,
				       tracker->language,
				       services);
	hits = tracker_query_tree_get_hits (tree, 0, 0);
	count = 0;

	for (i = 0; i < hits->len; i++) {
		TrackerDBResultSet *result_set;
		TrackerSearchHit hit;
		char *str_id, *mimetype;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);

		result_set = tracker_exec_proc (db_con, "GetFileByID", str_id, NULL);

		g_free (str_id);

		if (result_set) {
			tracker_db_result_set_get (result_set, 2, &mimetype, -1);

			if (tracker_string_in_string_list (mimetype, mime_array) != -1) {
				GValue value = { 0, };

				if (G_UNLIKELY (!result)) {
					result = _tracker_db_result_set_new (2);
				}

				_tracker_db_result_set_append (result);

				/* copy value in column 0 */
				_tracker_db_result_set_get_value (result_set, 0, &value);
				_tracker_db_result_set_set_value (result, 0, &value);
				g_value_unset (&value);

				/* copy value in column 1 */
				_tracker_db_result_set_get_value (result_set, 1, &value);
				_tracker_db_result_set_set_value (result, 1, &value);
				g_value_unset (&value);

				count++;
			}

			g_free (mimetype);
			g_object_unref (result_set);
		}

		if (count > 2047) {
			break;
		}
	}

	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result)
		return NULL;

	if (tracker_db_result_set_get_n_rows (result) == 0) {
		g_object_unref (result);
		return NULL;
	}

	tracker_db_result_set_rewind (result);

	return result;
}

TrackerDBResultSet *
tracker_db_search_text_location (DBConnection *db_con, const char *text, const char *location)
{
	TrackerDBResultSet *result;
	TrackerQueryTree *tree;
	GArray       *hits;
	char	     *location_prefix;
	int 	     count;
	gint         service_array[8];
	guint        i;
	GArray       *services;

	location_prefix = g_strconcat (location, G_DIR_SEPARATOR_S, NULL);

	service_array[0] = tracker_ontology_get_id_for_service_type ("Files");
	service_array[1] = tracker_ontology_get_id_for_service_type ("Folders");
	service_array[2] = tracker_ontology_get_id_for_service_type ("Documents");
	service_array[3] = tracker_ontology_get_id_for_service_type ("Images");
	service_array[4] = tracker_ontology_get_id_for_service_type ("Music");
	service_array[5] = tracker_ontology_get_id_for_service_type ("Videos");
	service_array[6] = tracker_ontology_get_id_for_service_type ("Text");
	service_array[7] = tracker_ontology_get_id_for_service_type ("Other");

	services = g_array_new (TRUE, TRUE, sizeof (gint));
	g_array_append_vals (services, service_array, 8);

	tree = tracker_query_tree_new (text, 
				       db_con->word_index, 
				       tracker->config,
				       tracker->language,
				       services);
	hits = tracker_query_tree_get_hits (tree, 0, 0);
	count = 0;

	for (i = 0; i < hits->len; i++) {
		TrackerDBResultSet *result_set;
		TrackerSearchHit hit;
		char *str_id, *path;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);

		result_set = tracker_exec_proc (db_con, "GetFileByID", str_id, NULL);

		g_free (str_id);

		if (result_set) {
			tracker_db_result_set_get (result_set, 0, &path, -1);

			if (g_str_has_prefix (path, location_prefix) || (strcmp (path, location) == 0)) {
				GValue value = { 0, };

				if (G_UNLIKELY (!result)) {
					result = _tracker_db_result_set_new (2);
				}

				_tracker_db_result_set_append (result);

				/* copy value in column 0 */
				_tracker_db_result_set_get_value (result_set, 0, &value);
				_tracker_db_result_set_set_value (result, 0, &value);
				g_value_unset (&value);

				/* copy value in column 1 */
				_tracker_db_result_set_get_value (result_set, 1, &value);
				_tracker_db_result_set_set_value (result, 1, &value);
				g_value_unset (&value);

				count++;
			}

			g_object_unref (result_set);
		}

		if (count > 2047) {
			break;
		}
	}

	g_free (location_prefix);

	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result)
		return NULL;

	if (tracker_db_result_set_get_n_rows (result) == 0) {
		g_object_unref (result);
		return NULL;
	}

	tracker_db_result_set_rewind (result);

	return result;
}

TrackerDBResultSet *
tracker_db_search_text_mime_location (DBConnection *db_con, const char *text, char **mime_array, const char *location)
{
	TrackerDBResultSet *result;
	TrackerQueryTree *tree;
	GArray       *hits;
	char	     *location_prefix;
	int	     count;
	gint         service_array[8];
	guint        i;
	GArray       *services;

	location_prefix = g_strconcat (location, G_DIR_SEPARATOR_S, NULL);

	service_array[0] = tracker_ontology_get_id_for_service_type ("Files");
	service_array[1] = tracker_ontology_get_id_for_service_type ("Folders");
	service_array[2] = tracker_ontology_get_id_for_service_type ("Documents");
	service_array[3] = tracker_ontology_get_id_for_service_type ("Images");
	service_array[4] = tracker_ontology_get_id_for_service_type ("Music");
	service_array[5] = tracker_ontology_get_id_for_service_type ("Videos");
	service_array[6] = tracker_ontology_get_id_for_service_type ("Text");
	service_array[7] = tracker_ontology_get_id_for_service_type ("Other");

	services = g_array_new (TRUE, TRUE, sizeof (gint));
	g_array_append_vals (services, service_array, 8);

	tree = tracker_query_tree_new (text, 
				       db_con->word_index, 
				       tracker->config,
				       tracker->language,
				       services);
	hits = tracker_query_tree_get_hits (tree, 0, 0);
	count = 0;

	for (i = 0; i < hits->len; i++) {
		TrackerDBResultSet *result_set;
		TrackerSearchHit hit;
		char *str_id, *path, *mimetype;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);

		result_set = tracker_exec_proc (db_con, "GetFileByID", str_id, NULL);

		g_free (str_id);

		if (result_set) {
			tracker_db_result_set_get (result_set,
						   0, &path,
						   2, &mimetype,
						   -1);

			if ((g_str_has_prefix (path, location_prefix) || (strcmp (path, location) == 0)) &&
			    tracker_string_in_string_list (mimetype, mime_array) != -1) {
				GValue value = { 0, };

				if (G_UNLIKELY (!result)) {
					result = _tracker_db_result_set_new (2);
				}

				_tracker_db_result_set_append (result);

				/* copy value in column 0 */
				_tracker_db_result_set_get_value (result_set, 0, &value);
				_tracker_db_result_set_set_value (result, 0, &value);
				g_value_unset (&value);

				/* copy value in column 1 */
				_tracker_db_result_set_get_value (result_set, 1, &value);
				_tracker_db_result_set_set_value (result, 1, &value);
				g_value_unset (&value);

				count++;
			}

			g_free (path);
			g_free (mimetype);
			g_object_unref (result_set);
		}

		if (count > 2047) {
			break;
		}
	}

	g_free (location_prefix);

	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result)
		return NULL;

	if (tracker_db_result_set_get_n_rows (result) == 0) {
		g_object_unref (result);
		return NULL;
	}

	tracker_db_result_set_rewind (result);

	return result;
}

TrackerDBResultSet *
tracker_db_get_metadata_types (DBConnection *db_con, const char *class, gboolean writeable)
{
	if (strcmp (class, "*") == 0) {
		if (writeable) {
			return tracker_exec_proc (db_con, "GetWriteableMetadataTypes", NULL);
		} else {
			return tracker_exec_proc (db_con, "GetMetadataTypes", NULL);
		}

	} else {
		if (writeable) {
			return tracker_exec_proc (db_con, "GetWriteableMetadataTypesLike", class, NULL);
		} else {
			return tracker_exec_proc (db_con, "GetMetadataTypesLike", class, NULL);
		}
	}
}

TrackerDBResultSet *
tracker_db_get_sub_watches (DBConnection *db_con, const char *dir)
{
	TrackerDBResultSet *result_set;
	char *folder;

	folder = g_strconcat (dir, G_DIR_SEPARATOR_S, "*", NULL);

	result_set = tracker_exec_proc (db_con->cache, "GetSubWatches", folder, NULL);

	g_free (folder);

	return result_set;
}


TrackerDBResultSet *
tracker_db_delete_sub_watches (DBConnection *db_con, const char *dir)
{
	TrackerDBResultSet *result_set;
	char *folder;

	folder = g_strconcat (dir, G_DIR_SEPARATOR_S, "*", NULL);

	result_set = tracker_exec_proc (db_con->cache, "DeleteSubWatches", folder, NULL);

	g_free (folder);

	return result_set;
}


void
tracker_db_move_file (DBConnection *db_con, const char *moved_from_uri, const char *moved_to_uri)
{

	tracker_log ("Moving file %s to %s", moved_from_uri, moved_to_uri);

	/* if orig file not in DB, treat it as a create action */
	guint32 id = tracker_db_get_file_id (db_con, moved_from_uri);
	if (id == 0) {
		tracker_debug ("WARNING: original file %s not found in DB", moved_from_uri);
		tracker_db_insert_pending_file (db_con, id, moved_to_uri,  NULL, "unknown", 0, TRACKER_DB_ACTION_WRITABLE_FILE_CLOSED, FALSE, TRUE, -1);
		tracker_db_interface_end_transaction (db_con->db);
		return;
	}

	char *str_file_id = tracker_uint_to_string (id);
	char *name = g_path_get_basename (moved_to_uri);
	char *path = g_path_get_dirname (moved_to_uri);
	char *old_name = g_path_get_basename (moved_from_uri);
	char *old_path = g_path_get_dirname (moved_from_uri);


	/* update db so that fileID reflects new uri */
	tracker_exec_proc (db_con, "UpdateFileMove", path, name, str_file_id, NULL);

	tracker_db_create_event (db_con, str_file_id, "Update");

	/* update File:Path and File:Filename metadata */
	tracker_db_set_single_metadata (db_con, "Files", str_file_id, "File:Path", path, FALSE);
	tracker_db_set_single_metadata (db_con, "Files", str_file_id, "File:Name", name, FALSE);

	char *ext = strrchr (moved_to_uri, '.');
	if (ext) {
		ext++;
		tracker_db_set_single_metadata (db_con, "Files", str_file_id,  "File:Ext", ext, FALSE);
	}

	/* update backup service if necessary */
	tracker_exec_proc (db_con->common, "UpdateBackupService", path, name, old_path, old_name, NULL);

	g_free (str_file_id);
	g_free (name);
	g_free (path);
	g_free (old_name);
	g_free (old_path);


}



static char *
str_get_after_prefix (const char *source,
		      const char *delimiter)
{
	char *prefix_start, *str;

	g_return_val_if_fail (source != NULL, NULL);

	if (delimiter == NULL) {
		return g_strdup (source);
	}

	prefix_start = strstr (source, delimiter);

	if (prefix_start == NULL) {
		return NULL;
	}

	str = prefix_start + strlen (delimiter);

	return g_strdup (str);
}




/* update all non-dirs in a dir for a file move */
static void
move_directory_files (DBConnection *db_con, const char *moved_from_uri, const char *moved_to_uri)
{
	TrackerDBResultSet *result_set;

	/* get all sub files (excluding folders) that were moved and add watches */
	result_set = tracker_exec_proc (db_con, "SelectFileChildWithoutDirs", moved_from_uri, NULL);

	if (result_set) {
		gboolean valid = TRUE;
		gchar *prefix, *name, *file_name, *moved_file_name;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   0, &prefix,
						   1, &name,
						   -1);

			if (prefix && name) {
				file_name = g_build_filename (prefix, name, NULL);
				moved_file_name = g_build_filename (moved_to_uri, name, NULL);

				tracker_db_move_file (db_con, file_name, moved_file_name);

				g_free (moved_file_name);
				g_free (file_name);
				g_free (prefix);
				g_free (name);
			}

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}
}


static inline void
move_directory (DBConnection *db_con, const char *moved_from_uri, const char *moved_to_uri)
{

	/* stop watching old dir, start watching new dir */
	tracker_remove_watch_dir (moved_from_uri, TRUE, db_con);
		
	tracker_db_move_file (db_con, moved_from_uri, moved_to_uri);
	move_directory_files (db_con, moved_from_uri, moved_to_uri);

	if (tracker_count_watch_dirs () < (int) tracker->watch_limit) {
		tracker_add_watch_dir (moved_to_uri, db_con);
	}
	
}


void
tracker_db_move_directory (DBConnection *db_con, const char *moved_from_uri, const char *moved_to_uri)
{
	TrackerDBResultSet *result_set;
	char *old_path;

	old_path = g_strconcat (moved_from_uri, G_DIR_SEPARATOR_S, NULL);

	/* get all sub folders that were moved and add watches */
	result_set = tracker_db_get_file_subfolders (db_con, moved_from_uri);

	if (result_set) {
		gboolean valid = TRUE;

		while (valid) {
			gchar *prefix, *name;
			gchar *dir_name, *sep, *new_path;

			tracker_db_result_set_get (result_set,
						   1, &prefix,
						   2, &name,
						   -1);

			dir_name = g_build_filename (prefix, name, NULL);
			sep = str_get_after_prefix (dir_name, old_path);

			if (!sep) {
				g_free (dir_name);
				continue;
			}

			new_path = g_build_filename (moved_to_uri, sep, NULL);
			g_free (sep);

			tracker_info ("moving subfolder %s to %s", dir_name, new_path);

			move_directory (db_con, dir_name, new_path);

			g_usleep (1000);

			g_free (prefix);
			g_free (name);
			g_free (new_path);
			g_free (dir_name);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	move_directory (db_con, moved_from_uri, moved_to_uri);

	g_free (old_path);
}


TrackerDBResultSet *
tracker_db_get_file_subfolders (DBConnection *db_con, const char *uri)
{
	TrackerDBResultSet *result_set;
	char *folder;

	folder = g_strconcat (uri, G_DIR_SEPARATOR_S, "*", NULL);

	result_set = tracker_exec_proc (db_con, "SelectFileSubFolders", uri, folder, NULL);

	g_free (folder);

	return result_set;
}

static void
append_index_data (gpointer key,
		   gpointer value,
		   gpointer user_data)
{
	char		*word;
	int		score;
	ServiceTypeInfo	*info;

	word = (char *) key;
	score = GPOINTER_TO_INT (value);
	info = user_data;

	if (score != 0) {
		/* cache word update */
		tracker_cache_add (word, info->service_id, info->service_type_id, score, TRUE);
	}


}


static void
update_index_data (gpointer key,
		   gpointer value,
		   gpointer user_data)
{
	char		*word;
	int		score;
	ServiceTypeInfo	*info;

	word = (char *) key;
	score = GPOINTER_TO_INT (value);
	info = user_data;

	if (score == 0) return;

	tracker_debug ("updating index for word %s with score %d", word, score);
	
	tracker_cache_add (word, info->service_id, info->service_type_id, score, FALSE);

}


void
tracker_db_update_indexes_for_new_service (guint32 service_id, int service_type_id, GHashTable *table)
{
	
	if (table) {
		ServiceTypeInfo *info;

		info = g_slice_new (ServiceTypeInfo);

		info->service_id = service_id;
		info->service_type_id = service_type_id;
	
		g_hash_table_foreach (table, append_index_data, info);
		g_slice_free (ServiceTypeInfo, info);
	}
}





static void
cmp_data (gpointer key,
	  gpointer value,
	  gpointer user_data)
{
	char	   *word;
	int	   score;
	GHashTable *new_table;

	gpointer k=0,v=0;

	word = (char *) key;
	score = GPOINTER_TO_INT (value);
	new_table = user_data;

	if (!g_hash_table_lookup_extended (new_table, word, &k, &v)) {
		g_hash_table_insert (new_table, g_strdup (word), GINT_TO_POINTER (0 - score));
	} else {
		g_hash_table_insert (new_table, (char *) word, GINT_TO_POINTER (GPOINTER_TO_INT (v) - score));
	}
}


void
tracker_db_update_differential_index (GHashTable *old_table, GHashTable *new_table, const char *id, int service_type_id)
{
	ServiceTypeInfo *info;

	g_return_if_fail (id || service_type_id > -1);

	if (!new_table) {
		new_table = g_hash_table_new (g_str_hash, g_str_equal);
	}

	/* calculate the differential word scores between old and new data*/
	if (old_table) {
		g_hash_table_foreach (old_table, cmp_data, new_table);
	}

	info = g_new (ServiceTypeInfo, 1);

	info->service_id = strtoul (id, NULL, 10);
	info->service_type_id = service_type_id;

	g_hash_table_foreach (new_table, update_index_data, info);

	g_free (info);
}


TrackerDBResultSet *
tracker_db_get_keyword_list (DBConnection *db_con, const char *service)
{

	tracker_debug (service);
	return tracker_exec_proc (db_con, "GetKeywordList", service, service, NULL);
}

GSList *
tracker_db_mime_query (DBConnection *db_con, 
                       const gchar  *stored_proc, 
                       gint          service_id)
{
	TrackerDBResultSet *result_set;
	GSList  *result = NULL;
	gchar   *service_id_str;

	service_id_str = g_strdup_printf ("%d", service_id);
	result_set = tracker_exec_proc (db_con, stored_proc, service_id_str, NULL);
	g_free (service_id_str);

	if (result_set) {
		gboolean valid = TRUE;
		gchar *str;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &str, -1);
			result = g_slist_prepend (result, str);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	return result;
}

GSList *
tracker_db_get_mimes_for_service_id (DBConnection *db_con, 
                                     gint          service_id) 
{
	return  tracker_db_mime_query (db_con, "GetMimeForServiceId", service_id);
}

GSList *
tracker_db_get_mime_prefixes_for_service_id (DBConnection *db_con,
                                             gint          service_id) 
{
	return tracker_db_mime_query (db_con, "GetMimePrefixForServiceId", service_id);
}

static TrackerService *
db_row_to_service (TrackerDBResultSet *result_set)
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
        
        tracker_service_set_show_service_files (service, show_service_files);
        tracker_service_set_show_service_directories (service, show_service_directories);
        
        for (i = 12; i < 23; i++) {
		gchar *metadata;

		tracker_db_result_set_get (result_set, i, &metadata, -1);

		if (metadata) {
			new_list = g_slist_prepend (new_list, metadata);
		}
        }
        
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
        
        new_list = g_slist_reverse (new_list);
        
        tracker_service_set_key_metadata (service, new_list);
	g_slist_foreach (new_list, (GFunc) g_free, NULL);
        g_slist_free (new_list);

        return service;
} 

static TrackerField *
db_row_to_field_def (TrackerDBResultSet *result_set) {

	TrackerField *field_def;
	TrackerFieldType field_type;
	gchar *field_name, *name;
	gint weight, id;
	gboolean embedded, multiple_values, delimited, filtered, store_metadata;

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


/* get static data like metadata field definitions and services definitions and load them into hashtables */
void
tracker_db_get_static_data (DBConnection *db_con)
{
	TrackerDBResultSet *result_set;

	/* get static metadata info */
	result_set  = tracker_exec_proc (db_con, "GetMetadataTypes", 0);

	if (result_set) {
		gboolean valid = TRUE;
		gchar *name;
		gint id;

		while (valid) {
			TrackerDBResultSet *result_set2;
			TrackerField *def;
			GSList *child_ids = NULL;

			def = db_row_to_field_def (result_set);

			result_set2 = tracker_exec_proc (db_con, "GetMetadataAliases", tracker_field_get_id (def), NULL);

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

			tracker_ontology_add_field (def);
			tracker_debug ("loading metadata def %s with weight %d", 
				       tracker_field_get_name (def), tracker_field_get_weight (def));

			g_free (name);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	/* get static service info */	
	result_set  = tracker_exec_proc (db_con, "GetAllServices", 0);

	if (result_set) {
		gboolean valid = TRUE;
		
		tracker->email_service_min = 0;
		tracker->email_service_max = 0;

		while (valid) {
			TrackerService *service;
			GSList *mimes, *mime_prefixes;
			const gchar *name;
			gint id;

                        service = db_row_to_service (result_set);

                        if (!service) {
                                continue;
			}

                        id = tracker_service_get_id (service);
                        name = tracker_service_get_name (service);

                        mimes = tracker_db_get_mimes_for_service_id (db_con, id);
                        mime_prefixes = tracker_db_get_mime_prefixes_for_service_id (db_con, id);

                        tracker_debug ("Adding service definition for %s with id %d", name, id);
                        tracker_ontology_add_service_type (service, 
							   mimes, 
							   mime_prefixes);

                        g_slist_free (mimes);
                        g_slist_free (mime_prefixes);
                        g_object_unref (service);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);

		/* check for web history */
		if (!tracker_ontology_get_service_type_by_name ("Webhistory")) {
			tracker_log ("Adding missing Webhistory service");
			tracker_exec_proc (db_con, "InsertServiceType", "Webhistory", NULL);
		}
	}
}

DBConnection *
tracker_db_get_service_connection (DBConnection *db_con, const char *service)
{
	TrackerDBType type;

	type = tracker_ontology_get_db_for_service_type (service);

	if (type == TRACKER_DB_TYPE_EMAIL) {
		return db_con->emails;
	}

	return db_con;
}


char *
tracker_db_get_service_for_entity (DBConnection *db_con, const char *id)
{
	TrackerDBResultSet *result_set;
	char *result = NULL;

	result_set = tracker_exec_proc (db_con, "GetFileByID2", id, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 1, &result, -1);
		g_object_unref (result_set);
	}

	return result;
}

// FIXME Do this in a non-retarded way

gboolean
get_service_mapping (DBConnection *db_con, const char *type, GList **list)
{
	TrackerDBResultSet *result_set;
	gboolean valid = TRUE;

	result_set = tracker_exec_proc (db_con, "GetXesamServiceMappings", type, NULL);

	if (result_set) {
		while (valid) {
			gchar *st;
			
			tracker_db_result_set_get (result_set, 0, &st, -1);
			if (strcmp(st, " ") != 0) {			
				*list = g_list_append (*list,g_strdup (st));
			}
			
			valid = tracker_db_result_set_iter_next (result_set);
			g_free (st);
		}
		
		g_object_unref (result_set);
	}

	result_set = tracker_exec_proc (db_con, "GetXesamServiceChildren", type, NULL);
	valid = TRUE;

	if (result_set) {
		while (valid) {
			gchar *st;
			
			tracker_db_result_set_get (result_set, 0, &st, -1);
			get_service_mapping(db_con, st ,list);
			
			valid = tracker_db_result_set_iter_next (result_set);
			g_free (st);
		}
		
		g_object_unref (result_set);
	}

	return TRUE;
}

gboolean
get_metadata_mapping(DBConnection *db_con, const char *type, GList **list)
{
	TrackerDBResultSet *result_set;
	gboolean valid = TRUE;

	result_set = tracker_exec_proc (db_con, "GetXesamMetaDataMappings", type, NULL);

	if (result_set) {
		while (valid) {
			gchar *st;
			
			tracker_db_result_set_get (result_set, 0, &st, -1);
			if (strcmp(st, " ") != 0) {			
				*list = g_list_append (*list,g_strdup (st));
			}
			
			valid = tracker_db_result_set_iter_next (result_set);
			g_free (st);
		}

		g_object_unref (result_set);
	}

	result_set = tracker_exec_proc (db_con, "GetXesamMetaDataChildren", type, NULL);
	valid = TRUE;

	if (result_set) {
		while (valid) {
			gchar *st;
			
			tracker_db_result_set_get (result_set, 0, &st, -1);
			get_service_mapping(db_con, st ,list);
			
			valid = tracker_db_result_set_iter_next (result_set);
			g_free (st);
		}
		
		g_object_unref (result_set);
	}

	return TRUE;
}


gboolean
tracker_db_create_xesam_lookup (DBConnection *db_con)
{
	TrackerDBResultSet   *result_set;
	gboolean              valid = TRUE;

	result_set = tracker_exec_proc (db_con, "GetXesamServiceTypes", NULL);
	
	if (result_set) {
		while (valid) {
				gchar *st;
				GList *list = NULL;
				GList *iter = NULL;
				
				tracker_db_result_set_get (result_set, 0, &st, -1);
				get_service_mapping(db_con, st, &list);
				
				iter = g_list_first(list);
				while (iter) {
					tracker_exec_proc (db_con, "InsertXesamServiceLookup", st, iter->data, NULL);
					g_free(iter->data);
					
					iter = g_list_next (iter);
				}			
				
				g_list_free (list);
				
				valid = tracker_db_result_set_iter_next (result_set);
				g_free (st);
		}
	}

	g_object_unref (result_set);	
	valid = TRUE;

	result_set = tracker_exec_proc (db_con, "GetXesamMetaDataTypes", NULL);

	if (result_set) {
		while (valid) {
				gchar *st;
				GList *list = NULL;
				GList *iter = NULL;	

				tracker_db_result_set_get (result_set, 0, &st, -1);
				get_metadata_mapping(db_con, st, &list);
				
				iter = g_list_first(list);
				while (iter) {
					tracker_exec_proc (db_con, "InsertXesamMetaDataLookup", st, iter->data, NULL);
					g_free(iter->data);
					
					iter = g_list_next (iter);
				}			
				
				g_list_free (list);
				
				valid = tracker_db_result_set_iter_next (result_set);
				g_free (st);
		}
	}
	
	g_object_unref (result_set);	

	return TRUE;
}

gboolean
tracker_db_load_xesam_service_file (DBConnection *db_con, const char *filename)
{
	GKeyFile 		*key_file = NULL;
	const char * const 	*locale_array;
	char 			*service_file, *sql;
	gboolean		is_metadata = FALSE, is_service = FALSE, is_metadata_mapping = FALSE, is_service_mapping = FALSE;
	int			id;

	char *DataTypeArray[11] = {"string", "float", "integer", "boolean", "dateTime", "List of strings", "List of Uris", "List of Urls", NULL};

	service_file = tracker_db_manager_get_service_file (filename);

	locale_array = g_get_language_names ();

	key_file = g_key_file_new ();

	if (g_key_file_load_from_file (key_file, service_file, G_KEY_FILE_NONE, NULL)) {
		
		if (g_str_has_suffix (filename, ".metadata")) {
			is_metadata = TRUE;
		} else if (g_str_has_suffix (filename, ".service")) {
			is_service = TRUE;
		} else if (g_str_has_suffix (filename, ".mmapping")) {
			is_metadata_mapping = TRUE;
		} else if (g_str_has_suffix (filename, ".smapping")) {
			is_service_mapping = TRUE;
		} else {
			g_key_file_free (key_file);
			g_free (service_file);		
			return FALSE;
		} 


		char **groups = g_key_file_get_groups (key_file, NULL);
		char **array;

		for (array = groups; *array; array++) {
                        if (is_metadata) {

		                tracker_exec_proc (db_con, "InsertXesamMetadataType", *array, NULL);
				id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (db_con->db));


		        } else if (is_service) {
				
		                tracker_exec_proc (db_con, "InsertXesamServiceType", *array, NULL);
				id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (db_con->db));

			} else {
				/* Nothing required */
			}

			/* get inserted ID */
			
			char *str_id = tracker_uint_to_string (id);

			char **keys = g_key_file_get_keys (key_file, *array, NULL, NULL);
			char **array2;
	
			for (array2 = keys; *array2; array2++) {
	
				char *value = g_key_file_get_locale_string (key_file, *array, *array2, locale_array[0], NULL);

				if (value) {

					if (strcasecmp (value, "true") == 0) {

						g_free (value);
						value = g_strdup ("1");

					} else if  (strcasecmp (value, "false") == 0) {

						g_free (value);
						value = g_strdup ("0");
					}

					if (is_metadata) {
						
						if (strcasecmp (*array2, "Parents") == 0) {
							
							char **parents, **parents_p ;

							parents = g_strsplit_set (value, ";", -1);
							
							if (parents) {
								for (parents_p = parents; *parents_p; parents_p++) {
									sql = g_strdup_printf ("INSERT INTO XesamMetadataChildren (Parent, Child) VALUES ('%s', '%s')", *parents_p, *array);
									tracker_db_exec_no_reply (db_con->db, sql);
									
									g_free(sql);
								}
							}

						} else if (strcasecmp (*array2, "ValueType") == 0) {
							
							int data_id =  tracker_string_in_string_list (value, DataTypeArray);
							
							if (data_id != -1) {
								sql = g_strdup_printf ("update XesamMetadataTypes set DataTypeID = %d where ID = %s", data_id, str_id);
								tracker_db_exec_no_reply (db_con->db, sql);
								g_free (sql);
								
							}
							
							
						} else {
							char *esc_value = tracker_escape_string (value);
							
							sql = g_strdup_printf ("update XesamMetadataTypes set  %s = '%s' where ID = %s", *array2, esc_value, str_id);
								
							tracker_db_exec_no_reply (db_con->db, sql);
							g_free (sql);
							g_free (esc_value);
						}
	
					} else 	if (is_service) {
						if (strcasecmp (*array2, "Parents") == 0) {
							
							char **parents, **parents_p ;

							parents = g_strsplit_set (value, ";", -1);
							
							if (parents) {
								for (parents_p = parents; *parents_p; parents_p++) {
									sql = g_strdup_printf ("INSERT INTO XesamServiceChildren (Parent, Child) VALUES ('%s', '%s')", *parents_p, *array);
									tracker_db_exec_no_reply (db_con->db, sql);
									
									g_free(sql);
								}
							}
						} else {
							char *esc_value = tracker_escape_string (value);
							sql = g_strdup_printf ("update XesamServiceTypes set  %s = '%s' where typeID = %s", *array2, esc_value, str_id);
							tracker_db_exec_no_reply (db_con->db, sql);
							g_free (sql);
							g_free (esc_value);
						}
	
					} else 	if (is_metadata_mapping) {
						char **mappings, **mappings_p ;
						
						mappings = g_strsplit_set (value, ";", -1);
						
						if (mappings) {
							for (mappings_p = mappings; *mappings_p; mappings_p++) {
								char *esc_value = tracker_escape_string (*mappings_p);
								tracker_exec_proc (db_con, "InsertXesamMetaDataMapping", *array, esc_value, NULL);
								g_free (esc_value);
							}
						}
							
					} else {
						char **mappings, **mappings_p ;
						
						mappings = g_strsplit_set (value, ";", -1);
						
						if (mappings) {
							for (mappings_p = mappings; *mappings_p; mappings_p++) {
								char *esc_value = tracker_escape_string (*mappings_p);
								tracker_exec_proc (db_con, "InsertXesamServiceMapping", *array, esc_value, NULL);
								g_free (esc_value);
							}
						}
						
					}
					
					g_free (value);
					
				}
			}

			if (keys) {
				g_strfreev (keys);
			}

			g_free (str_id);

		}


		if (groups) {
			g_strfreev (groups);
		}
			

		g_key_file_free (key_file);

	} else {
		g_key_file_free (key_file);
		return FALSE;
	}
		       
	g_free (service_file);		

	return TRUE;
}


FieldData *
tracker_db_get_metadata_field (DBConnection *db_con, const char *service, const char *field_name, int field_count, gboolean is_select, gboolean is_condition)
{
	FieldData    *field_data = NULL;
	const TrackerField *def;

	field_data = g_new0 (FieldData, 1);

	field_data->is_select = is_select;
	field_data->is_condition = is_condition;
	field_data->field_name = g_strdup (field_name);

	def = tracker_ontology_get_field_def (field_name);

	if (def) {
	
		field_data->table_name = tracker_get_metadata_table (tracker_field_get_data_type (def));
		field_data->alias = g_strdup_printf ("M%d", field_count);
		field_data->data_type = tracker_field_get_data_type (def);
		field_data->id_field = g_strdup (tracker_field_get_id (def));
		field_data->multiple_values = tracker_field_get_multiple_values (def);
			
		char *my_field = tracker_db_get_field_name (service, field_name);

		if (my_field) {
			field_data->select_field = g_strdup_printf (" S.%s ", my_field);
			g_free (my_field);
			field_data->needs_join = FALSE;	
		} else {
			char *disp_field = tracker_ontology_get_display_field (def);
			field_data->select_field = g_strdup_printf ("M%d.%s", field_count, disp_field);
			g_free (disp_field);
			field_data->needs_join = TRUE;
		}
			
		if (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_DOUBLE) {
			field_data->where_field = g_strdup_printf ("M%d.MetaDataDisplay", field_count);
		} else {
			field_data->where_field = g_strdup_printf ("M%d.MetaDataValue", field_count);
		}

	} else {
		g_free (field_data);
		return NULL;
	}


	return field_data;
}

char *
tracker_db_get_option_string (DBConnection *db_con, const char *option)
{
	TrackerDBResultSet *result_set;
	gchar *value = NULL;

	result_set = tracker_exec_proc (db_con->common, "GetOption", option, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &value, -1);
		g_object_unref (result_set);
	}

	return value;
}


void
tracker_db_set_option_string (DBConnection *db_con, const char *option, const char *value)
{
	tracker_exec_proc (db_con->common, "SetOption", value, option, NULL);
}


int
tracker_db_get_option_int (DBConnection *db_con, const char *option)
{
	TrackerDBResultSet *result_set;
	gchar *str;
	int value = 0;

	result_set = tracker_exec_proc (db_con->common, "GetOption", option, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &str, -1);

		if (str) {
			value = atoi (str);
			g_free (str);
		}

		g_object_unref (result_set);
	}

	return value;
}


void
tracker_db_set_option_int (DBConnection *db_con, const char *option, int value)
{
	char *str_value = tracker_int_to_string (value);

	tracker_exec_proc (db_con->common, "SetOption", str_value, option, NULL);

	g_free (str_value);
}

static gint 
get_memory_usage (void)
{
#if defined(__linux__)
	gint    fd, length, mem = 0;
	gchar   buffer[8192];
	gchar  *stat_file;
	gchar **terms;

	stat_file = g_strdup_printf ("/proc/%d/stat", tracker->pid);
	fd = open (stat_file, O_RDONLY); 
	g_free (stat_file);

	if (fd ==-1) {
		return 0;
	}
	
	length = read (fd, buffer, 8192);
	buffer[length] = 0;
	close (fd);

	terms = g_strsplit (buffer, " ", -1);

	if (terms) {
		gint i;

		for (i = 0; i < 24; i++) {
			if (!terms[i]) {
				break;
			}		

			if (i==23) {
				mem = 4 * atoi (terms[23]);
			}
		}
	}

	g_strfreev (terms);

	return mem;	
#endif
	return 0;
}

gboolean
tracker_db_regulate_transactions (DBConnection *db_con, int interval)
{
	tracker->index_count++;
	
	if ((tracker->index_count == 1 || tracker->index_count == interval  || (tracker->index_count >= interval && tracker->index_count % interval == 0))) {
			
		if (tracker->index_count > 1) {
			tracker_db_end_index_transaction (db_con);
			tracker_db_start_index_transaction (db_con);
			tracker_log ("Current memory usage is %d, word count %d and hits %d", 
				     get_memory_usage (), 
				     tracker->word_count, 
				     tracker->word_detail_count);
		}

		return TRUE;
			
	}

	return FALSE;

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
