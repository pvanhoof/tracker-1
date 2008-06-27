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
#include <libtracker-common/tracker-service.h>
#include <libtracker-common/tracker-field.h>
#include <libtracker-common/tracker-ontology.h>
#include <glib.h>
#include <glib/gtestutils.h>
#include <tracker-test-helpers.h>

gboolean
test_cmp_servicedef_equals (TrackerService *one, 
                            TrackerService *two)
{
	
	if ( one && !two) {
		return FALSE; // one is null and two not
	}

	if ( !one && two ) {
		return FALSE;
	}

	if ( !one && one == two ) {
		return TRUE; // Both null
	}

	return ( tracker_service_get_id (one) == tracker_service_get_id (two) 
		 && tracker_test_helpers_cmpstr_equal ( tracker_service_get_name (one), 
                                               tracker_service_get_name (two))
                 && tracker_test_helpers_cmpstr_equal ( tracker_service_get_parent (one),
                                         tracker_service_get_parent (two))
		 && tracker_service_get_db_type (one) == tracker_service_get_db_type (two)
		 && tracker_service_get_embedded (one) == tracker_service_get_embedded (two));

}

TrackerField *
create_field_definition (const gchar *id, 
                         const gchar *name, 
                         TrackerFieldType data_type,
                         const gchar *field_name,
                         gboolean multiple_values,
                         GSList *child_ids) 
{
        TrackerField *field;

        field = tracker_field_new ();

        tracker_field_set_id (field, id);
        tracker_field_set_name (field, name);
        tracker_field_set_data_type (field, data_type);
        tracker_field_set_field_name (field, field_name);
        tracker_field_set_multiple_values (field, multiple_values);
        tracker_field_set_child_ids (field, child_ids);

        return field;
}

TrackerService *
create_service_definition (int id, const char *name, const char *parent, gboolean embedded) {

        TrackerService *def;
        
        def = tracker_service_new ();
        tracker_service_set_id (def, id);
        tracker_service_set_name (def, name);
        tracker_service_set_parent (def, parent);
        tracker_service_set_db_type (def, TRACKER_DB_TYPE_CONTENT);
        tracker_service_set_enabled (def, FALSE);
        tracker_service_set_embedded (def, embedded);
        tracker_service_set_has_thumbs (def, TRUE);
        tracker_service_set_has_full_text (def, TRUE);
        tracker_service_set_has_metadata (def, FALSE);

	return def;
}


static GSList *
array_to_list (char **array)                                                                          
{                                                                                                     
        GSList  *list = NULL;
        int     i;

        for (i = 0; array[i] != NULL; i++) {
		list = g_slist_prepend (list, g_strdup (array[i]));
        }                                                                                                      
        return list; 
}

typedef struct {
	TrackerService *def;
	TrackerService *parent_def;
} ExpectedResults;

static ExpectedResults *expected_results = NULL;

static void
tracker_services_general_setup ()
{
	TrackerService *def, *parent_def, *other_def;
        TrackerService *conv_def, *gaim_def, *gossip_def, *new_gaim_def;
        TrackerField *field_title;

	GSList *mimes, *mime_prefixes; //, *app_dirs_list, *app_extension_list;

	def = create_service_definition (0, "Test service", "Parent service", TRUE);
	parent_def = create_service_definition (1, "Parent service", NULL, FALSE);
        other_def = create_service_definition (2, "Applications", NULL, FALSE);
        conv_def = create_service_definition (3, "Conversations", NULL, FALSE);
        gaim_def = create_service_definition (4, "GaimConversations", "Conversations", FALSE);
        gossip_def = create_service_definition (5, "GossipConversations", "Conversations", FALSE);
        new_gaim_def = create_service_definition (6, "NewGaimConversations", "GaimConversations", FALSE);

        field_title = create_field_definition ("0", 
                                               "App.Title", 
                                               TRACKER_FIELD_TYPE_INDEX,
                                               "Title",
                                               TRUE,
                                               NULL);

	char * m[] = {"application/rtf", "text/joke", "test/1", NULL};
	mimes = array_to_list (m);

	char *mp[] = {"images/", "video/", "other.mimes.", NULL};
	mime_prefixes = array_to_list (mp);
	tracker_ontology_init ();
        
        expected_results = g_new0 (ExpectedResults, 1);
        expected_results->def = def;
	expected_results->parent_def = parent_def;

	tracker_ontology_add_service_type (def, NULL, NULL); 
	tracker_ontology_add_service_type (parent_def, mimes, mime_prefixes);
        tracker_ontology_add_service_type (other_def, NULL, NULL); 
	tracker_ontology_add_service_type (conv_def, NULL, NULL); 
	tracker_ontology_add_service_type (gaim_def, NULL, NULL); 
	tracker_ontology_add_service_type (gossip_def, NULL, NULL); 
	tracker_ontology_add_service_type (new_gaim_def, NULL, NULL); 

        tracker_ontology_add_field (field_title);

	g_slist_free (mimes);
	g_slist_free (mime_prefixes);

}

static void
test_get_id_for_service ()
{
	gint result_int;

	result_int = tracker_ontology_get_id_for_service_type ("Test service");
	g_assert_cmpint (result_int, ==, 0);
	result_int = tracker_ontology_get_id_for_service_type ("trash");
	g_assert_cmpint (result_int, ==, -1);
}


