/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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
 * if sourceName is null, source name is treated as standard input.
 * source must not be null.
 * if terminated by exit command or terminated normally,
 * return exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSContext_eval(DSContext *ctx, const char *sourceName, const char *source);

/**
 * evaluate file content.
 * if sourceName is null, source name is treated as standard input.
 * fp must be opened binary mode.
 * if terminated by exit command or terminated normally,
 * return exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSContext_loadAndEval(DSContext *ctx, const char *sourceName, FILE *fp);

/**
 * execute builtin command.
 * first element of argv must be command name.
 * last element of argv must be null.
 * return exit status of executed command.
 * if command not found, return 1.
 */
int DSContext_exec(DSContext *ctx, char *const argv[]);

void DSContext_setLineNum(DSContext *ctx, unsigned int lineNum);
unsigned int DSContext_lineNum(DSContext *ctx);

/**
 * set shell name ($0).
 * if shellName is null, do nothing.
 */
void DSContext_setShellName(DSContext *ctx, const char *shellName);

/**
 * set arguments ($@).
 * last element of args must be null.
 * if args is null, do nothing.
 */
void DSContext_setArguments(DSContext *ctx, char *const args[]);


#define DS_OPTION_DUMP_UAST  ((unsigned int) (1 << 0))
#define DS_OPTION_DUMP_AST   ((unsigned int) (1 << 1))
#define DS_OPTION_PARSE_ONLY ((unsigned int) (1 << 2))
#define DS_OPTION_ASSERT     ((unsigned int) (1 << 3))
#define DS_OPTION_TOPLEVEL   ((unsigned int) (1 << 4))
#define DS_OPTION_TRACE_EXIT ((unsigned int) (1 << 5))

void DSContext_setOption(DSContext *ctx, unsigned int optionSet);
void DSContext_unsetOption(DSContext *ctx, unsigned int optionSet);

/**
 * if n is 1, return primary prompt.
 * if n is 2, return secondary prompt.
 * otherwise, return empty string.
 */
const char *DSContext_prompt(DSContext *ctx, unsigned int n);

/**
 * return 1 if support D-Bus.
 * otherwise, return 0.
 */
int DSContext_supportDBus();

// for version information
unsigned int DSContext_majorVersion();
unsigned int DSContext_minorVersion();
unsigned int DSContext_patchVersion();

/**
 * get version string (include some build information)
 */
const char *DSContext_version();

const char *DSContext_copyright();

// for feature detection
#define DS_FEATURE_LOGGING    ((unsigned int) (1 << 0))
#define DS_FEATURE_DBUS       ((unsigned int) (1 << 1))
#define DS_FEATURE_SAFE_CAST  ((unsigned int) (1 << 2))
#define DS_FEATURE_FIXED_TIME ((unsigned int) (1 << 3))

unsigned int DSContext_featureBit();

// for execution status
#define DS_STATUS_SUCCESS         ((unsigned int) 0)
#define DS_STATUS_PARSE_ERROR     ((unsigned int) 1)
#define DS_STATUS_TYPE_ERROR      ((unsigned int) 2)
#define DS_STATUS_RUNTIME_ERROR   ((unsigned int) 3)
#define DS_STATUS_ASSERTION_ERROR ((unsigned int) 4)
#define DS_STATUS_EXIT            ((unsigned int) 5)

/**
 * return type of status.
 * see DS_STATUS_* macro.
 */
unsigned int DSContext_status(DSContext *ctx);

/**
 * return line number of error location.
 * if type is DS_STATUS_SUCCESS, return always 0.
 */
unsigned int DSContext_errorLineNum(DSContext *ctx);

/**
 * if type is DS_STATUS_PARSE_ERROR or DS_STATUS_TYPE_ERROR, return error kind.
 * if type is DS_STATUS_RUNTIME_ERROR return raised type name.
 * otherwise, return always empty string.
 */
const char *DSContext_errorKind(DSContext *ctx);

// for termination hook
/**
 * status indicates execution status (DS_STATUS_ASSERTION_ERROR or DS_STATUS_EXIT).
 */
typedef void (*TerminationHook)(unsigned int status, unsigned int errorLineNum);

/**
 * when calling builtin exit command or raising assertion error, invoke hook and terminate immediately.
 */
void DSContext_addTerminationHook(DSContext *ctx, TerminationHook hook);


// for input completion
typedef struct {
    /**
     * size of values.
     */
    unsigned int size;

    /**
     * if size is 0, it is null.
     */
    char **values;
} DSCandidates;

/**
 * fill the candidates of possible token.
 * if buf or ctx is null, write 0 and null to c.
 * if c is null, do nothing.
 * call DSCandidates_release() to release candidate.
 */
void DSContext_complete(DSContext *ctx, const char *buf, size_t cursor, DSCandidates *c);

/**
 * release buffer of candidates.
 * after call it, assign 0 and null to c.
 * if c is null, do nothing.
 */
void DSCandidates_release(DSCandidates *c);


#ifdef __cplusplus
}
#endif

#endif //YDSH_YDSH_H
