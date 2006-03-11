/*-------
 * Module:			pgapi30.c
 *
 * Description:		This module contains routines related to ODBC 3.0
 *			most of their implementations are temporary
 *			and must be rewritten properly.
 *			2001/07/23	inoue
 *
 * Classes:			n/a
 *
 * API functions:	PGAPI_ColAttribute, PGAPI_GetDiagRec,
			PGAPI_GetConnectAttr, PGAPI_GetStmtAttr,
			PGAPI_SetConnectAttr, PGAPI_SetStmtAttr
 *-------
 */

#include "psqlodbc.h"

#if (ODBCVER >= 0x0300)
#include <stdio.h>
#include <string.h>

#include "environ.h"
#include "connection.h"
#include "statement.h"
#include "descriptor.h"
#include "qresult.h"
#include "pgapifunc.h"

#ifdef	WIN32
#ifdef	_HANDLE_ENLIST_IN_DTC_
RETCODE EnlistInDtc();
#endif /* _HANDLE_ENLIST_IN_DTC_ */
#endif /* WIN32 */

/*	SQLError -> SQLDiagRec */
RETCODE		SQL_API
PGAPI_GetDiagRec(SQLSMALLINT HandleType, SQLHANDLE Handle,
		SQLSMALLINT RecNumber, SQLCHAR *Sqlstate,
		SQLINTEGER *NativeError, SQLCHAR *MessageText,
		SQLSMALLINT BufferLength, SQLSMALLINT *TextLength)
{
	RETCODE		ret;
	CSTR func = "PGAPI_GetDiagRec";

	mylog("%s entering type=%d rec=%d\n", func, HandleType, RecNumber);
	switch (HandleType)
	{
		case SQL_HANDLE_ENV:
			ret = PGAPI_EnvError(Handle, RecNumber, Sqlstate,
					NativeError, MessageText,
					BufferLength, TextLength, 0);
			break;
		case SQL_HANDLE_DBC:
			ret = PGAPI_ConnectError(Handle, RecNumber, Sqlstate,
					NativeError, MessageText, BufferLength,
					TextLength, 0);
			break;
		case SQL_HANDLE_STMT:
			ret = PGAPI_StmtError(Handle, RecNumber, Sqlstate,
					NativeError, MessageText, BufferLength,
					TextLength, 0);
			break;
		case SQL_HANDLE_DESC:
			ret = PGAPI_DescError(Handle, RecNumber, Sqlstate,
					NativeError,
					MessageText, BufferLength,
					TextLength, 0);
			break;
		default:
			ret = SQL_ERROR;
	}
	mylog("%s exiting %d\n", func, ret);
	return ret;
}

/*
 *	Minimal implementation. 
 *
 */
RETCODE		SQL_API
PGAPI_GetDiagField(SQLSMALLINT HandleType, SQLHANDLE Handle,
		SQLSMALLINT RecNumber, SQLSMALLINT DiagIdentifier,
		PTR DiagInfoPtr, SQLSMALLINT BufferLength,
		SQLSMALLINT *StringLengthPtr)
{
	RETCODE		ret = SQL_ERROR, rtn;
	ConnectionClass	*conn;
	StatementClass	*stmt;
	SDWORD		rc;
	SWORD		pcbErrm;
	int		rtnlen = -1, rtnctype = SQL_C_CHAR;
	CSTR func = "PGAPI_GetDiagField";

	mylog("%s entering rec=%d", func, RecNumber);
	switch (HandleType)
	{
		case SQL_HANDLE_ENV:
			switch (DiagIdentifier)
			{
				case SQL_DIAG_CLASS_ORIGIN:
				case SQL_DIAG_SUBCLASS_ORIGIN:
				case SQL_DIAG_CONNECTION_NAME:
				case SQL_DIAG_SERVER_NAME:
					rtnlen = 0;
					if (DiagInfoPtr && BufferLength > rtnlen)
					{
						ret = SQL_SUCCESS;
						*((char *) DiagInfoPtr) = '\0';
					}
					else
						ret = SQL_SUCCESS_WITH_INFO;
					break;
				case SQL_DIAG_MESSAGE_TEXT:
					ret = PGAPI_EnvError(Handle, RecNumber,
                        			NULL, NULL, DiagInfoPtr,
						BufferLength, StringLengthPtr, 0);  
					break;
				case SQL_DIAG_NATIVE:
					rtnctype = SQL_C_LONG;
					ret = PGAPI_EnvError(Handle, RecNumber,
                        			NULL, (SQLINTEGER *) DiagInfoPtr, NULL,
						0, NULL, 0);  
					break;
				case SQL_DIAG_NUMBER:
					rtnctype = SQL_C_LONG;
					ret = PGAPI_EnvError(Handle, RecNumber,
                        			NULL, NULL, NULL,
						0, NULL, 0);
					if (SQL_SUCCESS == ret ||
					    SQL_SUCCESS_WITH_INFO == ret)
					{
						*((SQLINTEGER *) DiagInfoPtr) = 1;
					}
					break;
				case SQL_DIAG_SQLSTATE:
					rtnlen = 5;
					ret = PGAPI_EnvError(Handle, RecNumber,
                        			DiagInfoPtr, NULL, NULL,
						0, NULL, 0);
					if (SQL_SUCCESS_WITH_INFO == ret)  
						ret = SQL_SUCCESS;
					break;
				case SQL_DIAG_RETURNCODE: /* driver manager returns */
					break;
				case SQL_DIAG_CURSOR_ROW_COUNT:
				case SQL_DIAG_ROW_COUNT:
				case SQL_DIAG_DYNAMIC_FUNCTION:
				case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
					/* options for statement type only */
					break;
			}
			break;
		case SQL_HANDLE_DBC:
			conn = (ConnectionClass *) Handle;
			switch (DiagIdentifier)
			{
				case SQL_DIAG_CLASS_ORIGIN:
				case SQL_DIAG_SUBCLASS_ORIGIN:
				case SQL_DIAG_CONNECTION_NAME:
					rtnlen = 0;
					if (DiagInfoPtr && BufferLength > rtnlen)
					{
						ret = SQL_SUCCESS;
						*((char *) DiagInfoPtr) = '\0';
					}
					else
						ret = SQL_SUCCESS_WITH_INFO;
					break;
				case SQL_DIAG_SERVER_NAME:
					rtnlen = strlen(CC_get_DSN(conn));
					if (DiagInfoPtr)
					{
						strncpy_null((SQLCHAR *) DiagInfoPtr, CC_get_DSN(conn), BufferLength);
						ret = (BufferLength > rtnlen ? SQL_SUCCESS : SQL_SUCCESS_WITH_INFO);
					}
					break;
				case SQL_DIAG_MESSAGE_TEXT:
					ret = PGAPI_ConnectError(Handle, RecNumber,
                        			NULL, NULL, DiagInfoPtr,
						BufferLength, StringLengthPtr, 0);  
					break;
				case SQL_DIAG_NATIVE:
					rtnctype = SQL_C_LONG;
					ret = PGAPI_ConnectError(Handle, RecNumber,
                        			NULL, (SQLINTEGER *) DiagInfoPtr, NULL,
						0, NULL, 0);  
					break;
				case SQL_DIAG_NUMBER:
					rtnctype = SQL_C_LONG;
					ret = PGAPI_ConnectError(Handle, RecNumber,
                        			NULL, NULL, NULL,
						0, NULL, 0);
					if (SQL_SUCCESS == ret ||
					    SQL_SUCCESS_WITH_INFO == ret)
					{
						*((SQLINTEGER *) DiagInfoPtr) = 1;
					}
					break;  
				case SQL_DIAG_SQLSTATE:
					rtnlen = 5;
					ret = PGAPI_ConnectError(Handle, RecNumber,
                        			DiagInfoPtr, NULL, NULL,
						0, NULL, 0);
					if (SQL_SUCCESS_WITH_INFO == ret)  
						ret = SQL_SUCCESS;
					break;
				case SQL_DIAG_RETURNCODE: /* driver manager returns */
					break;
				case SQL_DIAG_CURSOR_ROW_COUNT:
				case SQL_DIAG_ROW_COUNT:
				case SQL_DIAG_DYNAMIC_FUNCTION:
				case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
					/* options for statement type only */
					break;
			}
			break;
		case SQL_HANDLE_STMT:
			conn = (ConnectionClass *) SC_get_conn(((StatementClass *) Handle));
			switch (DiagIdentifier)
			{
				case SQL_DIAG_CLASS_ORIGIN:
				case SQL_DIAG_SUBCLASS_ORIGIN:
				case SQL_DIAG_CONNECTION_NAME:
					rtnlen = 0;
					if (DiagInfoPtr && BufferLength > rtnlen)
					{
						ret = SQL_SUCCESS;
						*((char *) DiagInfoPtr) = '\0';
					}
					else
						ret = SQL_SUCCESS_WITH_INFO;
					break;
				case SQL_DIAG_SERVER_NAME:
					rtnlen = strlen(CC_get_DSN(conn));
					if (DiagInfoPtr)
					{
						strncpy_null((SQLCHAR *) DiagInfoPtr, CC_get_DSN(conn), BufferLength);
						ret = (BufferLength > rtnlen ? SQL_SUCCESS : SQL_SUCCESS_WITH_INFO);
					}
					break;
				case SQL_DIAG_MESSAGE_TEXT:
					ret = PGAPI_StmtError(Handle, RecNumber,
                        			NULL, NULL, DiagInfoPtr,
						BufferLength, StringLengthPtr, 0);  
					break;
				case SQL_DIAG_NATIVE:
					rtnctype = SQL_C_LONG;
					ret = PGAPI_StmtError(Handle, RecNumber,
                        			NULL, (SQLINTEGER *) DiagInfoPtr, NULL,
						0, NULL, 0);  
					break;
				case SQL_DIAG_NUMBER:
					rtnctype = SQL_C_LONG;
					*((SQLINTEGER *) DiagInfoPtr) = 0;
					ret = SQL_NO_DATA_FOUND;
					stmt = (StatementClass *) Handle;
					rtn = PGAPI_StmtError(Handle, -1, NULL,
						 NULL, NULL, 0, &pcbErrm, 0);
					switch (rtn)
					{
						case SQL_SUCCESS:
						case SQL_SUCCESS_WITH_INFO:
							ret = SQL_SUCCESS;
							if (pcbErrm > 0 && stmt->pgerror)
							
								*((SQLINTEGER *) DiagInfoPtr) = (pcbErrm  - 1)/ stmt->pgerror->recsize + 1;
							break;
						default:
							break;
					}
					break;
				case SQL_DIAG_SQLSTATE:
					rtnlen = 5;
					ret = PGAPI_StmtError(Handle, RecNumber,
                        			DiagInfoPtr, NULL, NULL,
						0, NULL, 0);
					if (SQL_SUCCESS_WITH_INFO == ret)  
						ret = SQL_SUCCESS;
					break;
				case SQL_DIAG_CURSOR_ROW_COUNT:
					rtnctype = SQL_C_LONG;
					stmt = (StatementClass *) Handle;
					rc = -1;
					if (stmt->status == STMT_FINISHED)
					{
						QResultClass *res = SC_get_Curres(stmt);

						/*if (!res)
							return SQL_ERROR;*/
						if (stmt->proc_return > 0)
							rc = 0;
						else if (res && QR_NumResultCols(res) > 0 && !SC_is_fetchcursor(stmt))
							rc = QR_get_num_total_tuples(res) - res->dl_count;
					} 
					*((SQLINTEGER *) DiagInfoPtr) = rc;
inolog("rc=%d\n", rc);
					ret = SQL_SUCCESS;
					break;
				case SQL_DIAG_ROW_COUNT:
					rtnctype = SQL_C_LONG;
					stmt = (StatementClass *) Handle;
					*((SQLINTEGER *) DiagInfoPtr) = stmt->diag_row_count;
					ret = SQL_SUCCESS;
					break;
                                case SQL_DIAG_ROW_NUMBER:
					rtnctype = SQL_C_LONG;
					*((SQLINTEGER *) DiagInfoPtr) = SQL_ROW_NUMBER_UNKNOWN;
					ret = SQL_SUCCESS;
					break;
                                case SQL_DIAG_COLUMN_NUMBER:
					rtnctype = SQL_C_LONG;
					*((SQLINTEGER *) DiagInfoPtr) = SQL_COLUMN_NUMBER_UNKNOWN;
					ret = SQL_SUCCESS;
					break;
				case SQL_DIAG_RETURNCODE: /* driver manager returns */
					break;
			}
			break;
		case SQL_HANDLE_DESC:
			conn = DC_get_conn(((DescriptorClass *) Handle)); 
			switch (DiagIdentifier)
			{
				case SQL_DIAG_CLASS_ORIGIN:
				case SQL_DIAG_SUBCLASS_ORIGIN:
				case SQL_DIAG_CONNECTION_NAME:
					rtnlen = 0;
					if (DiagInfoPtr && BufferLength > rtnlen)
					{
						ret = SQL_SUCCESS;
						*((char *) DiagInfoPtr) = '\0';
					}
					else
						ret = SQL_SUCCESS_WITH_INFO;
					break;
				case SQL_DIAG_SERVER_NAME:
					rtnlen = strlen(CC_get_DSN(conn));
					if (DiagInfoPtr)
					{
						strncpy_null((SQLCHAR *) DiagInfoPtr, CC_get_DSN(conn), BufferLength);
						ret = (BufferLength > rtnlen ? SQL_SUCCESS : SQL_SUCCESS_WITH_INFO);
					}
					break;
				case SQL_DIAG_MESSAGE_TEXT:
				case SQL_DIAG_NATIVE:
				case SQL_DIAG_NUMBER:
					break;
				case SQL_DIAG_SQLSTATE:
					rtnlen = 5;
					ret = PGAPI_DescError(Handle, RecNumber,
                        			DiagInfoPtr, NULL, NULL,
						0, NULL, 0);
					if (SQL_SUCCESS_WITH_INFO == ret)  
						ret = SQL_SUCCESS;
					break;
				case SQL_DIAG_RETURNCODE: /* driver manager returns */
					break;
				case SQL_DIAG_CURSOR_ROW_COUNT:
				case SQL_DIAG_ROW_COUNT:
				case SQL_DIAG_DYNAMIC_FUNCTION:
				case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
					rtnctype = SQL_C_LONG;
					/* options for statement type only */
					break;
			}
			break;
		default:
			ret = SQL_ERROR;
	}
	if (SQL_C_LONG == rtnctype)
	{
		if (SQL_SUCCESS_WITH_INFO == ret)
			ret = SQL_SUCCESS;
		if (StringLengthPtr)  
			*StringLengthPtr = sizeof(SQLINTEGER);
	}
	else if (rtnlen >= 0)
	{
		if (rtnlen >= BufferLength)
		{
			if (SQL_SUCCESS == ret)
				ret = SQL_SUCCESS_WITH_INFO;
			if (BufferLength > 0)
				((char *) DiagInfoPtr) [BufferLength - 1] = '\0';
		}
		if (StringLengthPtr)  
			*StringLengthPtr = rtnlen;
	}
	mylog("%s exiting %d\n", func, ret);
	return ret;
}