static void
test_get_service_by_id ()
{
	gchar *result_string;

	result_string = tracker_ontology_get_service_type_by_id (0);
	g_assert ( g_str_equal (result_string, "Test service"));
	g_free (result_string);
	result_string = tracker_ontology_get_service_type_by_id (20);
	g_assert (!result_string);
}


static void
test_get_parent_service_by_id ()
{
	gchar *result_string;

	result_string = tracker_ontology_get_parent_service_by_id (0);
	g_assert ( g_str_equal (result_string, "Parent service"));
	g_free (result_string);
	result_string = tracker_ontology_get_parent_service_by_id (1);
	g_assert (!result_string);
}

static void
test_get_parent_id_for_service_id ()
{
	gint result_int;

	result_int = tracker_ontology_get_parent_id_for_service_id (0);
	g_assert_cmpint (result_int, ==, 1);
	result_int = tracker_ontology_get_parent_id_for_service_id (1);
	g_assert_cmpint (result_int, ==, -1);
}

static void
test_get_parent_service ()
{
	gchar *result_string;

	result_string = tracker_ontology_get_parent_service ("Test service");
	g_assert (g_str_equal (result_string, "Parent service"));
	g_free (result_string);
	result_string = tracker_ontology_get_parent_service ("Parent service");
	g_assert (!result_string);
}


static void
test_get_service_type_for_mime ()
{
	g_assert ( g_str_equal ("Parent service", 
				tracker_ontology_get_service_type_for_mime ("application/rtf")));
	g_assert ( g_str_equal ("Parent service",
				tracker_ontology_get_service_type_for_mime ("images/jpeg")));
	g_assert ( g_str_equal ("Other",
				tracker_ontology_get_service_type_for_mime ("noexists/bla")));
}




static void
test_get_service ()
{
	TrackerService *result_def;

	result_def = tracker_ontology_get_service_type_by_name ("Test service");
	g_assert (test_cmp_servicedef_equals (result_def, expected_results->def));
	result_def = tracker_ontology_get_service_type_by_name ("No no no");
	g_assert (!test_cmp_servicedef_equals (result_def, expected_results->def));
	result_def = tracker_ontology_get_service_type_by_name ("Parent service");
	g_assert (test_cmp_servicedef_equals (result_def, expected_results->parent_def));
}


static void
test_get_db_for_service ()
{
	TrackerDBType result_db;

	result_db = tracker_ontology_get_db_for_service_type ("Test service");
	g_assert (result_db == TRACKER_DB_TYPE_DATA); // ????? HARDCODED IN tracker-utils!!!!!
	result_db = tracker_ontology_get_db_for_service_type ("trash");
	g_assert (result_db == TRACKER_DB_TYPE_DATA);
}


static void
test_is_service_embedded ()
{
	g_assert (tracker_ontology_service_type_has_embedded ("Test service"));
	g_assert (!tracker_ontology_service_type_has_embedded ("Parent service"));
	g_assert (!tracker_ontology_service_type_has_embedded ("Trash"));
}

static void
test_has_thumbnails (
		     )
{
	g_assert (tracker_ontology_service_type_has_thumbnails ("Test service"));
	g_assert (!tracker_ontology_service_type_has_thumbnails ("trash"));
}

static void
test_has_text ()
{
	g_assert (tracker_ontology_service_type_has_text ("Test service"));
	g_assert (!tracker_ontology_service_type_has_text ("trash"));
}

static void
test_has_metadata ()
{
	g_assert (!tracker_ontology_service_type_has_metadata ("Test service"));
	g_assert (!tracker_ontology_service_type_has_metadata ("trash"));
}

static void
test_field_in_ontology ()
{
        TrackerField *field;

        field = tracker_ontology_get_field_def ("App.Title");
        g_assert (field);
        g_assert (!tracker_ontology_get_field_def ("nooooo"));
}

int
main (int argc, char **argv) {

        int result;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);

        tracker_services_general_setup ();

	g_test_add_func ("/trackerd/tracker-services/get_id_for_service",  
                         test_get_id_for_service);
	g_test_add_func ("/trackerd/tracker-services/get_service_for_id",  
                         test_get_service_by_id);
        g_test_add_func ("/trackerd/tracker-services/get_parent_service_by_id",  
                          test_get_parent_service_by_id); 
	g_test_add_func ("/trackerd/tracker-services/get_parent_id_for_service_id",  
                         test_get_parent_id_for_service_id);
	g_test_add_func ("/trackerd/tracker-services/get_parent_service", 
                         test_get_parent_service);
	g_test_add_func ("/trackerd/tracker-services/get_service_type_for_mime", 
                         test_get_service_type_for_mime);
	g_test_add_func ("/trackerd/tracker-services/get_service", 
                         test_get_service);
	g_test_add_func ("/trackerd/tracker-services/get_db_for_service", 
                         test_get_db_for_service);
	g_test_add_func ("/trackerd/tracker-services/is_service_embedded", 
                         test_is_service_embedded);
	g_test_add_func ("/trackerd/tracker-services/has_thumbnails", 
                         test_has_thumbnails);
	g_test_add_func ("/trackerd/tracker-services/has_text", 
                         test_has_text);
	g_test_add_func ("/trackerd/tracker-services/has_metadata", 
                         test_has_metadata);
        g_test_add_func ("/trackerd/tracker-services/test_field_in_ontology",
                         test_field_in_ontology);

        result = g_test_run ();
        
        tracker_ontology_shutdown ();
        return result;
}
