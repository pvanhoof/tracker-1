/* Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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
#include <glib/gstdio.h>
#include <gmime/gmime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <gconf/gconf-client.h>
#include <tracker-indexer/tracker-module.h>
#include <tracker-indexer/tracker-metadata.h>
#include <libtracker-common/tracker-utils.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-type-utils.h>

#define METADATA_FILE_PATH           "File:Path"
#define METADATA_FILE_NAME           "File:Name"
#define METADATA_EMAIL_RECIPIENT     "Email:Recipient"
#define METADATA_EMAIL_DATE          "Email:Date"
#define METADATA_EMAIL_SENDER        "Email:Sender"
#define METADATA_EMAIL_SUBJECT       "Email:Subject"
#define METADATA_EMAIL_SENT_TO       "Email:SentTo"
#define METADATA_EMAIL_CC            "Email:CC"
#define METADATA_EMAIL_BODY          "Email:Body"

typedef union EvolutionFileData EvolutionFileData;
typedef struct EvolutionLocalData EvolutionLocalData;
typedef struct EvolutionImapData EvolutionImapData;
typedef struct EvolutionAccountContext EvolutionAccountContext;
typedef enum MailStorageType MailStorageType;

enum MailStorageType {
        MAIL_STORAGE_NONE,
        MAIL_STORAGE_LOCAL,
        MAIL_STORAGE_IMAP
};

struct EvolutionLocalData {
        MailStorageType type;
        GMimeStream *stream;
        GMimeParser *parser;
        GMimeMessage *message;
};

struct EvolutionImapData {
        MailStorageType type;
        gint fd;
        FILE *summary;
        guint n_messages;
        guint cur_message;
};

union EvolutionFileData {
        MailStorageType type;
        EvolutionLocalData mbox;
        EvolutionImapData imap;
};

enum SummaryDataType {
        SUMMARY_TYPE_INT32,
        SUMMARY_TYPE_UINT32,
        SUMMARY_TYPE_STRING,
        SUMMARY_TYPE_TOKEN,
        SUMMARY_TYPE_TIME_T
};

struct EvolutionAccountContext {
        gchar *account;
        gchar *uid;
};

enum EvolutionFlags {
	EVOLUTION_MESSAGE_ANSWERED     = 1 << 0,
	EVOLUTION_MESSAGE_DELETED      = 1 << 1,
	EVOLUTION_MESSAGE_DRAFT        = 1 << 2,
	EVOLUTION_MESSAGE_FLAGGED      = 1 << 3,
	EVOLUTION_MESSAGE_SEEN         = 1 << 4,
	EVOLUTION_MESSAGE_ATTACHMENTS  = 1 << 5,
	EVOLUTION_MESSAGE_ANSWERED_ALL = 1 << 6,
	EVOLUTION_MESSAGE_JUNK         = 1 << 7,
	EVOLUTION_MESSAGE_SECURE       = 1 << 8
};


static gchar *local_dir = NULL;
static gchar *imap_dir = NULL;
static GHashTable *accounts = NULL;


void   get_imap_accounts (void);


static gboolean
read_summary (FILE *summary,
              ...)
{
        va_list args;
        gint value_type;

        if (!summary) {
                return FALSE;
        }

        va_start (args, summary);

        while ((value_type = va_arg (args, gint)) != -1) {
                switch (value_type) {
                case SUMMARY_TYPE_TIME_T:
                case SUMMARY_TYPE_INT32: {
                        gint32 value, *dest;

                        if (fread (&value, sizeof (gint32), 1, summary) != 1) {
                                return FALSE;
                        }

                        dest = va_arg (args, gint32*);

                        if (dest) {
                                *dest = g_ntohl (value);
                        }
                        break;
                }
                case SUMMARY_TYPE_UINT32: {
                        guint32 *dest, value = 0;
                        gint c;

                        while (((c = fgetc (summary)) & 0x80) == 0 && c != EOF) {
                                value |= c;
                                value <<= 7;
                        }

                        if (c == EOF) {
                                return FALSE;
                        } else {
                                value |= (c & 0x7f);
                        }

                        dest = va_arg (args, guint32*);

                        if (dest) {
                                *dest = value;
                        }
                        break;
                }
                case SUMMARY_TYPE_STRING:
                case SUMMARY_TYPE_TOKEN: {
                        guint32 len;
                        gchar *str, **dest;

                        /* read string length */
                        read_summary (summary, SUMMARY_TYPE_UINT32, &len, -1);
                        dest = va_arg (args, gchar **);

                        if (dest) {
                                *dest = NULL;
                        }

                        if (value_type == SUMMARY_TYPE_TOKEN) {
                                if (len < 32) {
                                        continue;
                                } else {
                                        len -= 31;
                                }
                        }

                        if (len <= 1) {
                                continue;
                        }

                        str = g_malloc0 (len);

                        if (fread (str, len - 1, 1, summary) != 1) {
                                g_free (str);
                                return FALSE;
                        }

                        if (dest) {
                                *dest = str;
                        } else {
                                g_free (str);
                        }

                        break;
                }
                default:
                        break;
                }
        }

        va_end (args);

        return TRUE;
}