/*	SQLGetConnectOption -> SQLGetconnectAttr */
RETCODE		SQL_API
PGAPI_GetConnectAttr(HDBC ConnectionHandle,
			SQLINTEGER Attribute, PTR Value,
			SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	CSTR func = "PGAPI_GetConnectAttr";
	ConnectionClass *conn = (ConnectionClass *) ConnectionHandle;
	RETCODE	ret = SQL_SUCCESS;
	SQLINTEGER	len = 4;

	mylog("PGAPI_GetConnectAttr %d\n", Attribute);
	switch (Attribute)
	{
		case SQL_ATTR_ASYNC_ENABLE:
			*((SQLUINTEGER *) Value) = SQL_ASYNC_ENABLE_OFF;
			break;
		case SQL_ATTR_AUTO_IPD:
			*((SQLUINTEGER *) Value) = SQL_FALSE;
			break;
		case SQL_ATTR_CONNECTION_DEAD:
			*((SQLUINTEGER *) Value) = (conn->status == CONN_NOT_CONNECTED || conn->status == CONN_DOWN);
			break;
		case SQL_ATTR_CONNECTION_TIMEOUT:
			*((SQLUINTEGER *) Value) = 0;
			break;
		case SQL_ATTR_METADATA_ID:
			*((SQLUINTEGER *) Value) = conn->stmtOptions.metadata_id;
			break;
		default:
			ret = PGAPI_GetConnectOption(ConnectionHandle, (UWORD) Attribute, Value);
	}
	if (StringLength)
		*StringLength = len;
	return ret;
}

static SQLHDESC
descHandleFromStatementHandle(HSTMT StatementHandle, SQLINTEGER descType) 
{
	StatementClass	*stmt = (StatementClass *) StatementHandle;

	switch (descType)
	{
		case SQL_ATTR_APP_ROW_DESC:		/* 10010 */
			return (HSTMT) stmt->ard;
		case SQL_ATTR_APP_PARAM_DESC:		/* 10011 */
			return (HSTMT) stmt->apd;
		case SQL_ATTR_IMP_ROW_DESC:		/* 10012 */
			return (HSTMT) stmt->ird;
		case SQL_ATTR_IMP_PARAM_DESC:		/* 10013 */
			return (HSTMT) stmt->ipd;
	}
	return (HSTMT) 0;
}

static  void column_bindings_set(ARDFields *opts, int cols, BOOL maxset)
{
	int	i;

	if (cols == opts->allocated)
		return;
	if (cols > opts->allocated)
	{
		extend_column_bindings(opts, cols);
		return;
	}
	if (maxset)	return;

	for (i = opts->allocated; i > cols; i--)
		reset_a_column_binding(opts, i);
	opts->allocated = cols;
	if (0 == cols)
	{
		free(opts->bindings);
		opts->bindings = NULL;
	}
}

