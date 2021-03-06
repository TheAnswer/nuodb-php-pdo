/****************************************************************************
 * Copyright (c) 2012 - 2014, NuoDB, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of NuoDB, Inc. nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NUODB, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _MSC_VER  /* Visual Studio specific */
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utime.h>
#else
#include <sys/time.h>
#endif

#include "php.h"
#ifdef ZEND_ENGINE_2
# include "zend_exceptions.h"
#endif
#include "php_ini.h"
#include "ext/standard/info.h"
#include "pdo/php_pdo.h"
#include "pdo/php_pdo_driver.h"
#include "php_pdo_nuodb.h"

#include "php_pdo_nuodb_c_cpp_common.h"
#include "php_pdo_nuodb_int.h"

#include <time.h>

#define CHAR_BUF_LEN 24

int nuodb_handle_commit(pdo_dbh_t * dbh TSRMLS_DC);

int pdo_nuodb_stmt_set_blob(pdo_nuodb_stmt *S, zend_long paramno, char *blob_val, int len);

static void _release_PdoNuoDbStatement(pdo_nuodb_stmt * S)
{
    pdo_nuodb_stmt_delete(S);
}


/* {{{ nuodb_stmt_dtor
 *
 * Called by PDO to clean up a statement handle
 */
int nuodb_stmt_dtor(pdo_stmt_t * pdo_stmt TSRMLS_DC)
{
    int result = 1;
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)pdo_stmt->driver_data;
    PDO_DBG_ENTER("nuodb_stmt_dtor", pdo_stmt->dbh);
    PDO_DBG_INF_FMT("dbh=%p : S=%p", pdo_stmt->dbh, S);

    if (S->commit_on_close == 1) {
        nuodb_handle_commit(pdo_stmt->dbh TSRMLS_CC);
    }

    _release_PdoNuoDbStatement(S); /* release the statement */

    if (S->sql) {
        free(S->sql);
        S->sql = NULL;
    }

    if (S->einfo.errmsg) {
        pefree(S->einfo.errmsg, pdo_stmt->dbh->is_persistent);
        S->einfo.errmsg = NULL;
    }

    /* clean up input params */
    if (S->in_params != NULL)
    {
        efree(S->in_params);
    }

    efree(S);
    PDO_DBG_RETURN(result, pdo_stmt->dbh);
}
/* }}} */


/* {{{ nuodb_stmt_execute
 *
 * Called by PDO to execute a prepared query
 */
static int nuodb_stmt_execute(pdo_stmt_t * pdo_stmt TSRMLS_DC)
{
    int status;
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)pdo_stmt->driver_data;
    pdo_nuodb_db_handle * H = NULL;

    PDO_DBG_ENTER("nuodb_stmt_execute", pdo_stmt->dbh);
    PDO_DBG_INF_FMT("dbh=%p S=%p sql=%s", pdo_stmt->dbh, pdo_stmt->driver_data, S->sql);
    if (!S) {
        PDO_DBG_RETURN(0, pdo_stmt->dbh);
    }

    if ((pdo_stmt->dbh->auto_commit == 0) &&
        (pdo_stmt->dbh->in_txn == 0))
    {
        H = (pdo_nuodb_db_handle *)pdo_stmt->dbh->driver_data;
        if ((H->in_nuodb_implicit_txn == 0) && (H->in_nuodb_explicit_txn == 0)) {
            H->in_nuodb_implicit_txn = 1;
            S->commit_on_close = 1;
            S->implicit_txn = 1;
        }
    }
    status = pdo_nuodb_stmt_execute(S, &pdo_stmt->column_count, &pdo_stmt->row_count);
    if (status == 0) {
        PDO_DBG_RETURN(0, pdo_stmt->dbh);
    }

    PDO_DBG_RETURN(1, pdo_stmt->dbh);
}
/* }}} */


/* {{{ nuodb_stmt_fetch
 *
 * Called by PDO to fetch the next row from a statement
 */
