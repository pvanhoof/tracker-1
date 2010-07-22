int
main( string[] args )
{
	TestApp app = new TestApp (Tracker.Sparql.Connection.get());

	return app.run ();
}