static RETCODE SQL_API
ARDSetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength)
{
	RETCODE		ret = SQL_SUCCESS;
	PTR		tptr;
	ARDFields	*opts = (ARDFields *) (desc + 1);
	SQLSMALLINT	row_idx;

	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_SIZE:
			opts->size_of_rowset = (SQLUINTEGER) Value;
			return ret; 
		case SQL_DESC_ARRAY_STATUS_PTR:
			opts->row_operation_ptr = Value;
			return ret;
		case SQL_DESC_BIND_OFFSET_PTR:
			opts->row_offset_ptr = Value;
			return ret;
		case SQL_DESC_BIND_TYPE:
			opts->bind_size = (SQLUINTEGER) Value;
			return ret;
		case SQL_DESC_COUNT:
			column_bindings_set(opts, (SQLUINTEGER) Value, FALSE);
			return ret;

		case SQL_DESC_TYPE:
		case SQL_DESC_DATETIME_INTERVAL_CODE:
		case SQL_DESC_CONCISE_TYPE:
			column_bindings_set(opts, RecNumber, TRUE);
			break;
	}
	if (RecNumber < 0 || RecNumber > opts->allocated)
	{
		DC_set_error(desc, DESC_INVALID_COLUMN_NUMBER_ERROR, "invalid column number");
		return SQL_ERROR;
	}
	if (0 == RecNumber) /* bookmark column */
	{
		BindInfoClass	*bookmark = ARD_AllocBookmark(opts);

		switch (FieldIdentifier)
		{
			case SQL_DESC_DATA_PTR:
				bookmark->buffer = Value;
				break;
			case SQL_DESC_INDICATOR_PTR:
				tptr = bookmark->used;
				if (Value != tptr)
				{
					DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER, "INDICATOR != OCTET_LENGTH_PTR"); 
					ret = SQL_ERROR;
				}
				break;
			case SQL_DESC_OCTET_LENGTH_PTR:
				bookmark->used = Value;
				break;
			default:
				DC_set_error(desc, DESC_INVALID_COLUMN_NUMBER_ERROR, "invalid column number");
				ret = SQL_ERROR;
		}
		return ret;
	}
	row_idx = RecNumber - 1;
	switch (FieldIdentifier)
	{
		case SQL_DESC_TYPE:
			reset_a_column_binding(opts, RecNumber);
			opts->bindings[row_idx].returntype = (Int4) Value;
			break;
		case SQL_DESC_DATETIME_INTERVAL_CODE:
			switch (opts->bindings[row_idx].returntype)
			{
				case SQL_DATETIME:
				case SQL_C_TYPE_DATE:
				case SQL_C_TYPE_TIME:
				case SQL_C_TYPE_TIMESTAMP:
				switch ((Int4) Value)
				{
					case SQL_CODE_DATE:
						opts->bindings[row_idx].returntype = SQL_C_TYPE_DATE;
						break;
					case SQL_CODE_TIME:
						opts->bindings[row_idx].returntype = SQL_C_TYPE_TIME;
						break;
					case SQL_CODE_TIMESTAMP:
						opts->bindings[row_idx].returntype = SQL_C_TYPE_TIMESTAMP;
						break;
				}
				break;
			}
			break;
		case SQL_DESC_CONCISE_TYPE:
			opts->bindings[row_idx].returntype = (Int4) Value;
			break;
		case SQL_DESC_DATA_PTR:
			opts->bindings[row_idx].buffer = Value;
			break;
		case SQL_DESC_INDICATOR_PTR:
			tptr = opts->bindings[row_idx].used;
			if (Value != tptr)
			{
				ret = SQL_ERROR;
				DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER, "INDICATOR != OCTET_LENGTH_PTR"); 
			}
			break;
		case SQL_DESC_OCTET_LENGTH_PTR:
			opts->bindings[row_idx].used = Value;
			break;
		case SQL_DESC_OCTET_LENGTH:
			opts->bindings[row_idx].buflen = (Int4) Value;
			break;
		case SQL_DESC_PRECISION:
			opts->bindings[row_idx].precision = (Int2)((Int4) Value);
			break;
		case SQL_DESC_SCALE:
			opts->bindings[row_idx].scale = (Int4) Value;
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		case SQL_DESC_LENGTH:
		case SQL_DESC_NUM_PREC_RADIX:
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invalid descriptor identifier"); 
	}
	return ret;
}

static  void parameter_bindings_set(APDFields *opts, int params, BOOL maxset)
{
	int	i;

	if (params == opts->allocated)
		return;
	if (params > opts->allocated)
	{
		extend_parameter_bindings(opts, params);
		return;
	}
	if (maxset)	return;

	for (i = opts->allocated; i > params; i--)
		reset_a_parameter_binding(opts, i);
	opts->allocated = params;
	if (0 == params)
	{
		free(opts->parameters);
		opts->parameters = NULL;
	}
}

static  void parameter_ibindings_set(IPDFields *opts, int params, BOOL maxset)
{
	int	i;

	if (params == opts->allocated)
		return;
	if (params > opts->allocated)
	{
		extend_iparameter_bindings(opts, params);
		return;
	}
	if (maxset)	return;

	for (i = opts->allocated; i > params; i--)
		reset_a_iparameter_binding(opts, i);
	opts->allocated = params;
	if (0 == params)
	{
		free(opts->parameters);
		opts->parameters = NULL;
	}
}

static RETCODE SQL_API
APDSetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength)
{
	RETCODE		ret = SQL_SUCCESS;
	APDFields	*opts = (APDFields *) (desc + 1);
	SQLSMALLINT	para_idx;

	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_SIZE:
			opts->paramset_size = (SQLUINTEGER) Value;
			return ret; 
		case SQL_DESC_ARRAY_STATUS_PTR:
			opts->param_operation_ptr = Value;
			return ret;
		case SQL_DESC_BIND_OFFSET_PTR:
			opts->param_offset_ptr = Value;
			return ret;
		case SQL_DESC_BIND_TYPE:
			opts->param_bind_type = (SQLUINTEGER) Value;
			return ret;
		case SQL_DESC_COUNT:
			parameter_bindings_set(opts, (SQLUINTEGER) Value, FALSE);
			return ret; 

		case SQL_DESC_TYPE:
		case SQL_DESC_DATETIME_INTERVAL_CODE:
		case SQL_DESC_CONCISE_TYPE:
			parameter_bindings_set(opts, RecNumber, TRUE);
			break;
	}
	if (RecNumber <=0)
	{
inolog("APDSetField RecN=%d allocated=%d\n", RecNumber, opts->allocated);
		DC_set_error(desc, DESC_BAD_PARAMETER_NUMBER_ERROR,
				"bad parameter number");
		return SQL_ERROR;
	}
	if (RecNumber > opts->allocated)
	{
inolog("APDSetField RecN=%d allocated=%d\n", RecNumber, opts->allocated);
		parameter_bindings_set(opts, RecNumber, TRUE);
		/* DC_set_error(desc, DESC_BAD_PARAMETER_NUMBER_ERROR,
				"bad parameter number");
		return SQL_ERROR;*/
	}
	para_idx = RecNumber - 1; 
	switch (FieldIdentifier)
	{
		case SQL_DESC_TYPE:
			reset_a_parameter_binding(opts, RecNumber);
			opts->parameters[para_idx].CType = (Int4) Value;
			break;
		case SQL_DESC_DATETIME_INTERVAL_CODE:
			switch (opts->parameters[para_idx].CType)
			{
				case SQL_DATETIME:
				case SQL_C_TYPE_DATE:
				case SQL_C_TYPE_TIME:
				case SQL_C_TYPE_TIMESTAMP:
				switch ((Int4) Value)
				{
					case SQL_CODE_DATE:
						opts->parameters[para_idx].CType = SQL_C_TYPE_DATE;
						break;
					case SQL_CODE_TIME:
						opts->parameters[para_idx].CType = SQL_C_TYPE_TIME;
						break;
					case SQL_CODE_TIMESTAMP:
						opts->parameters[para_idx].CType = SQL_C_TYPE_TIMESTAMP;
						break;
				}
				break;
			}
			break;
		case SQL_DESC_CONCISE_TYPE:
			opts->parameters[para_idx].CType = (Int4) Value;
			break;
		case SQL_DESC_DATA_PTR:
			opts->parameters[para_idx].buffer = Value;
			break;
		case SQL_DESC_INDICATOR_PTR:
			if (Value != opts->parameters[para_idx].used)
			{
				ret = SQL_ERROR;
				DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER, "INDICATOR != OCTET_LENGTH_PTR"); 
			}
			break;
		case SQL_DESC_OCTET_LENGTH:
			opts->parameters[para_idx].buflen = (Int4) Value;
			break;
		case SQL_DESC_OCTET_LENGTH_PTR:
			opts->parameters[para_idx].used = Value;
			break;
		case SQL_DESC_PRECISION:
			opts->parameters[para_idx].precision = (Int2) ((Int4) Value);
			break;
		case SQL_DESC_SCALE:
			opts->parameters[para_idx].scale = (Int2) ((Int4) Value);
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		case SQL_DESC_LENGTH:
		case SQL_DESC_NUM_PREC_RADIX:
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invaid descriptor identifier"); 
	}
	return ret;
}

