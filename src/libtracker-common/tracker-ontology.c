/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "tracker-ontology.h"

typedef struct {
	gchar *prefix;
	gint   service;
} ServiceMimePrefixes;

/* Hash (gint service_type_id, TrackerService *service) */ 
static GHashTable *service_id_table;   

/* Hash (gchar *service_name, TrackerService *service) */
static GHashTable *service_table;      

/* Hash (gchar *mime, gint service_type_id) */
static GHashTable *mime_service;       

/* List of ServiceMimePrefixes */
static GSList     *mime_prefix_service; 

/* The service directory table is used to store a ServiceInfo struct
 * for a directory path - used for determining which service a uri
 * belongs to for things like files, emails, conversations etc 
 */ 
static GHashTable *service_directory_table;
static GSList	  *service_directory_list;

/* Field descriptions */
static GHashTable *metadata_table;

/* FieldType enum class */
static gpointer field_type_enum_class;



static void
ontology_mime_prefix_foreach (gpointer data, 
			      gpointer user_data) 
{
	ServiceMimePrefixes *mime_prefix;

	mime_prefix = (ServiceMimePrefixes*) data;

	g_free (mime_prefix->prefix);
	g_free (mime_prefix);
}

gpointer
ontology_hash_lookup_by_str (GHashTable  *hash_table, 
			     const gchar *str)
{
	gpointer *data;
	gchar    *str_lower;

	str_lower = g_utf8_strdown (str, -1);
	data = g_hash_table_lookup (hash_table, str_lower);
	g_free (str_lower);

	return data;
}

gpointer
ontology_hash_lookup_by_id (GHashTable  *hash_table, 
			    gint         id)
{
	gpointer *data;
	gchar    *str;

	str = g_strdup_printf ("%d", id);
	data = g_hash_table_lookup (hash_table, str);
	g_free (str);

	return data;
}

void
tracker_ontology_init (void)
{

	g_return_if_fail (service_id_table == NULL 
			  && service_table == NULL
			  && mime_service == NULL);

	service_id_table = g_hash_table_new_full (g_str_hash, 
						  g_str_equal, 
						  g_free, 
						  g_object_unref);
	
	service_table = g_hash_table_new_full (g_str_hash, 
					       g_str_equal,
					       g_free, 
					       g_object_unref);
	
	mime_service = g_hash_table_new_full (g_str_hash, 
					      g_str_equal, 
					      NULL, 
					      NULL);

	service_directory_table = g_hash_table_new_full (g_str_hash, 
							 g_str_equal, 
							 g_free, 
							 g_free);

	metadata_table = g_hash_table_new_full (g_str_hash,
						g_str_equal,
						NULL, //Pointer to the object name
						g_object_unref);

	/* We will need the class later in order to match strings to enum values
	 * when inserting metadata types in the DB, so the enum class needs to be
	 * created beforehand.
	 */
	field_type_enum_class = g_type_class_ref (TRACKER_TYPE_FIELD_TYPE);
}

void
tracker_ontology_shutdown (void)
{
	g_hash_table_remove_all (service_directory_table);
	g_hash_table_remove_all (service_id_table);
	g_hash_table_remove_all (service_table);
	g_hash_table_remove_all (mime_service);
	g_hash_table_remove_all (metadata_table);

	if (mime_prefix_service) {
		g_slist_foreach (mime_prefix_service, 
				 ontology_mime_prefix_foreach, 
				 NULL); 
		g_slist_free (mime_prefix_service);
	}

	g_type_class_unref (field_type_enum_class);
	field_type_enum_class = NULL;
}

void 
tracker_ontology_add_service_type (TrackerService *service,
				   GSList         *mimes,
				   GSList         *mime_prefixes)
{

	GSList              *mime, *prefix;
	ServiceMimePrefixes *service_mime_prefix;
	gint                 id;
	const gchar         *name;

	g_return_if_fail (TRACKER_IS_SERVICE (service));

	id = tracker_service_get_id (service);
	name = tracker_service_get_name (service);

	g_hash_table_insert (service_table, 
			     g_utf8_strdown (name, -1), 
			     g_object_ref (service));
	g_hash_table_insert (service_id_table, 
			     g_strdup_printf ("%d", id), 
			     g_object_ref (service));

	for (mime = mimes; mime != NULL && mime->data != NULL; mime = mime->next) {
		g_hash_table_insert (mime_service, 
				     mime->data, 
				     GINT_TO_POINTER (id));
	}

	for (prefix = mime_prefixes; prefix != NULL; prefix = prefix->next) {
		service_mime_prefix = g_new0 (ServiceMimePrefixes, 1);
		service_mime_prefix->prefix = prefix->data;
		service_mime_prefix->service = id;
		mime_prefix_service = g_slist_prepend (mime_prefix_service, 
						       service_mime_prefix);
	}
}

