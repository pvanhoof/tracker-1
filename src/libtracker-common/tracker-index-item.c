#include "tracker-index-item.h"

guint32 
tracker_index_item_calc_amalgamated (gint service_type, gint score)
{
	unsigned char a[4];
	gint16        score16;
	guint8        service_type_8;

	if (score > 30000) {
		score16 = 30000;
	} else {
		score16 = (gint16) score;
	}

	service_type_8 = (guint8) service_type;

	/* Amalgamate and combine score and service_type into a single
         * 32-bit int for compact storage.
         */
	a[0] = service_type_8;
	a[1] = (score16 >> 8) & 0xFF;
	a[2] = score16 & 0xFF;
	a[3] = 0;

	return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

guint8  
tracker_index_item_get_service_type (TrackerIndexItem *details)
{
        g_return_val_if_fail (details != NULL, 0);

	return (details->amalgamated >> 24) & 0xFF;
}


gint16  
tracker_index_item_get_score (TrackerIndexItem *details)
{
	unsigned char a[2];

        g_return_val_if_fail (details != NULL, 0);

	a[0] = (details->amalgamated >> 16) & 0xFF;
	a[1] = (details->amalgamated >> 8) & 0xFF;

	return (gint16) (a[0] << 8) | (a[1]);	
}
