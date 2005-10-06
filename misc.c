/*-------
 * Module:			misc.c
 *
 * Description:		This module contains miscellaneous routines
 *					such as for debugging/logging and string functions.
 *
 * Classes:			n/a
 *
 * API functions:	none
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *-------
 */

#include "psqlodbc.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef WIN32
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <process.h>			/* Byron: is this where Windows keeps def.
								 * of getpid ? */
#endif

#include "connection.h"
#include "multibyte.h"

extern GLOBAL_VALUES globals;
void		generate_filename(const char *, const char *, char *);


void
generate_filename(const char *dirname, const char *prefix, char *filename)
{
	int			pid = 0;

#ifndef WIN32
	struct passwd *ptr = 0;

	ptr = getpwuid(getuid());
#endif
	pid = getpid();
	if (dirname == 0 || filename == 0)
		return;

	strcpy(filename, dirname);
	strcat(filename, DIRSEPARATOR);
	if (prefix != 0)
		strcat(filename, prefix);
#ifndef WIN32
	strcat(filename, ptr->pw_name);
#endif
	sprintf(filename, "%s%u%s", filename, pid, ".log");
	return;
}

#if defined(WIN_MULTITHREAD_SUPPORT)
CRITICAL_SECTION	qlog_cs, mylog_cs;
#elif defined(POSIX_MULTITHREAD_SUPPORT)
pthread_mutex_t	qlog_cs, mylog_cs;
#endif /* WIN_MULTITHREAD_SUPPORT */
static int	mylog_on = 0,
			qlog_on = 0;

int	get_mylog(void)
{
	return mylog_on;
}
int	get_qlog(void)
{
	return qlog_on;
}

void
logs_on_off(int cnopen, int mylog_onoff, int qlog_onoff)
{
	static int	mylog_on_count = 0,
				mylog_off_count = 0,
				qlog_on_count = 0,
				qlog_off_count = 0;

	ENTER_MYLOG_CS;
	ENTER_QLOG_CS;
	if (mylog_onoff)
		mylog_on_count += cnopen;
	else
		mylog_off_count += cnopen;
	if (mylog_on_count > 0)
		mylog_on = 1;
	else if (mylog_off_count > 0)
		mylog_on = 0;
	else
		mylog_on = globals.debug;
	if (qlog_onoff)
		qlog_on_count += cnopen;
	else
		qlog_off_count += cnopen;
	if (qlog_on_count > 0)
		qlog_on = 1;
	else if (qlog_off_count > 0)
		qlog_on = 0;
	else
		qlog_on = globals.commlog;
	LEAVE_QLOG_CS;
	LEAVE_MYLOG_CS;
}

#ifdef MY_LOG
static FILE *LOGFP = NULL;
void
mylog(char *fmt,...)
{
	va_list		args;
	char		filebuf[80];

#ifndef WIN32
	int		filedes=0;
#endif

	ENTER_MYLOG_CS;
	if (mylog_on)
	{
		va_start(args, fmt);

		if (!LOGFP)
		{
			generate_filename(MYLOGDIR, MYLOGFILE, filebuf);
#ifdef WIN32
			LOGFP = fopen(filebuf, PG_BINARY_A);
#else
			filedes = open(filebuf, O_WRONLY | O_APPEND | O_CREAT, S_IWUSR | S_IRUSR);
			LOGFP = fdopen(filedes, PG_BINARY_A);
#endif
			setbuf(LOGFP, NULL);
		}

#ifdef	WIN_MULTITHREAD_SUPPORT
#ifdef	WIN32
		if (LOGFP)
			fprintf(LOGFP, "[%d]", GetCurrentThreadId());
#endif /* WIN32 */
#endif /* WIN_MULTITHREAD_SUPPORT */
#if defined(POSIX_MULTITHREAD_SUPPORT)
		if (LOGFP)
			fprintf(LOGFP, "[%d]", pthread_self());
#endif /* POSIX_MULTITHREAD_SUPPORT */
		if (LOGFP)
			vfprintf(LOGFP, fmt, args);

		va_end(args);
	}
	LEAVE_MYLOG_CS;
}
#else
void
MyLog(char *fmt,...)
{
}
#endif


