int
main( string[] args )
{
	TestApp app = new TestApp (Tracker.Sparql.Connection.get());

	app.run ();

	return 0;
}
