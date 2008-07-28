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

#include "config.h"

#include <string.h>

#ifdef HAVE_UNAC
#include <unac.h>
#endif

#include <pango/pango.h>

#include "tracker-parser.h"
#include "tracker-log.h"
#include "tracker-utils.h"

#define INDEX_NUMBER_MIN_LENGTH 6

/* Need pango for CJK ranges which are : 0x3400 - 0x4DB5, 0x4E00 -
 * 0x9FA5, 0x20000 - <= 0x2A6D6
 */
#define NEED_PANGO(c) (((c) >= 0x3400 && (c) <= 0x4DB5)  || ((c) >= 0x4E00 && (c) <= 0x9FA5)  ||  ((c) >= 0x20000 && (c) <= 0x2A6D6))
#define IS_LATIN(c) (((c) <= 0x02AF) || ((c) >= 0x1E00 && (c) <= 0x1EFF))
#define IS_ASCII(c) ((c) <= 0x007F) 
#define IS_ASCII_ALPHA_LOWER(c) ( (c) >= 0x0061 && (c) <= 0x007A )
#define IS_ASCII_ALPHA_HIGHER(c) ( (c) >= 0x0041 && (c) <= 0x005A )
#define IS_ASCII_NUMERIC(c) ((c) >= 0x0030 && (c) <= 0x0039)
#define IS_ASCII_IGNORE(c) ((c) <= 0x002C) 
#define IS_HYPHEN(c) ((c) == 0x002D)
#define IS_UNDERSCORE(c) ((c) == 0x005F)

typedef enum {
	TRACKER_PARSER_WORD_ASCII_HIGHER,
	TRACKER_PARSER_WORD_ASCII_LOWER,
	TRACKER_PARSER_WORD_HYPHEN,
	TRACKER_PARSER_WORD_UNDERSCORE,
	TRACKER_PARSER_WORD_NUM,
	TRACKER_PARSER_WORD_ALPHA_HIGHER,
	TRACKER_PARSER_WORD_ALPHA_LOWER,
	TRACKER_PARSER_WORD_ALPHA,
	TRACKER_PARSER_WORD_ALPHA_NUM,
	TRACKER_PARSER_WORD_IGNORE
} TrackerParserWordType;

static inline TrackerParserWordType
get_word_type (gunichar c)
{
	/* Fast ascii handling */
	if (IS_ASCII (c)) {
		if (IS_ASCII_ALPHA_LOWER (c)) {
			return TRACKER_PARSER_WORD_ASCII_LOWER;
		}

		if (IS_ASCII_ALPHA_HIGHER (c)) {
			return TRACKER_PARSER_WORD_ASCII_HIGHER;
		}

		if (IS_ASCII_IGNORE (c)) {
			return TRACKER_PARSER_WORD_IGNORE;	
		}

		if (IS_ASCII_NUMERIC (c)) {
			return TRACKER_PARSER_WORD_NUM;
		}

		if (IS_HYPHEN (c)) {
			return TRACKER_PARSER_WORD_HYPHEN;
		}

		if (IS_UNDERSCORE (c)) {
			return TRACKER_PARSER_WORD_UNDERSCORE;
		}
	} else 	{
		if (g_unichar_isalpha (c)) {
			if (!g_unichar_isupper (c)) {
				return  TRACKER_PARSER_WORD_ALPHA_LOWER;
			} else {
				return  TRACKER_PARSER_WORD_ALPHA_HIGHER;
			}
		} else if (g_unichar_isdigit (c)) {
			return  TRACKER_PARSER_WORD_NUM;
		} 
	}

	return TRACKER_PARSER_WORD_IGNORE;
}

static inline gchar *
strip_word (const gchar *str, 
            gint         length, 
            guint32     *len)
{
#ifdef HAVE_UNAC
	gchar *s = NULL;

	if (unac_string ("UTF-8", str, length, &s, &*len) != 0) {
		g_warning ("UNAC failed to strip accents");
	}

	return s;
#else
	*len = length;
	return NULL;	
#endif
}

static gboolean
text_needs_pango (const gchar *text)
{
	const gchar *p;
	gunichar     c;
	gint         i = 0;

	/* Grab first 1024 non-whitespace chars and test */
	for (p = text; *p && i < 1024; p = g_utf8_next_char (p)) {
		c = g_utf8_get_char (p);

		if (!g_unichar_isspace (c)) {
			i++;
		}

		if (NEED_PANGO(c)) {
			return TRUE;
		}
	}

	return FALSE;
}

