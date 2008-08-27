#include <stdio.h>
#include <sqlite3.h>
#include <glib.h>

static int 
callback (void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
  	for (i=0; i<argc; i++){
    		printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
  	}
  
  	printf("\n");
  	return 0;
}


static void
exec_sql (sqlite3 *db, const char *sql)
{
	char *zErrMsg = 0;
	
	
	
	int rc = sqlite3_exec (db, sql , callback, 0, &zErrMsg);
	
  	if( rc!=SQLITE_OK ){
    		fprintf(stderr, "SQL error: %s\n", zErrMsg);
    		sqlite3_free(zErrMsg);
  	}
  	
	

}



int 
main (int argc, char **argv)
{
	sqlite3 *db;
	char *zErrMsg = 0;
	int rc;
	gboolean db_exists = FALSE;
	
	g_type_init ();
        g_thread_init (NULL);
        
	if( argc != 2 ){
		fprintf(stderr, "Usage: %s MATCH_TERM\n", argv[0]);
		fprintf(stderr, "EG: %s stew\n", argv[0]);
		exit(1);
	}
	
	db_exists = g_file_test ("/tmp/test.db", G_FILE_TEST_EXISTS);
	
	rc = sqlite3_open("/tmp/test.db", &db);
	if( rc ){
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}
	
	sqlite3_enable_load_extension(db, 1);
	
	char *st = NULL;
	
	sqlite3_load_extension (db, "tracker-fts.so", NULL, &st);
	
	if (st) {
		fprintf(stderr, "SQL error: %s\n", st);
		sqlite3_free(st);
	}
	
	if (!db_exists) {
		exec_sql (db, "create virtual table recipe using trackerfts (name, ingredients)");
		exec_sql (db, "insert into recipe (name, ingredients) values ('broccoli stew', 'broccoli,peppers,cheese and tomatoes')");
		exec_sql (db, "insert into recipe (name, ingredients) values ('pumpkin stew', 'pumpkin,onions,garlic and celery')");
		exec_sql (db, "insert into recipe (name, ingredients) values ('broccoli pie', 'broccoli,cheese,onions and flour.')");
		exec_sql (db, "insert into recipe (name, ingredients) values ('pumpkin pie', 'pumpkin,sugar,flour and butter.')");
	}
	
	
	char *sql = g_strdup_printf ("select rowid, name, ingredients, snippet(recipe), offsets(recipe) from recipe where recipe match '%s'", argv[1]);
	exec_sql (db, sql);
	g_free (sql);
	
		
	sqlite3_close(db);
	return 0;
}

