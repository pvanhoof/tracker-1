/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Tracker - indexer and metadata database engine
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <string.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-xesam-query.h"


/* XESAM Query Condition
<query>
	<and>
        	<greaterThan>
            		<field name="File:Size" />
            		<integer>1000000<integer>
          	</greaterThan>
          	<equals>
             		<field name="File:Path" />
             		<string>/home/jamie<string>
           	</equals>
	</and>
</Condition>
*/


/* main elements */
#define ELEMENT_XESAM_QUERY 	        "query"
#define ELEMENT_XESAM_USER_QUERY        "userQuery"
#define ELEMENT_XESAM_FIELD 		"field"

/* operators */
#define ELEMENT_XESAM_AND 		"and"
#define ELEMENT_XESAM_OR	 	"or"
#define ELEMENT_XESAM_EQUALS 		"equals"
#define ELEMENT_XESAM_GREATER_THAN	"greaterThan"
#define ELEMENT_XESAM_GREATER_OR_EQUAL	"greaterOrEqual"
#define ELEMENT_XESAM_LESS_THAN 	"lessThan"
#define ELEMENT_XESAM_LESS_OR_EQUAL	"lessOrEqual"

/* extension operators - "contains" does a substring or full text match, "in_Set" does string in list match */
#define ELEMENT_XESAM_CONTAINS 		"contains"
#define ELEMENT_XESAM_REGEX        	"regex"
#define ELEMENT_XESAM_STARTS_WITH 	"startsWith"
#define ELEMENT_XESAM_IN_SET		"inSet"

/* types */
#define ELEMENT_XESAM_INTEGER 		"integer"
#define ELEMENT_XESAM_DATE 		"date"
#define ELEMENT_XESAM_STRING 		"string"
#define ELEMENT_XESAM_FLOAT 		"float"
#define ELEMENT_XESAM_BOOLEAN           "boolean"

#define ELEMENT_IS(name) (strcmp (element_name, (name)) == 0)


enum {
	NO_ERROR,
	PARSE_ERROR,
};


typedef enum {
	STATE_START,
	STATE_QUERY,
	STATE_END_QUERY,
	STATE_USER_QUERY,
	STATE_END_USER_QUERY,
	STATE_FIELD,
	STATE_AND,
	STATE_END_AND,
	STATE_OR,
	STATE_END_OR,
	STATE_EQUALS,
	STATE_END_EQUALS,
	STATE_GREATER_THAN,
	STATE_END_GREATER_THAN,
	STATE_GREATER_OR_EQUAL,
	STATE_END_GREATER_OR_EQUAL,
	STATE_LESS_THAN,
	STATE_END_LESS_THAN,
	STATE_LESS_OR_EQUAL,
	STATE_END_LESS_OR_EQUAL,
	STATE_CONTAINS,
	STATE_END_CONTAINS,
	STATE_REGEX,
	STATE_END_REGEX,
	STATE_STARTS_WITH,
	STATE_END_STARTS_WITH,
	STATE_IN_SET,
	STATE_END_IN_SET,
	STATE_INTEGER,
	STATE_END_INTEGER,
	STATE_STRING,
	STATE_END_STRING,
	STATE_FLOAT,
	STATE_END_FLOAT,
	STATE_DATE,
	STATE_END_DATE,
	STATE_BOOLEAN,
	STATE_END_BOOLEAN
} ParseState;


typedef enum {
	OP_NONE,
	OP_EQUALS,
	OP_GREATER,
	OP_GREATER_EQUAL,
	OP_LESS,
	OP_LESS_EQUAL,
	OP_CONTAINS,
	OP_REGEX,
	OP_SET,
	OP_STARTS
} Operators;


typedef enum {
	LOP_NONE,
	LOP_AND,
	LOP_OR
} LogicOperators;


typedef struct {
	GMarkupParseContext 	*context;
	GMarkupParser       	*parser;
	GSList 			*stack;
	GSList 			*fields;
	gboolean		query_okay;
	int			statement_count;
	LogicOperators		current_logic_operator;
	Operators		current_operator;
	char 			*current_field;
	char			*current_value;
	DBConnection	        *db_con;
	GString			*sql_from;
	GString			*sql_where;
	char			*service;
} ParserData;

static GQuark error_quark;