void
tracker_module_init (void)
{
        g_mime_init (0);
        get_imap_accounts ();

        local_dir = g_build_filename (g_get_home_dir (), ".evolution", "mail", "local", G_DIR_SEPARATOR_S, NULL);
        imap_dir = g_build_filename (g_get_home_dir (), ".evolution", "mail", "imap", G_DIR_SEPARATOR_S, NULL);
}

void
tracker_module_shutdown (void)
{
        g_mime_shutdown ();

        g_hash_table_destroy (accounts);
        g_free (local_dir);
        g_free (imap_dir);
}

G_CONST_RETURN gchar *
tracker_module_get_name (void)
{
        /* Return module name here */
        return "EvolutionEmails";
}

static void
account_start_element_handler (GMarkupParseContext *context,
			       const gchar         *element_name,
			       const gchar         **attr_names,
			       const gchar         **attr_values,
			       gpointer	           user_data,
			       GError              **error)
{
        EvolutionAccountContext *account_context;
        gint i = 0;

        if (strcmp (element_name, "account") != 0) {
                return;
        }

        account_context = (EvolutionAccountContext *) user_data;

        while (attr_names[i]) {
                if (strcmp (attr_names[i], "uid") == 0) {
                        account_context->uid = g_strdup (attr_values[i]);
                        return;
                }

                i++;
        }
}

static gchar *
get_account_name_from_imap_uri (const gchar *imap_uri)
{
        const gchar *start, *at, *semic;
        gchar *user_name, *at_host_name, *account_name;
        
        /* Assume url schema is:
         * imap://foo@imap.free.fr/;etc
         * or
         * imap://foo;auth=DIGEST-MD5@imap.bar.com/;etc
         *
         * We try to get "foo@imap.free.fr".
         */

        if (!g_str_has_prefix (imap_uri, "imap://")) {
                return NULL;
        }

        user_name = at_host_name = account_name = NULL;

        /* check for embedded @ and then look for first colon after that */
        start = imap_uri + 7;
        at = strchr (start, '@');
        semic = strchr (start, ';');

        if ( strlen (imap_uri) < 7 || at == NULL ) {
                return g_strdup ("Unknown");
        }

        if (semic < at) {
                /* we have a ";auth=FOO@host" schema
                   Set semic to the next semicolon, which ends the hostname. */
                user_name = g_strndup (start, semic - start);
                /* look for ';' at the end of the domain name */
                semic = strchr (at, ';');
        } else {
                user_name = g_strndup (start, at - start);
        }

        at_host_name = g_strndup (at, (semic - 1) - at);

        account_name = g_strconcat (user_name, at_host_name, NULL);

        g_free (user_name);
        g_free (at_host_name);

        return account_name;
}

static void
account_text_handler (GMarkupParseContext  *context,
                      const gchar          *text,
                      gsize                 text_len,
                      gpointer              user_data,
                      GError              **error)
{
        EvolutionAccountContext *account_context;
        const GSList *uri_element, *source_element;
        gchar *url;

        uri_element = g_markup_parse_context_get_element_stack (context);
        source_element = uri_element->next;

        if (strcmp ((gchar *) uri_element->data, "url") != 0 ||
            !source_element ||
            strcmp ((gchar *) source_element->data, "source") != 0) {
                return;
        }

        account_context = (EvolutionAccountContext *) user_data;

        url = g_strndup (text, text_len);
        account_context->account = get_account_name_from_imap_uri (url);
        g_free (url);
}

