using Tracker;
using Tracker.Sparql;
using Tracker.Direct;

int
main( string[] args )
{
//	Sparql.Connection con = new Direct.Connection ();
//	Cursor cursor = con.query ("SELECT ?u WHERE { ?u a rdfs:Class }");

//	while (cursor.next()) {
//		int i;

//		for (i = 0; i < cursor.n_columns; i++) {
//			print ("%s%s", i != 0 ? ",":"", cursor.get_string (i));
//		}

//		print ("\n");
//	}
	
	// Testing new API with GModule
	
	print ("\n\n");

	Lookup foo = new Lookup ();

	return( 0 );
}