static RETCODE SQL_API
IRDSetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength)
{
	RETCODE		ret = SQL_SUCCESS;
	IRDFields	*opts = (IRDFields *) (desc + 1);

	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_STATUS_PTR:
			opts->rowStatusArray = (SQLUSMALLINT *) Value;
			break;
		case SQL_DESC_ROWS_PROCESSED_PTR:
			opts->rowsFetched = (UInt4 *) Value;
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */
		case SQL_DESC_COUNT: /* read-only */
		case SQL_DESC_AUTO_UNIQUE_VALUE: /* read-only */
		case SQL_DESC_BASE_COLUMN_NAME: /* read-only */
		case SQL_DESC_BASE_TABLE_NAME: /* read-only */
		case SQL_DESC_CASE_SENSITIVE: /* read-only */
		case SQL_DESC_CATALOG_NAME: /* read-only */
		case SQL_DESC_CONCISE_TYPE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_CODE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_PRECISION: /* read-only */
		case SQL_DESC_DISPLAY_SIZE: /* read-only */
		case SQL_DESC_FIXED_PREC_SCALE: /* read-only */
		case SQL_DESC_LABEL: /* read-only */
		case SQL_DESC_LENGTH: /* read-only */
		case SQL_DESC_LITERAL_PREFIX: /* read-only */
		case SQL_DESC_LITERAL_SUFFIX: /* read-only */
		case SQL_DESC_LOCAL_TYPE_NAME: /* read-only */
		case SQL_DESC_NAME: /* read-only */
		case SQL_DESC_NULLABLE: /* read-only */
		case SQL_DESC_NUM_PREC_RADIX: /* read-only */
		case SQL_DESC_OCTET_LENGTH: /* read-only */
		case SQL_DESC_PRECISION: /* read-only */
#if (ODBCVER >= 0x0350)
		case SQL_DESC_ROWVER: /* read-only */
#endif /* ODBCVER */
		case SQL_DESC_SCALE: /* read-only */
		case SQL_DESC_SCHEMA_NAME: /* read-only */
		case SQL_DESC_SEARCHABLE: /* read-only */
		case SQL_DESC_TABLE_NAME: /* read-only */
		case SQL_DESC_TYPE: /* read-only */
		case SQL_DESC_TYPE_NAME: /* read-only */
		case SQL_DESC_UNNAMED: /* read-only */
		case SQL_DESC_UNSIGNED: /* read-only */
		case SQL_DESC_UPDATABLE: /* read-only */
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invalid descriptor identifier"); 
	}
	return ret;
}

static RETCODE SQL_API
IPDSetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength)
{
	RETCODE		ret = SQL_SUCCESS;
	IPDFields	*ipdopts = (IPDFields *) (desc + 1);
	SQLSMALLINT	para_idx;

	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_STATUS_PTR:
			ipdopts->param_status_ptr = (SQLUSMALLINT *) Value;
			return ret;
		case SQL_DESC_ROWS_PROCESSED_PTR:
			ipdopts->param_processed_ptr = (UInt4 *) Value;
			return ret;
		case SQL_DESC_COUNT:
			parameter_ibindings_set(ipdopts, (SQLUINTEGER) Value, FALSE);
			return ret;
		case SQL_DESC_UNNAMED: /* only SQL_UNNAMED is allowed */ 
			if (SQL_UNNAMED !=  (SQLUINTEGER) Value)
			{
				ret = SQL_ERROR;
				DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
					"invalid descriptor identifier");
				return ret;
			}
		case SQL_DESC_NAME:
		case SQL_DESC_TYPE:
		case SQL_DESC_DATETIME_INTERVAL_CODE:
		case SQL_DESC_CONCISE_TYPE:
			parameter_ibindings_set(ipdopts, RecNumber, TRUE);
			break;
	}
	if (RecNumber <= 0 || RecNumber > ipdopts->allocated)
	{
inolog("IPDSetField RecN=%d allocated=%d\n", RecNumber, ipdopts->allocated);
		DC_set_error(desc, DESC_BAD_PARAMETER_NUMBER_ERROR,
				"bad parameter number");
		return SQL_ERROR;
	}
	para_idx = RecNumber - 1;
	switch (FieldIdentifier)
	{
		case SQL_DESC_TYPE:
			reset_a_iparameter_binding(ipdopts, RecNumber);
			ipdopts->parameters[para_idx].SQLType = (Int4) Value;
			break;
		case SQL_DESC_DATETIME_INTERVAL_CODE:
			switch (ipdopts->parameters[para_idx].SQLType)
			{
				case SQL_DATETIME:
				case SQL_TYPE_DATE:
				case SQL_TYPE_TIME:
				case SQL_TYPE_TIMESTAMP:
				switch ((Int4) Value)
				{
					case SQL_CODE_DATE:
						ipdopts->parameters[para_idx].SQLType = SQL_TYPE_DATE;
						break;
					case SQL_CODE_TIME:
						ipdopts->parameters[para_idx].SQLType = SQL_TYPE_TIME;
						break;
					case SQL_CODE_TIMESTAMP:
						ipdopts->parameters[para_idx].SQLType = SQL_TYPE_TIMESTAMP;
						break;
				}
				break;
			}
			break;
		case SQL_DESC_CONCISE_TYPE:
			ipdopts->parameters[para_idx].SQLType = (Int4) Value;
			break;
		case SQL_DESC_NAME:
			if (Value)
				STR_TO_NAME(ipdopts->parameters[para_idx].paramName, Value);
			else
				NULL_THE_NAME(ipdopts->parameters[para_idx].paramName);
			break;
		case SQL_DESC_PARAMETER_TYPE:
			ipdopts->parameters[para_idx].paramType = (Int2) ((Int4) Value);
			break;
		case SQL_DESC_SCALE:
			ipdopts->parameters[para_idx].decimal_digits = (Int2) ((Int4) Value);
			break;
		case SQL_DESC_UNNAMED: /* only SQL_UNNAMED is allowed */ 
			if (SQL_UNNAMED !=  (SQLUINTEGER) Value)
			{
				ret = SQL_ERROR;
				DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
					"invalid descriptor identifier");
			}
			else
				NULL_THE_NAME(ipdopts->parameters[para_idx].paramName);
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */ 
		case SQL_DESC_CASE_SENSITIVE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		case SQL_DESC_FIXED_PREC_SCALE: /* read-only */
		case SQL_DESC_LENGTH:
		case SQL_DESC_LOCAL_TYPE_NAME: /* read-only */
		case SQL_DESC_NULLABLE: /* read-only */
		case SQL_DESC_NUM_PREC_RADIX:
		case SQL_DESC_OCTET_LENGTH:
		case SQL_DESC_PRECISION:
#if (ODBCVER >= 0x0350)
		case SQL_DESC_ROWVER: /* read-only */
#endif /* ODBCVER */
		case SQL_DESC_TYPE_NAME: /* read-only */
		case SQL_DESC_UNSIGNED: /* read-only */
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invalid descriptor identifier"); 
	}
	return ret;
}