static int nuodb_stmt_fetch(pdo_stmt_t * pdo_stmt,
                            enum pdo_fetch_orientation ori,
                            long offset TSRMLS_DC)
{
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)pdo_stmt->driver_data;
    PDO_DBG_ENTER("nuodb_stmt_fetch", pdo_stmt->dbh);
    PDO_DBG_INF_FMT("dbh=%p : S=%p", pdo_stmt->dbh, S);

    if (!pdo_stmt->executed)
    {
        _record_error_formatted(pdo_stmt->dbh, pdo_stmt, __FILE__, __LINE__, "01001", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Cannot fetch from a closed cursor");
        PDO_DBG_RETURN(0, pdo_stmt->dbh);
    }
    PDO_DBG_RETURN(pdo_nuodb_stmt_fetch(S, &pdo_stmt->row_count), pdo_stmt->dbh);
}
/* }}} */


/* {{{ nuodb_stmt_describe
 *
 * Called by PDO to retrieve information about the fields being
 * returned
 */
static int nuodb_stmt_describe(pdo_stmt_t * pdo_stmt, int colno TSRMLS_DC)
{
    pdo_nuodb_stmt *S = (pdo_nuodb_stmt *)pdo_stmt->driver_data;
    struct pdo_column_data *col = &pdo_stmt->columns[colno];
    //zend_string *cp;
    char const *column_name;
    int colname_len;
    int sqlTypeNumber;

    col->precision = 0;
    col->maxlen = 0;

    column_name = pdo_nuodb_stmt_get_column_name(S, colno);
    if (column_name == NULL) {
        return 0;
    }
    colname_len = strlen(column_name);
    //col->namelen = colname_len;
    col->name /*= cp*/ = zend_string_init(column_name, colname_len, 1);//(char *) emalloc(colname_len + 1);

    PDO_DBG_INF_FMT("dbh=%p : col->name:%s",
                    pdo_stmt->dbh,  ZSTR_VAL(col->name));
    //memmove(cp, column_name, colname_len);
    //*(cp+colname_len) = '\0';
    sqlTypeNumber = pdo_nuodb_stmt_get_sql_type(S, colno);
    switch (sqlTypeNumber)
    {
        case PDO_NUODB_SQLTYPE_NULL:
        {
            col->param_type = PDO_PARAM_NULL;
            break;
        }
        case PDO_NUODB_SQLTYPE_BOOLEAN:
        {
            col->param_type = PDO_PARAM_BOOL;
            break;
        }
        case PDO_NUODB_SQLTYPE_INTEGER:
        {
            col->param_type = PDO_PARAM_INT;
            break;
        }
        case PDO_NUODB_SQLTYPE_BIGINT:
        {
            col->maxlen = CHAR_BUF_LEN;
            col->param_type = PDO_PARAM_STR;
            break;
        }
        case PDO_NUODB_SQLTYPE_DOUBLE:
        {
            col->param_type = PDO_PARAM_STR;
            break;
        }
        case PDO_NUODB_SQLTYPE_STRING:
        {
            col->param_type = PDO_PARAM_STR;
            break;
        }
        case PDO_NUODB_SQLTYPE_DATE:
        {
            col->param_type = PDO_PARAM_INT;
            break;
        }
        case PDO_NUODB_SQLTYPE_TIME:
        {
            col->param_type = PDO_PARAM_INT;
            break;
        }
        case PDO_NUODB_SQLTYPE_TIMESTAMP:
        {
            col->param_type = PDO_PARAM_STR;
            break;
        }
        case PDO_NUODB_SQLTYPE_BLOB:
        {
            col->param_type = PDO_PARAM_STR;
            break;
        }
        case PDO_NUODB_SQLTYPE_CLOB:
        {
            col->param_type = PDO_PARAM_STR;
            break;
        }
        default:
        {
            _record_error_formatted(pdo_stmt->dbh, pdo_stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_INTERNAL_ERROR, "unknown/unsupported type: '%d' in nuodb_stmt_describe()", sqlTypeNumber);
            return 0;
        }
    }

    return 1;
}
/* }}} */


/* {{{ nuodb_stmt_get_col
 *
 * Get column information.
 */