static const gchar *
analyze_text (const gchar      *text, 
              TrackerLanguage  *language,
              gint              max_word_length,
              gint              min_word_length,
              gboolean          filter_words, 
              gboolean          filter_numbers, 
              gboolean          delimit_hyphen,
              gchar           **index_word)
{
        TrackerParserWordType word_type;
        gunichar              word[64];
        gboolean              do_strip;
        gboolean              is_valid;
        gint                  length;
        glong                 bytes;
	const char           *p;
	const char           *start;

	*index_word = NULL;

	if (!text) {
                return NULL;
        }

        word_type = TRACKER_PARSER_WORD_IGNORE;
        do_strip = FALSE;
        is_valid = TRUE;
        length = 0;
        bytes = 0;
        start = NULL;

        for (p = text; *p; p = g_utf8_next_char (p)) {
                TrackerParserWordType type;
                gunichar              c;
                
                c = g_utf8_get_char (p);
                type = get_word_type (c);
                
                if (type == TRACKER_PARSER_WORD_IGNORE || 
                    (delimit_hyphen && 
                     (type == TRACKER_PARSER_WORD_HYPHEN || 
                      type == TRACKER_PARSER_WORD_UNDERSCORE))) {
                        if (!start) {
                                continue;
                        } else {
                                break;
                        }
                } 
                
                if (!is_valid) {
                        continue;
                }
                
                if (!start) {
                        start = p;
                        
                        /* Valid words must start with an alpha or
                         * underscore if we are filtering.
                         */
                        if (filter_numbers) {
                                if (type == TRACKER_PARSER_WORD_NUM) {
                                        is_valid = FALSE;
                                        continue;
                                } else {
                                        if (type == TRACKER_PARSER_WORD_HYPHEN) {
                                                is_valid = FALSE;
                                                continue;
                                        }
                                }	
                        }				
                }
                
                if (length >= max_word_length) {
                        continue;
                }
		
                length++;
                
                switch (type) {
                case TRACKER_PARSER_WORD_ASCII_HIGHER: 
                        c += 32;
                        
                case TRACKER_PARSER_WORD_ASCII_LOWER: 
                case TRACKER_PARSER_WORD_HYPHEN:
                case TRACKER_PARSER_WORD_UNDERSCORE:
                        if (word_type == TRACKER_PARSER_WORD_NUM || 
                            word_type == TRACKER_PARSER_WORD_ALPHA_NUM) {
                                word_type = TRACKER_PARSER_WORD_ALPHA_NUM;
                        } else {
                                word_type = TRACKER_PARSER_WORD_ALPHA;
                        }
			
                        break;
                        
                case TRACKER_PARSER_WORD_NUM: 
                        if (word_type == TRACKER_PARSER_WORD_ALPHA || 
                            word_type == TRACKER_PARSER_WORD_ALPHA_NUM) {
                                word_type = TRACKER_PARSER_WORD_ALPHA_NUM;
                        } else {
                                word_type = TRACKER_PARSER_WORD_NUM;
                        }
                        break;
                        
                case TRACKER_PARSER_WORD_ALPHA_HIGHER: 
                        c = g_unichar_tolower (c);
                        
                case TRACKER_PARSER_WORD_ALPHA_LOWER: 
                        if (!do_strip) {
                                do_strip = TRUE;
                        }
                        
                        if (word_type == TRACKER_PARSER_WORD_NUM || 
                            word_type == TRACKER_PARSER_WORD_ALPHA_NUM) {
                                word_type = TRACKER_PARSER_WORD_ALPHA_NUM;
                        } else {
                                word_type = TRACKER_PARSER_WORD_ALPHA;
                        }
			
                        break;
                        
                default: 
                        break;
                }
                
                word[length -1] = c;
        }
        
        if (is_valid) {
                if (word_type == TRACKER_PARSER_WORD_NUM) {
                        if (!filter_numbers || length >= INDEX_NUMBER_MIN_LENGTH) {
                                *index_word = g_ucs4_to_utf8 (word, length, NULL, NULL, NULL);
                        } 
                } else {
                        if (length >= min_word_length) {
                                gchar 	*str = NULL;
                                gchar   *tmp;
                                guint32  len;
                                gchar   *utf8;
                                
                                utf8 = g_ucs4_to_utf8 (word, length, NULL, &bytes, NULL);
                                
                                if (!utf8) {
                                        return p;
                                }
				
                                if (do_strip) {
                                        str = strip_word (utf8, bytes, &len);
                                }
                                
                                if (!str) {
                                        tmp = g_utf8_normalize (utf8, bytes, G_NORMALIZE_NFC);
                                } else {
                                        tmp = g_utf8_normalize (str, len, G_NORMALIZE_NFC);
                                        g_free (str);
                                }
                                
                                g_free (utf8);
                                
                                *index_word = tracker_language_stem_word (language, 
                                                                          tmp, 
                                                                          strlen (tmp));
                                if (*index_word) {
                                        g_free (tmp);
                                } else {
                                        *index_word = tmp;			
                                }
                        }
                }
        } 
        
        return p;	
}

