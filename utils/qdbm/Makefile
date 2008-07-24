all: print search

print: print-words.c
	gcc -o print print-words.c `pkg-config --cflags --libs qdbm glib-2.0`

search: search-word.c
	gcc -o search search-word.c `pkg-config --cflags --libs qdbm glib-2.0`
