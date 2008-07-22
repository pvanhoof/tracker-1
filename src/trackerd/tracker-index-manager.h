#ifndef __TRACKERD_INDEX_MANAGER_H__
#define __TRACKERD_INDEX_MANAGER_H__

#include <glib.h>

G_BEGIN_DECLS

#include "tracker-indexer.h"

typedef enum {
	TRACKER_INDEXER_TYPE_FILES,
	TRACKER_INDEXER_TYPE_EMAILS,
	TRACKER_INDEXER_TYPE_FILES_UPDATE
} TrackerIndexerType;


gboolean        tracker_index_manager_init                  (const gchar        *data_dir,
                                                             gint                min_bucket,
                                                             gint                max_bucket);
gchar *         tracker_index_manager_get_filename          (TrackerIndexerType index);
TrackerIndexer *tracker_index_manager_get_index             (TrackerIndexerType  index);
gboolean        tracker_index_manager_are_indexes_too_big   (void);
gboolean        tracker_index_manager_has_tmp_merge_files   (TrackerIndexerType  type);
void            tracker_index_manager_shutdown              (void);


G_END_DECLS

#endif
