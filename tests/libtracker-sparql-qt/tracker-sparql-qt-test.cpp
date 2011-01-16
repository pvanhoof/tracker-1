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

#include <glib.h>

#include "tracker-sparql-qt-test.h"

#include <tracker-sparql-qt.h>

#define VERIFY(assertion) \
	if (not assertion) { \
		qFatal("Assertion %s failed", #assertion); \
	}

#define COMPARE(a, b) \
	if (a != b) { \
		qFatal("Expected %s == %s, got %s", #a, #b, qPrintable(QVariant(a).toString())); \
	}

using namespace TrackerSparql;

void
test_tracker_sparql_qt_error()
{
	Error e;

	VERIFY(not e.valid());
	COMPARE(e.code(), -1);
	COMPARE(e.message(), QString());

	GError dummyError;
	dummyError.code = 42;
	dummyError.message = g_strdup("Dummy error");

	e = Error(&dummyError);

	COMPARE(e.code(), 42);
	COMPARE(e.message(), QString::fromLatin1("Dummy error"));

	Error ee = e;
	COMPARE(e.code(), ee.code());
	COMPARE(e.message(), ee.message());
}