static int nuodb_stmt_get_col(pdo_stmt_t * pdo_stmt, int colno,
                              char ** ptr, unsigned long * len,
                              int * caller_frees TSRMLS_DC)
{
    static void * (*ereallocPtr)(void *ptr, size_t size) = &_erealloc;

    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)pdo_stmt->driver_data;
    int sqlTypeNumber = 0;
    pdo_nuodb_db_handle *H = (pdo_nuodb_db_handle *)S->H;
    sqlTypeNumber = pdo_nuodb_stmt_get_sql_type(S, colno);

    *len = 0;
    *caller_frees = 1;

    if (*ptr != NULL) {
        efree(*ptr);
    }
    *ptr = NULL;

    switch (sqlTypeNumber)
    {
        /*
         * PDO_NUODB_SQLTYPE_NULL occurs when the NuoDB C++ API has
         * a NULL value in the result set.  Attempts to obtain any
         * value should be a NULL value.
         */
        case PDO_NUODB_SQLTYPE_NULL:
        {
            int str_len;
            const char * str = pdo_nuodb_stmt_get_string(S, colno);
            if (str == NULL)
            {
                *ptr = NULL;
                *len = 0;
                break;
            }
            _record_error_formatted(pdo_stmt->dbh, pdo_stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_INTERNAL_ERROR, "Unexpected value for SQLTYPE NULL: str=%s", str);
            return 0;
        }
        case PDO_NUODB_SQLTYPE_BOOLEAN:
        {
            char val = 0;
            char *pVal = &val;
            pdo_nuodb_stmt_get_boolean(S, colno, &pVal);
            if (pVal == NULL) {
                *ptr = NULL;
                *len = 0;
                break;
            }
            *ptr = (char *)emalloc(CHAR_BUF_LEN);
            (*ptr)[0] = val;
            *len = 1;
            break;
        }
        case PDO_NUODB_SQLTYPE_INTEGER:
        {
            int val = 0;
            int *pVal = &val;
            pdo_nuodb_stmt_get_integer(S, colno, &pVal);
            if (pVal == NULL) {
                *ptr = NULL;
                *len = 0;
            } else {
                long lval = val;
                *len = sizeof(long);
                *ptr = (char *)emalloc(*len);
                memmove(*ptr, &lval, *len);
            }
            break;
        }
        case PDO_NUODB_SQLTYPE_BIGINT:
        {
            int64_t val = 0;
            int64_t *pVal = &val;
            pdo_nuodb_stmt_get_long(S, colno, &pVal);
            if (pVal == NULL) {
                *ptr = NULL;
                *len = 0;
                break;
            }
            *ptr = (char *)emalloc(CHAR_BUF_LEN);
            *len = slprintf(*ptr, CHAR_BUF_LEN, "%d", val);
            break;
        }
        case PDO_NUODB_SQLTYPE_DOUBLE:
        {
            break;
        }
        case PDO_NUODB_SQLTYPE_STRING:
        {
            int str_len;
            const char * str = pdo_nuodb_stmt_get_string(S, colno);
            if (str == NULL) {
                *ptr = NULL;
                *len = 0;
                break;
            }
            str_len = strlen(str);
            *ptr = (char *) emalloc(str_len+1);
            memmove(*ptr, str, str_len);
            *((*ptr)+str_len)= 0;
            *len = str_len;
            break;
        }
        case PDO_NUODB_SQLTYPE_DATE:
        {
            int64_t val = 0;
            int64_t *pVal = &val;
            pdo_nuodb_stmt_get_date(S, colno, &pVal);
            if (pVal == NULL) {
                *ptr = NULL;
                *len = 0;
                break;
            }
            // Use "long" here because the PDO spec requires this: note "long" is different sizes
            // on different platforms.
            *len = sizeof(long);
            *ptr = (char *)emalloc(*len);
            memmove(*ptr, pVal, *len);
            break;
        }
        case PDO_NUODB_SQLTYPE_TIME:
        {
            int64_t val = 0;
            int64_t *pVal = &val;
            pdo_nuodb_stmt_get_time(S, colno, &pVal);
            if (pVal == NULL) {
                *ptr = NULL;
                *len = 0;
                break;
            }
            // Use "long" here because the PDO spec requires this: note "long" is different sizes
            // on different platforms.
            *len = sizeof(long);
            *ptr = (char *)emalloc(*len);
            memmove(*ptr, pVal, *len);
            break;
        }
        case PDO_NUODB_SQLTYPE_TIMESTAMP:
        {
            int str_len;
            const char * str = pdo_nuodb_stmt_get_timestamp(S, colno);
            if (str == NULL) {
                *ptr = NULL;
                *len = 0;
                break;
            }
            str_len = strlen(str);
            *ptr = (char *) emalloc(str_len+1);
            memmove(*ptr, str, str_len);
            *((*ptr)+str_len)= 0;
            *len = str_len;
            break;
        }
        case PDO_NUODB_SQLTYPE_BLOB:
        {
            pdo_nuodb_stmt_get_blob(S, colno, ptr, len, ereallocPtr);
            break;
        }
        case PDO_NUODB_SQLTYPE_CLOB:
        {
            pdo_nuodb_stmt_get_clob(S, colno, ptr, len, ereallocPtr);
            break;
        }
        default:
        {
            _record_error_formatted(pdo_stmt->dbh, pdo_stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_INTERNAL_ERROR, "unknown/unsupported type: '%d' in nuodb_stmt_get_col()", sqlTypeNumber);
            return 0;
            break;
        }
    }

    /* do we have a statement error code? */
    if ((pdo_stmt->error_code[0] != '\0') && strncmp(pdo_stmt->error_code, PDO_ERR_NONE, PDO_NUODB_SQLSTATE_LEN)) {
        return 0;
    }

    /* do we have a dbh error code? */
    if (strncmp(pdo_stmt->dbh->error_code, PDO_ERR_NONE, PDO_NUODB_SQLSTATE_LEN)) {
        return 0;
    }

    return 1;
}
/* }}} */