static void
delete_words (gpointer key,
              gpointer value,
              gpointer user_data)
{
	g_free (key);
}

gchar *
tracker_parser_text_to_string (const gchar     *txt, 
                               TrackerLanguage *language,
                               gint             max_word_length,
                               gint             min_word_length,
                               gboolean         filter_words, 
                               gboolean         filter_numbers, 
                               gboolean         delimit)
{
	const gchar *p = txt;
	gchar       *parsed_text;
	gchar       *word = NULL;
	guint32      i = 0;

        g_return_val_if_fail (language != NULL, NULL);

	if (!txt) {
                return NULL;
        }

        if (text_needs_pango (txt)) {
                /* CJK text does not need stemming or other
                 * treatment.
                 */
                PangoLogAttr *attrs;
                guint	      nb_bytes, str_len, word_start;
                GString	     *strs;
                
                nb_bytes = strlen (txt);
                str_len = g_utf8_strlen (txt, -1);
                
                strs = g_string_new (" ");
                
                attrs = g_new0 (PangoLogAttr, str_len + 1);
                
                pango_get_log_attrs (txt, 
                                     nb_bytes, 
                                     0, 
                                     pango_language_from_string ("C"), 
                                     attrs, 
                                     str_len + 1);
                
                word_start = 0;
                
                for (i = 0; i < str_len + 1; i++) {
                        if (attrs[i].is_word_end) {
                                gchar *start_word, *end_word;
                                
                                start_word = g_utf8_offset_to_pointer (txt, word_start);
                                end_word = g_utf8_offset_to_pointer (txt, i);
                                
                                if (start_word != end_word) {
                                        /* Normalize word */
                                        gchar *s;
                                        gchar *index_word;
                                        
                                        s = g_utf8_casefold (start_word, end_word - start_word);
                                        index_word  = g_utf8_normalize (s, -1, G_NORMALIZE_NFC);
                                        g_free (s);
					
                                        strs = g_string_append (strs, index_word);
                                        strs = g_string_append_c (strs, ' ');
                                        g_free (index_word);
                                }
                                
                                word_start = i;
                        }
                        
                        if (attrs[i].is_word_start) {
                                word_start = i;
                        }
                }
                
                g_free (attrs);
                
		parsed_text = g_string_free (strs, FALSE);
		return g_strstrip (parsed_text);
        } else {
                GString *str = g_string_new (" ");
                
                while (TRUE) {
                        i++;
                        p = analyze_text (p,
                                          language, 
                                          max_word_length,
                                          min_word_length,
                                          filter_words, 
                                          filter_numbers, 
                                          delimit,
                                          &word);
                        
                        if (word) {
                                g_string_append (str, word);
                                g_string_append_c (str, ' ');
                                g_free (word);			
                        }
                        
                        if (!p || !*p) {
                                parsed_text = g_string_free (str, FALSE);
				return g_strstrip (parsed_text);
                        }
                }
        }

	return NULL;
}

gchar **
tracker_parser_text_into_array (const gchar     *text,
                                TrackerLanguage *language,
                                gint             max_word_length,
                                gint             min_word_length)
{
	gchar  *s;
	gchar **strv;

        g_return_val_if_fail (language != NULL, NULL);

        s = tracker_parser_text_to_string (text, 
                                           language, 
                                           max_word_length,
                                           min_word_length,
                                           TRUE, 
                                           FALSE, 
                                           FALSE);
        strv = g_strsplit (g_strstrip (s), " ", -1);
	g_free (s);

	return strv;
}

