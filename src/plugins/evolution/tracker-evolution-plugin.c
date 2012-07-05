/*
 * Copyright (C) 2012, Martyn Russell <martyn@lanedo.com>
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
 *
 * Authors:
 *   Martyn Russell <martyn@lanedo.com>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include <camel/camel.h>

#if 0
/* FIXME: Shouldn't include this but we need it for EMailSession */
#include <mail/mail-ops.h>
#else
/* According to mbarnes this should be libemail-engine/e-mail-session.h, guessing this is a 3.2 vs 3.5 issue. */
#include <mail/e-mail-session.h>
#endif

#include "tracker-evolution-plugin.h"


/*
 * Tracker Miner Code
 */

#define TRACKER_SERVICE                 "org.freedesktop.Tracker1"
#define DATASOURCE_URN                  "urn:nepomuk:datasource:1cb1eb90-1241-11de-8c30-0800200c9a66"
#define TRACKER_EVOLUTION_GRAPH_URN     "urn:uuid:9a96d750-5182-11e0-b8af-0800200c9a66"

#define UIDS_CHUNK_SIZE 200

#define TRACKER_MINER_EVOLUTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_MINER_EVOLUTION, TrackerMinerEvolutionPrivate))

typedef struct {
	gboolean resuming;
	gboolean paused;
	gint total_popped;
	gint of_total;
	gint watch_name_id;
	GCancellable *sparql_cancel;
	GTimer *timer_since_stopped;
} TrackerMinerEvolutionPrivate;

/* Prototype declarations */
static void     miner_evolution_initable_iface_init (GInitableIface         *iface);
static gboolean miner_evolution_initable_init       (GInitable              *initable,
                                                     GCancellable           *cancellable,
                                                     GError                **error);
static void     miner_started                       (TrackerMiner           *miner);
static void     miner_stopped                       (TrackerMiner           *miner);
static void     miner_paused                        (TrackerMiner           *miner);
static void     miner_resumed                       (TrackerMiner           *miner);
static void     miner_start_watching                (TrackerMiner           *miner);
static void     miner_stop_watching                 (TrackerMiner           *miner);

static GInitableIface *miner_evolution_initable_parent_iface;
static TrackerMinerEvolution *manager = NULL;

G_DEFINE_TYPE_WITH_CODE (TrackerMinerEvolution, tracker_miner_evolution, TRACKER_TYPE_MINER,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                miner_evolution_initable_iface_init));

static void
miner_evolution_initable_iface_init (GInitableIface *iface)
{
	miner_evolution_initable_parent_iface = g_type_interface_peek_parent (iface);
	iface->init = miner_evolution_initable_init;
}

static gboolean
miner_evolution_initable_init (GInitable     *initable,
                               GCancellable  *cancellable,
                               GError       **error)
{
	GError *inner_error = NULL;

	/* Chain up parent's initable callback before calling child's one */
	if (!miner_evolution_initable_parent_iface->init (initable, cancellable, &inner_error)) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	return TRUE;
}

static void
tracker_miner_evolution_finalize (GObject *plugin)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (plugin);

	if (priv->sparql_cancel) {
		/* We reuse the cancellable */
		g_cancellable_cancel (priv->sparql_cancel);
	}

	if (priv->timer_since_stopped) {
		g_timer_destroy (priv->timer_since_stopped);
		priv->timer_since_stopped = NULL;
	}

	if (priv->sparql_cancel) {
		g_cancellable_cancel (priv->sparql_cancel);
		g_object_unref (priv->sparql_cancel);
	}

	G_OBJECT_CLASS (tracker_miner_evolution_parent_class)->finalize (plugin);
}

static void
tracker_miner_evolution_class_init (TrackerMinerEvolutionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TrackerMinerClass *miner_class = TRACKER_MINER_CLASS (klass);

	g_setenv ("TRACKER_SPARQL_BACKEND", "bus", TRUE);

	miner_class->started = miner_started;
	miner_class->stopped = miner_stopped;
	miner_class->paused  = miner_paused;
	miner_class->resumed = miner_resumed;

	object_class->finalize = tracker_miner_evolution_finalize;

	g_type_class_add_private (object_class, sizeof (TrackerMinerEvolutionPrivate));
}

static void
tracker_miner_evolution_init (TrackerMinerEvolution *plugin)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (plugin);

	priv->sparql_cancel = g_cancellable_new ();

	priv->resuming = FALSE;
	priv->paused = FALSE;
}

static void
on_tracker_store_appeared (GDBusConnection *d_connection,
                           const gchar     *name,
                           const gchar     *name_owner,
                           gpointer         user_data)

{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (user_data);

	if (priv->timer_since_stopped && g_timer_elapsed (priv->timer_since_stopped, NULL) > 5) {
		g_timer_destroy (priv->timer_since_stopped);
		priv->timer_since_stopped = NULL;
	}

	/* register_client (user_data); */
}

