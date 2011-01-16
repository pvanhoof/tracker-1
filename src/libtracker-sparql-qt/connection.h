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

#ifndef _TRACKER_SPARQL_QT_CONNECTION_H

#include <QtCore>

namespace TrackerSparql {

class ConnectionPrivate;
class Cursor;
class Error;

class Connection {
public:
	static Connection get();

	Connection(const Connection &other);
	Connection& operator=(const Connection &other);

	~Connection();

	static const int HighPriority;
	static const int DefaultPriority;
	static const int HighIdlePriority;
	static const int DefaultIdlePriority;
	static const int LowPriority;

	void load(const QFile &file);
	Cursor query(const QString &sparql);
	Cursor statistics();
	void update(const QString &sparql, int priority = DefaultPriority);
	QList<QList<QHash<QString, QString> > > updateBlank(const QString &sparql, int priority = DefaultPriority);

	bool valid() const;
	Error error() const;

private:
	Connection();

	QExplicitlySharedDataPointer<ConnectionPrivate> d;
	static Connection *m_instance;
};

} // namespace TrackerSparql

#endif // _TRACKER_SPARQL_QT_CONNECTION_H