static void start_element_handler (GMarkupParseContext *context,
				   const gchar *element_name,
				   const gchar **attribute_names,
				   const gchar **attribute_values,
				   gpointer user_data,
				   GError **error);

static void end_element_handler (GMarkupParseContext *context,
				 const gchar *element_name,
				 gpointer user_data,
				 GError **error);

static void text_handler (GMarkupParseContext *context,
		     const gchar *text,
		     gsize text_len,
		     gpointer user_data,
		     GError **error);

static void error_handler (GMarkupParseContext *context,
		      GError *error,
		      gpointer user_data);


static gboolean
is_operator (ParseState state)
{
	return state == STATE_EQUALS || state == STATE_GREATER_THAN || state == STATE_LESS_THAN ||
			state == STATE_CONTAINS || state == STATE_IN_SET || STATE_LESS_OR_EQUAL ||
			STATE_GREATER_OR_EQUAL || state == STATE_STARTS_WITH || state == STATE_REGEX;

}


static gboolean
is_end_operator (ParseState state)
{
	return state == STATE_END_EQUALS || state == STATE_END_GREATER_THAN || state == STATE_END_LESS_THAN ||
		        state == STATE_END_CONTAINS || state == STATE_END_IN_SET || STATE_END_LESS_OR_EQUAL ||
			STATE_END_GREATER_OR_EQUAL || state == STATE_END_STARTS_WITH || state == STATE_END_REGEX;

}


static gboolean
is_logic (ParseState state)
{
	return state == STATE_AND || state == STATE_OR;
}


static gboolean
is_end_logic (ParseState state)
{
	return state == STATE_END_AND || state == STATE_END_OR;
}


static void
set_error (GError              **err,
           GMarkupParseContext  *context,
           int                   error_code,
           const char           *format,
           ...)
{
	int	line, ch;
	va_list args;
	char    *str;

	g_markup_parse_context_get_position (context, &line, &ch);

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_set_error (err, error_quark, error_code, "Line %d character %d: %s", line, ch, str);

	g_free (str);
}


static gboolean
set_error_on_fail (gboolean condition, GMarkupParseContext *context, const char *msg, GError **err)
{
	if (!condition) {
		set_error (err, context, 1, msg);
		return TRUE;
	}

	return FALSE;
}


static const char *
get_attribute_value (const char *name,
		     const char **names,
		     const char **values)
{
	int i;

	i = 0;

	while (names[i]) {
		if (strcmp (name, names[i]) == 0) {
			return values[i];
		}
		i++;
	}

	return NULL;
}


static const char *
get_attribute_value_required (GMarkupParseContext *context,
			      const char          *tag,
			      const char          *name,
			      const char          **names,
			      const char          **values,
			      GError              **error)
{
	const char *value;

	value = get_attribute_value (name, names, values);

	if (!value) {
		set_error (error, context, PARSE_ERROR,
			   "%s must have \"%s\" attribute",
			   tag, name);
	}

	return value;
}


static void
push_stack (ParserData *data, ParseState  state)
{
	data->stack = g_slist_prepend (data->stack, GINT_TO_POINTER (state));
}


static void
pop_stack (ParserData *data)
{
	g_return_if_fail (data->stack != NULL);

	data->stack = g_slist_remove (data->stack, data->stack->data);
}


static ParseState
peek_state (ParserData *data)
{
	g_return_val_if_fail (data->stack != NULL, STATE_START);

	return GPOINTER_TO_INT (data->stack->data);
}


static void
pop_stack_until (ParserData *data, ParseState state)
{
	while (data->stack != NULL) {

		if (state == peek_state (data)) {
			pop_stack (data);
			break;
		}

		pop_stack (data);
	}
}



