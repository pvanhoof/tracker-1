/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-keyfile-object.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-type-utils.h>

#include "tracker-config.h"

#define TRACKER_CONFIG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_CONFIG, TrackerConfigPrivate))

/* GKeyFile defines */
#define GROUP_GENERAL     "General"

/* Default values */
#define DEFAULT_VERBOSITY 0
#define DEFAULT_MAX_BYTES 1048576   /* 1Mbyte */
#define ABSOLUTE_MAX_BYTES 10485760 /* 10 Mbytes (GB#616845) */

typedef struct {
	/* General */
	gint verbosity;
	gint max_bytes;
	GSList   *ignore_images_under;
} TrackerConfigPrivate;

typedef struct {
	GType  type;
	const gchar *property;
	const gchar *group;
	const gchar *key;
} ObjectToKeyFile;

static void     config_set_property         (GObject       *object,
                                             guint          param_id,
                                             const GValue  *value,
                                             GParamSpec    *pspec);
static void     config_get_property         (GObject       *object,
                                             guint          param_id,
                                             GValue        *value,
                                             GParamSpec    *pspec);
static void     config_finalize             (GObject       *object);
static void     config_constructed          (GObject       *object);
static void     config_load                 (TrackerConfig *config);
static gboolean config_save                 (TrackerConfig *config);
static void     config_create_with_defaults (TrackerConfig *config,
                                             GKeyFile      *key_file,
                                             gboolean       overwrite);

enum {
	PROP_0,

	/* General */
	PROP_VERBOSITY,
	PROP_MAX_BYTES,
	PROP_IGNORE_IMAGES_UNDER,
};

static ObjectToKeyFile conversions[] = {
	{ G_TYPE_INT,     "verbosity",           GROUP_GENERAL,  "Verbosity"         },
	{ G_TYPE_INT,     "max-bytes",           GROUP_GENERAL,  "MaxBytes"          },
	{ G_TYPE_POINTER, "ignore-images-under", GROUP_GENERAL,  "IgnoreImagesUnder" },
};

G_DEFINE_TYPE (TrackerConfig, tracker_config, TRACKER_TYPE_CONFIG_FILE);

