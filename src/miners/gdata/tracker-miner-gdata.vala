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

public class MinerGData {
	private static MinerPicasaweb miner_picasaweb;
	private static const string NMM_PHOTO = "http://www.tracker-project.org/temp/nmm#Photo";

	private static MainLoop main_loop;

	private static bool in_loop = false;
	private static void signal_handler (int signo) {
		if (in_loop) {
			Posix.exit (Posix.EXIT_FAILURE);
		}

		switch (signo) {
			case Posix.SIGINT:
			case Posix.SIGTERM:
				in_loop = true;
				main_loop.quit ();
				break;
		}
	}

	private static void init_signals () {
#if G_OS_WIN32
#else
		Posix.sigaction_t act = Posix.sigaction_t ();
		Posix.sigset_t    empty_mask = Posix.sigset_t ();
		Posix.sigemptyset (empty_mask);
		act.sa_handler = signal_handler;
		act.sa_mask    = empty_mask;
		act.sa_flags   = 0;

		Posix.sigaction (Posix.SIGTERM, act, null);
		Posix.sigaction (Posix.SIGINT, act, null);
#endif
	}

//	private static void writeback_cb (GLib.HashTable<string, void*> resources) {
//		List<weak string> uris = (List<weak string>)resources.get_keys ();
//		weak string[] rdf_classes;
//
//		foreach (string uri in uris) {
//			rdf_classes = (string[])resources.lookup (uri);
//
//			for (uint i = 0 ; rdf_classes[i] != null ; i++) {
//				if (rdf_classes[i] == NMM_PHOTO) {
//					//miner_picasaweb.writeback (uri);
//					return;
//				}
//			}
//		}
//	}

	public static void main (string[] args) {
		Environment.set_application_name ("GData tracker miner");
		miner_picasaweb = new MinerPicasaweb ();

		init_signals ();

		main_loop = new MainLoop (null, false);
		main_loop.run ();

		miner_picasaweb.shutdown ();
	}
}

} // End namespace Tracker