static RETCODE SQL_API
ARDGetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength,
		SQLINTEGER *StringLength)
{
	RETCODE		ret = SQL_SUCCESS;
	SQLINTEGER	len, ival, rettype = 0;
	PTR		ptr = NULL;
	const ARDFields	*opts = (ARDFields *) (desc + 1);
	SQLSMALLINT	row_idx;

	len = 4;
	if (0 == RecNumber) /* bookmark */
	{
		BindInfoClass	*bookmark = opts->bookmark;
		switch (FieldIdentifier)
		{
			case SQL_DESC_DATA_PTR:
				rettype = SQL_IS_POINTER;
				ptr = bookmark ? bookmark->buffer : NULL;
				break;
			case SQL_DESC_INDICATOR_PTR:
				rettype = SQL_IS_POINTER;
				ptr = bookmark ? bookmark->used : NULL;
				break;
			case SQL_DESC_OCTET_LENGTH_PTR:
				rettype = SQL_IS_POINTER;
				ptr = bookmark ? bookmark->used : NULL;
				break;
		}
		if (ptr)
		{
			*((void **) Value) = ptr;
			if (StringLength)
				*StringLength = len;
			return ret;
		}
	}
	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_SIZE:
		case SQL_DESC_ARRAY_STATUS_PTR:
		case SQL_DESC_BIND_OFFSET_PTR:
		case SQL_DESC_BIND_TYPE:
		case SQL_DESC_COUNT:
			break;
		default:
			if (RecNumber <= 0 || RecNumber > opts->allocated)
			{
				DC_set_error(desc, DESC_INVALID_COLUMN_NUMBER_ERROR,
					"invalid column number");
				return SQL_ERROR;
			}
	}
	row_idx = RecNumber - 1;
	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_SIZE:
			ival = opts->size_of_rowset;
			break; 
		case SQL_DESC_ARRAY_STATUS_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->row_operation_ptr;
			break;
		case SQL_DESC_BIND_OFFSET_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->row_offset_ptr;
			break;
		case SQL_DESC_BIND_TYPE:
			ival = opts->bind_size;
			break;
		case SQL_DESC_TYPE:
			switch (opts->bindings[row_idx].returntype)
			{
				case SQL_C_TYPE_DATE:
				case SQL_C_TYPE_TIME:
				case SQL_C_TYPE_TIMESTAMP:
					ival = SQL_DATETIME;
					break;
				default:
					ival = opts->bindings[row_idx].returntype;
			}
			break;
		case SQL_DESC_DATETIME_INTERVAL_CODE:
			switch (opts->bindings[row_idx].returntype)
			{
				case SQL_C_TYPE_DATE:
					ival = SQL_CODE_DATE;
					break;
				case SQL_C_TYPE_TIME:
					ival = SQL_CODE_TIME;
					break;
				case SQL_C_TYPE_TIMESTAMP:
					ival = SQL_CODE_TIMESTAMP;
					break;
				default:
					ival = 0;
					break;
			}
			break;
		case SQL_DESC_CONCISE_TYPE:
			ival = opts->bindings[row_idx].returntype;
			break;
		case SQL_DESC_DATA_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->bindings[row_idx].buffer;
			break;
		case SQL_DESC_INDICATOR_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->bindings[row_idx].used;
			break;
		case SQL_DESC_OCTET_LENGTH_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->bindings[row_idx].used;
			break;
		case SQL_DESC_COUNT:
			ival = opts->allocated;
			break;
		case SQL_DESC_OCTET_LENGTH:
			ival = opts->bindings[row_idx].buflen;
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */
			if (desc->embedded)
				ival = SQL_DESC_ALLOC_AUTO;
			else
				ival = SQL_DESC_ALLOC_USER;
			break;
		case SQL_DESC_PRECISION:
			ival = opts->bindings[row_idx].precision;
			break;
		case SQL_DESC_SCALE:
			ival = opts->bindings[row_idx].scale;
			break;
		case SQL_DESC_NUM_PREC_RADIX:
			ival = 10;
			break;
		case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		case SQL_DESC_LENGTH:
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invalid descriptor identifier"); 
	}
	switch (rettype)
	{
		case 0:
		case SQL_IS_INTEGER:
			len = 4;
			*((SQLINTEGER *) Value) = ival;
			break;
		case SQL_IS_POINTER:
			len = 4;
			*((void **) Value) = ptr;
			break;
	}
			
	if (StringLength)
		*StringLength = len;
	return ret;
}

static RETCODE SQL_API
APDGetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength,
		SQLINTEGER *StringLength)
{
	RETCODE		ret = SQL_SUCCESS;
	SQLINTEGER	ival = 0, len, rettype = 0;
	PTR		ptr = NULL;
	const APDFields	*opts = (const APDFields *) (desc + 1);
	SQLSMALLINT	para_idx;

	len = 4;
	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_SIZE:
		case SQL_DESC_ARRAY_STATUS_PTR:
		case SQL_DESC_BIND_OFFSET_PTR:
		case SQL_DESC_BIND_TYPE:
		case SQL_DESC_COUNT:
			break; 
		default:if (RecNumber <= 0 || RecNumber > opts->allocated)
			{
inolog("APDGetField RecN=%d allocated=%d\n", RecNumber, opts->allocated);
				DC_set_error(desc, DESC_BAD_PARAMETER_NUMBER_ERROR,
					"bad parameter number");
				return SQL_ERROR;
			} 
	}
	para_idx = RecNumber - 1;
	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_SIZE:
			rettype = SQL_IS_POINTER;
			ival = opts->paramset_size;
			break; 
		case SQL_DESC_ARRAY_STATUS_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->param_operation_ptr;
			break;
		case SQL_DESC_BIND_OFFSET_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->param_offset_ptr;
			break;
		case SQL_DESC_BIND_TYPE:
			ival = opts->param_bind_type;
			break;

		case SQL_DESC_TYPE:
			switch (opts->parameters[para_idx].CType)
			{
				case SQL_C_TYPE_DATE:
				case SQL_C_TYPE_TIME:
				case SQL_C_TYPE_TIMESTAMP:
					ival = SQL_DATETIME;
					break;
				default:
					ival = opts->parameters[para_idx].CType;
			}
			break;
		case SQL_DESC_DATETIME_INTERVAL_CODE:
			switch (opts->parameters[para_idx].CType)
			{
				case SQL_C_TYPE_DATE:
					ival = SQL_CODE_DATE;
					break;
				case SQL_C_TYPE_TIME:
					ival = SQL_CODE_TIME;
					break;
				case SQL_C_TYPE_TIMESTAMP:
					ival = SQL_CODE_TIMESTAMP;
					break;
				default:
					ival = 0;
					break;
			}
			break;
		case SQL_DESC_CONCISE_TYPE:
			ival = opts->parameters[para_idx].CType;
			break;
		case SQL_DESC_DATA_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->parameters[para_idx].buffer;
			break;
		case SQL_DESC_INDICATOR_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->parameters[para_idx].used;
			break;
		case SQL_DESC_OCTET_LENGTH:
			ival = opts->parameters[para_idx].buflen;
			break;
		case SQL_DESC_OCTET_LENGTH_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->parameters[para_idx].used;
			break;
		case SQL_DESC_COUNT:
			ival = opts->allocated;
			break; 
		case SQL_DESC_ALLOC_TYPE: /* read-only */
			if (desc->embedded)
				ival = SQL_DESC_ALLOC_AUTO;
			else
				ival = SQL_DESC_ALLOC_USER;
			break;
		case SQL_DESC_NUM_PREC_RADIX:
			ival = 10;
			break;
		case SQL_DESC_PRECISION:
			ival = opts->parameters[para_idx].precision;
			break;
		case SQL_DESC_SCALE:
			ival = opts->parameters[para_idx].scale;
			break;
		case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		case SQL_DESC_LENGTH:
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
					"invalid descriptor identifer"); 
	}
	switch (rettype)
	{
		case 0:
		case SQL_IS_INTEGER:
			len = 4;
			*((Int4 *) Value) = ival;
			break;
		case SQL_IS_POINTER:
			len = 4;
			*((void **) Value) = ptr;
			break;
	}
			
	if (StringLength)
		*StringLength = len;
	return ret;
}

static RETCODE SQL_API
IRDGetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength,
		SQLINTEGER *StringLength)
{
	RETCODE		ret = SQL_SUCCESS;
	SQLINTEGER	ival = 0, len, rettype = 0;
	PTR		ptr = NULL;
	BOOL		bCallColAtt = FALSE;
	const IRDFields	*opts = (IRDFields *) (desc + 1);

	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_STATUS_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->rowStatusArray;
			break;
		case SQL_DESC_ROWS_PROCESSED_PTR:
			rettype = SQL_IS_POINTER;
			ptr = opts->rowsFetched;
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */
			ival = SQL_DESC_ALLOC_AUTO;
			break;
		case SQL_DESC_COUNT: /* read-only */
		case SQL_DESC_AUTO_UNIQUE_VALUE: /* read-only */
		case SQL_DESC_CASE_SENSITIVE: /* read-only */
		case SQL_DESC_CONCISE_TYPE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_CODE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_PRECISION: /* read-only */
		case SQL_DESC_DISPLAY_SIZE: /* read-only */
		case SQL_DESC_FIXED_PREC_SCALE: /* read-only */
		case SQL_DESC_LENGTH: /* read-only */
		case SQL_DESC_NULLABLE: /* read-only */
		case SQL_DESC_NUM_PREC_RADIX: /* read-only */
		case SQL_DESC_OCTET_LENGTH: /* read-only */
		case SQL_DESC_PRECISION: /* read-only */
#if (ODBCVER >= 0x0350)
		case SQL_DESC_ROWVER: /* read-only */
#endif /* ODBCVER */
		case SQL_DESC_SCALE: /* read-only */
		case SQL_DESC_SEARCHABLE: /* read-only */
		case SQL_DESC_TYPE: /* read-only */
		case SQL_DESC_UNNAMED: /* read-only */
		case SQL_DESC_UNSIGNED: /* read-only */
		case SQL_DESC_UPDATABLE: /* read-only */
			bCallColAtt = TRUE;
			break;
		case SQL_DESC_BASE_COLUMN_NAME: /* read-only */
		case SQL_DESC_BASE_TABLE_NAME: /* read-only */
		case SQL_DESC_CATALOG_NAME: /* read-only */
		case SQL_DESC_LABEL: /* read-only */
		case SQL_DESC_LITERAL_PREFIX: /* read-only */
		case SQL_DESC_LITERAL_SUFFIX: /* read-only */
		case SQL_DESC_LOCAL_TYPE_NAME: /* read-only */
		case SQL_DESC_NAME: /* read-only */
		case SQL_DESC_SCHEMA_NAME: /* read-only */
		case SQL_DESC_TABLE_NAME: /* read-only */
		case SQL_DESC_TYPE_NAME: /* read-only */
			rettype = SQL_NTS;
			bCallColAtt = TRUE;
			break; 
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invalid descriptor identifier"); 
	}
	if (bCallColAtt)
	{
		SQLSMALLINT	pcbL;
		StatementClass	*stmt;

		stmt = opts->stmt;
		ret = PGAPI_ColAttributes(stmt, RecNumber,
			FieldIdentifier, Value, (SQLSMALLINT) BufferLength,
				&pcbL, &ival);
		len = pcbL;
	} 
	switch (rettype)
	{
		case 0:
		case SQL_IS_INTEGER:
			len = 4;
			*((Int4 *) Value) = ival;
			break;
		case SQL_IS_UINTEGER:
			len = 4;
			*((UInt4 *) Value) = ival;
			break;
		case SQL_IS_POINTER:
			len = 4;
			*((void **) Value) = ptr;
			break;
	}
			
	if (StringLength)
		*StringLength = len;
	return ret;
}