#ifdef Q_LOG
void
qlog(char *fmt,...)
{
	va_list		args;
	char		filebuf[80];
	static FILE *LOGFP = NULL;

#ifndef WIN32
        int             filedes=0;
#endif

	ENTER_QLOG_CS;
	if (qlog_on)
	{
		va_start(args, fmt);

		if (!LOGFP)
		{
			generate_filename(QLOGDIR, QLOGFILE, filebuf);
#ifdef WIN32
                        LOGFP = fopen(filebuf, PG_BINARY_A);
#else
                        filedes = open(filebuf, O_WRONLY | O_APPEND | O_CREAT, S_IWUSR | S_IRUSR);
                        LOGFP = fdopen(filedes, PG_BINARY_A);
#endif
			setbuf(LOGFP, NULL);
		}

		if (LOGFP)
			vfprintf(LOGFP, fmt, args);

		va_end(args);
	}
	LEAVE_QLOG_CS;
}
#endif


/*
 *	returns STRCPY_FAIL, STRCPY_TRUNCATED, or #bytes copied
 *	(not including null term)
 */
int
my_strcpy(char *dst, int dst_len, const char *src, int src_len)
{
	if (dst_len <= 0)
		return STRCPY_FAIL;

	if (src_len == SQL_NULL_DATA)
	{
		dst[0] = '\0';
		return STRCPY_NULL;
	}
	else if (src_len == SQL_NTS)
		src_len = strlen(src);

	if (src_len <= 0)
		return STRCPY_FAIL;
	else
	{
		if (src_len < dst_len)
		{
			memcpy(dst, src, src_len);
			dst[src_len] = '\0';
		}
		else
		{
			memcpy(dst, src, dst_len - 1);
			dst[dst_len - 1] = '\0';	/* truncated */
			return STRCPY_TRUNCATED;
		}
	}

	return strlen(dst);
}


/*
 * strncpy copies up to len characters, and doesn't terminate
 * the destination string if src has len characters or more.
 * instead, I want it to copy up to len-1 characters and always
 * terminate the destination string.
 */
char *
strncpy_null(char *dst, const char *src, int len)
{
	int			i;


	if (NULL != dst)
	{
		/* Just in case, check for special lengths */
		if (len == SQL_NULL_DATA)
		{
			dst[0] = '\0';
			return NULL;
		}
		else if (len == SQL_NTS)
			len = strlen(src) + 1;

		for (i = 0; src[i] && i < len - 1; i++)
			dst[i] = src[i];

		if (len > 0)
			dst[i] = '\0';
	}
	return dst;
}


/*------
 *	Create a null terminated string (handling the SQL_NTS thing):
 *		1. If buf is supplied, place the string in there
 *		   (at most bufsize-1 bytes) and return buf.
 *		2. If buf is not supplied, malloc space and return this string
 *		   (buflen is ignored in this case)
 *------
 */
char *
make_string(const char *s, int len, char *buf, size_t bufsize)
{
	unsigned int	length;
	char	   *str;

	if (s && (len > 0 || (len == SQL_NTS && strlen(s) > 0)))
	{
		length = (len > 0) ? len : strlen(s);

		if (buf)
		{
			if (length >= bufsize)
				length = bufsize - 1;
			strncpy_null(buf, s, length + 1);
			return buf;
		}

		str = malloc(length + 1);
		if (!str)
			return NULL;

		strncpy_null(str, s, length + 1);
		return str;
	}

	return NULL;
}

/*------
 *	Create a null terminated lower-case string if the
 *	original string contains upper-case characters.
 *	The SQL_NTS length is considered.
 *------
 */