static GList *
add_metadata_field (ParserData *data, const char *xesam_name, gboolean is_select, gboolean is_condition)
{
	gboolean           field_exists;
	FieldData          *field_data;
	const GSList       *tmp;
	GList              *reply=NULL;
	TrackerDBResultSet *result_set;
	gboolean valid = TRUE;

	field_exists = FALSE;
	field_data = NULL;

	// Do the xesam mapping
	
	result_set = tracker_get_xesam_metadata_names (data->db_con, xesam_name);
	if (!result_set) {
		return NULL;
	}

	while (valid) {
		gchar *field_name;
		
		tracker_db_result_set_get (result_set, 0, &field_name, -1);

		// check if field is already in list
		for (tmp = data->fields; tmp; tmp = tmp->next) {
			FieldData *tmp_field;
			
			tmp_field = tmp->data;
			
			if (tmp_field && tmp_field->field_name) {
				if (strcasecmp (tmp_field->field_name, field_name) == 0) {
					
					field_data = tmp_field;
					
					field_exists = TRUE;
					
					if (is_condition) {
						field_data->is_condition = TRUE;
					} 
					
					if (is_select) {
						if (!field_data->is_select) {
							
							field_data->is_select = TRUE;
							//g_string_append_printf (data->sql_select, ", %s", field_data->select_field);
						}
						
					}
					
					break;
					
				}
			}
		}
		
		
		
		if (!field_exists) {
			
			field_data = tracker_db_get_metadata_field (data->db_con, data->service, field_name, g_slist_length (data->fields), is_select, is_condition);
			if (field_data) {
				data->fields = g_slist_prepend (data->fields, field_data);
				if (is_select) {
					//					g_string_append_printf (data->sql_select, ", %s", field_data->select_field);
				}
				
			} 
		} 
		
		reply = g_list_append(reply, field_data);

		valid = tracker_db_result_set_iter_next (result_set);
		g_free (field_name);
	}
	
	return reply;
}