void
get_imap_accounts (void)
{
        GConfClient *client;
        GMarkupParser parser = { 0 };
        GMarkupParseContext *parse_context;
        GSList *list, *l;
        EvolutionAccountContext account_context = { 0 };

        client = gconf_client_get_default ();

        list = gconf_client_get_list (client,
                                      "/apps/evolution/mail/accounts",
                                      GCONF_VALUE_STRING,
                                      NULL);

        parser.start_element = account_start_element_handler;
        parser.text = account_text_handler;
        parse_context = g_markup_parse_context_new (&parser, 0, &account_context, NULL);

        accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          (GDestroyNotify) g_free,
                                          (GDestroyNotify) g_free);

        for (l = list; l; l = l->next) {
                g_markup_parse_context_parse (parse_context, (const gchar *) l->data, -1, NULL);

                if (account_context.account &&
                    account_context.uid) {
                        g_hash_table_insert (accounts,
                                             account_context.account,
                                             account_context.uid);
                } else {
                        g_free (account_context.account);
                        g_free (account_context.uid);
                }
        }

        g_markup_parse_context_free (parse_context);

        g_slist_foreach (list, (GFunc) g_free, NULL);
        g_slist_free (list);
}

static MailStorageType
get_mail_storage_type_from_path (const gchar *path)
{
        MailStorageType type = MAIL_STORAGE_NONE;
        gchar *basename;

        basename = g_path_get_basename (path);

        if (g_str_has_prefix (path, local_dir) &&
            strchr (basename, '.') == NULL) {
                type = MAIL_STORAGE_LOCAL;
        } else if (g_str_has_prefix (path, imap_dir) &&
                   strcmp (basename, "summary") == 0) {
                type = MAIL_STORAGE_IMAP;
        }

        /* Exclude non wanted folders */
        if (strcasestr (path, "junk") ||
            strcasestr (path, "spam") ||
            strcasestr (path, "trash") ||
            strcasestr (path, "drafts") ||
            strcasestr (path, "outbox")) {
                type = MAIL_STORAGE_NONE;
        }

        g_free (basename);

        return type;
}

static GMimeStream *
email_get_stream (const gchar *path,
                  gint         flags,
                  off_t        start)
{
        GMimeStream *stream;
        gint fd;

        fd = g_open (path, flags, S_IRUSR | S_IWUSR);

        if (fd == -1) {
                return NULL;
        }

        stream = g_mime_stream_fs_new_with_bounds (fd, start, -1);

        if (!stream) {
                close (fd);
        }

        return stream;
}

static gint
read_summary_header (FILE *summary)
{
        gint32 version, n_messages;

        read_summary (summary,
                      SUMMARY_TYPE_INT32, &version,
                      SUMMARY_TYPE_INT32, NULL,
                      SUMMARY_TYPE_INT32, NULL,
                      SUMMARY_TYPE_INT32, NULL,
                      SUMMARY_TYPE_INT32, &n_messages,
                      -1);

        if ((version < 0x100 && version >= 13)) {
                read_summary (summary,
                              SUMMARY_TYPE_INT32, NULL,
                              SUMMARY_TYPE_INT32, NULL,
                              SUMMARY_TYPE_INT32, NULL,
                              -1);
        }

        if (version != 0x30c) {
                read_summary (summary,
                              SUMMARY_TYPE_INT32, NULL,
                              SUMMARY_TYPE_INT32, NULL,
                              -1);
        }

        return n_messages;
}

gpointer
tracker_module_file_get_data (const gchar *path)
{
        EvolutionFileData *data = NULL;
        MailStorageType type;

        type = get_mail_storage_type_from_path (path);

        if (type == MAIL_STORAGE_NONE) {
                return NULL;
        }

        data = g_slice_new0 (EvolutionFileData);
        data->type = type;

        if (type == MAIL_STORAGE_IMAP) {
                EvolutionImapData *imap_data;

                imap_data = (EvolutionImapData *) data;

                imap_data->fd = tracker_file_open (path, TRUE);

                if (imap_data->fd == -1) {
                        return NULL;
                }

                imap_data->summary = fdopen (imap_data->fd, "r");
                imap_data->n_messages = read_summary_header (imap_data->summary);
                imap_data->cur_message = 1;
        } else {
                EvolutionLocalData *local_data;

                local_data = (EvolutionLocalData *) data;

#if defined(__linux__)
                local_data->stream = email_get_stream (path, O_RDONLY | O_NOATIME, 0);
#else
                local_data->stream = email_get_stream (path, O_RDONLY, 0);
#endif

                if (local_data->stream) {
                        local_data->parser = g_mime_parser_new_with_stream (local_data->stream);
                        g_mime_parser_set_scan_from (local_data->parser, TRUE);

                        /* Initialize to the first message */
                        local_data->message = g_mime_parser_construct_message (local_data->parser);
                }
        }

        return data;
}

