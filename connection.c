
/* Module:          connection.c
 *
 * Description:     This module contains routines related to 
 *                  connecting to and disconnecting from the Postgres DBMS.
 *
 * Classes:         ConnectionClass (Functions prefix: "CC_")
 *
 * API functions:   SQLAllocConnect, SQLConnect, SQLDisconnect, SQLFreeConnect,
 *                  SQLBrowseConnect(NI)
 *
 * Comments:        See "notice.txt" for copyright and license information.
 *
 */

#include "environ.h"
#include "connection.h"
#include "socket.h"
#include "statement.h"
#include "qresult.h"
#include <stdio.h>
#include <odbcinst.h>

#define STMT_INCREMENT 16  /* how many statement holders to allocate at a time */

extern GLOBAL_VALUES globals;


RETCODE SQL_API SQLAllocConnect(
                                HENV     henv,
                                HDBC FAR *phdbc)
{
EnvironmentClass *env = (EnvironmentClass *)henv;
ConnectionClass *conn;


	conn = CC_Constructor();
	mylog("**** SQLAllocConnect: henv = %u, conn = %u\n", henv, conn);

    if( ! conn) {
        env->errormsg = "Couldn't allocate memory for Connection object.";
        env->errornumber = ENV_ALLOC_ERROR;
		*phdbc = SQL_NULL_HDBC;
        return SQL_ERROR;
    }

    if ( ! EN_add_connection(env, conn)) {
        env->errormsg = "Maximum number of connections exceeded.";
        env->errornumber = ENV_ALLOC_ERROR;
        CC_Destructor(conn);
		*phdbc = SQL_NULL_HDBC;
        return SQL_ERROR;
    }

	*phdbc = (HDBC) conn;

    return SQL_SUCCESS;
}


//      -       -       -       -       -       -       -       -       -

RETCODE SQL_API SQLConnect(
                           HDBC      hdbc,
                           UCHAR FAR *szDSN,
                           SWORD     cbDSN,
                           UCHAR FAR *szUID,
                           SWORD     cbUID,
                           UCHAR FAR *szAuthStr,
                           SWORD     cbAuthStr)
{
ConnectionClass *conn = (ConnectionClass *) hdbc;

	if ( ! conn) 
		return SQL_INVALID_HANDLE;

	make_string(szDSN, cbDSN, conn->connInfo.dsn);

	/*	get the values for the DSN from the registry */
	CC_DSN_info(conn, CONN_OVERWRITE);
	
	/*	override values from DSN info with UID and authStr(pwd) 
		This only occurs if the values are actually there.
	*/
	make_string(szUID, cbUID, conn->connInfo.username);
	make_string(szAuthStr, cbAuthStr, conn->connInfo.password);

	/* fill in any defaults */
	CC_set_defaults(conn);

	qlog("conn = %u, SQLConnect(DSN='%s', UID='%s', PWD='%s')\n", conn->connInfo.dsn, conn->connInfo.username, conn->connInfo.password);

	if ( CC_connect(conn, FALSE) <= 0)
		//	Error messages are filled in
		return SQL_ERROR;

	return SQL_SUCCESS;
}

//      -       -       -       -       -       -       -       -       -

RETCODE SQL_API SQLBrowseConnect(
        HDBC      hdbc,
        UCHAR FAR *szConnStrIn,
        SWORD     cbConnStrIn,
        UCHAR FAR *szConnStrOut,
        SWORD     cbConnStrOutMax,
        SWORD FAR *pcbConnStrOut)
{
	return SQL_SUCCESS;
}

//      -       -       -       -       -       -       -       -       -

/* Drop any hstmts open on hdbc and disconnect from database */
RETCODE SQL_API SQLDisconnect(
        HDBC      hdbc)
{
ConnectionClass *conn = (ConnectionClass *) hdbc;

	mylog("**** in SQLDisconnect\n");

	if ( ! conn) 
		return SQL_INVALID_HANDLE;

	qlog("conn=%u, SQLDisconnect\n", conn);

	if (conn->status == CONN_EXECUTING) {
		conn->errornumber = CONN_IN_USE;
		conn->errormsg = "A transaction is currently being executed";
		return SQL_ERROR;
	}

	mylog("SQLDisconnect: about to CC_cleanup\n");

	/*  Close the connection and free statements */
	CC_cleanup(conn);

	mylog("SQLDisconnect: done CC_cleanup\n");
	mylog("exit SQLDisconnect\n");

	return SQL_SUCCESS;
}