static void
tracker_config_class_init (TrackerConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = config_set_property;
	object_class->get_property = config_get_property;
	object_class->finalize     = config_finalize;
	object_class->constructed  = config_constructed;

	/* General */
	g_object_class_install_property (object_class,
	                                 PROP_VERBOSITY,
	                                 g_param_spec_int ("verbosity",
	                                                   "Log verbosity",
	                                                   " Log verbosity (0=errors, 1=minimal, 2=detailed, 3=debug)",
	                                                   0,
	                                                   3,
	                                                   DEFAULT_VERBOSITY,
	                                                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
	                                 PROP_MAX_BYTES,
	                                 g_param_spec_int ("max-bytes",
	                                                   "Max Bytes",
	                                                   " Maximum number of UTF-8 bytes to extract per file [0->10485760]",
	                                                   0,
	                                                   ABSOLUTE_MAX_BYTES,
	                                                   DEFAULT_MAX_BYTES,
	                                                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
	                                 PROP_IGNORE_IMAGES_UNDER,
	                                 g_param_spec_pointer ("ignore-images-under",
	                                                       "Ignore images under",
	                                                       " List of directories to NOT extract images in (separator=;)",
	                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_type_class_add_private (object_class, sizeof (TrackerConfigPrivate));
}

static void
tracker_config_init (TrackerConfig *object)
{
}

static void
config_set_property (GObject      *object,
                     guint         param_id,
                     const GValue *value,
                     GParamSpec   *pspec)
{
	switch (param_id) {
		/* General */
	case PROP_VERBOSITY:
		tracker_config_set_verbosity (TRACKER_CONFIG (object),
		                              g_value_get_int (value));
		break;

	case PROP_MAX_BYTES:
		tracker_config_set_max_bytes (TRACKER_CONFIG (object),
		                              g_value_get_int (value));
		break;

	case PROP_IGNORE_IMAGES_UNDER:
		tracker_config_set_ignore_images_under (TRACKER_CONFIG (object),
		                                        g_value_get_pointer (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
config_get_property (GObject    *object,
                     guint       param_id,
                     GValue     *value,
                     GParamSpec *pspec)
{
	TrackerConfigPrivate *priv;

	priv = TRACKER_CONFIG_GET_PRIVATE (object);

	switch (param_id) {
		/* General */
	case PROP_VERBOSITY:
		g_value_set_int (value, priv->verbosity);
		break;

	case PROP_MAX_BYTES:
		g_value_set_int (value, priv->max_bytes);
		break;

	case PROP_IGNORE_IMAGES_UNDER:
		g_value_set_pointer (value, priv->ignore_images_under);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
config_finalize (GObject *object)
{
	TrackerConfigPrivate *priv;

	priv = TRACKER_CONFIG_GET_PRIVATE (object);

	g_slist_foreach (priv->ignore_images_under, (GFunc) g_free, NULL);
	g_slist_free (priv->ignore_images_under);

	(G_OBJECT_CLASS (tracker_config_parent_class)->finalize) (object);
}

static void
config_constructed (GObject *object)
{
	(G_OBJECT_CLASS (tracker_config_parent_class)->constructed) (object);

	config_load (TRACKER_CONFIG (object));
}

static void
config_create_with_defaults (TrackerConfig *config,
                             GKeyFile      *key_file,
                             gboolean       overwrite)
{
	gint i;

	g_message ("Loading defaults into GKeyFile...");

	for (i = 0; i < G_N_ELEMENTS (conversions); i++) {
		gboolean has_key;

		has_key = g_key_file_has_key (key_file,
		                              conversions[i].group,
		                              conversions[i].key,
		                              NULL);
		if (!overwrite && has_key) {
			continue;
		}

		switch (conversions[i].type) {
		case G_TYPE_INT:
			g_key_file_set_integer (key_file,
			                        conversions[i].group,
			                        conversions[i].key,
			                        tracker_keyfile_object_default_int (config,
			                                                            conversions[i].property));
			break;
		case G_TYPE_POINTER: {
				const gchar *string_list[] = { NULL };

				g_key_file_set_string_list (key_file,
				                            conversions[i].group,
				                            conversions[i].key,
				                            string_list,
				                            G_N_ELEMENTS (string_list));
			}
			break;
		default:
			g_assert_not_reached ();
		}

		g_key_file_set_comment (key_file,
		                        conversions[i].group,
		                        conversions[i].key,
		                        tracker_keyfile_object_blurb (config,
		                                                      conversions[i].property),
		                        NULL);
	}
}

static void
config_load (TrackerConfig *config)
{
	TrackerConfigFile *file;
	gint i;

	file = TRACKER_CONFIG_FILE (config);
	config_create_with_defaults (config, file->key_file, FALSE);

	if (!file->file_exists) {
		tracker_config_file_save (file);
	}

	for (i = 0; i < G_N_ELEMENTS (conversions); i++) {
		/* gboolean has_key;

		has_key = g_key_file_has_key (file->key_file,
		                              conversions[i].group,
		                              conversions[i].key,
		                              NULL); */

		switch (conversions[i].type) {
		case G_TYPE_INT:
			tracker_keyfile_object_load_int (G_OBJECT (file),
			                                 conversions[i].property,
			                                 file->key_file,
			                                 conversions[i].group,
			                                 conversions[i].key);
			break;

		case G_TYPE_POINTER: {
			GSList *new_dirs, *old_dirs, *l;
			gboolean equal;

			if (strcmp (conversions[i].property, "ignore-images-under") == 0) {
				tracker_keyfile_object_load_directory_list (G_OBJECT (file),
				                                            conversions[i].property,
				                                            file->key_file,
				                                            conversions[i].group,
				                                            conversions[i].key,
				                                            FALSE,
				                                            &new_dirs);

				for (l = new_dirs; l; l = l->next) {
					const gchar *path_to_use;

					/* Must be a special dir */
					if (strcmp (l->data, "&DESKTOP") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
					} else if (strcmp (l->data, "&DOCUMENTS") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);
					} else if (strcmp (l->data, "&DOWNLOAD") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
					} else if (strcmp (l->data, "&MUSIC") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_MUSIC);
					} else if (strcmp (l->data, "&PICTURES") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
					} else if (strcmp (l->data, "&PUBLIC_SHARE") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_PUBLIC_SHARE);
					} else if (strcmp (l->data, "&TEMPLATES") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_TEMPLATES);
					} else if (strcmp (l->data, "&VIDEOS") == 0) {
						path_to_use = g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS);
					} else {
						path_to_use = NULL;
					}

					if (path_to_use) {
						g_free (l->data);
						l->data = g_strdup (path_to_use);
					}
				}
			} else {
				tracker_keyfile_object_load_string_list (G_OBJECT (file),
				                                         conversions[i].property,
				                                         file->key_file,
				                                         conversions[i].group,
				                                         conversions[i].key,
				                                         &new_dirs);
			}

			g_object_get (config, conversions[i].property, &old_dirs, NULL);

			equal = tracker_gslist_with_string_data_equal (new_dirs, old_dirs);

			if (!equal) {
				g_object_set (config, conversions[i].property, new_dirs, NULL);
			}

			g_slist_foreach (new_dirs, (GFunc) g_free, NULL);
			g_slist_free (new_dirs);

			break;
		}

		default:
			g_assert_not_reached ();
			break;
		}
	}
}

static gboolean
config_save (TrackerConfig *config)
{
	TrackerConfigFile *file;
	gint i;

	file = TRACKER_CONFIG_FILE (config);

	if (!file->key_file) {
		g_critical ("Could not save config, GKeyFile was NULL, has the config been loaded?");

		return FALSE;
	}

	g_message ("Setting details to GKeyFile object...");

	for (i = 0; i < G_N_ELEMENTS (conversions); i++) {
		switch (conversions[i].type) {
		case G_TYPE_INT:
			tracker_keyfile_object_save_int (file,
			                                 conversions[i].property,
			                                 file->key_file,
			                                 conversions[i].group,
			                                 conversions[i].key);
			break;

		case G_TYPE_POINTER:
			if (strcmp (conversions[i].property, "ignore-images-under") == 0) {
				GSList *dirs, *l;

				g_object_get (config, conversions[i].property, &dirs, NULL);

				for (l = dirs; l; l = l->next) {
					const gchar *path_to_use;

					/* FIXME: This doesn't work
					 * perfectly, what if DESKTOP
					 * and DOCUMENTS are in the
					 * same place? Then this
					 * breaks. Need a better
					 * solution at some point.
					 */
					if (g_strcmp0 (l->data, g_get_home_dir ()) == 0) {
						/* Home dir gets straight into configuration,
						 * regardless of having XDG dirs pointing to it.
						 */
						path_to_use = "$HOME";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP)) == 0) {
						path_to_use = "&DESKTOP";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS)) == 0) {
						path_to_use = "&DOCUMENTS";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD)) == 0) {
						path_to_use = "&DOWNLOAD";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_MUSIC)) == 0) {
						path_to_use = "&MUSIC";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_PICTURES)) == 0) {
						path_to_use = "&PICTURES";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_PUBLIC_SHARE)) == 0) {
						path_to_use = "&PUBLIC_SHARE";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_TEMPLATES)) == 0) {
						path_to_use = "&TEMPLATES";
					} else if (g_strcmp0 (l->data, g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS)) == 0) {
						path_to_use = "&VIDEOS";
					} else {
						path_to_use = NULL;
					}

					if (path_to_use) {
						g_free (l->data);
						l->data = g_strdup (path_to_use);
					}
				}
			}

			tracker_keyfile_object_save_string_list (file,
			                                         conversions[i].property,
			                                         file->key_file,
			                                         conversions[i].group,
			                                         conversions[i].key);
			break;

		default:
			g_assert_not_reached ();
			break;
		}
	}

	return tracker_config_file_save (file);
}