/* {{{ nuodb_stmt_param_hook
 */
static int nuodb_stmt_param_hook(pdo_stmt_t * stmt, struct pdo_bound_param_data * param,
                                 enum pdo_param_event event_type TSRMLS_DC)
{
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)stmt->driver_data;
    nuo_params * nuodb_params = NULL;
    nuo_param * nuodb_param = NULL;

    nuodb_params = param->is_param ? S->in_params : S->out_params;
    if (param->is_param)
    {
        PDO_DBG_INF_FMT("dbh=%p : starting Param: %d event type: %d paramtype: %d param type: %d",
                        stmt->dbh,
                        param->paramno,
                        event_type,
                        Z_TYPE(param->parameter),
                        PDO_PARAM_TYPE(param->param_type));

        switch (event_type)
        {
            case PDO_PARAM_EVT_FREE:
            case PDO_PARAM_EVT_ALLOC:
            case PDO_PARAM_EVT_EXEC_POST:
            case PDO_PARAM_EVT_FETCH_PRE:
            case PDO_PARAM_EVT_FETCH_POST:
                return 1;

            case PDO_PARAM_EVT_NORMALIZE:
                /* decode name from :pdo1, :pdo2 into 0, 1 etc. */
                if (param->name)
                {
                    PDO_DBG_INF_FMT("dbh=%p : evt normalize Param: %d Name: %s",
                                    stmt->dbh,
                                    param->paramno,
                                    ZSTR_VAL(param->name));

                    if (!strncmp(ZSTR_VAL(param->name), ":pdo", 4)) {
                        param->paramno = atoi(ZSTR_VAL(param->name) + 4);

                        PDO_DBG_INF_FMT("dbh=%p : contains pdo Param: %d Name: %s",
                                        stmt->dbh,
                                        param->paramno,
                                        ZSTR_VAL(param->name));
                    } else {
                        /* resolve parameter name to rewritten name */
                        zval *nameptr = NULL;
                        if (stmt->bound_param_map &&
                                (nameptr = zend_hash_find(stmt->bound_param_map, param->name)) != NULL) {
                            param->paramno = atoi((char*)Z_STR_P(nameptr) + 4) - 1;

                            PDO_DBG_INF_FMT("dbh=%p : Param: %d Name: %s ConvName - 4: %s Type:%d",
                                            stmt->dbh,
                                            param->paramno,
                                            ZSTR_VAL(param->name),
                                            Z_STR_P(nameptr),
                                            Z_TYPE_P(nameptr));
                        } else {
                            _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "42P02", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Invalid parameter name '%s'", param->name);
                            return 0;
                        }
                    }
                }

                if (nuodb_params == NULL) {
                    _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "42P02", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Error processing parameters");
                    return 0;
                }

                nuodb_param = &nuodb_params->params[param->paramno];
                if (nuodb_param != NULL)
                {
                    switch(param->param_type)
                    {
                        case PDO_PARAM_INT:
                        {
                            nuodb_param->sqltype = PDO_NUODB_SQLTYPE_INTEGER;
                            break;
                        }
                        case PDO_PARAM_STR:
                        {
                            nuodb_param->sqltype = PDO_NUODB_SQLTYPE_STRING;
                            break;
                        }
                        case PDO_PARAM_LOB:
                        {
                            nuodb_param->sqltype = PDO_NUODB_SQLTYPE_BLOB;
                            break;
                        }
                    }
                }
                break;


            case PDO_PARAM_EVT_EXEC_PRE:
            {
                int num_input_params = 0;
                if (!stmt->bound_param_map) {
                    return 0;
                }

                if (param->paramno >= 0)
                {
                    if (param->paramno >= (long) S->qty_input_params) {
                        _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "HY093", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Invalid parameter number %d", param->paramno);
                        return 0;
                    }

                    if (nuodb_params == NULL) {
                        _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "nuodb_params is NULL");
                        return 0;
                    }

                    nuodb_param = &nuodb_params->params[param->paramno];
                    if (nuodb_param == NULL) {
                        _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Error locating parameters");
                        return 0;
                    }

                    if (param->name != NULL)
                    {
                        memcpy(nuodb_param->col_name, ZSTR_VAL(param->name), (strlen(ZSTR_VAL(param->name)) + 1));
                        nuodb_param->col_name_length = strlen(nuodb_param->col_name);

                        PDO_DBG_INF_FMT("dbh=%p : Param: %d  copied name: %s",
                                        stmt->dbh,
                                        param->paramno,
                                        nuodb_param->col_name);
                    }

                    /* TODO: add code to process streaming LOBs here
                     * when NuoDB supports it. */

                    if (PDO_PARAM_TYPE(param->param_type) == PDO_PARAM_NULL ||
                        Z_TYPE(param->parameter) == IS_NULL)
                    {
                        nuodb_param->len = 0;
                        nuodb_param->data = NULL;
                        PDO_DBG_INF_FMT("dbh=%p : Param: %d  Name: %s = NULL",
                                        stmt->dbh,
                                        param->paramno,
                                        param->name ? ZSTR_VAL(param->name) : NULL);

                    }
                    else if (PDO_PARAM_TYPE(param->param_type) == PDO_PARAM_INT)
                    {
                        nuodb_param->len = sizeof(int);
                        nuodb_param->data = (void *)zval_get_long(&param->parameter);
                        pdo_nuodb_stmt_set_integer(S, param->paramno, (long)nuodb_param->data);
                        PDO_DBG_INF_FMT("dbh=%p : Param: %d  Name: %s = %ld (LONG)",
                                        stmt->dbh,
                                        param->paramno,
                                        param->name ? ZSTR_VAL(param->name) : NULL,
                                        zval_get_long(&param->parameter));
                    }
                    else if (PDO_PARAM_TYPE(param->param_type) == PDO_PARAM_BOOL)
                    {
                        nuodb_param->len = 1;
                        nuodb_param->data = zval_is_true(&param->parameter) ? "t" : "f";
                        pdo_nuodb_stmt_set_boolean(S, param->paramno,  nuodb_param->data[0]);
                        PDO_DBG_INF_FMT("dbh=%p : Param: %d  Name: %s = %s (BOOL)",
                                        stmt->dbh,
                                        param->paramno,
                                        param->name ? ZSTR_VAL(param->name) : NULL,
                                        nuodb_param->data);
                    }
                    else if (PDO_PARAM_TYPE(param->param_type) == PDO_PARAM_STR)
                    {
                        PDO_DBG_INF_FMT("entering str param copy and setter");
                        SEPARATE_ZVAL_IF_NOT_REF(&param->parameter);
                        convert_to_string(&param->parameter);
                        nuodb_param->len = Z_STRLEN(param->parameter);
                        nuodb_param->data = Z_STRVAL(param->parameter);
                        PDO_DBG_INF_FMT("dbh=%p : Param: %d  Name: %s = %s (Bytes: %d)",
                                        stmt->dbh,
                                        param->paramno,
                                        param->name ? ZSTR_VAL(param->name) : NULL,
                                        nuodb_param->data,
                                        nuodb_param->len);
                        pdo_nuodb_stmt_set_string_with_length(S, param->paramno,  (const void *)nuodb_param->data, nuodb_param->len);
                    }
                    else if (PDO_PARAM_TYPE(param->param_type) == PDO_PARAM_LOB)
                    {
                        if (Z_TYPE(param->parameter) == IS_RESOURCE) {
                            php_stream *stm;
                            php_stream_from_zval_no_verify(stm, &param->parameter);
                            if (stm) {
                                _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Unsupported parameter type: %d", Z_TYPE(param->parameter));
/*
                                SEPARATE_ZVAL_IF_NOT_REF(&param->parameter);

                                param->parameter.u1.v.type = IS_STRING; //haaack

                                Z_STR(param->parameter) = php_stream_copy_to_mem(stm, PHP_STREAM_COPY_ALL, 0);;
                                */
                                return 0;
                            } else {
                                _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "HY105", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Expected a stream resource");
                                return 0;
                            }
                        } else {
                            /* If the parameter is not a stream
                               resource, then convert it to a
                               string. */
                            SEPARATE_ZVAL_IF_NOT_REF(&param->parameter);
                            convert_to_string(&param->parameter);
                            nuodb_param->len = Z_STRLEN(param->parameter);
                            nuodb_param->data = Z_STRVAL(param->parameter);
                            pdo_nuodb_stmt_set_blob(S, param->paramno,  nuodb_param->data, nuodb_param->len);
                            PDO_DBG_INF_FMT("dbh=%p : Param: %d  Name: %s = %ld (length: %d) (BLOB)",
                                            stmt->dbh,
                                            param->paramno,
                                            param->name ? ZSTR_VAL(param->name) : NULL,
                                            nuodb_param->data,
                                            nuodb_param->len);
                        }
                    }

                    else {
                        _record_error_formatted(stmt->dbh, stmt, __FILE__, __LINE__, "XX000", PDO_NUODB_SQLCODE_APPLICATION_ERROR, "Unsupported parameter type: %d", Z_TYPE(param->parameter));
                        return 0;
                    }
                }
            }
            break;
        }
    }
    return 1;
}
/* }}} */