//      -       -       -       -       -       -       -       -       -

RETCODE SQL_API SQLFreeConnect(
        HDBC      hdbc)
{
ConnectionClass *conn = (ConnectionClass *) hdbc;

	mylog("**** in SQLFreeConnect: hdbc=%u\n", hdbc);

	if ( ! conn) 
		return SQL_INVALID_HANDLE;

	/*  Remove the connection from the environment */
	if ( ! EN_remove_connection(conn->henv, conn)) {
		conn->errornumber = CONN_IN_USE;
		conn->errormsg = "A transaction is currently being executed";
		return SQL_ERROR;
	}

	CC_Destructor(conn);

	mylog("exit SQLFreeConnect\n");

	return SQL_SUCCESS;
}


/*
*
*       IMPLEMENTATION CONNECTION CLASS
*
*/

ConnectionClass
*CC_Constructor()
{
ConnectionClass *rv;

    rv = (ConnectionClass *)malloc(sizeof(ConnectionClass));

    if (rv != NULL) {

		rv->henv = NULL; /* not yet associated with an environment */

        rv->errormsg = NULL;
        rv->errornumber = 0;
		rv->errormsg_created = FALSE;

        rv->status = CONN_NOT_CONNECTED;
        rv->transact_status = CONN_IN_AUTOCOMMIT; // autocommit by default

		memset(&rv->connInfo, 0, sizeof(ConnInfo));

		rv->sock = SOCK_Constructor();
		if ( ! rv->sock)
			return NULL;

		rv->stmts = (StatementClass **) malloc( sizeof(StatementClass *) * STMT_INCREMENT);
		if ( ! rv->stmts)
			return NULL;
		memset(rv->stmts, 0, sizeof(StatementClass *) * STMT_INCREMENT);

		rv->num_stmts = STMT_INCREMENT;

    } 
    return rv;
}


char
CC_Destructor(ConnectionClass *self)
{

	mylog("enter CC_Destructor, self=%u\n", self);

	if (self->status == CONN_EXECUTING)
		return 0;

	CC_cleanup(self);   /* cleanup socket and statements */

	mylog("after CC_Cleanup\n");

	/*  Free up statement holders */
	if (self->stmts) {
		free(self->stmts);
		self->stmts = NULL;
	}
	mylog("after free statement holders\n");

	free(self);

	mylog("exit CC_Destructor\n");

	return 1;
}

void 
CC_clear_error(ConnectionClass *self)
{
	self->errornumber = 0; 
	self->errormsg = NULL; 
	self->errormsg_created = FALSE;
}

//	Used to cancel a transaction
//	We are almost always in the middle of a transaction.
char
CC_abort(ConnectionClass *self)
{
QResultClass *res;

	if ( CC_is_in_trans(self)) {
		res = NULL;

		mylog("CC_abort:  sending ABORT!\n");

		res = CC_send_query(self, "ABORT", NULL, NULL);
		CC_set_no_trans(self);

		if (res != NULL)
			QR_Destructor(res);
		else
			return FALSE;

	}

	return TRUE;
}

/* This is called by SQLDisconnect also */
char
CC_cleanup(ConnectionClass *self)
{
int i;
StatementClass *stmt;

	if (self->status == CONN_EXECUTING)
		return FALSE;

	mylog("in CC_Cleanup, self=%u\n", self);

	// Cancel an ongoing transaction
	// We are always in the middle of a transaction,
	// even if we are in auto commit.
	if (self->sock)
		CC_abort(self);

	mylog("after CC_abort\n");

	/*  This actually closes the connection to the dbase */
	if (self->sock) {
	    SOCK_Destructor(self->sock);
		self->sock = NULL;
	}

	mylog("after SOCK destructor\n");

	/*  Free all the stmts on this connection */
	for (i = 0; i < self->num_stmts; i++) {
		stmt = self->stmts[i];
		if (stmt) {

			stmt->hdbc = NULL;	/* prevent any more dbase interactions */

			SC_Destructor(stmt);

			self->stmts[i] = NULL;
		}
	}
	mylog("exit CC_Cleanup\n");
	return TRUE;
}

