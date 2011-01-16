#ifndef _TRACKER_SPARQL_QT_COMMON_H

#include <QtGlobal>

#define RETURN_VAL_IF_FAIL(assertion, message, value) \
	if (not assertion) { \
		qWarning("%s: %s", Q_FUNC_INFO, message); \
		return value; \
	}

#define RETURN_IF_FAIL(assertion,message) \
	RETURN_VAL_IF_FAIL(assertion,message,)

#endif // _TRACKER_SPARQL_QT_COMMON_H
