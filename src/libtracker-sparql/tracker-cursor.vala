/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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

public abstract class Tracker.Sparql.Cursor : Object {
	public Connection connection { get; set; }
	public abstract int n_columns { get; }
	public abstract unowned string get_string (int column, out int length = null);
	public virtual bool interrupt () throws GLib.Error {
		warning ("Interrupt interface called when not implemented");
		return false;
	}

	public abstract bool iter_next () throws GLib.Error;
	public abstract void rewind ();
}
