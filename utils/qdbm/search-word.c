#include <depot.h>
#include <glib.h>

#define USAGE "Usage: print -f qdbm-file -w word\n"

static gchar *filename = NULL;
static gchar *word  = NULL;

static GOptionEntry   entries_qdbm[] = {
	{ "index-file", 'f', 0, 
	  G_OPTION_ARG_STRING, &filename, 
	  "QDBM index file", 
	  NULL },
	{ "word", 'w', 0, 
	  G_OPTION_ARG_STRING, &word,
	  "Print service ID and service type ID of every hit for this word", 
	  NULL },
	{ NULL }
};


typedef struct  {                         
	/* Service ID number of the document */
	guint32 id;              

	/* Amalgamation of service_type and score of the word in the
	 * document's metadata.
	 */
	gint    amalgamated;     
} TrackerIndexerWordDetails;

TrackerIndexerWordDetails *
tracker_indexer_get_word_hits (DEPOT          *index,
			       const gchar    *word,
			       guint          *count)
{
	TrackerIndexerWordDetails *details;
	gint                       tsiz;
	gchar                     *tmp;

        g_return_val_if_fail (word != NULL, NULL);

	details = NULL;

        if (count) {
                *count = 0;
        }

	if ((tmp = dpget (index, word, -1, 0, 1000000, &tsiz)) != NULL) {
		if (tsiz >= (gint) sizeof (TrackerIndexerWordDetails)) {
			details = (TrackerIndexerWordDetails *) tmp;

                        if (count) {
                                *count = tsiz / sizeof (TrackerIndexerWordDetails);
                        }
		}
	}

	return details;
}


guint8
tracker_indexer_word_details_get_service_type (TrackerIndexerWordDetails *details)
{
        g_return_val_if_fail (details != NULL, 0);

	return (details->amalgamated >> 24) & 0xFF;
}


void
show_term_in_index (const gchar* filename, const gchar *word){

    DEPOT *depot;
    int counter = 0, start_time, total_time;
    gint hits, i;
    TrackerIndexerWordDetails *results;

    depot = dpopen (filename, DP_OREADER, -1);

    if ( depot == NULL ) {
	   g_print ("Unable to open file: %s (Could be a lock problem: is tracker running?)\n", filename);
	   g_print ("Using version %s of qdbm\n", dpversion);
	   return;
    }


    results = tracker_indexer_get_word_hits (depot, word, &hits);

    if (hits < 1 ) {
            g_print ("No results for %s\n", word);
            return;
    }
    g_print (" - %s ", word);


    for (i = 0; i < hits; i++) {
            g_print (" (id:%d  t:%d) ", 
                     results[i].id,
                     tracker_indexer_word_details_get_service_type (&results[i]));
    }
    
    g_print ("\n");

    g_print ("Total: %d terms.\n", dprnum (depot));
    dpclose (depot);

}


int
main (gint argc, gchar** argv)
{

        GOptionContext *context;
        GOptionGroup *group;
        GError       *error = NULL;
	context = g_option_context_new ("- QDBM index searcher");

	/* Daemon group */
	group = g_option_group_new ("qdbm", 
				    "QDBM searcher Options",
                                    "Show searcher options", 
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

        if (!filename || !word) {
                g_printerr (USAGE);
                return EXIT_FAILURE;
        }

        show_term_in_index (filename, word);

        g_print ("ok\n");
        return 0;
}
