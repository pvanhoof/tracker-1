public class Tracker.Config : Tracker.ConfigFile {
	public enum DownloadBehaviour {
		GPRS, /* Download if connected via GPRS, EDGE, 3G or LAN */
		EDGE, /* Download if connected via EDGE, 3G or LAN */
		@3G,  /* Download if connected via 3G or LAN */
		LAN   /* Download only if connected to a LAN */
	}

	/* GLib.KeyFile defines */
	public static const string GROUP_GENERAL = "General";
	public static const string GROUP_NETWORK = "Network";

	private struct ConfigKey {
		GLib.Type type;
		string property;
		string group;
		string key;

		ConfigKey (GLib.Type type, string property, string group, string key) {
			this.type = type;
			this.property = property;
			this.group = group;
			this.key = key;
		}
	}

	static List<ConfigKey?> config_keys = new List<ConfigKey?> ();

	/* Default values */
	public static const uint DEFAULT_DOWNLOAD_BEHAVIOUR = (uint)DownloadBehaviour.@3G;

	private uint _download_behaviour = DEFAULT_DOWNLOAD_BEHAVIOUR;
	public uint download_behaviour {
		get {
			return _download_behaviour;
		}
		set {
			if (value < (uint)DownloadBehaviour.LAN || value > (uint)DownloadBehaviour.GPRS) {
				return;
			}
			_download_behaviour = value;
		}
	}

	static construct {
		config_keys.append(ConfigKey (typeof (int), "download-behaviour", GROUP_NETWORK, "DownloadBehaviour"));
	}

	construct {
		domain = "tracker-miner-gdata";
		base.constructed ();

		load (true);
	}

	public override void changed () {
		load (false);
	}

	public void load (bool use_defaults) {
		if (use_defaults) {
			create_with_defaults ();
		}

		if (!file_exists) {
			base.save ();
		}

		foreach (unowned ConfigKey key in config_keys) {
			if (key.type == typeof (int)) {
				KeyfileObject.load_int (this, key.property, key_file, key.group, key.key);
			} else {
				assert_not_reached ();
			}
		}
	}

	public new bool save () {
		if (key_file == null) {
			critical ("Could not save config, GKeyFile was NULL, has the config been loaded?");
			return false;
		}

		KeyfileObject.save_int (this, "download-behaviour", key_file, GROUP_NETWORK, "DownloadBehaviour");

		return base.save ();
	}

	private void create_with_defaults () {
		try {
			key_file.has_key (GROUP_NETWORK, "DownloadBehaviour");
		} catch (Error e) {
			if (!(e is KeyFileError.KEY_NOT_FOUND) && !(e is KeyFileError.GROUP_NOT_FOUND)) {
				critical ("Could not load config default: %s", e.message);
			} else {
				key_file.set_integer (GROUP_NETWORK, "DownloadBehaviour",
				                      DownloadBehaviour.@3G);
			}
		}
	}
}
