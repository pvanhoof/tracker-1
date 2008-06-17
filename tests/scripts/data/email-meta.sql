BEGIN TRANSACTION;
CREATE TABLE Services
(
	ID            		Integer primary key not null,
	ServiceTypeID		Integer  default 0, /* see ServiceTypes table above for ID values. A value of 0 indicates a group resource rather than a service */
	Path 			Text  not null, /* non-file objects should use service name here */
	Name	 		Text default ' ', /* name of file or object - the combination path and name must be unique for all objects */
	Enabled			Integer default 1,
	Mime			Text default ' ',
	Size			Integer default 0,
	Rank			Integer default 5,
	ParentID		Integer,

	KeyMetadata1		Text,
	KeyMetadata2		Text,
	KeyMetadata3		Text,
	KeyMetadata4		Text,
	KeyMetadata5		Text,
	KeyMetadata6		Text,
	KeyMetadata7		Text,
	KeyMetadata8		Text,
	KeyMetadata9		Text,
	KeyMetadata10		Text,
	KeyMetadata11		Text,

	Icon			Text,
	CanWrite		Integer default 1,
	CanExecute		Integer default 1,

	LanguageId		Integer default 0,
	IsDirectory   		Integer default 0,
    	IsLink        		Integer default 0,
	AuxilaryID		Integer default 0, /* link to Volumes table for files, link to MailSummary table for emails*/
	IndexTime  		Integer default 0, /* should equal st_mtime for file if up-to-date */
	Accessed  		Integer default 0, /* last accessed */
	Offset			Integer default 0, /* last used disk offset for indexable files that always grow (like chat logs) or email offset */
	MD5			Text,

    	unique (Path, Name)

);
CREATE TABLE ChildServices
(
	ParentID            		Integer not null,
	ChildID				Integer not null,
	MetaDataID			Integer not null,

	primary key (ParentID, ChildID, MetaDataID)
);
CREATE TABLE ServiceMetaData 
(
	ID			Integer primary key AUTOINCREMENT not null,
	ServiceID		Integer not null,
	MetaDataID 		Integer  not null,
	MetaDataValue     	Text,
	MetaDataDisplay		Text

);
DELETE FROM sqlite_sequence;
CREATE TABLE ServiceKeywordMetaData 
(
	ID			Integer primary key AUTOINCREMENT not null,
	ServiceID		Integer not null,
	MetaDataID 		Integer not null,
	MetaDataValue		Text COLLATE NOCASE
);
CREATE TABLE ServiceNumericMetaData 
(
	ID			Integer primary key AUTOINCREMENT not null,
	ServiceID		Integer not null,
	MetaDataID 		Integer not null,
	MetaDataValue		Integer not null
);
CREATE TABLE MailSummary
(
	ID		Integer primary key AUTOINCREMENT not null,
	MailApp		Integer not null,
	MailType	Integer not null,
	FileName	Text not null,
	Path		Text not null,
	UriPrefix	Text,
	NeedsChecking	Integer default 0,
	MailCount	Integer,
	JunkCount	Integer,
	DeleteCount	Integer,
	Offset		Integer,
	LastOffset	Integer,
	MTime		integer,

	unique (Path)
);
CREATE TABLE JunkMail
(
	UID			integer not null,
	SummaryID		Integer not null,

	primary key (UID, SummaryID)
);
ANALYZE sqlite_master;
CREATE INDEX ServiceIndex1 ON Services (ServiceTypeID);
CREATE INDEX ChildServicesIndex1 ON ChildServices (ChildID);
CREATE INDEX ServiceMetaDataIndex1 ON ServiceMetaData (ServiceID);
CREATE INDEX ServiceMetaDataIndex2 ON ServiceMetaData (MetaDataID);
CREATE INDEX ServiceKeywordMetaDataIndex1 ON ServiceKeywordMetaData (MetaDataID, MetaDataValue);
CREATE INDEX ServiceKeywordMetaDataIndex2 ON ServiceKeywordMetaData (ServiceID);
CREATE INDEX ServiceNumericMetaDataIndex1 ON ServiceNumericMetaData (MetaDataID, MetaDataValue);
CREATE INDEX ServiceNumericMetaDataIndex2 ON ServiceNumericMetaData (ServiceID);
CREATE TRIGGER delete_service BEFORE DELETE ON Services 
BEGIN  
	DELETE FROM ServiceMetaData WHERE ServiceID = old.ID;
	DELETE FROM ServiceKeywordMetaData WHERE ServiceID = old.ID;
	DELETE FROM ServiceNumericMetaData WHERE ServiceID = old.ID;
	DELETE FROM ChildServices WHERE (ParentID = old.ID);
	DELETE FROM ChildServices WHERE (ChildID = old.ID);
	
END;
COMMIT;