static RETCODE SQL_API
IPDGetField(DescriptorClass *desc, SQLSMALLINT RecNumber,
		SQLSMALLINT FieldIdentifier, PTR Value, SQLINTEGER BufferLength,
		SQLINTEGER *StringLength)
{
	RETCODE		ret = SQL_SUCCESS;
	SQLINTEGER	ival = 0, len, rettype = 0;
	PTR		ptr = NULL;
	const IPDFields	*ipdopts = (const IPDFields *) (desc + 1);
	SQLSMALLINT	para_idx;

	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_STATUS_PTR:
		case SQL_DESC_ROWS_PROCESSED_PTR:
		case SQL_DESC_COUNT:
			break; 
		default:if (RecNumber <= 0 || RecNumber > ipdopts->allocated)
			{
inolog("IPDGetField RecN=%d allocated=%d\n", RecNumber, ipdopts->allocated);
				DC_set_error(desc, DESC_BAD_PARAMETER_NUMBER_ERROR,
					"bad parameter number");
				return SQL_ERROR;
			}
	}
	para_idx = RecNumber - 1;
	switch (FieldIdentifier)
	{
		case SQL_DESC_ARRAY_STATUS_PTR:
			rettype = SQL_IS_POINTER;
			ptr = ipdopts->param_status_ptr;
			break;
		case SQL_DESC_ROWS_PROCESSED_PTR:
			rettype = SQL_IS_POINTER;
			ptr = ipdopts->param_processed_ptr;
			break;
		case SQL_DESC_UNNAMED:
			ival = NAME_IS_NULL(ipdopts->parameters[para_idx].paramName) ? SQL_UNNAMED : SQL_NAMED;
			break;
		case SQL_DESC_TYPE:
			switch (ipdopts->parameters[para_idx].SQLType)
			{
				case SQL_TYPE_DATE:
				case SQL_TYPE_TIME:
				case SQL_TYPE_TIMESTAMP:
					ival = SQL_DATETIME;
					break;
				default:
					ival = ipdopts->parameters[para_idx].SQLType;
			}
			break;
		case SQL_DESC_DATETIME_INTERVAL_CODE:
			switch (ipdopts->parameters[para_idx].SQLType)
			{
				case SQL_TYPE_DATE:
					ival = SQL_CODE_DATE;
				case SQL_TYPE_TIME:
					ival = SQL_CODE_TIME;
					break;
				case SQL_TYPE_TIMESTAMP:
					ival = SQL_CODE_TIMESTAMP;
					break;
				default:
					ival = 0;
			}
			break;
		case SQL_DESC_CONCISE_TYPE:
			ival = ipdopts->parameters[para_idx].SQLType;
			break;
		case SQL_DESC_COUNT:
			ival = ipdopts->allocated;
			break; 
		case SQL_DESC_PARAMETER_TYPE:
			ival = ipdopts->parameters[para_idx].paramType;
			break;
		case SQL_DESC_PRECISION:
			switch (ipdopts->parameters[para_idx].SQLType)
			{
				case SQL_TYPE_DATE:
				case SQL_TYPE_TIME:
				case SQL_TYPE_TIMESTAMP:
				case SQL_DATETIME:
					ival = ipdopts->parameters[para_idx].decimal_digits;
					break;
			}
			break;
		case SQL_DESC_SCALE:
			switch (ipdopts->parameters[para_idx].SQLType)
			{
				case SQL_NUMERIC:
					ival = ipdopts->parameters[para_idx].decimal_digits;
					break;
			}
			break;
		case SQL_DESC_ALLOC_TYPE: /* read-only */
			ival = SQL_DESC_ALLOC_AUTO;
			break; 
		case SQL_DESC_CASE_SENSITIVE: /* read-only */
		case SQL_DESC_DATETIME_INTERVAL_PRECISION:
		case SQL_DESC_FIXED_PREC_SCALE: /* read-only */
		case SQL_DESC_LENGTH:
		case SQL_DESC_LOCAL_TYPE_NAME: /* read-only */
		case SQL_DESC_NAME:
		case SQL_DESC_NULLABLE: /* read-only */
		case SQL_DESC_NUM_PREC_RADIX:
		case SQL_DESC_OCTET_LENGTH:
#if (ODBCVER >= 0x0350)
		case SQL_DESC_ROWVER: /* read-only */
#endif /* ODBCVER */
		case SQL_DESC_TYPE_NAME: /* read-only */
		case SQL_DESC_UNSIGNED: /* read-only */
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INVALID_DESCRIPTOR_IDENTIFIER,
				"invalid descriptor identifier"); 
	}
	switch (rettype)
	{
		case 0:
		case SQL_IS_INTEGER:
			len = 4;
			*((Int4 *) Value) = ival;
			break;
		case SQL_IS_POINTER:
			len = 4;
			*((void **)Value) = ptr;
			break;
	}
			
	if (StringLength)
		*StringLength = len;
	return ret;
}

/*	SQLGetStmtOption -> SQLGetStmtAttr */
RETCODE		SQL_API
PGAPI_GetStmtAttr(HSTMT StatementHandle,
		SQLINTEGER Attribute, PTR Value,
		SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	CSTR func = "PGAPI_GetStmtAttr";
	StatementClass *stmt = (StatementClass *) StatementHandle;
	RETCODE		ret = SQL_SUCCESS;
	int			len = 0;

	mylog("%s Handle=%x %d\n", func, StatementHandle, Attribute);
	switch (Attribute)
	{
		case SQL_ATTR_FETCH_BOOKMARK_PTR:		/* 16 */
			*((void **) Value) = stmt->options.bookmark_ptr;
			len = 4;
			break;
		case SQL_ATTR_PARAM_BIND_OFFSET_PTR:	/* 17 */
			*((SQLUINTEGER **) Value) = (SQLUINTEGER *) SC_get_APDF(stmt)->param_offset_ptr;
			len = 4;
			break;
		case SQL_ATTR_PARAM_BIND_TYPE:	/* 18 */
			*((SQLUINTEGER *) Value) = SC_get_APDF(stmt)->param_bind_type;
			len = 4;
			break;
		case SQL_ATTR_PARAM_OPERATION_PTR:		/* 19 */
			*((SQLUSMALLINT **) Value) = SC_get_APDF(stmt)->param_operation_ptr;
			len = 4;
			break;
		case SQL_ATTR_PARAM_STATUS_PTR: /* 20 */
			*((SQLUSMALLINT **) Value) = SC_get_IPDF(stmt)->param_status_ptr;
			len = 4;
			break;
		case SQL_ATTR_PARAMS_PROCESSED_PTR:		/* 21 */
			*((SQLUINTEGER **) Value) = (SQLUINTEGER *) SC_get_IPDF(stmt)->param_processed_ptr;
			len = 4;
			break;
		case SQL_ATTR_PARAMSET_SIZE:	/* 22 */
			*((SQLUINTEGER *) Value) = SC_get_APDF(stmt)->paramset_size;
			len = 4;
			break;
		case SQL_ATTR_ROW_BIND_OFFSET_PTR:		/* 23 */
			*((SQLUINTEGER **) Value) = (SQLUINTEGER *) SC_get_ARDF(stmt)->row_offset_ptr;
			len = 4;
			break;
		case SQL_ATTR_ROW_OPERATION_PTR:		/* 24 */
			*((SQLUSMALLINT **) Value) = SC_get_ARDF(stmt)->row_operation_ptr;
			len = 4;
			break;
		case SQL_ATTR_ROW_STATUS_PTR:	/* 25 */
			*((SQLUSMALLINT **) Value) = SC_get_IRDF(stmt)->rowStatusArray;
			len = 4;
			break;
		case SQL_ATTR_ROWS_FETCHED_PTR: /* 26 */
			*((SQLUINTEGER **) Value) = (SQLUINTEGER *) SC_get_IRDF(stmt)->rowsFetched;
			len = 4;
			break;
		case SQL_ATTR_ROW_ARRAY_SIZE:	/* 27 */
			*((SQLUINTEGER *) Value) = SC_get_ARDF(stmt)->size_of_rowset;
			len = 4;
			break;
		case SQL_ATTR_APP_ROW_DESC:		/* 10010 */
		case SQL_ATTR_APP_PARAM_DESC:	/* 10011 */
		case SQL_ATTR_IMP_ROW_DESC:		/* 10012 */
		case SQL_ATTR_IMP_PARAM_DESC:	/* 10013 */
			len = 4;
			*((HSTMT *) Value) = descHandleFromStatementHandle(StatementHandle, Attribute); 
			break;

		case SQL_ATTR_CURSOR_SCROLLABLE:		/* -1 */
			len = 4;
			if (SQL_CURSOR_FORWARD_ONLY == stmt->options.cursor_type)
				*((SQLUINTEGER *) Value) = SQL_NONSCROLLABLE;
			else
				*((SQLUINTEGER *) Value) = SQL_SCROLLABLE;
			break;
		case SQL_ATTR_CURSOR_SENSITIVITY:		/* -2 */
			len = 4;
			if (SQL_CONCUR_READ_ONLY == stmt->options.scroll_concurrency)
				*((SQLUINTEGER *) Value) = SQL_INSENSITIVE;
			else
				*((SQLUINTEGER *) Value) = SQL_UNSPECIFIED;
			break;
		case SQL_ATTR_METADATA_ID:		/* 10014 */
			*((SQLUINTEGER *) Value) = stmt->options.metadata_id;
			break;
		case SQL_ATTR_AUTO_IPD:	/* 10001 */
			/* case SQL_ATTR_ROW_BIND_TYPE: ** == SQL_BIND_TYPE(ODBC2.0) */
		case SQL_ATTR_ENABLE_AUTO_IPD:	/* 15 */
			SC_set_error(stmt, DESC_INVALID_OPTION_IDENTIFIER, "Unsupported statement option (Get)", func);
			return SQL_ERROR;
		default:
			len = 4;
			ret = PGAPI_GetStmtOption(StatementHandle, (UWORD) Attribute, Value);
	}
	if (ret == SQL_SUCCESS && StringLength)
		*StringLength = len;
	return ret;
}