static void
free_imap_data (EvolutionImapData *data)
{
        fclose (data->summary);
        close (data->fd);
}

static void
free_local_data (EvolutionLocalData *data)
{
        if (data->message) {
                g_object_unref (data->message);
        }

        if (data->parser) {
                g_object_unref (data->parser);
        }

        if (data->stream) {
                g_mime_stream_close (data->stream);
                g_object_unref (data->stream);
        }
}

void
tracker_module_file_free_data (gpointer file_data)
{
        EvolutionFileData *data;

        data = (EvolutionFileData *) file_data;

        if (data->type == MAIL_STORAGE_LOCAL) {
                free_local_data ((EvolutionLocalData *) data);
        } else if (data->type == MAIL_STORAGE_IMAP) {
                free_imap_data ((EvolutionImapData *) data);
        }

        g_slice_free (EvolutionFileData, data);
}

static gint
get_mbox_message_id (GMimeMessage *message)
{
        const gchar *header, *pos;
        gchar *number;
        gint id;

        header = g_mime_message_get_header (message, "X-Evolution");
        pos = strchr (header, '-');

        number = g_strndup (header, pos - header);
        id = strtoul (number, NULL, 16);

        g_free (number);

        return id;
}

static guint
get_mbox_message_flags (GMimeMessage *message)
{
        const gchar *header, *pos;

        header = g_mime_message_get_header (message, "X-Evolution");
        pos = strchr (header, '-');

        return (guint) strtoul (pos + 1, NULL, 16);
}

static void
get_mbox_uri (TrackerFile   *file,
              GMimeMessage  *message,
              gchar        **dirname,
              gchar        **basename)
{
        gchar *dir, *name;

        dir = tracker_string_replace (file->path, local_dir, NULL);
        name = g_strdup_printf ("%s;uid=%d", dir, get_mbox_message_id (message));

        *dirname = g_strdup ("email://local@local");
        *basename = name;

        g_free (dir);
}

static GList *
get_mbox_recipient_list (GMimeMessage *message,
                         const gchar  *type)
{
        GList *list = NULL;
        const InternetAddressList *addresses;

        addresses = g_mime_message_get_recipients (message, type);

        while (addresses) {
                InternetAddress *address;
                gchar *str;

                address = addresses->address;

                if (address->name && address->value.addr) {
                        str = g_strdup_printf ("%s %s", address->name, address->value.addr);
                } else if (address->value.addr) {
                        str = g_strdup (address->value.addr);
                } else if (address->name) {
                        str = g_strdup (address->name);
                }

                list = g_list_prepend (list, str);
                addresses = addresses->next;
        }

        return g_list_reverse (list);
}

TrackerMetadata *
get_metadata_for_mbox (TrackerFile *file)
{
        EvolutionLocalData *data;
        GMimeMessage *message;
        TrackerMetadata *metadata;
        gchar *dirname, *basename, *body;
        gboolean is_html;
        time_t date;
        GList *list;
        guint flags;

        data = file->data;
        message = data->message;

        if (!message) {
                return NULL;
        }

        flags = get_mbox_message_flags (message);

        if (flags & EVOLUTION_MESSAGE_JUNK ||
            flags & EVOLUTION_MESSAGE_DELETED) {
                return NULL;
        }

        metadata = tracker_metadata_new ();

        get_mbox_uri (file, message, &dirname, &basename);
        tracker_metadata_insert (metadata, METADATA_FILE_PATH, dirname);
        tracker_metadata_insert (metadata, METADATA_FILE_NAME, basename);

        g_mime_message_get_date (message, &date, NULL);
	tracker_metadata_insert (metadata, METADATA_EMAIL_DATE,
                                 tracker_uint_to_string (date));

        tracker_metadata_insert (metadata, METADATA_EMAIL_SENDER,
                                 g_strdup (g_mime_message_get_sender (message)));
        tracker_metadata_insert (metadata, METADATA_EMAIL_SUBJECT,
                                 g_strdup (g_mime_message_get_subject (message)));

        list = get_mbox_recipient_list (message, GMIME_RECIPIENT_TYPE_TO);
        tracker_metadata_insert_multiple_values (metadata, METADATA_EMAIL_SENT_TO, list);

        list = get_mbox_recipient_list (message, GMIME_RECIPIENT_TYPE_CC);
        tracker_metadata_insert_multiple_values (metadata, METADATA_EMAIL_CC, list);

        body = g_mime_message_get_body (message, TRUE, &is_html);
        tracker_metadata_insert (metadata, METADATA_EMAIL_BODY, body);

        /* FIXME: Missing attachments handling */

        return metadata;
}