TrackerConfig *
tracker_config_new (void)
{
	return g_object_new (TRACKER_TYPE_CONFIG, NULL);
}

gboolean
tracker_config_save (TrackerConfig *config)
{
	g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);

	return config_save (config);
}

gint
tracker_config_get_verbosity (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_VERBOSITY);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->verbosity;
}

GSList *
tracker_config_get_ignore_images_under (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->ignore_images_under;
}


void
tracker_config_set_verbosity (TrackerConfig *config,
                              gint           value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	if (!tracker_keyfile_object_validate_int (config, "verbosity", value)) {
		return;
	}

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->verbosity = value;
	g_object_notify (G_OBJECT (config), "verbosity");
}


gint
tracker_config_get_max_bytes (TrackerConfig *config)
{
	TrackerConfigPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), DEFAULT_MAX_BYTES);

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	return priv->max_bytes;
}

void
tracker_config_set_max_bytes (TrackerConfig *config,
                              gint           value)
{
	TrackerConfigPrivate *priv;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	if (!tracker_keyfile_object_validate_int (config, "max-bytes", value)) {
		return;
	}

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	priv->max_bytes = value;
	g_object_notify (G_OBJECT (config), "max-bytes");
}

void
tracker_config_set_ignore_images_under (TrackerConfig *config,
                                        GSList        *roots)
{
	TrackerConfigPrivate *priv;
	GSList *l;
	gboolean equal;

	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = TRACKER_CONFIG_GET_PRIVATE (config);

	l = priv->ignore_images_under;

	equal = tracker_gslist_with_string_data_equal (roots, l);

	if (!roots) {
		priv->ignore_images_under = NULL;
	} else {
		priv->ignore_images_under =
			tracker_gslist_copy_with_string_data (roots);
	}

	g_slist_foreach (l, (GFunc) g_free, NULL);
	g_slist_free (l);

	if (equal) {
		return;
	}

	g_object_notify (G_OBJECT (config), "ignore-images-under");
}