/*	SQLSetConnectOption -> SQLSetConnectAttr */
RETCODE		SQL_API
PGAPI_SetConnectAttr(HDBC ConnectionHandle,
			SQLINTEGER Attribute, PTR Value,
			SQLINTEGER StringLength)
{
	CSTR	func = "PGAPI_SetConnectAttr";
	ConnectionClass *conn = (ConnectionClass *) ConnectionHandle;
	RETCODE	ret = SQL_SUCCESS;

	mylog("PGAPI_SetConnectAttr %d %x\n", Attribute, Value);
	switch (Attribute)
	{
		case SQL_ATTR_METADATA_ID:
			conn->stmtOptions.metadata_id = (SQLUINTEGER) Value;
			break;
#if (ODBCVER >= 0x0351)
		case SQL_ATTR_ANSI_APP:
			if (SQL_AA_FALSE != (SQLINTEGER) Value)
			{
				mylog("the application is ansi\n");
				if (CC_is_in_unicode_driver(conn)) /* the driver is unicode */
					CC_set_in_ansi_app(conn); /* but the app is ansi */
			}
			else
			{
				mylog("the application is unicode\n");
			}
			/*return SQL_ERROR;*/
			return SQL_SUCCESS;
#endif /* ODBCVER */
		case SQL_ATTR_ENLIST_IN_DTC:
#ifdef	WIN32
#ifdef	_HANDLE_ENLIST_IN_DTC_
			mylog("SQL_ATTR_ENLIST_IN_DTC %x request received\n", Value);
			return EnlistInDtc(conn, Value, conn->connInfo.xa_opt); /* telling a lie */
#endif /* _HANDLE_ENLIST_IN_DTC_ */
#endif /* WIN32 */
		case SQL_ATTR_ASYNC_ENABLE:
		case SQL_ATTR_AUTO_IPD:
		case SQL_ATTR_CONNECTION_DEAD:
		case SQL_ATTR_CONNECTION_TIMEOUT:
			CC_set_error(conn, CONN_OPTION_NOT_FOR_THE_DRIVER, "Unsupported connect attribute (Set)", func);
			return SQL_ERROR;
		default:
			ret = PGAPI_SetConnectOption(ConnectionHandle, (UWORD) Attribute, (UDWORD) Value);
	}
	return ret;
}

/*	new function */
RETCODE		SQL_API
PGAPI_GetDescField(SQLHDESC DescriptorHandle,
			SQLSMALLINT RecNumber, SQLSMALLINT FieldIdentifier,
			PTR Value, SQLINTEGER BufferLength,
			SQLINTEGER *StringLength)
{
	CSTR func = "PGAPI_GetDescField";
	RETCODE		ret = SQL_SUCCESS;
	DescriptorClass *desc = (DescriptorClass *) DescriptorHandle;

	mylog("%s h=%x rec=%d field=%d blen=%d\n", func, DescriptorHandle, RecNumber, FieldIdentifier, BufferLength);
	switch (desc->desc_type)
	{
		case SQL_ATTR_APP_ROW_DESC:
			ret = ARDGetField(desc, RecNumber, FieldIdentifier, Value, BufferLength, StringLength);
			break;
		case SQL_ATTR_APP_PARAM_DESC:
			ret = APDGetField(desc, RecNumber, FieldIdentifier, Value, BufferLength, StringLength);
			break;
		case SQL_ATTR_IMP_ROW_DESC:
			ret = IRDGetField(desc, RecNumber, FieldIdentifier, Value, BufferLength, StringLength);
			break;
		case SQL_ATTR_IMP_PARAM_DESC:
			ret = IPDGetField(desc, RecNumber, FieldIdentifier, Value, BufferLength, StringLength);
			break;
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INTERNAL_ERROR, "Error not implemented");
	}
	if (ret == SQL_ERROR)
	{
		if (!DC_get_errormsg(desc))
		{
			switch (DC_get_errornumber(desc))
			{
				case DESC_INVALID_DESCRIPTOR_IDENTIFIER:
					DC_set_errormsg(desc, "can't SQLGetDescField for this descriptor identifier");
					break;
				case DESC_INVALID_COLUMN_NUMBER_ERROR:
					DC_set_errormsg(desc, "can't SQLGetDescField for this column number");
					break;
				case DESC_BAD_PARAMETER_NUMBER_ERROR:
					DC_set_errormsg(desc, "can't SQLGetDescField for this parameter number");
					break;
			}
		} 
		DC_log_error(func, "", desc);
	}
	return ret;
}

/*	new function */
RETCODE		SQL_API
PGAPI_SetDescField(SQLHDESC DescriptorHandle,
			SQLSMALLINT RecNumber, SQLSMALLINT FieldIdentifier,
			PTR Value, SQLINTEGER BufferLength)
{
	CSTR func = "PGAPI_SetDescField";
	RETCODE		ret = SQL_SUCCESS;
	DescriptorClass *desc = (DescriptorClass *) DescriptorHandle;

	mylog("%s h=%x rec=%d field=%d val=%x,%d\n", func, DescriptorHandle, RecNumber, FieldIdentifier, Value, BufferLength);
	switch (desc->desc_type)
	{
		case SQL_ATTR_APP_ROW_DESC:
			ret = ARDSetField(desc, RecNumber, FieldIdentifier, Value, BufferLength);
			break;
		case SQL_ATTR_APP_PARAM_DESC:
			ret = APDSetField(desc, RecNumber, FieldIdentifier, Value, BufferLength);
			break;
		case SQL_ATTR_IMP_ROW_DESC:
			ret = IRDSetField(desc, RecNumber, FieldIdentifier, Value, BufferLength);
			break;
		case SQL_ATTR_IMP_PARAM_DESC:
			ret = IPDSetField(desc, RecNumber, FieldIdentifier, Value, BufferLength);
			break;
		default:ret = SQL_ERROR;
			DC_set_error(desc, DESC_INTERNAL_ERROR, "Error not implemented");
	}
	if (ret == SQL_ERROR)
	{
		if (!DC_get_errormsg(desc))
		{
			switch (DC_get_errornumber(desc))
			{
				case DESC_INVALID_DESCRIPTOR_IDENTIFIER:
					DC_set_errormsg(desc, "can't SQLSetDescField for this descriptor identifier");
				case DESC_INVALID_COLUMN_NUMBER_ERROR:
					DC_set_errormsg(desc, "can't SQLSetDescField for this column number");
					break;
				case DESC_BAD_PARAMETER_NUMBER_ERROR:
					DC_set_errormsg(desc, "can't SQLSetDescField for this parameter number");
					break;
				break;
			}
		} 
		DC_log_error(func, "", desc);
	}
	return ret;
}

