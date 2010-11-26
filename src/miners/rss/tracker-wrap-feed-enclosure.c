/*
 * Copyright (C) 2010, Roberto Guido <madbob@users.barberaware.org>
 *                     Michele Tameni <michele@amdplanet.it>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <unistd.h>
#include <errno.h>

#include <dbus/dbus-glib.h>

#include "tracker-wrap-feed-enclosure.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), WRAP_FEED_ENCLOSURE_TYPE, WrapFeedEnclosurePrivate))

typedef struct _WrapFeedEnclosurePrivate	WrapFeedEnclosurePrivate;

struct _WrapFeedEnclosurePrivate {
	FeedEnclosure *enclosure;
	WrapFeedChannel *channel;

	gchar *save_path;

	gchar *data;
	gsize data_len;
};

G_DEFINE_TYPE (WrapFeedEnclosure, wrap_feed_enclosure, G_TYPE_OBJECT);

static void
wrap_feed_enclosure_finalize (GObject *obj)
{
	WrapFeedEnclosure *enc;
	WrapFeedEnclosurePrivate *priv;

	enc = WRAP_FEED_ENCLOSURE (obj);
	priv = GET_PRIV (enc);

	g_object_unref (priv->enclosure);
	g_object_unref (priv->channel);

	if (priv->save_path != NULL)
		g_free (priv->save_path);
	if (priv->data != NULL)
		g_free (priv->data);
}

static void
wrap_feed_enclosure_class_init (WrapFeedEnclosureClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = wrap_feed_enclosure_finalize;

	g_type_class_add_private (klass, sizeof (WrapFeedEnclosurePrivate));
}

static void
wrap_feed_enclosure_init (WrapFeedEnclosure *node)
{
}

WrapFeedEnclosure*
wrap_feed_enclosure_new (FeedEnclosure   *enclosure,
                         WrapFeedChannel *channel)
{
	WrapFeedEnclosure *ret;
	WrapFeedEnclosurePrivate *priv;

	ret = g_object_new (WRAP_FEED_ENCLOSURE_TYPE, NULL);

	priv = GET_PRIV (ret);
	priv->enclosure = enclosure;
	g_object_ref (priv->enclosure);
	priv->channel = channel;
	g_object_ref (priv->channel);

	return ret;
}

static const gchar*
saving_path (WrapFeedEnclosure *enclosure)
{
	int modifier;
	gchar *name;
	gchar *new_name;
	gchar *path;
	const gchar *folder;
	WrapFeedEnclosurePrivate *priv;

	priv = GET_PRIV (enclosure);

	if (priv->save_path == NULL || strlen (priv->save_path) == 0) {
		if (priv->save_path != NULL)
			g_free (priv->save_path);
		priv->save_path = NULL;

		folder = wrap_feed_channel_get_enclosures_saving_path (priv->channel);

		if (folder == NULL) {
			g_warning ("No saving folder set for enclosures.");
		}
		else {
			name = g_path_get_basename (feed_enclosure_get_url (priv->enclosure));
			path = g_build_filename (folder, name, NULL);

			/* This is to avoid overlapping existing files with the same name */

			modifier = 0;

			while (access (path, F_OK) == 0) {
				modifier++;
				new_name = g_strdup_printf ("%d_%s", modifier, name);

				g_free (path);
				g_free (name);

				path = g_build_filename (folder, new_name, NULL);
				name = new_name;
			}

			g_free (name);
			priv->save_path = path;
		}
	}

	return (const gchar*) priv->save_path;
}

static gchar*
get_local_node_query (WrapFeedEnclosure *enclosure)
{
	gchar *query;
	const gchar *path;
	WrapFeedEnclosurePrivate *priv;

	path = saving_path (enclosure);
	if (path == NULL)
		return NULL;

	priv = GET_PRIV (enclosure);

	query = g_strdup_printf ("INSERT {_:enclosure a nfo:FileDataObject; nie:url \"%s\" . ?i mfo:localLink _:enclosure} "
				 "WHERE {?r nie:url \"%s\" . ?i mfo:remoteLink ?r}",
				 path, feed_enclosure_get_url (priv->enclosure));

	return query;
}

static gboolean
notify_miner_fs (TrackerMiner *miner, const gchar *path)
{
	gchar **params;

	params = g_new0 (gchar*, 2);
	params [0] = (gchar*) path;

	tracker_miner_ignore_next_update (miner, params);

	g_free (params);
	return TRUE;
}

static void
verify_enclosure_unmandatory (GObject      *source,
                              GAsyncResult *result,
                              gpointer      user_data)
{
	GError *error;
	WrapFeedEnclosure *enclosure;

	enclosure = user_data;

	error = NULL;
	tracker_sparql_connection_update_finish (TRACKER_SPARQL_CONNECTION (source), result, &error);

	if (error != NULL) {
		g_critical ("Could not remove flag about mandatory enclosure, %s", error->message);
		g_error_free (error);
	}

	g_object_unref (enclosure);
}

static void
unmandatory_enclosure (WrapFeedEnclosure *enclosure)
{
	gchar *query;
	WrapFeedEnclosurePrivate *priv;

	priv = GET_PRIV (enclosure);

	query = g_strdup_printf ("DELETE {?e mfo:optional ?o} "
				 "WHERE {?r nie:url \"%s\" . ?e mfo:remoteLink ?r . ?e mfo:optional ?o}",
				 feed_enclosure_get_url (priv->enclosure));

	tracker_sparql_connection_update_async (tracker_miner_get_connection (TRACKER_MINER (wrap_feed_channel_get_referring_miner (priv->channel))),
                                                query,
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                verify_enclosure_unmandatory,
                                                NULL);

	g_free (query);
}

static void
enclosure_node_set (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
	const gchar *path;
	FILE *fd;
	GError *error;
	WrapFeedEnclosurePrivate *priv;
	WrapFeedEnclosure *enclosure;

	error = NULL;
	enclosure = user_data;

	tracker_sparql_connection_update_finish (TRACKER_SPARQL_CONNECTION (source), result, &error);
	if (error != NULL) {
		g_critical ("Could not save enclosure informations, %s", error->message);
		g_error_free (error);
		g_object_unref (enclosure);
	}
	else {
		priv = GET_PRIV (enclosure);

		path = saving_path (enclosure);
		if (path == NULL)
			return;

		if (notify_miner_fs (TRACKER_MINER (source), path) == FALSE)
			return;

		fd = fopen (path, "w+");
		if (fd == NULL) {
			g_warning ("Unable to open saving location (%s) for enclosure.", path);
		}
		else {
			if (fwrite (priv->data, priv->data_len, 1, fd) != 1)
				g_warning ("Error while writing enclosure contents on the filesystem: %s.", strerror (errno));
			fclose (fd);
		}

		unmandatory_enclosure (enclosure);
	}
}

void
wrap_feed_enclosure_save_data (WrapFeedEnclosure *enclosure,
                               gchar             *data,
                               gsize              len)
{
	gchar *query;
	WrapFeedEnclosurePrivate *priv;

	priv = GET_PRIV (enclosure);
	priv->data = data;
	priv->data_len = len;

	g_object_ref (enclosure);

	query = get_local_node_query (enclosure);
	if (query == NULL)
		return;

	tracker_sparql_connection_update_async (tracker_miner_get_connection (TRACKER_MINER (wrap_feed_channel_get_referring_miner (priv->channel))),
                                                query,
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                enclosure_node_set,
                                                enclosure);

	g_free (query);
}