static void
start_element_handler (GMarkupParseContext *context,
		       const gchar	   *element_name,
		       const gchar	   **attribute_names,
		       const gchar	   **attribute_values,
		       gpointer		   user_data,
		       GError		   **error)
{
	ParserData *data;
	ParseState state;

	data = user_data;
	state = peek_state (data);

	if (ELEMENT_IS (ELEMENT_XESAM_QUERY)) {
		const char *content;
		const char *source;

		if (set_error_on_fail ((state == STATE_START), context, "Query element not expected here", error)) {
			return;
		}

		content = get_attribute_value ("content", attribute_names, attribute_values);
		source = get_attribute_value ("source", attribute_names, attribute_values);

		/* FIXME This is a bit clumsy, check that OK and get the defaults (all) from somewhere. CHECK MEMORY LEAKS! */
		if(content) {
			TrackerDBResultSet  *result_set;
			gboolean            valid = TRUE;
			result_set = tracker_get_xesam_service_names (data->db_con, content);
		 
			if (result_set) {
				
				while (valid) {

				}

			}
			
			content = strdup(content);
		} else {
			content = strdup("Files");
		}
		
		// FIXME Fix the service problems.
		data->service = strdup("Files");

		if(source) {

		} else {
			// FIXME
			source = "Files";
		}

		g_string_append_printf (data->sql_where, "\n WHERE (S.ServiceTypeID in (select TypeId from ServiceTypes where TypeName = '%s' or Parent = '%s')) AND ", content, source);

		push_stack (data, STATE_QUERY);

	} else if (ELEMENT_IS (ELEMENT_XESAM_FIELD)) {
		const char *name;

		if (set_error_on_fail ( is_operator (state), context,  "Field element not expected here", error)) {
			return;
		}

		name = get_attribute_value_required (context, "<field>", "name",
						     attribute_names, attribute_values,
						     error);

		if (!name) {
			return;
		} else {

			if (data->current_operator == OP_NONE ) {
				set_error (error, context, PARSE_ERROR, "no operator found for field \"%s\"", name);
				return;
			}

			data->current_field =  g_strdup (name);

			push_stack (data, STATE_FIELD);
		}

	} else if (ELEMENT_IS (ELEMENT_XESAM_AND)) {
                const char *negate;

		if (set_error_on_fail ((state == STATE_QUERY || is_logic (state) || is_end_logic (state) || is_end_operator (state)),
				       context, "AND element not expected here", error)) {
			return;
		}

		if (data->statement_count > 1) {
		  if (data->current_logic_operator == LOP_AND) {
		    data->sql_where = g_string_append (data->sql_where, " AND ");
		  } else {
		    if (data->current_logic_operator == LOP_OR) {
		      data->sql_where = g_string_append (data->sql_where, " OR ");
		    }
		  }
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->statement_count = 0;
		data->sql_where = g_string_append (data->sql_where, " ( ");
		data->current_logic_operator = LOP_AND;
		push_stack (data, STATE_AND);

	} else if (ELEMENT_IS (ELEMENT_XESAM_OR)) {
                const char *negate;

		if (set_error_on_fail ((state == STATE_QUERY || is_logic (state) || is_end_logic (state) || is_end_operator (state)),
				       context, "OR element not expected here", error)) {
			return;
		}

		if (data->statement_count > 1) {
			if (data->current_logic_operator == LOP_AND) {
				data->sql_where = g_string_append (data->sql_where, " AND ");
			} else {
				if (data->current_logic_operator == LOP_OR) {
					data->sql_where = g_string_append (data->sql_where, " OR ");
				}
			}
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->statement_count = 0;
		data->sql_where = g_string_append (data->sql_where, " ( ");
		data->current_logic_operator = LOP_OR;
		push_stack (data, STATE_OR);

	} else if (ELEMENT_IS (ELEMENT_XESAM_EQUALS)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "EQUALS element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_EQUALS;
		push_stack (data, STATE_EQUALS);

	} else if (ELEMENT_IS (ELEMENT_XESAM_GREATER_THAN)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context,  "GREATERTHAN element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_GREATER;
		push_stack (data, STATE_GREATER_THAN);

	} else if (ELEMENT_IS (ELEMENT_XESAM_GREATER_OR_EQUAL)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "GREATEROREQUAL element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_GREATER_EQUAL;
		push_stack (data, STATE_GREATER_OR_EQUAL);

	} else if (ELEMENT_IS (ELEMENT_XESAM_LESS_THAN )) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "LESSTHAN element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_LESS;
		push_stack (data, STATE_LESS_THAN);

	} else if (ELEMENT_IS (ELEMENT_XESAM_LESS_OR_EQUAL )) {
                const char *negate;


		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "LESSOREQUAL element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_LESS_EQUAL;
		push_stack (data, STATE_LESS_OR_EQUAL);

	} else if (ELEMENT_IS (ELEMENT_XESAM_CONTAINS)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "CONTAINS element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_CONTAINS;
		push_stack (data, STATE_CONTAINS);

	} else if (ELEMENT_IS (ELEMENT_XESAM_REGEX)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "REGEX element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_REGEX;
		push_stack (data, STATE_REGEX);

	} else if (ELEMENT_IS (ELEMENT_XESAM_STARTS_WITH)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "STARTSWITH element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_STARTS;
		push_stack (data, STATE_STARTS_WITH);

	} else if (ELEMENT_IS (ELEMENT_XESAM_IN_SET)) {
                const char *negate;

		if (set_error_on_fail ( state == STATE_QUERY || is_logic (state) ||
					((data->current_logic_operator == LOP_AND || data->current_logic_operator == LOP_OR)
					 && is_end_operator (state)),
					context, "IN SET element not expected here", error)) {
			return;
		}

		negate = get_attribute_value ("negate", attribute_names, attribute_values);
		if (negate&&(!strcmp(negate,"true")))
		{
		  data->sql_where = g_string_append (data->sql_where, " NOT "); 
		}

		data->current_operator = OP_SET;
		push_stack (data, STATE_IN_SET);


	} else if (ELEMENT_IS (ELEMENT_XESAM_INTEGER)) {

		if (set_error_on_fail (state == STATE_FIELD, context, "INTEGER element not expected here", error)) {
			return;
		}

		push_stack (data, STATE_INTEGER);


	} else if (ELEMENT_IS (ELEMENT_XESAM_DATE)) {

		if (set_error_on_fail (state == STATE_FIELD, context, "DATE element not expected here", error)) {
			return;
		}

		push_stack (data, STATE_DATE);


	} else if (ELEMENT_IS (ELEMENT_XESAM_STRING)) {

		if (set_error_on_fail (state == STATE_FIELD, context, "STRING element not expected here", error)) {
			return;
		}

		push_stack (data, STATE_STRING);

	} else if (ELEMENT_IS (ELEMENT_XESAM_FLOAT)) {

		if (set_error_on_fail (state == STATE_FIELD, context, "FLOAT element not expected here", error)) {
			return;
		}

		push_stack (data, STATE_FLOAT);

	} else if (ELEMENT_IS (ELEMENT_XESAM_BOOLEAN)) {

		if (set_error_on_fail (state == STATE_FIELD, context, "BOOLEAN element not expected here", error)) {
			return;
		}

		push_stack (data, STATE_BOOLEAN);
	}
}


static char *
get_value (const char *value, gboolean quote)
{
	if (quote) {
		return g_strconcat (" '", value, "' ", NULL);
	} else {
		return g_strdup (value);
	}
}


static gboolean
build_sql (ParserData *data)
{
	ParseState state;
	char 	   *avalue, *value, *sub;
	GList      *field_data = NULL;
	GList      *field_data_list = NULL;
	GString    *str;
	int        i=0;

	g_return_val_if_fail (data->current_field && data->current_operator != OP_NONE && data->current_value, FALSE);

	data->statement_count++;

	state = peek_state (data);

	avalue = get_value (data->current_value, (state != STATE_END_DATE && state != STATE_END_INTEGER && state != STATE_END_FLOAT && state != STATE_END_BOOLEAN));

	field_data_list = add_metadata_field (data, data->current_field, FALSE, TRUE);

	if (!field_data_list) {
		g_free (avalue);
		g_free (data->current_field);
		g_free (data->current_value);
		return FALSE;
	}

	data->sql_where = g_string_append (data->sql_where, " ( ");

	field_data = g_list_first (field_data_list);

	while (field_data) {
		i++;
		str = g_string_new ("");
	
		if (i>1) {
			g_string_append (str, " OR ");	
		}

		if (((FieldData *)field_data->data)->data_type ==  TRACKER_FIELD_TYPE_DATE) {
			char *bvalue;
			int cvalue;
			
			bvalue = tracker_date_format (avalue);
			g_debug (bvalue);
			cvalue = tracker_string_to_date (bvalue);
			g_debug ("%d", cvalue);
			value = tracker_int_to_string (cvalue);
			g_free (bvalue);
		} else if (state == STATE_END_BOOLEAN) { /* FIXME We do a state check here, because TRACKER_FIELD_TYPE_BOOLEAN is not in db */
			if (!strcmp(avalue,"true")) {
				value = g_strdup("1");
			} else if(!strcmp(avalue,"false")) {
				value = g_strdup("0");
			} else {
				return FALSE; /* TODO Add error message */
			}				         
		} else {
			value = g_strdup (avalue);
		}
				
		if (data->statement_count > 1) {
			if (data->current_logic_operator == LOP_AND) {
				data->sql_where = g_string_append (data->sql_where, "\n AND ");
			} else {
				if (data->current_logic_operator == LOP_OR) {
					data->sql_where = g_string_append (data->sql_where, "\n OR ");
				}
			}
		}

		char **s;
		
		switch (data->current_operator) {
			
		case OP_EQUALS:
			
			sub = strchr (data->current_value, '*');
			if (sub) {
				g_string_append_printf (str, " (%s glob '%s') ", ((FieldData *)field_data->data)->where_field, data->current_value);
			} else {
				if (((FieldData *)field_data->data)->data_type == TRACKER_FIELD_TYPE_DATE 
				    || ((FieldData *)field_data->data)->data_type == TRACKER_FIELD_TYPE_INTEGER 
				    || ((FieldData *)field_data->data)->data_type == TRACKER_FIELD_TYPE_DOUBLE) {
					g_string_append_printf (str, " (%s = %s) ", ((FieldData *)field_data->data)->where_field, value);
				} else {
					g_string_append_printf (str, " (%s = '%s') ", ((FieldData *)field_data->data)->where_field, value);
				}
			}
			
			break;
			
		case OP_GREATER:
			
			g_string_append_printf (str, " (%s > %s) ", ((FieldData *)field_data->data)->where_field, value);

			break;

		case OP_GREATER_EQUAL:

			g_string_append_printf (str, " (%s >= %s) ", ((FieldData *)field_data->data)->where_field, value);

			break;

		case OP_LESS:

			g_string_append_printf (str, " (%s < %s) ", ((FieldData *)field_data->data)->where_field, value);

			break;

		case OP_LESS_EQUAL:

			g_string_append_printf (str, " (%s <= %s) ", ((FieldData *)field_data->data)->where_field, value);

			break;

		case OP_CONTAINS:

			sub = strchr (data->current_value, '*');

			if (sub) {
				g_string_append_printf (str, " (%s like '%s%s%s') ", ((FieldData *)field_data->data)->where_field, "%", data->current_value, "%");
			} else {
				g_string_append_printf (str, " (%s like '%s%s%s') ", ((FieldData *)field_data->data)->where_field, "%", data->current_value, "%");
			}

			break;

		case OP_STARTS:
			
			sub = strchr (data->current_value, '*');

			if (sub) {
				g_string_append_printf (str, " (%s like '%s') ", ((FieldData *)field_data->data)->where_field, data->current_value);
			} else {
				g_string_append_printf (str, " (%s like '%s%s') ", ((FieldData *)field_data->data)->where_field, data->current_value, "%");
			}
			
			break;

		case OP_REGEX:

			g_string_append_printf (str, " (%s REGEXP '%s') ", ((FieldData *)field_data->data)->where_field, data->current_value);

			break;

		case OP_SET:

			s = g_strsplit (data->current_value, ",", 0);
			
			if (s && s[0]) {

				g_string_append_printf (str, " (%s in ('%s'", ((FieldData *)field_data->data)->where_field, s[0]);

				char **p;
				for (p = s+1; *p; p++) {
					g_string_append_printf (str, ",'%s'", *p); 					
				}
				g_string_append_printf (str, ") ) " ); 					
					
			}

			break;

		default:
			
			break;
		}
		
		data->sql_where = g_string_append (data->sql_where, str->str);
		g_string_free (str, TRUE);
		field_data = g_list_next (field_data);

	}

	data->sql_where = g_string_append (data->sql_where, " ) ");

	g_free (avalue);

	g_free (data->current_field);
	data->current_field = NULL;

	g_free (data->current_value);
	data->current_value = NULL;

	g_free (value);

	return TRUE;
}


static void
end_element_handler (GMarkupParseContext *context,
		     const gchar	 *element_name,
		     gpointer		 user_data,
		     GError		 **error)
{
	ParserData *data;

	data = user_data;

	if (ELEMENT_IS (ELEMENT_XESAM_QUERY)) {

		push_stack (data, STATE_END_QUERY);
		data->query_okay = TRUE;

	} else if (ELEMENT_IS (ELEMENT_XESAM_AND)) {

		data->sql_where = g_string_append (data->sql_where, " ) ");

		pop_stack_until (data, STATE_AND);

		if (peek_state (data) != STATE_AND) {
			if (peek_state (data) == STATE_OR) {
				data->current_logic_operator = LOP_OR;
			} else {
				data->current_logic_operator = LOP_NONE;
			}
		}

	} else if (ELEMENT_IS (ELEMENT_XESAM_OR)) {

		data->sql_where = g_string_append (data->sql_where, " ) ");

		pop_stack_until (data, STATE_OR);

		if (peek_state (data) != STATE_OR) {
			if (peek_state (data) == STATE_AND) {
				data->current_logic_operator = LOP_AND;
			} else {
				data->current_logic_operator = LOP_NONE;
			}
		}

	} else if (ELEMENT_IS (ELEMENT_XESAM_EQUALS)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}
		push_stack (data, STATE_END_EQUALS);

	} else if (ELEMENT_IS (ELEMENT_XESAM_GREATER_THAN)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_GREATER_THAN);

	} else if (ELEMENT_IS (ELEMENT_XESAM_GREATER_OR_EQUAL)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_GREATER_OR_EQUAL);

	} else if (ELEMENT_IS (ELEMENT_XESAM_LESS_THAN )) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_LESS_THAN );

	} else if (ELEMENT_IS (ELEMENT_XESAM_LESS_OR_EQUAL )) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_LESS_OR_EQUAL );


	} else if (ELEMENT_IS (ELEMENT_XESAM_CONTAINS)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_CONTAINS);

	} else if (ELEMENT_IS (ELEMENT_XESAM_REGEX)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_REGEX);

	} else if (ELEMENT_IS (ELEMENT_XESAM_STARTS_WITH)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_STARTS_WITH);

	} else if (ELEMENT_IS (ELEMENT_XESAM_IN_SET)) {

		if (!build_sql (data)) {
			set_error (error, context, 1, "parse error");
			return;
		}

		push_stack (data, STATE_END_IN_SET);


	} else if (ELEMENT_IS (ELEMENT_XESAM_INTEGER)) {

		push_stack (data, STATE_END_INTEGER);


	} else if (ELEMENT_IS (ELEMENT_XESAM_DATE)) {

		push_stack (data, STATE_END_DATE);


	} else if (ELEMENT_IS (ELEMENT_XESAM_STRING)) {

		push_stack (data, STATE_END_STRING);


	}  else if (ELEMENT_IS (ELEMENT_XESAM_FLOAT)) {

		push_stack (data, STATE_END_FLOAT);


	}  else if (ELEMENT_IS (ELEMENT_XESAM_BOOLEAN)) {

		push_stack (data, STATE_END_BOOLEAN);
	}
}


