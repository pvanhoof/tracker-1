#ifndef __TRACKER_EMAIL_PLUGIN_H__
#define __TRACKER_EMAIL_PLUGIN_H__

#include <libtracker-common/tracker-config.h>

#include <libtracker-db/tracker-db-interface.h>
#include <libtracker-db/tracker-db-file-info.h>

G_BEGIN_DECLS

gboolean     tracker_email_plugin_init                (void);
gboolean     tracker_email_plugin_finalize            (void);
const gchar *tracker_email_plugin_get_name            (void);
void         tracker_email_plugin_watch_emails        (TrackerDBInterface *iface);
gboolean     tracker_email_plugin_index_file          (TrackerDBInterface *iface,
                                                       TrackerDBFileInfo  *info);
gboolean     tracker_email_plugin_file_is_interesting (TrackerDBFileInfo  *info);

G_END_DECLS

#endif
