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

#include <tracker-sparql-qt.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

using namespace TrackerSparql;

static void
usage(const char *argv0)
{
	printf("Usage: %s [OPTIONS...]\n\n"
	       "Options:\n"
	       "  -h, --help    Show this message\n"
	       "  -u, --update  Run an update query (use for INSERT and DELETE)\n"
	       "  -f, --file    Read the query from a file, not from stdin\n",
	       argv0);
}

int
main(int argc, char **argv)
{
	int updateFlag = 0;
	char *filePath = 0;

	{ // Option parsing
		static struct option options[] = {
			{"update", no_argument, 0, 'u'},
			{"help", no_argument, 0, 'h'},
			{"file", required_argument, 0, 'f'}
		};

		int c;

		while ((c = getopt_long(argc, argv, "uhf:", options, 0)) != -1) {
			switch (c) {
				case 0:
					break;
				case 'f':
					filePath = strdup(optarg);
					break;
				case 'h':
					usage(argv[0]);
					return 1;
				case 'u':
					updateFlag = 1;
					break;
				default:
					return 1;
			}
		}
	} // Option parsing

	QString sparql;

	if (filePath) {
		QFile file(QString::fromUtf8(filePath));
		free (filePath);

		if (not file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			fprintf(stderr, "File %s does not exist or cannot be read\n",
			                qPrintable(file.fileName()));
			return 1;
		}

		sparql = file.readAll();

		file.close();
	} else {
		if (optind >= argc) {
			usage(argv[0]);
			return 1;
		}

		sparql = QString::fromUtf8(argv[optind]);
	}

	Connection connection = Connection::get();

	if (not connection.valid()) {
		fprintf(stderr, "Couldn't connect to Tracker: %s (Error code %d)\n",
		                qPrintable(connection.error().message()),
		                connection.error().code());
		return 1;
	}

	if (updateFlag) {
		connection.update(sparql);

		if (connection.error().valid()) {
			fprintf(stderr, "Error while running query: %s (Error code %d)\n",
			                qPrintable(connection.error().message()),
			                connection.error().code());
			return 1;
		}
	} else {
		Cursor cursor = connection.query(sparql);

		if (cursor.error().valid()) {
			fprintf(stderr, "Error while running query: %s (Error code %d)\n",
			                qPrintable(cursor.error().message()),
			                cursor.error().code());
			return 1;
		}

		while (cursor.next()) {
			for (int i = 0; i < cursor.nColumns(); ++i) {
				printf("%s%s", qPrintable(cursor.getString(i)),
				               (i == cursor.nColumns() - 1) ? "\n" : ",");
			}
		}
	}

	return 0;
}
