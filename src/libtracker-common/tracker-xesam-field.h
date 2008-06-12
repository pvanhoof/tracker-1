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

#ifndef __TRACKER_XESAM_FIELD_H__
#define __TRACKER_XESAM_FIELD_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	TRACKER_XESAM_FIELD_TYPE_STRING,
	TRACKER_XESAM_FIELD_TYPE_FLOAT,
	TRACKER_XESAM_FIELD_TYPE_INTEGER,
	TRACKER_XESAM_FIELD_TYPE_BOOLEAN,
	TRACKER_XESAM_FIELD_TYPE_DATE,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_STRINGS,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_URIS,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_URLS,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_BOOLEANS,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_DATETIMES,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_FLOATS,
	TRACKER_XESAM_FIELD_TYPE_LIST_OF_INTEGERS
} TrackerXesamFieldType;

GType tracker_xesam_field_type_get_type (void) G_GNUC_CONST;

#define TRACKER_TYPE_XESAM_FIELD         (tracker_xesam_field_get_type ())
#define TRACKER_TYPE_XESAM_FIELD_TYPE    (tracker_xesam_field_type_get_type ())
#define TRACKER_XESAM_FIELD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_XESAM_FIELD, TrackerXesamField))
#define TRACKER_XESAM_FIELD_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), TRACKER_TYPE_XESAM_FIELD, TrackerXesamFieldClass))
#define TRACKER_IS_XESAM_FIELD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_XESAM_FIELD))
#define TRACKER_IS_XESAM_FIELD_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TRACKER_TYPE_XESAM_FIELD))
#define TRACKER_XESAM_FIELD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_XESAM_FIELD, TrackerXesamFieldClass))

typedef struct _TrackerXesamField      TrackerXesamField;
typedef struct _TrackerXesamFieldClass TrackerXesamFieldClass;

struct _TrackerXesamField {
	GObject      parent;
};

struct _TrackerXesamFieldClass {
	GObjectClass parent_class;
};

GType                 tracker_xesam_field_get_type       (void) G_GNUC_CONST;

TrackerXesamField *   tracker_xesam_field_new                 (void);

const gchar *    tracker_xesam_field_get_id              (TrackerXesamField     *field);
const gchar *    tracker_xesam_field_get_name            (TrackerXesamField     *field);
TrackerXesamFieldType tracker_xesam_field_get_data_type       (TrackerXesamField     *field);
const gchar *    tracker_xesam_field_get_field_name      (TrackerXesamField     *field);
gint             tracker_xesam_field_get_weight          (TrackerXesamField     *service);
gboolean         tracker_xesam_field_get_embedded        (TrackerXesamField     *field);
gboolean         tracker_xesam_field_get_multiple_values (TrackerXesamField     *field);
gboolean         tracker_xesam_field_get_delimited       (TrackerXesamField     *field);
gboolean         tracker_xesam_field_get_filtered        (TrackerXesamField     *field);
gboolean         tracker_xesam_field_get_store_metadata  (TrackerXesamField     *field);
const GSList *   tracker_xesam_field_get_child_ids       (TrackerXesamField     *field);

void             tracker_xesam_field_set_id              (TrackerXesamField     *field,
							  const gchar      *value);
void             tracker_xesam_field_set_name            (TrackerXesamField     *field,
							  const gchar      *value);
void             tracker_xesam_field_set_data_type       (TrackerXesamField     *field,
							  TrackerXesamFieldType  value);
void             tracker_xesam_field_set_field_name      (TrackerXesamField     *field,
							  const gchar      *value);
void             tracker_xesam_field_set_weight          (TrackerXesamField     *field,
							  gint              value);
void             tracker_xesam_field_set_embedded        (TrackerXesamField     *field,
							  gboolean          value);
void             tracker_xesam_field_set_multiple_values (TrackerXesamField     *field,
							  gboolean          value);
void             tracker_xesam_field_set_delimited       (TrackerXesamField     *field,
							  gboolean          value);
void             tracker_xesam_field_set_filtered        (TrackerXesamField     *field,
							  gboolean          value);
void             tracker_xesam_field_set_store_metadata  (TrackerXesamField     *field,
							  gboolean          value);
void             tracker_xesam_field_set_child_ids       (TrackerXesamField     *field,
							  const GSList     *value);
void             tracker_xesam_field_append_child_id     (TrackerXesamField     *field,
							  const gchar      *id);

G_END_DECLS

#endif /* __TRACKER_XESAM_FIELD_H__ */

