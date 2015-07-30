/*
 * Copyright (C) 2015 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef YDSH_YDSH_H
#define YDSH_YDSH_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DSContext;
typedef struct DSContext DSContext;

struct DSStatus;
typedef struct DSStatus DSStatus;


/***********************/
/**     DSContext     **/
/***********************/

/**
 * create new DSContext.
 * you can call DSContext_delete() to release object.
 */
DSContext *DSContext_create();

/**
 * delete DSContext. after release object, assign null to ctx.
 */
void DSContext_delete(DSContext **ctx);

/**
 * evaluate string.
 * if status is not null, write status and you can call DSStatus_free() to release object.
 * if terminated by exit command or terminated normally,
 * return exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSContext_eval(DSContext *ctx, const char *source, DSStatus **status);

/**
 * evaluate file content.
 * if sourceName is null, source name is treated as standard input.
 * fp must be opened binary mode.
 * if terminated by exit command or terminated normally,
 * return exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSContext_loadAndEval(DSContext *ctx, const char *sourceName, FILE *fp, DSStatus **status);

void DSContext_setLineNum(DSContext *ctx, unsigned int lineNum);
unsigned int DSContext_getLineNum(DSContext *ctx);

/**
 * first element of argv must be source name and not empty string.
 * last element of argv must be null.
 * empty string argument will be ignored.
 */
void DSContext_setArguments(DSContext *ctx, char *const argv[]);


#define DS_OPTION_DUMP_UAST  ((unsigned int)(1 << 0))
#define DS_OPTION_DUMP_AST   ((unsigned int)(1 << 1))
#define DS_OPTION_PARSE_ONLY ((unsigned int)(1 << 2))
#define DS_OPTION_ASSERT     ((unsigned int)(1 << 3))
#define DS_OPTION_TOPLEVEL   ((unsigned int)(1 << 4))
#define DS_OPTION_TRACE_EXIT ((unsigned int)(1 << 5))

void DSContext_setOption(DSContext *ctx, unsigned int optionSet);
void DSContext_unsetOption(DSContext *ctx, unsigned int optionSet);

/**
 * if n is 1, return primary prompt.
 * if n is 2, return secondary prompt.
 * otherwise, return empty string.
 */
const char *DSContext_getPrompt(DSContext *ctx, unsigned int n);

// for version information
unsigned int DSContext_getMajorVersion();
unsigned int DSContext_getMinorVersion();
unsigned int DSContext_getPatchVersion();

/**
 * get version string (include some build information)
 */
const char *DSContext_getVersion();

const char *DSContext_getCopyright();


/**********************/
/**     DSStatus     **/
/**********************/

/**
 * delete DSStatus. after release object, assign null to status.
 */
void DSStatus_free(DSStatus **status);


#define DS_STATUS_SUCCESS         0
#define DS_STATUS_PARSE_ERROR     1
#define DS_STATUS_TYPE_ERROR      2
#define DS_STATUS_RUNTIME_ERROR   3
#define DS_STATUS_ASSERTION_ERROR 4
#define DS_STATUS_EXIT            5

/**
 * return type of status.
 * see DS_STATUS_* macro.
 */
unsigned int DSStatus_getType(DSStatus *status);

/**
 * return line number of error location.
 * if type is DS_STATUS_SUCCESS, return always 0.
 */
unsigned int DSStatus_getErrorLineNum(DSStatus *status);

/**
 * if type is DS_STATUS_PARSE_ERROR or DS_STATUS_TYPE_ERROR, return error kind.
 * if type is DS_STATUS_RUNTIME_ERROR return raised type name.
 * otherwise, return always empty string.
 */
const char *DSStatus_getErrorKind(DSStatus *status);


#ifdef __cplusplus
}
#endif

#endif //YDSH_YDSH_H