/*	SQLSet(Param/Scroll/Stmt)Option -> SQLSetStmtAttr */
RETCODE		SQL_API
PGAPI_SetStmtAttr(HSTMT StatementHandle,
		SQLINTEGER Attribute, PTR Value,
		SQLINTEGER StringLength)
{
	RETCODE	ret = SQL_SUCCESS;
	CSTR func = "PGAPI_SetStmtAttr";
	StatementClass *stmt = (StatementClass *) StatementHandle;

	mylog("%s Handle=%x %d,%u\n", func, StatementHandle, Attribute, Value);
	switch (Attribute)
	{
		case SQL_ATTR_CURSOR_SCROLLABLE:		/* -1 */
		case SQL_ATTR_CURSOR_SENSITIVITY:		/* -2 */

		case SQL_ATTR_ENABLE_AUTO_IPD:	/* 15 */

		case SQL_ATTR_AUTO_IPD:	/* 10001 */
			SC_set_error(stmt, DESC_OPTION_NOT_FOR_THE_DRIVER, "Unsupported statement option (Set)", func);
			return SQL_ERROR;
		/* case SQL_ATTR_ROW_BIND_TYPE: ** == SQL_BIND_TYPE(ODBC2.0) */
		case SQL_ATTR_IMP_ROW_DESC:	/* 10012 (read-only) */
		case SQL_ATTR_IMP_PARAM_DESC:	/* 10013 (read-only) */

			/*
			 * case SQL_ATTR_PREDICATE_PTR: case
			 * SQL_ATTR_PREDICATE_OCTET_LENGTH_PTR:
			 */
			SC_set_error(stmt, DESC_INVALID_OPTION_IDENTIFIER, "Unsupported statement option (Set)", func);
			return SQL_ERROR;

		case SQL_ATTR_METADATA_ID:		/* 10014 */
			stmt->options.metadata_id = (SQLUINTEGER) Value; 
			break;
		case SQL_ATTR_APP_ROW_DESC:		/* 10010 */
			if (SQL_NULL_HDESC == Value)
			{
				stmt->ard = &(stmt->ardi);
			}
			else
			{ 
				stmt->ard = (ARDClass *) Value;
inolog("set ard=%x\n", stmt->ard);
			}
			break;
		case SQL_ATTR_APP_PARAM_DESC:	/* 10011 */
			if (SQL_NULL_HDESC == Value)
			{
				stmt->apd = &(stmt->apdi);
			}
			else
			{ 
				stmt->apd = (APDClass *) Value;
			}
			break;
		case SQL_ATTR_FETCH_BOOKMARK_PTR:		/* 16 */
			stmt->options.bookmark_ptr = Value;
			break;
		case SQL_ATTR_PARAM_BIND_OFFSET_PTR:	/* 17 */
			SC_get_APDF(stmt)->param_offset_ptr = (UInt4 *) Value;
			break;
		case SQL_ATTR_PARAM_BIND_TYPE:	/* 18 */
			SC_get_APDF(stmt)->param_bind_type = (SQLUINTEGER) Value;
			break;
		case SQL_ATTR_PARAM_OPERATION_PTR:		/* 19 */
			SC_get_APDF(stmt)->param_operation_ptr = Value;
			break;
		case SQL_ATTR_PARAM_STATUS_PTR:			/* 20 */
			SC_get_IPDF(stmt)->param_status_ptr = (SQLUSMALLINT *) Value;
			break;
		case SQL_ATTR_PARAMS_PROCESSED_PTR:		/* 21 */
			SC_get_IPDF(stmt)->param_processed_ptr = (UInt4 *) Value;
			break;
		case SQL_ATTR_PARAMSET_SIZE:	/* 22 */
			SC_get_APDF(stmt)->paramset_size = (SQLUINTEGER) Value;
			break;
		case SQL_ATTR_ROW_BIND_OFFSET_PTR:		/* 23 */
			SC_get_ARDF(stmt)->row_offset_ptr = (UInt4 *) Value;
			break;
		case SQL_ATTR_ROW_OPERATION_PTR:		/* 24 */
			SC_get_ARDF(stmt)->row_operation_ptr = Value;
			break;
		case SQL_ATTR_ROW_STATUS_PTR:	/* 25 */
			SC_get_IRDF(stmt)->rowStatusArray = (SQLUSMALLINT *) Value;
			break;
		case SQL_ATTR_ROWS_FETCHED_PTR: /* 26 */
			SC_get_IRDF(stmt)->rowsFetched = (UInt4 *) Value;
			break;
		case SQL_ATTR_ROW_ARRAY_SIZE:	/* 27 */
			SC_get_ARDF(stmt)->size_of_rowset = (SQLUINTEGER) Value;
			break;
		default:
			return PGAPI_SetStmtOption(StatementHandle, (UWORD) Attribute, (UDWORD) Value);
	}
	return SQL_SUCCESS;
}

#define	CALC_BOOKMARK_ADDR(book, offset, bind_size, index) \
	(book->buffer + offset + \
	(bind_size > 0 ? bind_size : (SQL_C_VARBOOKMARK == book->returntype ? book->buflen : sizeof(UInt4))) * index)    	

/*	SQL_NEED_DATA callback for PGAPI_BulkOperations */
typedef struct
{
	StatementClass	*stmt;
	SQLSMALLINT	operation;
	char		need_data_callback;
	char		auto_commit_needed;
	ARDFields	*opts;
	int		idx, processed;
}	bop_cdata;

static 
RETCODE	bulk_ope_callback(RETCODE retcode, void *para)
{
	RETCODE	ret = retcode;
	bop_cdata *s = (bop_cdata *) para;
	UInt4		offset, bind_size, global_idx;
	ConnectionClass	*conn;
	QResultClass	*res;
	IRDFields	*irdflds;
	BindInfoClass	*bookmark;

	if (s->need_data_callback)
	{
		mylog("bulk_ope_callback in\n");
		s->processed++;
		s->idx++;
	}
	else
	{
		s->idx = s->processed = 0;
	}
	s->need_data_callback = FALSE;
	bookmark = s->opts->bookmark;
	offset = s->opts->row_offset_ptr ? *(s->opts->row_offset_ptr) : 0;
	bind_size = s->opts->bind_size;
	for (; SQL_ERROR != ret && s->idx < s->opts->size_of_rowset; s->idx++)
	{
		if (SQL_ADD != s->operation)
		{
			memcpy(&global_idx, CALC_BOOKMARK_ADDR(bookmark, offset, bind_size, s->idx), sizeof(UInt4));
			global_idx = SC_resolve_bookmark(global_idx);
		}
		/* Note opts->row_operation_ptr is ignored */
		switch (s->operation)
		{
			case SQL_ADD:
				ret = SC_pos_add(s->stmt, (UWORD) s->idx);
				break;
			case SQL_UPDATE_BY_BOOKMARK:
				ret = SC_pos_update(s->stmt, (UWORD) s->idx, global_idx);
				break;
			case SQL_DELETE_BY_BOOKMARK:
				ret = SC_pos_delete(s->stmt, (UWORD) s->idx, global_idx);
				break;
			case SQL_FETCH_BY_BOOKMARK:
				ret = SC_pos_refresh(s->stmt, (UWORD) s->idx, global_idx);
				break;
		}
		if (SQL_NEED_DATA == ret)
		{
			bop_cdata *cbdata = (bop_cdata *) malloc(sizeof(bop_cdata));
			memcpy(cbdata, s, sizeof(bop_cdata));
			cbdata->need_data_callback = TRUE;
			enqueueNeedDataCallback(s->stmt, bulk_ope_callback, cbdata);
			return ret;
		}
		s->processed++;
	}
	conn = SC_get_conn(s->stmt);
	if (s->auto_commit_needed)
		PGAPI_SetConnectOption(conn, SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_ON);
	irdflds = SC_get_IRDF(s->stmt);
	if (irdflds->rowsFetched)
		*(irdflds->rowsFetched) = s->processed;

	if (res = SC_get_Curres(s->stmt), res)
		res->recent_processed_row_count = s->stmt->diag_row_count = s->processed;
	return ret;
}

RETCODE	SQL_API
PGAPI_BulkOperations(HSTMT hstmt, SQLSMALLINT operationX)
{
	CSTR func = "PGAPI_BulkOperations";
	bop_cdata	s;
	RETCODE		ret;
	ConnectionClass	*conn;
	BindInfoClass	*bookmark;

	mylog("%s operation = %d\n", func, operationX);
	s.stmt = (StatementClass *) hstmt;
	s.operation = operationX;
	SC_clear_error(s.stmt);
	s.opts = SC_get_ARDF(s.stmt);
	
	s.auto_commit_needed = FALSE;
	if (SQL_FETCH_BY_BOOKMARK != s.operation)
	{
		conn = SC_get_conn(s.stmt);
		if (s.auto_commit_needed = (char) CC_is_in_autocommit(conn), s.auto_commit_needed)
			PGAPI_SetConnectOption(conn, SQL_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF);
	}
	if (SQL_ADD != s.operation)
	{
		if (!(bookmark = s.opts->bookmark) || !(bookmark->buffer))
		{
			SC_set_error(s.stmt, DESC_INVALID_OPTION_IDENTIFIER, "bookmark isn't specified", func);
			return SQL_ERROR;
		}
	}

	/* StartRollbackState(s.stmt); */
	s.need_data_callback = FALSE;
	ret = bulk_ope_callback(SQL_SUCCESS, &s);
	if (s.stmt->internal)
		ret = DiscardStatementSvp(s.stmt, ret, FALSE);
	return ret;
}	
#endif /* ODBCVER >= 0x0300 */
