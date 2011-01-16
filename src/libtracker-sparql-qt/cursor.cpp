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

#include <tracker-sparql.h>

#include "cursor.h"

#include "common.h"
#include "error.h"

namespace TrackerSparql {
	class CursorPrivate : public QSharedData {
	public:
		CursorPrivate(TrackerSparqlCursor *cursor, GError *error);
		~CursorPrivate();

		TrackerSparqlCursor *cursor;
		Error error;
	};
} // namespace TrackerSparql

using namespace TrackerSparql;

CursorPrivate::CursorPrivate(TrackerSparqlCursor *cursor, GError *error)
	: QSharedData()
	, cursor(cursor)
	, error(error)
{
}

CursorPrivate::~CursorPrivate()
{
	if (cursor != 0) {
		g_object_unref(cursor);
	}
}

Cursor::Cursor()
	: d(new CursorPrivate(0, 0))
{
}

Cursor::Cursor(TrackerSparqlCursor *cursor, GError *error)
	: d(new CursorPrivate(cursor, error))
{
}

Cursor::Cursor(const Cursor &other)
	: d(other.d)
{
}

Cursor&
Cursor::operator=(const Cursor &other)
{
	return d = other.d, *this;
}

Cursor::~Cursor()
{
}

bool
Cursor::getBoolean(int column) const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", 0);

	return tracker_sparql_cursor_get_boolean(d->cursor, column);
}

double
Cursor::getDouble(int column) const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", 0.);

	return tracker_sparql_cursor_get_double(d->cursor, column);
}

qint64
Cursor::getInteger(int column) const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", 0);

	return tracker_sparql_cursor_get_integer(d->cursor, column);
}

QString
Cursor::getString(int column) const
{
	static const QString nullString;

	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", nullString);

	long size = -1;
	const gchar *data = tracker_sparql_cursor_get_string(d->cursor,
	                                                     column,
	                                                     &size);
	return QString::fromUtf8(data, size);
}

Cursor::ValueType
Cursor::getValueType(int column) const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", Unbound);

	return (ValueType)tracker_sparql_cursor_get_value_type(d->cursor, column);
}

QString
Cursor::getVariableName(int column) const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", 0);

	return QString::fromUtf8(tracker_sparql_cursor_get_variable_name(d->cursor,
	                                                                 column));
}

bool
Cursor::isBound(int column) const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", 0);

	return tracker_sparql_cursor_is_bound(d->cursor, column);
}

bool
Cursor::next()
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", 0);

	GError *error = 0;

	bool result = tracker_sparql_cursor_next(d->cursor, 0, &error);

	if (error) {
		d->error = Error(error);
		g_error_free(error);
	}

	return result;
}

void
Cursor::rewind()
{
	RETURN_IF_FAIL(valid(), "Cursor not valid");

	tracker_sparql_cursor_rewind(d->cursor);
}

int
Cursor::nColumns() const
{
	RETURN_VAL_IF_FAIL(valid(), "Cursor not valid", -1);

	return tracker_sparql_cursor_get_n_columns(d->cursor);
}

bool
Cursor::valid() const
{
	return (d->cursor != 0);
}

Error
Cursor::error() const
{
	return d->error;
}