char *
make_lstring_ifneeded(ConnectionClass *conn, const char *s, int len, BOOL ifallupper)
{
	int	length = len;
	char	   *str = NULL;

	if (s && (len > 0 || (len == SQL_NTS && (length = strlen(s)) > 0)))
	{
		int	i;
		const char *ptr;
		encoded_str encstr;

		make_encoded_str(&encstr, conn, s);
		for (i = 0, ptr = s; i < length; i++, ptr++)
		{
			encoded_nextchar(&encstr);
			if (ENCODE_STATUS(encstr) != 0)
				continue;
			if (ifallupper && islower(*ptr))
			{
				if (str)
				{
					free(str);
					str = NULL;
				}
				break;
			}
			if (tolower(*ptr) != *ptr)
			{
				if (!str)
				{
					str = malloc(length + 1);
					memcpy(str, s, length);
					str[length] = '\0';
				}
				str[i] = tolower(*ptr);
			}
		}
	}

	return str;
}


/*
 *	Concatenate a single formatted argument to a given buffer handling the SQL_NTS thing.
 *	"fmt" must contain somewhere in it the single form '%.*s'.
 *	This is heavily used in creating queries for info routines (SQLTables, SQLColumns).
 *	This routine could be modified to use vsprintf() to handle multiple arguments.
 */
char *
my_strcat(char *buf, const char *fmt, const char *s, int len)
{
	if (s && (len > 0 || (len == SQL_NTS && strlen(s) > 0)))
	{
		int			length = (len > 0) ? len : strlen(s);

		int			pos = strlen(buf);

		sprintf(&buf[pos], fmt, length, s);
		return buf;
	}
	return NULL;
}

char *
schema_strcat(char *buf, const char *fmt, const char *s, int len, const char *tbname, int tbnmlen, ConnectionClass *conn)
{
	if (!s || 0 == len)
	{
		/*
		 * Note that this driver assumes the implicit schema is
		 * the CURRENT_SCHEMA() though it doesn't worth the
		 * naming.
		 */
		if (conn->schema_support && tbname && (tbnmlen > 0 || tbnmlen == SQL_NTS))
			return my_strcat(buf, fmt, CC_get_current_schema(conn), SQL_NTS);
		return NULL;
	}
	return my_strcat(buf, fmt, s, len);
}


void
remove_newlines(char *string)
{
	unsigned int i;

	for (i = 0; i < strlen(string); i++)
	{
		if ((string[i] == '\n') ||
			(string[i] == '\r'))
			string[i] = ' ';
	}
}


char *
trim(char *s)
{
	int			i;

	for (i = strlen(s) - 1; i >= 0; i--)
	{
		if (s[i] == ' ')
			s[i] = '\0';
		else
			break;
	}

	return s;
}

char *
my_strcat1(char *buf, const char *fmt, const char *s1, const char *s, int len)
{
	int	length = len;

	if (s && (len > 0 || (len == SQL_NTS && (length = strlen(s)) > 0)))
	{
		int	pos = strlen(buf);

		if (s1)
			sprintf(&buf[pos], fmt, s1, length, s);
		else
			sprintf(&buf[pos], fmt, length, s);
		return buf;
	}
	return NULL;
}

char *
schema_strcat1(char *buf, const char *fmt, const char *s1, const char *s, int len, const char *tbname, int tbnmlen, ConnectionClass *conn)
{
	if (!s || 0 == len)
	{
		if (conn->schema_support && tbname && (tbnmlen > 0 || tbnmlen == SQL_NTS))
			return my_strcat1(buf, fmt, s1, CC_get_current_schema(conn), SQL_NTS);
		return NULL;
	}
	return my_strcat1(buf, fmt, s1, s, len);
}

int 
contains_token(char *data, char *token)
{
	int	i, tlen, dlen;

	dlen = strlen(data);
	tlen = strlen(token);

	for (i = 0; i < dlen-tlen+1; i++)
	{
		if (!strnicmp((const char *)data+i, token, tlen))
			return 1;
	}

	return 0;
}
