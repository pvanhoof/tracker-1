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

#include "connection.h"

#include "common.h"
#include "cursor.h"
#include "error.h"

namespace TrackerSparql {
	class ConnectionPrivate : public QSharedData {
	public:
		ConnectionPrivate();
		~ConnectionPrivate();

		TrackerSparqlConnection *connection;
		Error error;
	};
} // namespace TrackerSparql

using namespace TrackerSparql;

ConnectionPrivate::ConnectionPrivate()
	: QSharedData()
	, connection(0)
{
}

ConnectionPrivate::~ConnectionPrivate()
{
	if (connection) {
		g_object_unref(connection);
	}
}

Connection *Connection::m_instance = 0;
QMutex instanceMutex;

Connection
Connection::get()
{
	instanceMutex.lock();
	if (m_instance == 0) {
		g_type_init();
		m_instance = new Connection();
	}
	instanceMutex.unlock();

	return *m_instance;
}

Connection::Connection()
	: d(new ConnectionPrivate)
{
	GError *error = 0;

	d->connection = tracker_sparql_connection_get(0, &error);

	if (error) {
		d->error = Error(error);
		g_error_free(error);
	}
}

Connection::Connection(const Connection &other)
	: d(other.d)
{
}

Connection&
Connection::operator=(const Connection &other)
{
	return d = other.d, *this;
}

Connection::~Connection()
{
}

void
Connection::load(const QFile &file)
{
	RETURN_IF_FAIL(valid(), "Connection is not valid");

	GFile *gfile = g_file_new_for_path(file.fileName().toUtf8().constData());
	GError *error = 0;

	tracker_sparql_connection_load(d->connection, gfile, 0, &error);

	g_object_unref(gfile);

	if (error) {
		d->error = Error(error);
		g_error_free(error);
	}
}

Cursor
Connection::query(const QString &sparql)
{
	RETURN_VAL_IF_FAIL(valid(), "Connection is not valid", Cursor());

	GError *error = 0;
	TrackerSparqlCursor *cursor = tracker_sparql_connection_query(d->connection,
	                                                              sparql.toUtf8().constData(),
	                                                              0,
	                                                              &error);

	Cursor c(cursor, error);

	if (error) {
		g_error_free(error);
	}

	return c;
}

Cursor
Connection::statistics()
{
	RETURN_VAL_IF_FAIL(valid(), "Connection is not valid", Cursor());

	GError *error = 0;
	TrackerSparqlCursor *cursor = tracker_sparql_connection_statistics(d->connection,
	                                                                   0,
	                                                                   &error);

	Cursor c(cursor, error);

	if (error) {
		g_error_free(error);
	}

	return c;
}

void
Connection::update(const QString &sparql, int priority)
{
	RETURN_IF_FAIL(valid(), "Connection is not valid");

	GError *error = 0;
	tracker_sparql_connection_update(d->connection,
	                                 sparql.toUtf8().constData(),
	                                 priority,
	                                 0,
	                                 &error);

	if (error) {
		d->error = Error(error);
		g_error_free(error);
	}
}

QList<QList<QHash<QString, QString> > >
Connection::updateBlank(const QString &sparql, int priority)
{
	static const QList<QList<QHash<QString, QString> > > emptyResult;

	RETURN_VAL_IF_FAIL(valid(), "Connection is not valid", emptyResult);

	GError *error = 0;
	GVariant *result = tracker_sparql_connection_update_blank(d->connection,
	                                                          sparql.toUtf8().constData(),
	                                                          priority,
	                                                          0,
	                                                          &error);
	if (error) {
		d->error = Error(error);
		g_error_free(error);

		return emptyResult;
	}

	QList<QList<QHash<QString, QString> > > results;
	GVariantIter iter1, *iter2, *iter3;

	const gchar *node;
	const gchar *urn;

	g_variant_iter_init (&iter1, result);
	while (g_variant_iter_loop (&iter1, "aa{ss}", &iter2)) {
		QList<QHash<QString, QString> > innerList;

		while (g_variant_iter_loop (iter2, "a{ss}", &iter3)) {
			QHash<QString, QString> hash;

			while (g_variant_iter_loop (iter3, "{ss}", &node, &urn)) { /* {ss} */
				hash.insert(QString::fromUtf8(node), QString::fromUtf8(urn));
			}

			innerList.append(hash);
		}

		results.append(innerList);
	}

	g_variant_unref (result);

	return results;
}

bool
Connection::valid() const
{
	return (d->connection != 0);
}

Error
Connection::error() const
{
	return d->error;
}

const int Connection::HighPriority = G_PRIORITY_HIGH;
const int Connection::DefaultPriority = G_PRIORITY_DEFAULT;
const int Connection::HighIdlePriority = G_PRIORITY_HIGH_IDLE;
const int Connection::DefaultIdlePriority = G_PRIORITY_DEFAULT_IDLE;
const int Connection::LowPriority = G_PRIORITY_LOW;