GHashTable *
tracker_parser_text_fast (GHashTable  *word_table,
                          const gchar *txt, 
                          gint         weight)
{
	gchar    **tmp;	
	gchar    **array;
	gpointer   k = 0;
        gpointer   v = 0;

        /* Use this for already processed text only */
	if (!word_table) {
		word_table = g_hash_table_new (g_str_hash, g_str_equal);
	} 

	if (!txt || weight == 0) {
		return word_table;
	}

	array =  g_strsplit (txt, " ", -1);

	for (tmp = array; *tmp; tmp++) {
		if (**tmp) {
			if (!g_hash_table_lookup_extended (word_table, *tmp, &k, &v)) {
				g_hash_table_insert (word_table, 
                                                     g_strdup (*tmp), 
                                                     GINT_TO_POINTER (GPOINTER_TO_INT (v) + weight)); 
			} else {
				g_hash_table_insert (word_table, 
                                                     *tmp, 
                                                     GINT_TO_POINTER (GPOINTER_TO_INT (v) + weight)); 
			}
		}
	}

	g_strfreev (array);

	return word_table;
}

GHashTable *
tracker_parser_text (GHashTable      *word_table, 
                     const gchar     *txt, 
                     gint             weight, 
                     TrackerLanguage *language,
                     gint             max_words_to_index,
                     gint             max_word_length,
                     gint             min_word_length,
                     gboolean         filter_words, 
                     gboolean         delimit_words)
{
	const gchar *p;
	gchar       *word;
	guint32      i;

        /* Use this for unprocessed raw text */
	gint         total_words;

        g_return_val_if_fail (language != NULL, NULL);

	if (!word_table) {
		word_table = g_hash_table_new (g_str_hash, g_str_equal);
		total_words = 0;
	} else {
		total_words = g_hash_table_size (word_table);
	}

	if (!txt || weight == 0) {
		return word_table;
	}

	p = txt;	
	word = NULL;
	i = 0;

	if (text_needs_pango (txt)) {
		/* CJK text does not need stemming or other treatment */
		PangoLogAttr *attrs;
		guint	      nb_bytes, str_len, word_start;

                nb_bytes = strlen (txt);

                str_len = g_utf8_strlen (txt, -1);

		attrs = g_new0 (PangoLogAttr, str_len + 1);

		pango_get_log_attrs (txt, 
                                     nb_bytes, 
                                     0, 
                                     pango_language_from_string ("C"), 
                                     attrs, 
                                     str_len + 1);
		
		word_start = 0;

		for (i = 0; i < str_len + 1; i++) {
			if (attrs[i].is_word_end) {
				gchar *start_word, *end_word;

				start_word = g_utf8_offset_to_pointer (txt, word_start);
				end_word = g_utf8_offset_to_pointer (txt, i);

				if (start_word != end_word) {
					/* Normalize word */
					gchar *s;
					gchar *index_word;

                                        s = g_utf8_casefold (start_word, end_word - start_word);
					if (!s) {
                                                continue;
                                        }

                                        index_word = g_utf8_normalize (s, -1, G_NORMALIZE_NFC);
					g_free (s);

					if (!index_word) {
                                                continue;
                                        }
					
					total_words++;

					if (total_words <= max_words_to_index) { 
						gint count;

                                                count = GPOINTER_TO_INT (g_hash_table_lookup (word_table, index_word));
						g_hash_table_insert (word_table, index_word, GINT_TO_POINTER (count + weight));
						
						if (count != 0) {
							g_free (index_word);
						}
					} else {
						g_free (index_word);
						break;
					}
				}

				word_start = i;
			}
	
			if (attrs[i].is_word_start) {
				word_start = i;
			}
		}

		g_free (attrs);		
	} else {
		while (TRUE) {
			i++;
			p = analyze_text (p, 
                                          language,
                                          max_word_length,
                                          min_word_length,
                                          filter_words, 
                                          filter_words, 
                                          delimit_words,
                                          &word);

			if (word) {
				total_words++;

				if (total_words <= max_words_to_index) { 
                                        gint count;

                                        count = GPOINTER_TO_INT (g_hash_table_lookup (word_table, word));
					g_hash_table_insert (word_table, word, GINT_TO_POINTER (count + weight));
						
					if (count != 0) {
						g_free (word);
					}
				} else {
					g_free (word);
					break;
				}

			}				

			if (!p || !*p) {
				break;
			}
		}
	}

	return word_table;
}

void
tracker_parser_text_free (GHashTable *table)
{
	if (table) {
		g_hash_table_foreach (table, delete_words, NULL);		
		g_hash_table_destroy (table);
	}
}
