//
// Copyright 2010, Martyn Russell <martyn@lanedo.com>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.
//

using Gtk;

public class Tracker.View : ScrolledWindow {
	public enum Display {
		NO_RESULTS,
		CATEGORIES,
		FILE_LIST,
		FILE_ICONS
	}

	public Display display {
		get;
		private set;
	}

	public Query.Type query_type {
		get; set;
	}

	public ListStore store {
		get;
		private set;
	}

	public Tracker.View view {
		get; set;
	}

	// So we have 1 cursor per range of results 1->50
	private Tracker.Sparql.Cursor[] cursors = null;

	private Widget view = null;

	public View (Display? _display = Display.NO_RESULTS, ListStore? _store) {
		set_policy (PolicyType.NEVER, PolicyType.AUTOMATIC);

		display = _display;

		// Default to this
		find = Find.IN_TITLES;

		if (_store != null) {
			store = _store;
		} else {
			// Setup treeview
			store = new ListStore (10,
			                       typeof (Gdk.Pixbuf),  // Icon small
			                       typeof (Gdk.Pixbuf),  // Icon big
			                       typeof (string),      // URN
			                       typeof (string),      // URL
			                       typeof (string),      // Title
			                       typeof (string),      // Subtitle
			                       typeof (string),      // Column 2
			                       typeof (string),      // Column 3
			                       typeof (string),      // Tooltip
			                       typeof (bool));       // Category hint
		}

		switch (display) {
		case Display.NO_RESULTS:
			Label l;

			l = new Label ("");

			string message = _("No Search Results");
			string markup = @"<big>$message</big>";
			
			l.set_use_markup (true);
			l.set_markup (markup);

			view = l;
			break;

		case Display.CATEGORIES:
		case Display.FILE_LIST:
			view = new TreeView ();
			break;

		case Display.FILE_ICONS:
			view = new IconView ();
			break;
		}

		if (display == Display.NO_RESULTS) {
			add_with_viewport (view);
		} else {
			add (view);
			setup_model ();
		}

		base.show_all ();
	}

	public ~View() {
		// FIXME: clean up array of cursors
	}

	private void setup_model () {
		switch (display) {
		case Display.FILE_ICONS: {
			IconView iv = (IconView) view;

			iv.set_model (store);
			iv.set_item_width (96);
			iv.set_selection_mode (SelectionMode.SINGLE);
			iv.set_pixbuf_column (1);
			iv.set_text_column (4);

			break;
		}

		case Display.FILE_LIST: {
			TreeViewColumn col;
			TreeView tv = (TreeView) view;

			tv.set_model (store);
			tv.set_tooltip_column (8);
			tv.set_rules_hint (false);
			tv.set_grid_lines (TreeViewGridLines.VERTICAL);
			tv.set_headers_visible (true);

			var renderer1 = new CellRendererPixbuf ();
			var renderer2 = new Tracker.CellRendererText ();

			col = new TreeViewColumn ();
			col.pack_start (renderer1, false);
			col.add_attribute (renderer1, "pixbuf", 0);
			renderer1.xpad = 5;
			renderer1.ypad = 5;

			col.pack_start (renderer2, true);
			col.add_attribute (renderer2, "text", 4);
			renderer2.ellipsize = Pango.EllipsizeMode.MIDDLE;
			renderer2.show_fixed_height = false;

			col.set_title (_("File"));
			col.set_resizable (true);
			col.set_expand (true);
			col.set_sizing (TreeViewColumnSizing.AUTOSIZE);
			col.set_cell_data_func (renderer1, cell_renderer_func);
			col.set_cell_data_func (renderer2, cell_renderer_func);
			tv.append_column (col);

			var renderer3 = new Tracker.CellRendererText ();
			col = new TreeViewColumn ();
			col.pack_start (renderer3, true);
			col.add_attribute (renderer3, "text", 6);
			col.set_title (_("Last Changed"));
			col.set_cell_data_func (renderer3, cell_renderer_func);
			tv.append_column (col);

			var renderer4 = new Tracker.CellRendererText ();
			col = new TreeViewColumn ();
			col.pack_start (renderer4, true);
			col.add_attribute (renderer4, "text", 7);
			col.set_title (_("Size"));
			col.set_cell_data_func (renderer4, cell_renderer_func);
			tv.append_column (col);

			break;
		}

		case Display.CATEGORIES: {
			TreeViewColumn col;
			TreeView tv = (TreeView) view;

			tv.set_model (store);
			tv.set_tooltip_column (8);
			tv.set_rules_hint (false);
			tv.set_grid_lines (TreeViewGridLines.NONE);
			tv.set_headers_visible (false);

			var renderer1 = new CellRendererPixbuf ();
			var renderer2 = new Tracker.CellRendererText ();

			col = new TreeViewColumn ();
			col.pack_start (renderer1, false);
			col.add_attribute (renderer1, "pixbuf", 0);
			renderer1.xpad = 5;
			renderer1.ypad = 5;

			col.pack_start (renderer2, true);
			col.add_attribute (renderer2, "text", 4);
			col.add_attribute (renderer2, "subtext", 5);
			renderer2.ellipsize = Pango.EllipsizeMode.MIDDLE;
			renderer2.show_fixed_height = true;

			col.set_title (_("Item"));
			col.set_resizable (true);
			col.set_expand (true);
			col.set_sizing (TreeViewColumnSizing.AUTOSIZE);
			col.set_cell_data_func (renderer1, cell_renderer_func);
			col.set_cell_data_func (renderer2, cell_renderer_func);
			tv.append_column (col);

//			var renderer3 = new Tracker.CellRendererText ();
//			col = new TreeViewColumn ();
//			col.pack_start (renderer3, true);
//			col.add_attribute (renderer3, "text", 6);
//			col.set_title (_("Item Detail"));
//			col.set_cell_data_func (renderer3, cell_renderer_func);
//			tv.append_column (col);

			var renderer4 = new Tracker.CellRendererText ();
			col = new TreeViewColumn ();
			col.pack_start (renderer4, true);
			col.add_attribute (renderer4, "text", 7);
			col.set_title (_("Size"));
			col.set_cell_data_func (renderer4, cell_renderer_func);
			tv.append_column (col);

			break;
		}
		}
	}

