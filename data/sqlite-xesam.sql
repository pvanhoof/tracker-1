
CREATE TABLE  XesamMetaDataTypes 
(
	ID	 		Integer primary key AUTOINCREMENT not null,
	MetaName		Text not null  COLLATE NOCASE, 
	DataTypeID		Integer default 0,    /* 0=string, 1=float, 2=integer, 3=boolean, 4=dateTime, 5=List of strings, 8=List of Uris, 9=List of Urls */
	Description		text default ' ',
	Categories		text default ' ',
	Parents			text default ' ',

	Unique (MetaName)
);

CREATE TABLE  XesamServiceTypes
(
	TypeID 			Integer primary key AUTOINCREMENT not null,
	TypeName		Text COLLATE NOCASE not null,
	Description		Text default ' ',
	Parents			text default ' ',

	unique (TypeName)
);

CREATE TABLE  XesamServiceMapping
(
	ID			Integer primary key AUTOINCREMENT not null,
	XesamTypeName		Text,
	TypeName		Text,

	unique (XesamTypeName, TypeName)
);

CREATE TABLE XesamMetaDataMapping
(
	ID			Integer primary key AUTOINCREMENT not null,
	XesamMetaName		Text,
	MetaName		Text,

	unique (XesamMetaName, MetaName)
);

CREATE TABLE XesamServiceChildren
(
	Parent			Text,
	Child			Text,

	unique (Parent, Child)
);

CREATE TABLE XesamMetaDataChildren
(
	Parent			Text,
	Child			Text,

	unique (Parent, Child)
);

CREATE TABLE XesamServiceLookup
(	
	ID			Integer primary key AUTOINCREMENT not null,
	XesamTypeName		Text,
	TypeName		Text,

	unique (XesamTypeName, TypeName)
);

CREATE TABLE XesamMetaDataLookup
(	
	ID			Integer primary key AUTOINCREMENT not null,
	XesamMetaName		Text,
	MetaName		Text,

	unique (XesamMetaName, MetaName)
);