CREATE TEMPORARY TABLE Events
(
	ID		Integer primary key not null,
	ServiceID	Integer not null,
	BeingHandled	Integer default 0,
	EventType	Text
);

CREATE TEMPORARY TABLE XesamLiveSearches
(
	ID		Integer primary key not null,
	ServiceID	Integer not null,
	SearchID	Text
);
