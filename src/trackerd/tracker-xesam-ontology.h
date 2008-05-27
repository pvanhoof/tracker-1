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

#ifndef __TRACKERD_XESAM_ONTOLOGY_H__
#define __TRACKERD_XESAM_ONTOLOGY_H__

#include <glib-object.h>

#include <libtracker-common/tracker-field.h>
#include <libtracker-common/tracker-service.h>

G_BEGIN_DECLS

void            tracker_xesam_ontology_init                         (void);
void            tracker_xesam_ontology_shutdown                     (void);
void            tracker_xesam_ontology_add_service_type             (TrackerService *service,
								     GSList         *mimes,
								     GSList         *mime_prefixes);
TrackerService *tracker_xesam_ontology_get_service_type_by_name     (const gchar    *service_str);
gchar *         tracker_xesam_ontology_get_service_type_by_id       (gint            id);
gint            tracker_xesam_ontology_get_id_for_service_type      (const gchar    *service_str);
gchar *         tracker_xesam_ontology_get_parent_service           (const gchar    *service_str);
gchar *         tracker_xesam_ontology_get_parent_service_by_id     (gint            id);
gint            tracker_xesam_ontology_get_parent_id_for_service_id (gint            id);
TrackerDBType   tracker_xesam_ontology_get_db_for_service_type      (const gchar    *service_str);
gboolean        tracker_xesam_ontology_is_valid_service_type        (const gchar    *service_str);
gboolean        tracker_xesam_ontology_show_service_directories     (const gchar    *service_str);
gboolean        tracker_xesam_ontology_show_service_files           (const gchar    *service_str);

/* Service directories */
GSList *        tracker_xesam_ontology_get_dirs_for_service_type    (const gchar    *service);
void            tracker_xesam_ontology_add_dir_to_service_type      (const gchar    *service,
								     const gchar    *path);
void            tracker_xesam_ontology_remove_dir_to_service_type   (const gchar    *service,
								     const gchar    *path);
gchar *         tracker_xesam_ontology_get_service_type_for_dir     (const gchar    *path);

/* Field handling */
void            tracker_xesam_ontology_add_field                    (TrackerField   *field);
gchar *         tracker_xesam_ontology_get_field_column_in_services (TrackerField   *field,
								     const gchar    *service_type);
gchar *         tracker_xesam_ontology_get_display_field            (TrackerField   *field);
gboolean        tracker_xesam_ontology_field_is_child_of            (const gchar    *child,
								     const gchar    *parent);
TrackerField *  tracker_xesam_ontology_get_field_def                (const gchar    *name);
const gchar *   tracker_xesam_ontology_get_field_id                 (const gchar    *name);

G_END_DECLS

#endif /* __TRACKERD_XESAM_ONTOLOGY_H__ */

