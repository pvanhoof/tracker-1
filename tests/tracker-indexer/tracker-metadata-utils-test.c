#include <glib.h>
#include <glib/gtestutils.h>
#include <string.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-type-utils.h>
#include <tracker-test-helpers.h>
#include "tracker-metadata-utils.h"

/* From src/tracker-indexer/tracker-metadata-utils.c */
#define METADATA_FILE_EXT            "File:Ext"
#define METADATA_FILE_PATH           "File:Path"
#define METADATA_FILE_NAME           "File:Name"
#define METADATA_FILE_SIZE           "File:Size"

#define TEST_METADATA_PLAYCOUNT      "Audio:Playcount"
#define TEST_METADATA_SUBJECT        "DC:Subject"
#define TEST_METADATA_USER_KEYWORDS  "User:Keywords"

static void
ontology_init () 
{
        TrackerField *ext, *name, *path, *size, *playcount, *dc_subject, *user_keywords;

        tracker_ontology_init ();

        ext = g_object_new (TRACKER_TYPE_FIELD, 
                            "name", METADATA_FILE_EXT,
                            "embedded", TRUE,
                            "multiple-values", FALSE, 
                            NULL);
        
        name = g_object_new (TRACKER_TYPE_FIELD, 
                             "name", METADATA_FILE_NAME,
                             "embedded", TRUE,
                             "multiple-values", FALSE, 
                             NULL);
        path = g_object_new (TRACKER_TYPE_FIELD, 
                             "name", METADATA_FILE_PATH,
                             "embedded", TRUE,
                             "multiple-values", FALSE, 
                             NULL);
        
        size = g_object_new (TRACKER_TYPE_FIELD, 
                             "name", METADATA_FILE_SIZE,
                             "embedded", TRUE,
                             "multiple-values", FALSE, 
                             NULL);

        playcount = g_object_new (TRACKER_TYPE_FIELD, 
                                  "name", TEST_METADATA_PLAYCOUNT,
                                  "embedded", FALSE,
                                  "multiple-values", FALSE, 
                                  NULL);

        dc_subject = g_object_new (TRACKER_TYPE_FIELD, 
                                  "name", TEST_METADATA_SUBJECT,
                                  "embedded", TRUE,
                                  "multiple-values", TRUE, 
                                  NULL);

        user_keywords = g_object_new (TRACKER_TYPE_FIELD, 
                                      "name", TEST_METADATA_USER_KEYWORDS,
                                      "embedded", FALSE,
                                      "multiple-values", TRUE, 
                                      NULL);
        tracker_ontology_add_field (ext);
        tracker_ontology_add_field (name);
        tracker_ontology_add_field (path);
        tracker_ontology_add_field (size);
        tracker_ontology_add_field (playcount);
        tracker_ontology_add_field (dc_subject);
        tracker_ontology_add_field (user_keywords);
}

static void
ontology_shutdown ()
{
        tracker_ontology_shutdown ();
}

static GList *
array_to_glist (gchar **elements)
{
        GList *result = NULL;
        guint i;

        if (!elements) {
                return NULL;
        }

        for (i = 0; i < g_strv_length (elements); i++) {
                result = g_list_append (result, elements[i]);
        }

        return result;
}

static gboolean
action_in_list (GSList *actions, MetadataMergeAction action, const gchar *field_name)
{
        GSList *iter;

        for (iter = actions; iter != NULL; iter = iter->next) {
                MetadataActionItem *item = (MetadataActionItem *)iter->data;
                
                if (item->action == action 
                    && !strcmp (item->metadata_type, field_name)) {
                        return TRUE;
                }
        }

        return FALSE;
}


