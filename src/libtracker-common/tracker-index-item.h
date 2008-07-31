#ifndef __TRACKER_INDEX_ITEM_H__
#define __TRACKER_INDEX_ITEM_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct  {                         
	/* Service ID number of the document */
	guint32 id;              

	/* Amalgamation of service_type and score of the word in the
	 * document's metadata.
	 */
	gint    amalgamated;     
} TrackerIndexItem;

guint32 tracker_index_item_calc_amalgamated (gint              service_type,
                                             gint              score);
guint8  tracker_index_item_get_service_type (TrackerIndexItem *details);
gint16  tracker_index_item_get_score        (TrackerIndexItem *details);
guint32 tracker_index_item_get_id           (TrackerIndexItem *details);

G_END_DECLS

#endif