	private void get_cursor_for_index (int index)
	requires (index >= 0) {
		// So index is the real index in the list.
		try {
			cursor = yield query.perform_async (query_type);

			if (cursor == null) {
				search_finished (store);
				return;
			}

			while (true) {
				success = yield cursor.next_async ();
				if (!success) {
					break;
				}
			}

			// Debugging
//			for (int i = 0; i < cursor.n_columns; i++) {
//				if (i == 0) {
//					debug ("--> %s", cursor.get_string (i));
//				} else {
//					debug ("  --> %s", cursor.get_string (i));
//				}
//			}

			debug ("--> %s", cursor.get_string (1));

//				string urn = cursor.get_string (0);
//				string _file = cursor.get_string (1);
//				string title = cursor.get_string (2);
//				string _file_time = cursor.get_string (3);
//				string _file_size = cursor.get_string (4);
//				string tooltip = cursor.get_string (7);
//				Gdk.Pixbuf pixbuf_small = tracker_pixbuf_new_from_file (theme, _file, size_small, false);
//				Gdk.Pixbuf pixbuf_big = tracker_pixbuf_new_from_file (theme, _file, size_big, false);
//				string file_size = GLib.format_size_for_display (_file_size.to_int());
//				string file_time = tracker_time_format_from_iso8601 (_file_time);

				// FIXME: should optimise this a bit more, inserting 2 images into a list eek
//				store.append (out iter);
//				store.set (iter,
//					       0, pixbuf_small, // Small Image
//					       1, pixbuf_big,   // Large Image
//					       2, urn,          // URN
//					       3, _file,        // URL
//					       4, title,        // Title
//					       5, null,         // Subtitle
//					       6, file_time,    // Column2: Time
//					       7, file_size,    // Column3: Size
//					       8, tooltip,      // Tooltip
//					       -1);

		} catch (GLib.Error e) {
			warning ("Could not iterate query results: %s", e.message);
			search_finished (store);
			return;
		}
	}

	private void get_details () {
	}

	private void cell_renderer_func (CellLayout   cell_layout,
	                                 CellRenderer cell,
	                                 TreeModel    tree_model,
	                                 TreeIter     iter) {
		Gdk.Color color;
		Style style;
		string title = null;
		bool show_row_hint = false;

		tree_model.get (iter,
		                4, out title,
		                9, out show_row_hint,
		                -1);

		if (title == "...") {
			TreePath path = tree_model.get_path (iter);
			int index = path.get_indices ()[0];
			debug ("expose for item %d", index);
		}

		style = view.get_style ();

		color = style.base[StateType.SELECTED];
		int sum_normal = color.red + color.green + color.blue;
		color = style.base[StateType.NORMAL];
		int sum_selected = color.red + color.green + color.blue;
		color = style.text_aa[StateType.INSENSITIVE];

		if (sum_normal < sum_selected) {
			/* Found a light theme */
			color.red = (color.red + (style.white).red) / 2;
			color.green = (color.green + (style.white).green) / 2;
			color.blue = (color.blue + (style.white).blue) / 2;
		} else {
			/* Found a dark theme */
			color.red = (color.red + (style.black).red) / 2;
			color.green = (color.green + (style.black).green) / 2;
			color.blue = (color.blue + (style.black).blue) / 2;
		}

		// Set odd/even colours
		if (show_row_hint) {
//			((Widget) treeview).style_get ("odd-row-color", out color, null);
			cell.set ("cell-background-gdk", &color);
		} else {
//			((Widget) treeview).style_get ("even-row-color", out color, null);
			cell.set ("cell-background-gdk", null);
		}
	}
}