static TrackerMetadata *
get_metadata_table (const gchar *path, 
                    const gchar *name, 
                    const gchar *ext, 
                    const gchar *playcount,
                    gchar **subject,
                    gchar **user_keywords) {

        TrackerMetadata *table;

        table = tracker_metadata_new ();

        if (ext) {
                tracker_metadata_insert (table, METADATA_FILE_EXT, g_strdup (ext));
        }

        if (path) {
                tracker_metadata_insert (table, METADATA_FILE_PATH, g_strdup (path));
        }

        if (name) {
                tracker_metadata_insert (table, METADATA_FILE_NAME, g_strdup (name));
        }

        if (playcount) {
                tracker_metadata_insert (table, TEST_METADATA_PLAYCOUNT, g_strdup (playcount));
        }

        if (subject) {
                tracker_metadata_insert_multiple_values (table,
                                                         TEST_METADATA_SUBJECT,
                                                         array_to_glist (subject));
        }

        if (user_keywords) {
                tracker_metadata_insert_multiple_values (table,
                                                         TEST_METADATA_USER_KEYWORDS,
                                                         array_to_glist (subject));
        }

        tracker_metadata_insert (table, METADATA_FILE_SIZE, "0");

        return table;
}

static void
test_merge_no_changes ()
{
        TrackerMetadata *one;

        g_assert (TRACKER_IS_FIELD (tracker_ontology_get_field_def (METADATA_FILE_EXT)));
        one = get_metadata_table ("/test", "test-image", "png", NULL, NULL, NULL);
        g_assert (!tracker_metadata_utils_calculate_merge (one, one));
}

