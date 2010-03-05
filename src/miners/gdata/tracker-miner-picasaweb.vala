/*
 * Copyright (C) 2010, Adrien Bustany <abustany@gnome.org>
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

namespace Tracker {

public class MinerPicasaweb : Tracker.MinerWeb {
	private static const string API_KEY = "ABQIAAAAISU_TtyUrVg0N9RGhb8pTxRI3KVolA1NVmS0lLG3RQFyztcosBSHs5rHiQHsmL41EbX1SehKnf1rrw";
	private static const string DATASOURCE_URN = "urn:nepomuk:datasource:682654ec-b59d-4e99-9b0f-7559fc9c5d42";
	private static const string MINER_NAME = "PicasaWeb";
	private static const string SERVICE_DESCRIPTION = "Tracker miner for PicasaWeb";
	private static const uint   PULL_INTERVAL = 5*60; /* seconds */
	private uint pull_timeout_handle;

	private GData.PicasaWebService service = null;

	private Config config;
	private Sparql.Connection tracker;

	construct {
		name = MINER_NAME;
		associated = false;
		status = "Not authenticated";
		progress = 0.0;
	}

	public MinerPicasaweb () {
		config = new Config ();

		service = new GData.PicasaWebService (API_KEY);

		try {
			tracker = Sparql.Connection.get ();
		} catch (Error e) {
			critical ("Couldn't connect to Tracker: %s", e.message);
		}

		this.notify["associated"].connect (association_status_changed);

		authenticate ();
	}

	private void association_status_changed (Object source, ParamSpec pspec) {
		if (associated) {
			if (pull_timeout_handle != 0)
				return;

			message ("Miner is now associated. Initiating periodic pull.");
			pull_timeout_handle = Timeout.add_seconds (PULL_INTERVAL, pull_timeout_cb);
			Idle.add ( () => { pull_timeout_cb (); return false; });
		} else {
			if (pull_timeout_handle == 0)
				return;

			Source.remove (pull_timeout_handle);
		}
	}

	private bool pull_timeout_cb () {
		NetworkProviderStatus network_status = NetworkProvider.get ().get_status ();
		if (network_status == NetworkProviderStatus.DISCONNECTED) {
			return true;
		}

		if (network_status == NetworkProviderStatus.GPRS &&
		    (uint)config.download_behaviour > (uint)Config.DownloadBehaviour.GPRS) {
			return true;
		}

		if (network_status == NetworkProviderStatus.EDGE &&
		    (uint)config.download_behaviour > (uint)Config.DownloadBehaviour.EDGE) {
			return true;
		}

		if (network_status == NetworkProviderStatus.@3G &&
		    (uint)config.download_behaviour > (uint)Config.DownloadBehaviour.@3G) {
			return true;
		}

		status = "Refreshing data";
		progress = 0.0;

		service.query_all_albums_async (null, service.get_username (), null, albums_cb, pull_finished_cb);
		return true;
	}

	private void albums_cb (GData.Entry entry, uint entry_key, uint entry_count) {
		GData.Feed feed_files;
		GData.PicasaWebAlbum album = entry as GData.PicasaWebAlbum;
		GData.PicasaWebFile current_file;
		string current_file_url;
		string current_file_urn;
		string current_file_identifier;
		string current_album_urn;
		string current_album_identifier;
		bool resource_created;
		Sparql.Builder builder;
		List<string> album_files_urls;
		TimeVal tv = TimeVal ();

		try {
			feed_files = service.query_files (album, null, null, null);
		} catch (Error service_error) {
			warning ("Couldn't get files for album %s: %s", album.title, service_error.message);
			return;
		}

		set ("status", "Refreshing album \"%s\"".printf (album.get_title ()));

		album_files_urls = new List<string> ();

		foreach (GData.Entry current_entry in feed_files.get_entries ()) {
			current_file = current_entry as GData.PicasaWebFile;
			current_file_url = ((GData.MediaContent)current_file.get_contents ().first ().data).get_uri ();
			// The cast to GData.Entry is necessary to get the right id due to
			// a bug in current libgdata (gdata <= 0.6.1)
			current_file_identifier = ((GData.Entry)current_file).get_id ();
			try {
				current_file_urn = get_resource (current_file_url,
				                                 {"nmm:Photo", "nfo:RemoteDataObject", "nfo:MediaFileListEntry"},
				                                 current_file_identifier,
				                                 out resource_created);
			} catch (Error e) {
				warning ("Couldn't get resource for picture %s: %s", current_file_url, e.message);
				continue;
			}

			builder = new Sparql.Builder.update ();

			if (resource_created) {
				builder.insert_open (current_file_url);
				builder.subject_iri (current_file_urn);
				builder.predicate ("nie:dataSource");
				builder.object_iri (DATASOURCE_URN);
				builder.predicate ("nie:url");
				builder.object_string (current_file_url);
				builder.insert_close();
			}

			update_triple_string (builder,
			                      current_file_url,
			                      current_file_urn,
			                      "nie:title",
			                      (current_file.get_caption () != null ? current_file.get_caption () : current_file.get_title ()));

			current_file.get_timestamp (tv);
			update_triple_string (builder,
			                      current_file_url,
			                      current_file_urn,
			                      "nie:contentCreated",
			                      tv.to_iso8601 ());

			current_file.get_edited (tv);
			update_triple_string (builder,
			                      current_file_url,
			                      current_file_urn,
			                      "nie:contentLastModified",
			                      tv.to_iso8601 ());

			update_triple_int64 (builder,
			                     current_file_url,
			                     current_file_urn,
			                     "nfo:width",
			                     (int64)current_file.get_width ());

			update_triple_int64 (builder,
			                     current_file_url,
			                     current_file_urn,
			                     "nfo:height",
			                     (int64)current_file.get_height ());

			if (current_file.get_tags () != null) {
				builder.insert_open (current_file_url);
				builder.subject_iri (current_file_urn);

				foreach (string tag_label in current_file.get_tags ().split (",")) {
					builder.predicate ("nao:hasTag");
					builder.object_blank_open ();
					builder.predicate ("a");
					builder.object ("nao:Tag");
					builder.predicate ("nao:prefLabel");
					builder.object_string (tag_label);
					builder.object_blank_close ();
				}

				builder.insert_close ();
			}

			try {
				tracker.update (builder.result);
				album_files_urls.prepend (current_file_url);
			} catch (Error e) {
				warning ("Couldn't import picture %s: %s", current_file_url, e.message);
			}
		}

		builder = new Sparql.Builder.update ();

		current_album_identifier = ((GData.Entry)album).get_id ();
		try {
			current_album_urn = get_resource (DATASOURCE_URN,
			                                  {"nmm:ImageList", "nfo:RemoteDataObject"},
			                                  current_album_identifier,
			                                  out resource_created);
		} catch (Error e) {
			warning ("Coudln't get resource for album %s: %s", album.get_title (), e.message);
			return;
		}

		if (resource_created) {
			builder.predicate ("nie:dataSource");
			builder.object_iri (DATASOURCE_URN);
		}

		update_triple_string (builder,
		                      DATASOURCE_URN,
		                      current_album_urn,
		                      "nie:title",
		                      album.get_title ());

		if (album.get_summary () != null) {
			update_triple_string (builder,
			                      DATASOURCE_URN,
			                      current_album_urn,
			                      "rdfs:comment",
			                      album.get_summary ());
		}

		if (album.get_tags () != null) {
			foreach (string tag_label in album.get_tags ().split (",")) {
				builder.predicate ("nao:hasTag");
				builder.object_blank_open ();
				builder.predicate ("nao:prefLabel");
				builder.object_string (tag_label);
				builder.object_blank_close ();
			}
		}

		update_triple_int64 (builder,
		                     DATASOURCE_URN,
		                     current_album_urn,
		                     "nfo:entryCounter",
		                     (int64)album.get_num_photos ());

		album.get_timestamp (tv);
		update_triple_string (builder,
		                      DATASOURCE_URN,
		                      current_album_urn,
		                      "nie:contentCreated",
		                      tv.to_iso8601 ());

		album.get_edited (tv);
		update_triple_string (builder,
		                      DATASOURCE_URN,
		                      current_album_urn,
		                      "nie:contentLastModified",
		                      tv.to_iso8601 ());

		if (album_files_urls.length () > 0) {
			builder.insert_open (current_album_urn);
			builder.subject_iri (current_album_urn);

			foreach (string current_url in album_files_urls) {
				builder.predicate ("nfo:hasMediaFileListEntry");
				builder.object_blank_open ();
				builder.predicate ("a");
				builder.object ("nmm:Photo");
				builder.predicate ("a");
				builder.object ("nfo:MediaFileListEntry");
				builder.predicate ("a");
				builder.object ("nfo:RemoteDataObject");
				builder.predicate ("nie:url");
				builder.object_string (current_url);
				builder.object_blank_close ();
			}

			builder.insert_close ();
		}

		try {
			tracker.update (builder.result);
		} catch (Error e) {
			warning ("Couldn't save album %s: %s", album.title, e.message);
		}

		progress = (1.0 + entry_key)/entry_count;
	}

	private void pull_finished_cb () {
		status = Idle;
		progress = 1.0;
	}

	private string get_resource (string? graph, string[] types, string identifier, out bool created) throws GLib.Error {
		string inner_query;
		string select_query;
		string insert_query;
		Variant query_results;
		HashTable<string, string> anonymous_nodes;


		select_query = "";
		inner_query = "";
		created = false;

		foreach (string type in types) {
			inner_query += " a %s; ".printf (type);
		}
		inner_query += "nao:identifier \"%s\"".printf (identifier);

		select_query = "select ?urn where { ?urn %s }".printf (inner_query);

		try {
			Sparql.Cursor cursor = tracker.query (select_query);

			if (cursor.next ()) {
				return cursor.get_string (0);
			}
		} catch (Error tracker_error) {
			throw tracker_error;
		}

		if (graph == null) {
			insert_query = "insert { _:res %s }".printf (inner_query);
		} else {
			insert_query = "insert into <%s> { _:res %s }".printf (graph, inner_query);
		}

		try {
			created = true;
			query_results = tracker.update_blank (insert_query);
			anonymous_nodes = (HashTable<string, string>) query_results.get_child_value (0).get_child_value(0);
			return anonymous_nodes.lookup ("res");
		} catch (Error tracker_error) {
			throw tracker_error;
		}
	}

	public void update_triple_string (Sparql.Builder builder, string graph, string urn, string property, string new_value) {
		builder.delete_open (graph);
		builder.subject_iri (urn);
		builder.predicate (property);
		builder.object_string (new_value);
		builder.delete_close ();

		builder.insert_open (graph);
		builder.subject_iri (urn);
		builder.predicate (property);
		builder.object_string (new_value);
		builder.insert_close ();
	}

	public void update_triple_int64 (Sparql.Builder builder, string graph, string urn, string property, int64 new_value) {
		builder.delete_open (graph);
		builder.subject_iri (urn);
		builder.predicate (property);
		builder.object_int64 (new_value);
		builder.delete_close ();

		builder.insert_open (graph);
		builder.subject_iri (urn);
		builder.predicate (property);
		builder.object_int64 (new_value);
		builder.insert_close ();
	}

	public void shutdown () {
		set ("associated", false);
	}

	public override void authenticate () throws MinerWebError {
		PasswordProvider password_provider;
		string username;
		string password;
		bool verified = false;

		password_provider = PasswordProvider.get ();

		set ("associated", false);

		try {
			password = password_provider.get_password (MINER_NAME, out username);
		} catch (Error e) {
			if (e is PasswordProviderError.NOTFOUND) {
				throw new MinerWebError.NO_CREDENTIALS ("Miner is not associated");
			}
			throw new MinerWebError.KEYRING (e.message);
		}

		try {
			verified = service.authenticate (username, password, null);
		} catch (Error service_error) {
			throw new MinerWebError.SERVICE (service_error.message);
		}

		if (!verified) {
			throw new MinerWebError.WRONG_CREDENTIALS ("Wrong username and/or password");
		}

		message ("Authentication successful");
		set ("associated", true);
	}

	public override void dissociate () throws MinerWebError {
		var password_provider = PasswordProvider.get ();

		try {
			password_provider.forget_password (MINER_NAME);
		} catch (Error e) {
			if (e is PasswordProviderError.SERVICE) {
				throw new MinerWebError.KEYRING (e.message);
			}

			critical ("Internal error: %s", e.message);
			return;
		}

		set ("associated", false);
	}

	public override void associate (HashTable<string, string> association_data) throws Tracker.MinerWebError {
		assert (association_data.lookup ("username") != null && association_data.lookup ("password") != null);

		var password_provider = PasswordProvider.get ();

		try {
			password_provider.store_password (MINER_NAME,
			                                  SERVICE_DESCRIPTION,
			                                  association_data.lookup ("username"),
			                                  association_data.lookup ("password"));
		} catch (Error e) {
			if (e is PasswordProviderError.SERVICE) {
				throw new MinerWebError.KEYRING (e.message);
			}

			critical ("Internal error: %s", e.message);
			return;
		}
	}

	public override GLib.HashTable get_association_data () throws Tracker.MinerWebError {
		return new HashTable<string, string>(str_hash, str_equal);
	}

