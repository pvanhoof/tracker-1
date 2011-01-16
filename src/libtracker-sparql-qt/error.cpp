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

#include "error.h"

#include <glib.h>

namespace TrackerSparql {
	class ErrorPrivate : public QSharedData {
	public:
		ErrorPrivate();

		int code;
		QString message;
	};
} // namespace TrackerSparql

using namespace TrackerSparql;

ErrorPrivate::ErrorPrivate()
	: QSharedData()
	, code(-1)
{
}

Error::Error()
	: d(new ErrorPrivate)
{
}

Error::Error(GError *error)
	: d(new ErrorPrivate)
{
	if (error) {
		d->code = error->code;
		d->message = QString::fromUtf8(error->message);
	}
}

Error::Error(const Error &other)
	: d(other.d)
{
}

Error&
Error::operator=(const Error &other)
{
	return d = other.d, *this;
}

Error::~Error()
{
}

int
Error::code() const
{
	return d->code;
}

QString
Error::message() const
{
	return d->message;
}

bool
Error::valid() const
{
	// Can we have GError with negative error code?
	return (d->code != -1);
}