static void
test_merge_update_field ()
{

        TrackerMetadata *one, *two;
        GSList *actions;

        one = get_metadata_table ("/test", "test-image", "png", "0", NULL, NULL);
        two = get_metadata_table ("/test", "test-image", "jpeg", NULL, NULL, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        /* Expected one action, type update, property File:Ext 
         *
         */
        g_assert (actions);
        g_assert_cmpint (g_slist_length (actions), ==, 1);

        g_assert (action_in_list (actions, TRACKER_METADATA_ACTION_UPDATE, METADATA_FILE_EXT));

        g_assert_cmpint (((MetadataActionItem*)actions->data)->action, ==, TRACKER_METADATA_ACTION_UPDATE);
        g_assert (tracker_test_helpers_cmpstr_equal ("png", ((MetadataActionItem*)actions->data)->old_value));
        g_assert (tracker_test_helpers_cmpstr_equal ("jpeg", ((MetadataActionItem*)actions->data)->new_value));

        g_slist_foreach (actions, (GFunc)tracker_metadata_utils_action_item_free, NULL);
}

static void
test_merge_delete_field ()
{
        TrackerMetadata *one, *two;
        GSList *actions;

        one = get_metadata_table ("/test", "test-image", "png", "0", NULL, NULL);
        two = get_metadata_table ("/test", "test-image", NULL, NULL, NULL, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        /* Expected one action, type delete, property File:Ext 
         *  (The embedded data remain in the DB!)
         */
        g_assert (actions);
        g_assert_cmpint (g_slist_length (actions), ==, 1);
        g_assert_cmpint (((MetadataActionItem*)actions->data)->action, ==, TRACKER_METADATA_ACTION_DELETE);
        g_assert (tracker_test_helpers_cmpstr_equal ("png", ((MetadataActionItem*)actions->data)->old_value));
        g_assert (  !(((MetadataActionItem*)actions->data)->new_value) );

        g_slist_foreach (actions, (GFunc)tracker_metadata_utils_action_item_free, NULL);
}

static void
test_merge_new_field ()
{
        TrackerMetadata *one, *two;
        GSList *actions;

        one = get_metadata_table ("/test", "test-image", NULL, "0", NULL, NULL);
        two = get_metadata_table ("/test", "test-image", "png", NULL, NULL, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        /* Expected one action, type new , property File:Ext 
         *  (The embedded data remain in the DB!)
         */
        g_assert (actions);
        g_assert_cmpint (g_slist_length (actions), ==, 1);
        g_assert_cmpint (((MetadataActionItem*)actions->data)->action, ==, TRACKER_METADATA_ACTION_NEW);
        g_assert (tracker_test_helpers_cmpstr_equal ("png", ((MetadataActionItem*)actions->data)->new_value));
        g_assert (  !(((MetadataActionItem*)actions->data)->old_value) );

        g_slist_foreach (actions, (GFunc)tracker_metadata_utils_action_item_free, NULL);
}

static void
test_merge_no_changes_mv_embedded ()
{
        TrackerMetadata *one, *two;
        GSList *actions = NULL;
        gchar  *subject[] = {"line1", "line2", NULL};

        one = get_metadata_table ("/test", "test-image", NULL, NULL, subject, NULL);
        two = get_metadata_table ("/test", "test-image", NULL, NULL, subject, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        g_assert (!actions);
}

static void
test_merge_update_mv_embedded ()
{
        TrackerMetadata *one, *two;
        GSList *actions = NULL;
        gchar  *subject_1[] = {"line1", "line2", NULL};
        gchar  *subject_2[] = {"line1", "line3", NULL};

        one = get_metadata_table ("/test", "test-image", NULL, NULL, subject_1, NULL);
        two = get_metadata_table ("/test", "test-image", NULL, NULL, subject_2, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        g_assert (actions);
        g_assert (action_in_list (actions, TRACKER_METADATA_ACTION_UPDATE, TEST_METADATA_SUBJECT));
}

static void
test_merge_delete_mv_embedded ()
{
        TrackerMetadata *one, *two;
        GSList *actions = NULL;
        gchar  *subject[] = {"line1", "line2", NULL};

        one = get_metadata_table ("/test", "test-image", NULL, NULL, subject, NULL);
        two = get_metadata_table ("/test", "test-image", NULL, NULL, NULL, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        g_assert (actions);
        g_assert (action_in_list (actions, TRACKER_METADATA_ACTION_DELETE, TEST_METADATA_SUBJECT));
}

static void
test_merge_new_mv_embedded ()
{
        TrackerMetadata *one, *two;
        GSList *actions = NULL;
        gchar  *subject[] = {"line1", "line2", NULL};

        one = get_metadata_table ("/test", "test-image", NULL, NULL, NULL, NULL);
        two = get_metadata_table ("/test", "test-image", NULL, NULL, subject, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);

        g_assert (actions);
        g_assert (action_in_list (actions, TRACKER_METADATA_ACTION_NEW, TEST_METADATA_SUBJECT));
}


static void
test_merge_update_mv_no_embedded ()
{
        TrackerMetadata *one, *two;
        GSList *actions = NULL;
        gchar  *keywords[] = {"tag1", "tag2", NULL};

        one = get_metadata_table ("/test", "test-image", NULL, NULL, NULL, keywords);
        two = get_metadata_table ("/test", "test-image", NULL, NULL, NULL, NULL);
        actions = tracker_metadata_utils_calculate_merge (one, two);
        
        /* Are not-embedded data, set by user or applications.
         * The metadata must remain in the DB -> No action
         */
        g_assert (!actions);
}

int
main (int argc, char **argv) {

        int result;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);

        ontology_init ();

        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_no_changes",
                         test_merge_no_changes);
        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_update_field",
                         test_merge_update_field);
        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_delete_field",
                         test_merge_delete_field);
        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_new_field",
                         test_merge_new_field);

        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_no_changes_mv_emb",
                         test_merge_no_changes_mv_embedded);
        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_update_field_mv_emb",
                         test_merge_update_mv_embedded);
        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_delete_field_mv_emb",
                         test_merge_delete_mv_embedded);
        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_new_field_mv_emb",
                         test_merge_new_mv_embedded);


        g_test_add_func ("/tracker-indexer/tracker-metadata-utils/merge_update_field_mv_no_emb",
                         test_merge_update_mv_no_embedded);


        result = g_test_run ();
        
        /* End */

        ontology_shutdown ();

        return result;
}