TrackerService *
tracker_ontology_get_service_type_by_name (const gchar *service_str)
{
	return ontology_hash_lookup_by_str (service_table, service_str);
}

gchar *
tracker_ontology_get_service_type_by_id (gint id)
{
	TrackerService *service;

	service = ontology_hash_lookup_by_id (service_id_table, id);

	if (!service) {
		return NULL;
	}

	return g_strdup (tracker_service_get_name (service));
}

gchar *
tracker_ontology_get_service_type_for_mime (const gchar *mime) 
{
	gpointer            *id;
	ServiceMimePrefixes *item;
	GSList              *prefix_service;

	/* Try a complete mime */
	id = g_hash_table_lookup (mime_service, mime);
	if (id) {
		return tracker_ontology_get_service_type_by_id (GPOINTER_TO_INT (id));
	}

	/* Try in prefixes */
	for (prefix_service = mime_prefix_service; 
	     prefix_service != NULL; 
	     prefix_service = prefix_service->next) {
		item = prefix_service->data;
		if (g_str_has_prefix (mime, item->prefix)) {
			return tracker_ontology_get_service_type_by_id (item->service);
		}
	}
	
	/* Default option */
	return g_strdup ("Other");
}

gint
tracker_ontology_get_id_for_service_type (const char *service_str)
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return -1;
	}

	return tracker_service_get_id (service);
}

gchar *
tracker_ontology_get_parent_service (const gchar *service_str)
{
	TrackerService *service;
	const gchar    *parent = NULL;

	service = ontology_hash_lookup_by_str (service_table, service_str);
	
	if (service) {
		parent = tracker_service_get_parent (service);
	}

	return g_strdup (parent);
}

gchar *
tracker_ontology_get_parent_service_by_id (gint id)
{
	TrackerService *service;

	service = ontology_hash_lookup_by_id (service_id_table, id);

	if (!service) {
		return NULL;
	}

	return g_strdup (tracker_service_get_parent (service));
}

gint
tracker_ontology_get_parent_id_for_service_id (gint id)
{
	TrackerService *service;
	const gchar    *parent = NULL;

	service = ontology_hash_lookup_by_id (service_id_table, id);

	if (service) {
		parent = tracker_service_get_parent (service);
	}

	if (!parent) {
		return -1;
	}
	
	service = ontology_hash_lookup_by_str (service_table, parent);

	if (!service) {
		return -1;
	}

	return tracker_service_get_id (service);
}

TrackerDBType
tracker_ontology_get_db_for_service_type (const gchar *service_str)
{
	TrackerDBType  type;
	gchar         *str;

	type = TRACKER_DB_TYPE_DATA;
	str = g_utf8_strdown (service_str, -1);

	if (g_str_has_prefix (str, "emails") || 
	    g_str_has_prefix (str, "attachments")) {
		type = TRACKER_DB_TYPE_EMAIL;
	}

	g_free (str);

	return type;
}

gboolean
tracker_ontology_service_type_has_embedded (const gchar *service_str)
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return FALSE;
	}

	return tracker_service_get_embedded (service);
}

gboolean
tracker_ontology_is_valid_service_type (const gchar *service_str)
{
	return tracker_ontology_get_id_for_service_type (service_str) != -1;
}

gboolean
tracker_ontology_service_type_has_metadata (const gchar *service_str) 
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return FALSE;
	}

	return tracker_service_get_has_metadata (service);
}

gboolean
tracker_ontology_service_type_has_thumbnails (const gchar *service_str)
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return FALSE;
	}

	return tracker_service_get_has_thumbs (service);
}

gboolean 
tracker_ontology_service_type_has_text (const char *service_str) 
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return FALSE;
	}

	return tracker_service_get_has_full_text (service);
}

gint
tracker_ontology_metadata_key_in_service (const gchar *service_str, 
					  const gchar *meta_name)
{
	TrackerService *service;
	gint            i;
	const GSList   *l;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return 0;
	}

	for (l = tracker_service_get_key_metadata (service), i = 0; 
	     l; 
	     l = l->next, i++) {
		if (!l->data) {
			continue;
		}

		if (strcasecmp (l->data, meta_name) == 0) {
			return i;
		}
	}

	return 0;
}

gboolean
tracker_ontology_show_service_directories (const gchar *service_str) 
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return FALSE;
	}

	return tracker_service_get_show_service_directories (service);
}

gboolean
tracker_ontology_show_service_files (const gchar *service_str) 
{
	TrackerService *service;

	service = ontology_hash_lookup_by_str (service_table, service_str);

	if (!service) {
		return FALSE;
	}

	return tracker_service_get_show_service_files (service);
}