void
skip_content_info (FILE *summary)
{
        guint32 count, i;

        if (fgetc (summary)) {
                read_summary (summary,
                              SUMMARY_TYPE_TOKEN, NULL,
                              SUMMARY_TYPE_TOKEN, NULL,
                              SUMMARY_TYPE_UINT32, &count,
                              -1);

                if (count <= 500) {
                        for (i = 0; i < count; i++) {
                                read_summary (summary,
                                              SUMMARY_TYPE_TOKEN, NULL,
                                              SUMMARY_TYPE_TOKEN, NULL,
                                              -1);
                        }
                }

                read_summary (summary,
                              SUMMARY_TYPE_TOKEN, NULL,
                              SUMMARY_TYPE_TOKEN, NULL,
                              SUMMARY_TYPE_TOKEN, NULL,
                              SUMMARY_TYPE_UINT32, NULL,
                              -1);
        }

        read_summary (summary,
                      SUMMARY_TYPE_UINT32, &count,
                      -1);

        for (i = 0; i < count; i++) {
                skip_content_info (summary);
        }
}

void
get_imap_uri (TrackerFile  *file,
              gchar       **uri_base,
              gchar       **basename)
{
        GList *keys, *k;
        gchar *path, *dir, *subdirs;

        path = file->path;
        keys = g_hash_table_get_keys (accounts);
        *uri_base = *basename = NULL;

        for (k = keys; k; k = k->next) {
                if (strstr (path, k->data)) {
                        *uri_base = g_strdup_printf ("email://%s", (gchar *) g_hash_table_lookup (accounts, k->data));

                        dir = g_build_filename (imap_dir, k->data, NULL);

                        /* now remove all relevant info to create the email:// basename */
                        subdirs = g_strdup (path);
                        subdirs = tracker_string_remove (subdirs, dir);
                        subdirs = tracker_string_remove (subdirs, "/folders");
                        subdirs = tracker_string_remove (subdirs, "/subfolders");
                        subdirs = tracker_string_remove (subdirs, "/summary");

                        *basename = subdirs;

                        g_free (dir);

                        break;
                }
        }

        g_list_free (keys);

        return;
}

static GList *
get_imap_recipient_list (const gchar *str)
{
        GList *list = NULL;
        gchar **arr;
        gint i;

        if (!str) {
                return NULL;
        }

        arr = g_strsplit (str, ",", -1);

        for (i = 0; arr[i]; i++) {
                g_strstrip (arr[i]);
                list = g_list_prepend (list, g_strdup (arr[i]));
        }

        g_strfreev (arr);

        return g_list_reverse (list);
}

static gchar *
get_imap_message_body (TrackerFile *file,
                       const gchar *uid)
{
        gchar *prefix, *body_path;
        gchar *body = NULL;

        prefix = g_strndup (file->path, strlen (file->path) - strlen ("summary"));
        body_path = g_strconcat (prefix, uid, ".", NULL);

        g_file_get_contents (body_path, &body, NULL, NULL);

        g_free (prefix);
        g_free (body_path);

        return body;
}