static void
miner_start_watching (TrackerMiner *miner)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (miner);

	priv->watch_name_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
	                                        TRACKER_SERVICE,
	                                        G_BUS_NAME_WATCHER_FLAGS_NONE,
	                                        on_tracker_store_appeared,
	                                        NULL,
	                                        miner,
	                                        NULL);
}

static void
miner_stop_watching (TrackerMiner *miner)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (miner);

	if (priv->watch_name_id != 0)
		g_bus_unwatch_name (priv->watch_name_id);

	if (!priv->timer_since_stopped) {
		priv->timer_since_stopped = g_timer_new ();
	}

	if (priv->sparql_cancel) {
		/* We reuse the cancellable */
		g_cancellable_cancel (priv->sparql_cancel);
	}
}

static void
miner_started (TrackerMiner *miner)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (miner);

	if (priv->timer_since_stopped && g_timer_elapsed (priv->timer_since_stopped, NULL) > 5) {
		g_timer_destroy (priv->timer_since_stopped);
		priv->timer_since_stopped = NULL;
	}

	miner_start_watching (miner);

	g_debug ("Tracker plugin setting progress to '0.0' and status to 'Initializing'");
	g_object_set (miner,  "progress", 0.0, "status", "Initializing", NULL);
}

static void
miner_stopped (TrackerMiner *miner)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (miner);

	miner_paused (miner);
	priv->paused = FALSE;
}

static void
miner_paused (TrackerMiner *miner)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (miner);

	/* We don't really pause, we just completely stop */

	miner_stop_watching (miner);

	priv->paused = TRUE;
}

static void
miner_resumed (TrackerMiner *miner)
{
	TrackerMinerEvolutionPrivate *priv = TRACKER_MINER_EVOLUTION_GET_PRIVATE (miner);

	/* We don't really resume, we just completely restart */

	priv->resuming = FALSE;
	priv->paused = FALSE;
	priv->total_popped = 0;
	priv->of_total = 0;

	g_debug ("Tracker plugin setting progress to '0.0' and status to 'Processing'");
	g_object_set (miner,  "progress", 0.0, "status", _("Processing…"), NULL);

	miner_start_watching (miner);
}











#if 0

/*
 * Extensible
 */
#include <libebackend/libebackend.h>

G_DEFINE_TYPE_WITH_CODE (ECustomWidget, e_custom_widget, GTK_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (E_TYPE_EXTENSIBLE, NULL))

static void
e_custom_widget_constructed (GObject *object)
{
        /* Initialization code goes here... */

        e_extensible_load_extensions (E_EXTENSIBLE (object));
}

#endif















/*
 * Extension
 */
#include <shell/e-shell.h>

/* Commented out for now because this doesn't exist with 3.2. */
#if 0
#include <libebackend/libebackend.h>
#endif

typedef struct _ETracker ETracker;
typedef struct _ETrackerClass ETrackerClass;

struct _ETracker {
        EExtension parent;
};

struct _ETrackerClass {
        EExtensionClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_tracker_get_type (void);

G_DEFINE_DYNAMIC_TYPE (ETracker, e_tracker, E_TYPE_EXTENSION)

static void
e_tracker_constructed (GObject *object)
{
        EExtensible *extensible;
	GError *error = NULL;

        /* This retrieves the EShell instance we're extending. */
        extensible = e_extension_get_extensible (E_EXTENSION (object));

        /* This prints "Hello world from EShell!" */
        g_debug ("ETracker derives from %s!", G_OBJECT_TYPE_NAME (extensible));

	g_debug ("Creating new TrackerMinerEvolution object");

	manager = g_initable_new (TRACKER_TYPE_MINER_EVOLUTION,
	                          NULL,
	                          &error,
	                          "name", "Emails",
	                          NULL);

	if (error) {
		g_critical ("Could not start Tracker plugin, %s", error->message);
		g_error_free (error);
		return;
	}

	tracker_miner_start (TRACKER_MINER (manager));
}

static void
e_tracker_finalize (GObject *object)
{
	g_debug ("Finalizing Tracker plugin");

        /* Chain up to parent's finalize() method. */
        G_OBJECT_CLASS (e_tracker_parent_class)->finalize (object);
}

static void
e_tracker_class_init (ETrackerClass *class)
{
        GObjectClass *object_class;
        EExtensionClass *extension_class;

        object_class = G_OBJECT_CLASS (class);
        object_class->constructed = e_tracker_constructed;
        object_class->finalize = e_tracker_finalize;

        /* Specify the GType of the class we're extending.
         * The class must implement the EExtensible interface. */
        extension_class = E_EXTENSION_CLASS (class);
        extension_class->extensible_type = E_TYPE_MAIL_SESSION;
}

static void
e_tracker_class_finalize (ETrackerClass *class)
{
        /* This function is usually left empty. */
}

static void
e_tracker_init (ETracker *extension)
{
	g_debug ("Initializing Tracker plugin");
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	g_debug ("Loading Tracker plugin");
        e_tracker_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	g_debug ("Unloading Tracker plugin");
}