/*
 * Service directories
 */
GSList *
tracker_ontology_get_dirs_for_service_type (const gchar *service)
{
	GSList *list = NULL;
	GSList *l;

	g_return_val_if_fail (service != NULL, NULL);

	for (l = service_directory_list; l; l = l->next) {
		gchar *str;
		
		str = g_hash_table_lookup (service_directory_table, l->data);

		if (strcasecmp (service, str) == 0) {
			list = g_slist_prepend (list, l->data);
		}
	}

	return list;
}

void
tracker_ontology_add_dir_to_service_type (const gchar *service,  
					  const gchar *path)
{
	g_return_if_fail (service != NULL);
	g_return_if_fail (path != NULL);
	
	g_debug ("Adding path:'%s' for service:'%s'", path, service);

	service_directory_list = g_slist_prepend (service_directory_list, 
						  g_strdup (path));

	g_hash_table_insert (service_directory_table, 
			     g_strdup (path), 
			     g_strdup (service));
}

void
tracker_ontology_remove_dir_to_service_type (const gchar *service,  
					     const gchar *path)
{
	GSList *found;

	g_return_if_fail (service != NULL);
	g_return_if_fail (path != NULL);

	g_debug ("Removing path:'%s' for service:'%s'", path, service);

	found = g_slist_find_custom (service_directory_list, 
				     path, 
				     (GCompareFunc) strcmp);
	if (found) {
		service_directory_list = g_slist_remove_link (service_directory_list, found);
		g_free (found->data);
		g_slist_free (found);
	}

	g_hash_table_remove (service_directory_table, path);
}

gchar *
tracker_ontology_get_service_type_for_dir (const gchar *path)
{
	GSList *l;

	g_return_val_if_fail (path != NULL, g_strdup ("Files"));

	/* Check service dir list to see if a prefix */
	for (l = service_directory_list; l; l = l->next) {
		const gchar *str;

		if (!l->data || !g_str_has_prefix (path, l->data)) {
                        continue;
                }
		
		str = g_hash_table_lookup (service_directory_table, l->data);

		return g_strdup (str);
	}

	return g_strdup ("Files");
}

/* Field Handling */
void
tracker_ontology_add_field (TrackerField *field)
{
	g_return_if_fail (TRACKER_IS_FIELD (field));
	g_return_if_fail (tracker_field_get_name (field) != NULL);
	
	g_hash_table_insert (metadata_table, 
			     g_utf8_strdown (tracker_field_get_name (field), -1),
			     field);
}

gchar *
tracker_ontology_get_field_column_in_services (TrackerField *field, 
					       const gchar  *service_type)
{
	const gchar *field_name;
	const gchar *meta_name;
	gint         key_field;

	meta_name = tracker_field_get_name (field);
	key_field = tracker_ontology_metadata_key_in_service (service_type, 
							      meta_name);

	if (key_field > 0) {
		return g_strdup_printf ("KeyMetadata%d", key_field);

	} 

	/* TODO do it using field_name in TrackerField! */
	field_name = tracker_field_get_field_name (field);
	if (field_name) {
		return g_strdup (field_name);
	} else {
		return NULL;
	}
}

gchar *
tracker_ontology_get_display_field (TrackerField *field)
{
	TrackerFieldType type;

	type = tracker_field_get_data_type (field);

	if (type == TRACKER_FIELD_TYPE_INDEX ||
	    type == TRACKER_FIELD_TYPE_STRING || 
	    type == TRACKER_FIELD_TYPE_DOUBLE) {
		return g_strdup ("MetaDataDisplay");
	}

	return g_strdup ("MetaDataValue");
}

gboolean
tracker_ontology_field_is_child_of (const gchar *child, const gchar *parent) 
{
	TrackerField *def_child;
	TrackerField *def_parent;
	const GSList *tmp;

	def_child = tracker_ontology_get_field_def (child);

	if (!def_child) {
		return FALSE;
	}

	def_parent = tracker_ontology_get_field_def (parent);

	if (!def_parent) {
		return FALSE;
	}

	for (tmp = tracker_field_get_child_ids (def_parent); tmp; tmp = tmp->next) {
		
		if (!tmp->data) return FALSE;

		if (strcmp (tracker_field_get_id (def_child), tmp->data) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

TrackerField *
tracker_ontology_get_field_def (const gchar *name) 
{
	return ontology_hash_lookup_by_str (metadata_table, name);
}

const gchar *
tracker_ontology_get_field_id (const gchar *name)
{
	TrackerField *field;

	field = tracker_ontology_get_field_def (name);

	if (field) {
		return tracker_field_get_id (field);
	}
	
	return NULL;
}

