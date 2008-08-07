#include <depot.h>
#include <glib.h>
#include <libtracker-common/tracker-index-item.h>

#define USAGE "Usage: print -f qdbm-file\n"

static gchar *filename = NULL;
static gboolean print_services = FALSE;

static GOptionEntry   entries_qdbm[] = {
	{ "index-file", 'f', 0, 
	  G_OPTION_ARG_STRING, &filename, 
	  "QDBM index file", 
	  NULL },
	{ "print-services", 's', 0, 
	  G_OPTION_ARG_NONE, &print_services, 
	  "Print service ID and service type ID for each word", 
	  NULL },
	{ NULL }
};


TrackerIndexItem *
tracker_indexer_get_word_hits (DEPOT          *index,
			       const gchar    *word,
			       guint          *count)
{
	TrackerIndexItem *details;
	gint              tsiz;
	gchar            *tmp;

        g_return_val_if_fail (word != NULL, NULL);

	details = NULL;

        if (count) {
                *count = 0;
        }

	if ((tmp = dpget (index, word, -1, 0, 100, &tsiz)) != NULL) {
		if (tsiz >= (gint) sizeof (TrackerIndexItem)) {
			details = (TrackerIndexItem *) tmp;

                        if (count) {
                                *count = tsiz / sizeof (TrackerIndexItem);
                        }
		}
	}

	return details;
}

void
load_terms_from_index (gchar* filename)
{
    DEPOT *depot;
    char *key;
    guint hits, i;
    TrackerIndexItem *results;

    depot = dpopen (filename, DP_OREADER | DP_ONOLCK, -1);

    if ( depot == NULL ) {
	   g_print ("Unable to open file: %s (Could be a lock problem: is tracker running?)\n", filename);
	   g_print ("Using version %s of qdbm\n", dpversion);
	   return;
    }


    dpiterinit (depot);
    
    key = dpiternext (depot, NULL);

    while ( key != NULL ) {

            g_print (" - %s ", key);

            if (print_services) {
                    results = tracker_indexer_get_word_hits (depot, key, &hits);
                    for (i = 0; i < hits; i++) {
                            g_print (" (id:%d  t:%d s:%d) ", 
                                     tracker_index_item_get_id (&results[i]),
                                     tracker_index_item_get_service_type (&results[i]),
                                     tracker_index_item_get_score (&results[i]));
                    }
            }

            g_print ("\n");
            g_free (key);
            key = dpiternext (depot, NULL);
    }

    g_print ("Total: %d terms.\n", dprnum (depot));
    dpclose (depot);

}


int
main (gint argc, gchar** argv)
{

        GOptionContext *context;
        GOptionGroup *group;
        GError       *error = NULL;
	context = g_option_context_new ("- QDBM index printer");

	/* Daemon group */
	group = g_option_group_new ("qdbm", 
				    "QDBM printer Options",
                                    "Show daemon options", 
				    NULL, 
				    NULL);
	g_option_group_add_entries (group, entries_qdbm);
	g_option_context_add_group (context, group);

	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error) {
		g_printerr ("Invalid arguments, %s\n", error->message);
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

        if (!filename) {
                g_printerr (USAGE);
                return EXIT_FAILURE;
        }

        load_terms_from_index (filename);

        g_print ("ok\n");
        return 0;
}