/* {{{ nuodb_stmt_set_attribute
 */
static int nuodb_stmt_set_attribute(pdo_stmt_t * stmt, long attr, zval * val TSRMLS_DC)
{
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)stmt->driver_data;

    PDO_DBG_ENTER("nuodb_stmt_set_attribute", stmt->dbh);
    PDO_DBG_INF_FMT("dbh=%p : S=%p", stmt->dbh, S);

    switch (attr)
    {
        default:
            PDO_DBG_ERR_FMT("dbh=%p : unknown/unsupported attribute: %d", stmt->dbh, attr);
            PDO_DBG_RETURN(0, stmt->dbh);
        case PDO_ATTR_CURSOR_NAME:
            convert_to_string(val);
            strlcpy(S->name, Z_STRVAL_P(val), sizeof(S->name));
            break;
    }
    PDO_DBG_RETURN(1, stmt->dbh);
}
/* }}} */


/* {{{ nuodb_stmt_get_attribute
 */
static int nuodb_stmt_get_attribute(pdo_stmt_t * stmt, long attr,
                                    zval * val TSRMLS_DC)
{
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)stmt->driver_data;

    switch (attr)
    {
        default:
            return 0;
        case PDO_ATTR_CURSOR_NAME:
            if (*S->name) {
                ZVAL_STRING(val,S->name);
            } else {
                ZVAL_NULL(val);
            }
            break;
    }
    return 1;
}
/* }}} */


/* {{{ nuodb_stmt_cursor_closer
 */
static int nuodb_stmt_cursor_closer(pdo_stmt_t * stmt TSRMLS_DC)
{
    pdo_nuodb_stmt * S = (pdo_nuodb_stmt *)stmt->driver_data;

    if ((*S->name || S->cursor_open)) {
        _release_PdoNuoDbStatement(S);
    }
    *S->name = 0;
    S->cursor_open = 0;
    return 1;
}
/* }}} */


struct pdo_stmt_methods nuodb_stmt_methods =
{
    nuodb_stmt_dtor,
    nuodb_stmt_execute,
    nuodb_stmt_fetch,
    nuodb_stmt_describe,
    nuodb_stmt_get_col,
    nuodb_stmt_param_hook,
    nuodb_stmt_set_attribute,
    nuodb_stmt_get_attribute,
    NULL, /* get_column_meta_func */
    NULL, /* next_rowset_func */
    nuodb_stmt_cursor_closer
};

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