void
CC_set_defaults(ConnectionClass *self)
{
ConnInfo *ci = &(self->connInfo);

	if (ci->port[0] == '\0')
		strcpy(ci->port, DEFAULT_PORT);

	if (ci->readonly[0] == '\0')
		strcpy(ci->readonly, DEFAULT_READONLY);
}

void 
CC_DSN_info(ConnectionClass *self, char overwrite)
{
ConnInfo *ci = &(self->connInfo);
char *DSN = ci->dsn;

	//	If a driver keyword was present, then dont use a DSN and return.
	//	If DSN is null and no driver, then use the default datasource.
	if ( DSN[0] == '\0') {
		if ( ci->driver[0] != '\0')
			return;
		else
			strcpy(DSN, "DEFAULT");
	}

	//	Proceed with getting info for the given DSN.
	if ( ci->server[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_SERVER, "", ci->server, sizeof(ci->server), ODBC_INI);

	if ( ci->database[0] == '\0' || overwrite)
	    SQLGetPrivateProfileString(DSN, INI_DATABASE, "", ci->database, sizeof(ci->database), ODBC_INI);

	if ( ci->username[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_USER, "", ci->username, sizeof(ci->username), ODBC_INI);

	if ( ci->password[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_PASSWORD, "", ci->password, sizeof(ci->password), ODBC_INI);

	if ( ci->port[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_PORT, "", ci->port, sizeof(ci->port), ODBC_INI);

	if ( ci->readonly[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_READONLY, "", ci->readonly, sizeof(ci->readonly), ODBC_INI);

	if ( ci->protocol[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_PROTOCOL, "", ci->protocol, sizeof(ci->protocol), ODBC_INI);

	if ( ci->conn_settings[0] == '\0' || overwrite)
		SQLGetPrivateProfileString(DSN, INI_CONNSETTINGS, "", ci->conn_settings, sizeof(ci->conn_settings), ODBC_INI);

	qlog("conn=%u, DSN info(DSN='%s',server='%s',dbase='%s',user='%s',passwd='%s',port='%s',readonly='%s',protocol='%s',conn_settings='%s')\n", 
		self, DSN, 
		ci->server,
		ci->database,
		ci->username,
		ci->password,
		ci->port,
		ci->readonly,
		ci->protocol,
		ci->conn_settings);
}


char 
CC_connect(ConnectionClass *self, char do_password)
{
StartupPacket sp;
StartupPacket6_2 sp62;
QResultClass *res;
SocketClass *sock;
ConnInfo *ci = &(self->connInfo);
int areq = -1;
int beresp;
char msgbuffer[ERROR_MSG_LENGTH]; 
char salt[2];

	if ( do_password)

		sock = self->sock;		/* already connected, just authenticate */

	else {

		if (self->status != CONN_NOT_CONNECTED) {
			self->errormsg = "Already connected.";
			self->errornumber = CONN_OPENDB_ERROR;
			return 0;
		}

		if ( ci->server[0] == '\0' || ci->port[0] == '\0' || ci->database[0] == '\0') {
			self->errornumber = CONN_INIREAD_ERROR;
			self->errormsg = "Missing server name, port, or database name in call to CC_connect.";
			return 0;
		}

		mylog("CC_connect(): DSN = '%s', server = '%s', port = '%s', database = '%s', username = '%s', password='%s'\n",
				ci->dsn, ci->server, ci->port, ci->database, ci->username, ci->password);

		/* If the socket was closed for some reason (like a SQLDisconnect, but no SQLFreeConnect
			then create a socket now.
		*/
		if ( ! self->sock) {
			self->sock = SOCK_Constructor();
			if ( ! self->sock) {
				 self->errornumber = CONNECTION_SERVER_NOT_REACHED;
				 self->errormsg = "Could not open a socket to the server";
				 return 0;
			}
		}

		sock = self->sock;

		mylog("connecting to the server socket...\n");

		SOCK_connect_to(sock, (short) atoi(ci->port), ci->server);
		if (SOCK_get_errcode(sock) != 0) {
			mylog("connection to the server socket failed.\n");
			self->errornumber = CONNECTION_SERVER_NOT_REACHED;
			self->errormsg = "Could not connect to the server";
			return 0;
		}
		mylog("connection to the server socket succeeded.\n");

		if ( PROTOCOL_62(ci)) {
			sock->reverse = TRUE;		/* make put_int and get_int work for 6.2 */

			memset(&sp62, 0, sizeof(StartupPacket6_2));
			SOCK_put_int(sock, htonl(4+sizeof(StartupPacket6_2)), 4);
			sp62.authtype = htonl(NO_AUTHENTICATION);
			strncpy(sp62.database, ci->database, PATH_SIZE);
			strncpy(sp62.user, ci->username, NAMEDATALEN);
			SOCK_put_n_char(sock, (char *) &sp62, sizeof(StartupPacket6_2));
			SOCK_flush_output(sock);
		}
		else {
			memset(&sp, 0, sizeof(StartupPacket));

			mylog("sizeof startup packet = %d\n", sizeof(StartupPacket));

			// Send length of Authentication Block
			SOCK_put_int(sock, 4+sizeof(StartupPacket), 4); 

			sp.protoVersion = (ProtocolVersion) htonl(PG_PROTOCOL_LATEST);
			strncpy(sp.database, ci->database, SM_DATABASE);
			strncpy(sp.user, ci->username, SM_USER);

			SOCK_put_n_char(sock, (char *) &sp, sizeof(StartupPacket));
			SOCK_flush_output(sock);
		}

		mylog("sent the authentication block.\n");

		if (sock->errornumber != 0) {
			mylog("couldn't send the authentication block properly.\n");
			self->errornumber = CONN_INVALID_AUTHENTICATION;
			self->errormsg = "Sending the authentication packet failed";
			return 0;
		}
		mylog("sent the authentication block successfully.\n");
	}


	mylog("gonna do authentication\n");


	// ***************************************************
	//	Now get the authentication request from backend
	// ***************************************************

	if ( ! PROTOCOL_62(ci))	do {

		if (do_password)
			beresp = 'R';
		else
			beresp = SOCK_get_char(sock);

		switch(beresp) {
		case 'E':
			mylog("auth got 'E'\n");

			SOCK_get_string(sock, msgbuffer, ERROR_MSG_LENGTH);
			self->errornumber = CONN_INVALID_AUTHENTICATION;
			self->errormsg = msgbuffer;
			qlog("ERROR from backend during authentication: '%s'\n", self->errormsg);
			return 0;
		case 'R':

			if (do_password) {
				mylog("in 'R' do_password\n");
				areq = AUTH_REQ_PASSWORD;
				do_password = FALSE;
			}
			else {
				mylog("auth got 'R'\n");

				areq = SOCK_get_int(sock, 4);
				if (areq == AUTH_REQ_CRYPT)
					SOCK_get_n_char(sock, salt, 2);

				mylog("areq = %d\n", areq);
			}
			switch(areq) {
			case AUTH_REQ_OK:
				break;

			case AUTH_REQ_KRB4:
				self->errormsg = "Kerberos 4 authentication not supported";
				self->errornumber = CONN_AUTH_TYPE_UNSUPPORTED;
				return 0;

			case AUTH_REQ_KRB5:
				self->errormsg = "Kerberos 5 authentication not supported";
				self->errornumber = CONN_AUTH_TYPE_UNSUPPORTED;
				return 0;

			case AUTH_REQ_PASSWORD:
				mylog("in AUTH_REQ_PASSWORD\n");

				if (ci->password[0] == '\0') {
					self->errornumber = CONNECTION_NEED_PASSWORD;
					self->errormsg = "A password is required for this connection.";
					return -1;	/* need password */
				}

				mylog("past need password\n");

				SOCK_put_int(sock, 4+strlen(ci->password)+1, 4);
				SOCK_put_n_char(sock, ci->password, strlen(ci->password) + 1);
				SOCK_flush_output(sock);

				mylog("past flush\n");
				break;

			case AUTH_REQ_CRYPT:
				self->errormsg = "Password crypt authentication not supported";
				self->errornumber = CONN_AUTH_TYPE_UNSUPPORTED;
				return 0;

			default:
				self->errormsg = "Unknown authentication type";
				self->errornumber = CONN_AUTH_TYPE_UNSUPPORTED;
				return 0;
			}
			break;
		default:
			self->errormsg = "Unexpected protocol character during authentication";
			self->errornumber = CONN_INVALID_AUTHENTICATION;
			return 0;
		}

	} while (areq != AUTH_REQ_OK);		


	CC_clear_error(self);	/* clear any password error */

	/* send an empty query in order to find out whether the specified */
	/* database really exists on the server machine */
	mylog("sending an empty query...\n");

	res = CC_send_query(self, " ", NULL, NULL);
	if ( res == NULL || QR_get_status(res) != PGRES_EMPTY_QUERY) {
		mylog("got no result from the empty query.  (probably database does not exist)\n");
		self->errornumber = CONNECTION_NO_SUCH_DATABASE;
		self->errormsg = "The database does not exist on the server\nor user authentication failed.";
		if (res != NULL)
			QR_Destructor(res);
		return 0;
	}
	if (res)
		QR_Destructor(res);

	mylog("empty query seems to be OK.\n");


	/**********************************************/
	/*******   Send any initial settings  *********/
	/**********************************************/

	CC_send_settings(self);


	CC_clear_error(self);	/* clear any initial command errors */
	self->status = CONN_CONNECTED;

	return 1;

}

char
CC_add_statement(ConnectionClass *self, StatementClass *stmt)
{
int i;

	mylog("CC_add_statement: self=%u, stmt=%u\n", self, stmt);

	for (i = 0; i < self->num_stmts; i++) {
		if ( ! self->stmts[i]) {
			stmt->hdbc = self;
			self->stmts[i] = stmt;
			return TRUE;
		}
	}

	/* no more room -- allocate more memory */
	self->stmts = (StatementClass **) realloc( self->stmts, sizeof(StatementClass *) * (STMT_INCREMENT + self->num_stmts));
	if ( ! self->stmts)
		return FALSE;

	memset(&self->stmts[self->num_stmts], 0, sizeof(StatementClass *) * STMT_INCREMENT);

	stmt->hdbc = self;
	self->stmts[self->num_stmts] = stmt;

	self->num_stmts += STMT_INCREMENT;

	return TRUE;
}

char 
CC_remove_statement(ConnectionClass *self, StatementClass *stmt)
{
int i;

	for (i = 0; i < self->num_stmts; i++) {
		if (self->stmts[i] == stmt && stmt->status != STMT_EXECUTING) {
			self->stmts[i] = NULL;
			return TRUE;
		}
	}

	return FALSE;
}

/*	Create a more informative error message by concatenating the connection
	error message with its socket error message.
*/
char *
CC_create_errormsg(ConnectionClass *self)
{
SocketClass *sock = self->sock;
int pos;
static char msg[4096];

	mylog("enter CC_create_errormsg\n");

	msg[0] = '\0';

	if (self->errormsg)
		strcpy(msg, self->errormsg);

	mylog("msg = '%s'\n", msg);

	if (sock && sock->errormsg && sock->errormsg[0] != '\0') {
		pos = strlen(msg);
		sprintf(&msg[pos], ";\n%s", sock->errormsg);
	}

	mylog("exit CC_create_errormsg\n");
	return msg;
}


char 
CC_get_error(ConnectionClass *self, int *number, char **message)
{
int rv;

	mylog("enter CC_get_error\n");

	//	Create a very informative errormsg if it hasn't been done yet.
	if ( ! self->errormsg_created) {
		self->errormsg = CC_create_errormsg(self);
		self->errormsg_created = TRUE;
	}

	if (self->errornumber) {
		*number = self->errornumber;
		*message = self->errormsg;
	}
	rv = (self->errornumber != 0);

	self->errornumber = 0;		// clear the error

	mylog("exit CC_get_error\n");

	return rv;
}


/*	The "result_in" is only used by QR_next_tuple() to fetch another group of rows into
	the same existing QResultClass (this occurs when the tuple cache is depleted and
	needs to be re-filled).

	The "cursor" is used by SQLExecute to associate a statement handle as the cursor name
	(i.e., C3326857) for SQL select statements.  This cursor is then used in future 
	'declare cursor C3326857 for ...' and 'fetch 100 in C3326857' statements.
*/
QResultClass *
CC_send_query(ConnectionClass *self, char *query, QResultClass *result_in, char *cursor)
{
QResultClass *res = NULL;
char id, swallow;
SocketClass *sock = self->sock;
static char msgbuffer[MAX_MESSAGE_LEN+1];
char cmdbuffer[MAX_MESSAGE_LEN+1];	// QR_set_command() dups this string so dont need static


	mylog("send_query(): conn=%u, query='%s'\n", self, query);
	qlog("conn=%u, query='%s'\n", self, query);

	// Indicate that we are sending a query to the backend
	if(strlen(query) > MAX_MESSAGE_LEN-2) {
		self->errornumber = CONNECTION_MSG_TOO_LONG;
		self->errormsg = "Query string is too long";
		return NULL;
	}

	if ((NULL == query) || (query[0] == '\0'))
		return NULL;

	if (SOCK_get_errcode(sock) != 0) {
		self->errornumber = CONNECTION_COULD_NOT_SEND;
		self->errormsg = "Could not send Query to backend";
		CC_set_no_trans(self);
		return NULL;
	}

	SOCK_put_char(sock, 'Q');
	if (SOCK_get_errcode(sock) != 0) {
		self->errornumber = CONNECTION_COULD_NOT_SEND;
		self->errormsg = "Could not send Query to backend";
		CC_set_no_trans(self);
		return NULL;
	}

	SOCK_put_string(sock, query);
	SOCK_flush_output(sock);

	if (SOCK_get_errcode(sock) != 0) {
		self->errornumber = CONNECTION_COULD_NOT_SEND;
		self->errormsg = "Could not send Query to backend";
		CC_set_no_trans(self);
		return NULL;
	}

	mylog("send_query: done sending query\n");

	while(1) {
		/* what type of message is comming now ? */
		id = SOCK_get_char(sock);

		if ((SOCK_get_errcode(sock) != 0) || (id == EOF)) {
			self->errornumber = CONNECTION_NO_RESPONSE;
			self->errormsg = "No response from the backend";
			if (res)
				QR_Destructor(res);

			mylog("send_query: 'id' - %s\n", self->errormsg);
			CC_set_no_trans(self);
			return NULL;
		}

		mylog("send_query: got id = '%c'\n", id);

		switch (id) {
		case 'A' : /* Asynchronous Messages are ignored */
			(void)SOCK_get_int(sock, 4); /* id of notification */
			SOCK_get_string(sock, msgbuffer, MAX_MESSAGE_LEN);
			/* name of the relation the message comes from */
			break;
		case 'C' : /* portal query command, no tuples returned */
			/* read in the return message from the backend */
			SOCK_get_string(sock, cmdbuffer, MAX_MESSAGE_LEN);
			if (SOCK_get_errcode(sock) != 0) {
				self->errornumber = CONNECTION_NO_RESPONSE;
				self->errormsg = "No response from backend while receiving a portal query command";
				mylog("send_query: 'C' - %s\n", self->errormsg);
				CC_set_no_trans(self);
				return NULL;
			} else {

				char clear = 0;

				mylog("send_query: ok - 'C' - %s\n", cmdbuffer);

				if (res == NULL)	/* allow for "show" style notices */
					res = QR_Constructor();

				mylog("send_query: setting cmdbuffer = '%s'\n", cmdbuffer);

				/*	Only save the first command */
				QR_set_status(res, PGRES_COMMAND_OK);
				QR_set_command(res, cmdbuffer);

				/* (Quotation from the original comments)
					since backend may produze more than one result for some commands
					we need to poll until clear
					so we send an empty query, and keep reading out of the pipe
					until an 'I' is received
				*/


				SOCK_put_string(sock, "Q ");
				SOCK_flush_output(sock);
				while(!clear) {
					SOCK_get_string(sock, cmdbuffer, ERROR_MSG_LENGTH);
					mylog("send_query: read command '%s'\n", cmdbuffer);
					clear = (cmdbuffer[0] == 'I');
				}

				mylog("send_query: returning res = %u\n", res);
				return res;
			}
		case 'N' : /* NOTICE: */
			SOCK_get_string(sock, cmdbuffer, ERROR_MSG_LENGTH);

			res = QR_Constructor();
			QR_set_status(res, PGRES_NONFATAL_ERROR);
			QR_set_notice(res, cmdbuffer);	/* will dup this string */

			mylog("~~~ NOTICE: '%s'\n", cmdbuffer);
			qlog("NOTICE from backend during send_query: '%s'\n", cmdbuffer);

			continue;		// dont return a result -- continue reading

		case 'I' : /* The server sends an empty query */
				/* There is a closing '\0' following the 'I', so we eat it */
			swallow = SOCK_get_char(sock);
			if ((swallow != '\0') || SOCK_get_errcode(sock) != 0) {
				self->errornumber = CONNECTION_BACKEND_CRAZY;
				self->errormsg = "Unexpected protocol character from backend";
				res = QR_Constructor();
				QR_set_status(res, PGRES_FATAL_ERROR);
				return res;
			} else {
				/* We return the empty query */
				res = QR_Constructor();
				QR_set_status(res, PGRES_EMPTY_QUERY);
				return res;
			}
			break;
		case 'E' : 
			SOCK_get_string(sock, msgbuffer, ERROR_MSG_LENGTH);

			/*	Remove a newline */
			if (msgbuffer[0] != '\0' && msgbuffer[strlen(msgbuffer)-1] == '\n')
				msgbuffer[strlen(msgbuffer)-1] = '\0';

			self->errormsg = msgbuffer;

			mylog("send_query: 'E' - %s\n", self->errormsg);
			qlog("ERROR from backend during send_query: '%s'\n", self->errormsg);

			if ( ! strncmp(self->errormsg, "FATAL", 5)) {
				self->errornumber = CONNECTION_SERVER_REPORTED_ERROR;
				CC_set_no_trans(self);
			}
			else
				self->errornumber = CONNECTION_SERVER_REPORTED_WARNING;

			return NULL;

		case 'P' : /* get the Portal name */
			SOCK_get_string(sock, msgbuffer, MAX_MESSAGE_LEN);
			break;
		case 'T': /* Tuple results start here */
			if (result_in == NULL) {
				result_in = QR_Constructor();
				mylog("send_query: 'T' no result_in: res = %u\n", result_in);
				if ( ! result_in) {
					self->errornumber = CONNECTION_COULD_NOT_RECEIVE;
					self->errormsg = "Could not create result info in send_query.";
					return NULL;
				}

				if ( ! QR_fetch_tuples(result_in, self, cursor)) {
					self->errornumber = CONNECTION_COULD_NOT_RECEIVE;
					self->errormsg = QR_get_message(result_in);
					return NULL;
				}
			}
			else {  // next fetch, so reuse an existing result
				if ( ! QR_fetch_tuples(result_in, NULL, NULL)) {
					self->errornumber = CONNECTION_COULD_NOT_RECEIVE;
					self->errormsg = QR_get_message(result_in);
					return NULL;
				}
			}

			return result_in;
		case 'D': /* Copy in command began successfully */
			res = QR_Constructor();
			QR_set_status(res, PGRES_COPY_IN);
			return res;
		case 'B': /* Copy out command began successfully */
			res = QR_Constructor();
			QR_set_status(res, PGRES_COPY_OUT);
			return res;
		default:
			self->errornumber = CONNECTION_BACKEND_CRAZY;
			self->errormsg = "Unexpected protocol character from backend";
			CC_set_no_trans(self);

			mylog("send_query: error - %s\n", self->errormsg);
			return NULL;
		}
	}
}

char
CC_send_settings(ConnectionClass *self)
{
char ini_query[MAX_MESSAGE_LEN];
ConnInfo *ci = &(self->connInfo);
QResultClass *res;

	ini_query[0] = '\0';

	/*	Turn on/off genetic optimizer based on global flag */
	if (globals.optimizer[0] != '\0')
		sprintf(ini_query, "set geqo to '%s'", globals.optimizer);

	/*	Global settings */
	if (globals.conn_settings[0] != '\0')
		sprintf(&ini_query[strlen(ini_query)], "%s%s", 
			ini_query[0] != '\0' ? "; " : "",
			globals.conn_settings);

	/*	Per Datasource settings */
	if (ci->conn_settings[0] != '\0')
		sprintf(&ini_query[strlen(ini_query)], "%s%s",
			ini_query[0] != '\0' ? "; " : "",
			ci->conn_settings);

	if (ini_query[0] != '\0') {
		mylog("Sending Initial Connection query: '%s'\n", ini_query);

		res = CC_send_query(self, ini_query, NULL, NULL);
		if (res && QR_get_status(res) != PGRES_FATAL_ERROR) {
			mylog("Initial Query response: '%s'\n", QR_get_notice(res));
		}

		if ( res == NULL || 
			QR_get_status(res) == PGRES_BAD_RESPONSE ||
			QR_get_status(res) == PGRES_FATAL_ERROR || 
			QR_get_status(res) == PGRES_INTERNAL_ERROR) {

			self->errornumber = CONNECTION_COULD_NOT_SEND;
			self->errormsg = "Error sending ConnSettings";
			if (res)
				QR_Destructor(res);
			return 0;
		}

		if (res)
			QR_Destructor(res);
	}
	return TRUE;
}