TrackerMetadata *
get_metadata_for_imap (TrackerFile *file)
{
        EvolutionImapData *data;
        TrackerMetadata *metadata;
        gchar *dirname, *basename;
        gchar *uid, *subject, *from, *to, *cc, *body;
        gint32 i, count, flags;
        time_t date;
        GList *list;

        data = file->data;

        if (data->cur_message > data->n_messages) {
                return NULL;
        }

        read_summary (data->summary,
                      SUMMARY_TYPE_STRING, &uid, /* message uid */
                      SUMMARY_TYPE_UINT32, &flags, /* flags */
                      -1);

        if (flags & EVOLUTION_MESSAGE_JUNK ||
            flags & EVOLUTION_MESSAGE_DELETED) {
                g_free (uid);
                return NULL;
        }

        read_summary (data->summary,
                      SUMMARY_TYPE_UINT32, NULL, /* size */
                      SUMMARY_TYPE_TIME_T, NULL, /* date sent */
                      SUMMARY_TYPE_TIME_T, &date, /* date received */
                      SUMMARY_TYPE_STRING, &subject, /* subject */
                      SUMMARY_TYPE_STRING, &from, /* from */
                      SUMMARY_TYPE_STRING, &to, /* to */
                      SUMMARY_TYPE_STRING, &cc, /* cc */
                      SUMMARY_TYPE_STRING, NULL, /* mlist */
                      -1);

	metadata = tracker_metadata_new ();
        get_imap_uri (file, &dirname, &basename);

        tracker_metadata_insert (metadata, METADATA_FILE_PATH, dirname);
        tracker_metadata_insert (metadata, METADATA_FILE_NAME, g_strdup_printf ("%s;uid=%s", basename, uid));
        g_free (basename);

	tracker_metadata_insert (metadata, METADATA_EMAIL_DATE,
                                 tracker_uint_to_string (date));

        tracker_metadata_insert (metadata, METADATA_EMAIL_SENDER, from);
        tracker_metadata_insert (metadata, METADATA_EMAIL_SUBJECT, subject);

        list = get_imap_recipient_list (to);
        tracker_metadata_insert_multiple_values (metadata, METADATA_EMAIL_SENT_TO, list);

        list = get_imap_recipient_list (cc);
        tracker_metadata_insert_multiple_values (metadata, METADATA_EMAIL_CC, list);

        body = get_imap_message_body (file, uid);
        tracker_metadata_insert (metadata, METADATA_EMAIL_BODY, body);

        g_free (uid);
        g_free (to);
        g_free (cc);

        read_summary (data->summary,
                      SUMMARY_TYPE_INT32, NULL,
                      SUMMARY_TYPE_INT32, NULL,
                      SUMMARY_TYPE_UINT32, &count,
                      -1);

        /* references */
        for (i = 0; i < count; i++) {
                read_summary (data->summary,
                              SUMMARY_TYPE_INT32, NULL,
                              SUMMARY_TYPE_INT32, NULL,
                              -1);
        }

        read_summary (data->summary, SUMMARY_TYPE_UINT32, &count, -1);

        /* user flags */
        for (i = 0; i < count; i++) {
                read_summary (data->summary, SUMMARY_TYPE_STRING, NULL, -1);
        }

        read_summary (data->summary, SUMMARY_TYPE_UINT32, &count, -1);

        /* user tags */
        for (i = 0; i < count; i++) {
                read_summary (data->summary,
                              SUMMARY_TYPE_STRING, NULL,
                              SUMMARY_TYPE_STRING, NULL,
                              -1);
        }

        /* server flags */
        read_summary (data->summary,
                      SUMMARY_TYPE_UINT32, NULL,
                      -1);

        skip_content_info (data->summary);

        return metadata;
}

void
tracker_module_file_get_uri (TrackerFile  *file,
                             gchar       **dirname,
                             gchar       **basename)
{
        EvolutionFileData *file_data;

        file_data = file->data;

        if (!file_data) {
                /* It isn't any of the files the
                 * module is interested for */
                return;
        }

        switch (file_data->type) {
        case MAIL_STORAGE_LOCAL: {
                EvolutionLocalData *data = file->data;

                if (!data->message) {
                        return;
                }

                get_mbox_uri (file, data->message, dirname, basename);
                break;
        }
        case MAIL_STORAGE_IMAP: {
                get_imap_uri (file, dirname, basename);
                break;
        }
        default:
                break;
        }
}

TrackerMetadata *
tracker_module_file_get_metadata (TrackerFile *file)
{
        EvolutionFileData *data;

        data = file->data;

        if (!data) {
                /* It isn't any of the files the
                 * module is interested for */
                return NULL;
        }

        switch (data->type) {
        case MAIL_STORAGE_LOCAL:
                return get_metadata_for_mbox (file);
        case MAIL_STORAGE_IMAP:
                return get_metadata_for_imap (file);
        default:
                break;
        }

        return NULL;
}

gboolean
tracker_module_file_iter_contents (TrackerFile *file)
{
        EvolutionFileData *data;

        data = file->data;

        if (data->type == MAIL_STORAGE_IMAP) {
                EvolutionImapData *imap_data = file->data;

                imap_data->cur_message++;

                return (imap_data->cur_message < imap_data->n_messages);
        } else if (data->type == MAIL_STORAGE_LOCAL) {
                EvolutionLocalData *local_data = file->data;

                if (!local_data->parser) {
                        return FALSE;
                }

                if (local_data->message) {
                        g_object_unref (local_data->message);
                }

                local_data->message = g_mime_parser_construct_message (local_data->parser);

                return (local_data->message != NULL);
        }

        return FALSE;
}