//	public async void writeback (string uri) {
//		weak PtrArray results;
//		weak string[][] triples;
//		GData.Query query;
//		GData.PicasaWebFile file;
//		string[] local_tags = new string[] {};
//
//		try {
//			results = yield execute_sparql (("select ?photo_id ?tag where {"
//			                               + "<%s> nie:dataSource <%s> ;"
//			                               +      "nao:identifier ?photo_id ;"
//			                               +      "nao:hasTag ?t ."
//			                               + " ?t  nao:prefLabel ?tag }").printf (uri, DATASOURCE_URN));
//		} catch (Error tracker_error) {
//			warning ("Tracker error while doing writeback for photo %s: %s", uri, tracker_error.message);
//			return;
//		}
//
//		message ("writeback for %s", uri);
//
//		if (results.len == 0)
//			return;
//
//		triples = (string[][])results.pdata;
//
//		for (uint i = 0 ; i < results.len ; ++i) {
//			// See http://code.google.com/apis/picasaweb/docs/2.0/developers_guide_protocol.html#Tags
//			local_tags += triples[i][1].replace (",", "%2C");
//		}
//
//		try {
//			file = service.query_single_entry (triples[0][0], null, typeof (GData.PicasaWebFile), null) as GData.PicasaWebFile;
//		} catch (Error gdata_error) {
//			warning ("GData error while doing writeback for photo %s: %s", uri, gdata_error.message);
//			return;
//		}
//
//		if (file == null) {
//			warning ("Could not writeback photo %s: file not found on remote service", uri);
//			return;
//		}
//
//		file.tags = local_tags;
//
//		try {
//			service.update_entry (file, null);
//		} catch (Error update_error) {
//			warning ("Could not writeback photo %s: %s", uri, update_error.message);
//		}
//	}
}

} // End namespace Tracker
