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

#ifndef _TRACKER_SPARQL_QT_ERROR_H

#include <QtCore>

typedef struct _GError GError;

namespace TrackerSparql {

class ErrorPrivate;

class Error {
public:
	Error();
	Error(GError *error);
	Error(const Error &other);
	Error& operator=(const Error &other);
	~Error();

	int code() const;
	QString message() const;

	bool valid() const;

private:
	QSharedDataPointer<ErrorPrivate> d;
};

} // namespace TrackerSparql

#endif // _TRACKER_SPARQL_QT_ERROR_H
