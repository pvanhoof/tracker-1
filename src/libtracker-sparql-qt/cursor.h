/*
 * Copyright (C) 2011, Adrien Bustany <abustany@gnome.org>
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

#ifndef _TRACKER_SPARQL_QT_CURSOR_H

#include <QtCore>

typedef struct _TrackerSparqlCursor TrackerSparqlCursor;
typedef struct _GError GError;

namespace TrackerSparql {

class Connection;
class CursorPrivate;
class Error;

class Cursor {
public:
	enum ValueType {
		Unbound,
		Uri,
		String,
		Integer,
		Double,
		DateTime,
		BlankNode,
		Boolean
	};

	~Cursor();

	Cursor(const Cursor &other);
	Cursor& operator=(const Cursor &other);

	bool getBoolean(int column) const;
	double getDouble(int column) const;
	qint64 getInteger(int column) const;
	QString getString(int column) const;
	ValueType getValueType(int column) const;
	QString getVariableName(int column) const;
	bool isBound(int column) const;
	bool next();
	void rewind();
	int nColumns() const;

	bool valid() const;
	Error error() const;

private:
	friend class Connection;
	Cursor();
	Cursor(TrackerSparqlCursor *cursor, GError *error);

	QExplicitlySharedDataPointer<CursorPrivate> d;
};

}

#endif // _TRACKER_SPARQL_QT_CURSOR_H
