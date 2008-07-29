/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia

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

#include <glib.h>
#include <libtracker-common/tracker-ontology.h>
#include "tracker-metadata.h"

struct TrackerMetadata {
        GHashTable *table;
};

TrackerMetadata *
tracker_metadata_new (void)
{
        TrackerMetadata *metadata;

        metadata = g_slice_new (TrackerMetadata);
        metadata->table = g_hash_table_new_full (g_direct_hash,
                                                 g_direct_equal,
                                                 (GDestroyNotify) g_object_unref,
                                                 NULL);
        return metadata;
}

static gboolean
remove_metadata_foreach (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
        TrackerField *field;

        field = (TrackerField *) key;

        if (tracker_field_get_multiple_values (field)) {
                GList *list = (GList *) value;

                switch (tracker_field_get_data_type (field)) {
                case TRACKER_FIELD_TYPE_KEYWORD:
                case TRACKER_FIELD_TYPE_INDEX:
                case TRACKER_FIELD_TYPE_FULLTEXT:
                case TRACKER_FIELD_TYPE_STRING:
                        g_list_foreach (list, (GFunc) g_free, NULL);
                        /* pass through */
                case TRACKER_FIELD_TYPE_INTEGER:
                case TRACKER_FIELD_TYPE_DOUBLE:
                case TRACKER_FIELD_TYPE_DATE:
                        g_list_free (list);
                        break;
                case TRACKER_FIELD_TYPE_BLOB:
                case TRACKER_FIELD_TYPE_STRUCT:
                case TRACKER_FIELD_TYPE_LINK:
                default:
                        /* Unhandled for now */
                        break;
                }
        } else {
                switch (tracker_field_get_data_type (field)) {
                case TRACKER_FIELD_TYPE_KEYWORD:
                case TRACKER_FIELD_TYPE_INDEX:
                case TRACKER_FIELD_TYPE_FULLTEXT:
                case TRACKER_FIELD_TYPE_STRING:
                        g_free (value);
                        break;
                case TRACKER_FIELD_TYPE_INTEGER:
                case TRACKER_FIELD_TYPE_DOUBLE:
                case TRACKER_FIELD_TYPE_DATE:
                        /* Nothing to free here */
                        break;
                case TRACKER_FIELD_TYPE_BLOB:
                case TRACKER_FIELD_TYPE_STRUCT:
                case TRACKER_FIELD_TYPE_LINK:
                default:
                        /* Unhandled for now */
                        break;
                }
        }

        return TRUE;
}

void
tracker_metadata_free (TrackerMetadata *metadata)
{
        g_hash_table_foreach_remove (metadata->table,
                                     remove_metadata_foreach,
                                     NULL);
        g_hash_table_destroy (metadata->table);
        g_slice_free (TrackerMetadata, metadata);
}

void
tracker_metadata_insert (TrackerMetadata *metadata,
                         const gchar     *field_name,
                         gpointer         value)
{
        TrackerField *field;

        field = tracker_ontology_get_field_def (field_name);

        g_return_if_fail (tracker_field_get_multiple_values (field) == FALSE);

        g_hash_table_insert (metadata->table,
                             g_object_ref (field),
                             value);
}

void
tracker_metadata_insert_multiple_values (TrackerMetadata *metadata,
                                         const gchar     *field_name,
                                         GList           *list)
{
        TrackerField *field;

        field = tracker_ontology_get_field_def (field_name);

        g_return_if_fail (tracker_field_get_multiple_values (field) == TRUE);

        g_hash_table_insert (metadata->table,
                             g_object_ref (field),
                             list);
}

gpointer
tracker_metadata_lookup (TrackerMetadata *metadata,
                         const gchar     *field_name)
{
        TrackerField *field;

        field = tracker_ontology_get_field_def (field_name);

        g_return_val_if_fail (tracker_field_get_multiple_values (field) == FALSE, NULL);

        return g_hash_table_lookup (metadata->table, field);
}

G_CONST_RETURN GList *
tracker_metadata_lookup_multiple_values (TrackerMetadata *metadata,
                                         const gchar     *field_name)
{
        TrackerField *field;

        field = tracker_ontology_get_field_def (field_name);

        g_return_val_if_fail (tracker_field_get_multiple_values (field) == TRUE, NULL);

        return g_hash_table_lookup (metadata->table, field);
}

void
tracker_metadata_foreach (TrackerMetadata        *metadata,
                          TrackerMetadataForeach  func,
                          gpointer                user_data)
{
        g_hash_table_foreach (metadata->table,
                              (GHFunc) func,
                              user_data);
}