static void
text_handler (GMarkupParseContext *context,
	      const gchar	  *text,
	      gsize		  text_len,
	      gpointer		  user_data,
	      GError		  **error)
{
	ParserData *data;
	ParseState state;

	data = user_data;
	state = peek_state (data);

	switch (state) {

		case STATE_INTEGER:
		case STATE_STRING:
		case STATE_DATE:
		case STATE_FLOAT:
          	case STATE_BOOLEAN:

			data->current_value = g_strstrip (g_strndup (text, text_len));
			break;

		default :
			break;
	}
}


static void
error_handler (GMarkupParseContext *context,
	       GError		   *error,
	       gpointer		   user_data)
{
	g_critical ("Failed to parse RDF query, %s", error->message);
}


void
tracker_xesam_query_to_sql (DBConnection *db_con, const char *query, gchar **from, gchar **where, GError **error)
{
	static     gboolean inited = FALSE;
	ParserData data;
	char       *result;
	char       *table_name;

	g_return_if_fail (query != NULL);

	if (!inited) {
		error_quark = g_quark_from_static_string ("XESAM-parser-error-quark");
		inited = TRUE;
	}

	memset (&data, 0, sizeof (data));
	data.db_con = db_con;
	data.statement_count = 0;

	table_name = "Services";

	data.sql_from = g_string_new ("");

	g_string_append_printf (data.sql_from, "\n FROM %s S ", table_name);
	
	data.sql_where = g_string_new ("");

	data.parser = g_new0 (GMarkupParser, 1);
	data.parser->start_element = start_element_handler;
	data.parser->text = text_handler;
	data.parser->end_element = end_element_handler;
	data.parser->error = error_handler;

	data.current_operator = OP_NONE;
	data.current_logic_operator = LOP_NONE;
	data.query_okay = FALSE;

	data.context = g_markup_parse_context_new (data.parser, 0, &data, NULL);

	push_stack (&data, STATE_START);

	result = NULL;

	if (!g_markup_parse_context_parse (data.context, query, -1, error)) {

		g_string_free (data.sql_from, TRUE);
		g_string_free (data.sql_where, TRUE);

		*from = NULL;
		*where = NULL;

	} else {
		const GSList *tmp;
		FieldData    *tmp_field;

		for (tmp = data.fields; tmp; tmp = tmp->next) {
			tmp_field = tmp->data;

			if (!tmp_field->is_condition) {
				if (tmp_field->needs_join) {
					g_string_append_printf (data.sql_from, "\n LEFT OUTER JOIN %s %s ON (S.ID = %s.ServiceID and %s.MetaDataID = %s) ", tmp_field->table_name, tmp_field->alias, tmp_field->alias, tmp_field->alias, tmp_field->id_field);
				}
			} else {
				char *related_metadata = tracker_get_related_metadata_names (db_con, tmp_field->field_name);
				g_string_append_printf (data.sql_from, "\n INNER JOIN %s %s ON (S.ID = %s.ServiceID and %s.MetaDataID in (%s)) ", tmp_field->table_name, tmp_field->alias, tmp_field->alias, tmp_field->alias, related_metadata);
				g_free (related_metadata);
			}
		}

		*from = g_strdup (data.sql_from->str);
		*where = g_strdup (data.sql_where->str);

		g_string_free (data.sql_from, TRUE);
		g_string_free (data.sql_where, TRUE);
	}

	g_slist_foreach (data.fields, (GFunc) tracker_free_metadata_field, NULL);
	g_slist_free (data.fields);

	g_slist_free (data.stack);
	g_markup_parse_context_free (data.context);

	if (data.current_field) {
		g_free (data.current_field);
	}

	if (data.current_value) {
		g_free (data.current_value);
	}

	g_free (data.parser);

	return;
}


