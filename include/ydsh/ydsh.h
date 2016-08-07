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

struct DSState;
typedef struct DSState DSState;

/*********************/
/**     DSState     **/
/*********************/

/**
 * create new DSState.
 * you can call DSState_delete() to release object.
 * @return
 */
DSState *DSState_create();

/**
 * delete DSState. after release object, assign null to ctx.
 * @param st
 * may be null
 */
void DSState_delete(DSState **st);

void DSState_setLineNum(DSState *st, unsigned int lineNum);
unsigned int DSState_lineNum(DSState *st);

/**
 * set shell name ($0).
 * @param st
 * not null
 * @param shellName
 * if null, do nothing.
 */
void DSState_setShellName(DSState *st, const char *shellName);

/**
 * set arguments ($@).
 * @param st
 * not null.
 * @param args
 * if null, do nothing.
 */
void DSState_setArguments(DSState *st, char *const *args);


// for option
#define DS_OPTION_DUMP_UAST  ((unsigned int) (1 << 0))
#define DS_OPTION_DUMP_AST   ((unsigned int) (1 << 1))
#define DS_OPTION_DUMP_CODE  ((unsigned int) (1 << 2))
#define DS_OPTION_PARSE_ONLY ((unsigned int) (1 << 3))
#define DS_OPTION_ASSERT     ((unsigned int) (1 << 4))
#define DS_OPTION_TOPLEVEL   ((unsigned int) (1 << 5))
#define DS_OPTION_TRACE_EXIT ((unsigned int) (1 << 6))

void DSState_setOption(DSState *st, unsigned int optionSet);
void DSState_unsetOption(DSState *st, unsigned int optionSet);


// for indicating error kind.
#define DS_ERROR_KIND_SUCCESS         ((unsigned int) 0)
#define DS_ERROR_KIND_PARSE_ERROR     ((unsigned int) 1)
#define DS_ERROR_KIND_TYPE_ERROR      ((unsigned int) 2)
#define DS_ERROR_KIND_RUNTIME_ERROR   ((unsigned int) 3)
#define DS_ERROR_KIND_ASSERTION_ERROR ((unsigned int) 4)
#define DS_ERROR_KIND_EXIT            ((unsigned int) 5)

typedef struct {
    /**
     * kind of error.
     * see DS_ERROR_KIND_ * macro
     */
    unsigned int kind;

    /**
     * indicate the line number of the error location.
     * if kind is, DS_ERROR_KIND_SUCCESS, it is 0.
     */
    unsigned int lineNum;

    /**
     * indicate error name.
     * if DS_ERROR_KIND_PARSE_ERROR or DS_ERROR_KIND_TYPE_ERROR, error kind.
     * if DS_ERROR_KIND_RUNTIME_ERROR, raised type name.
     * otherwise, null
     */
    char *name;
} DSError;

/**
 * release buffer of DSError.
 * if e is null, do nothing.
 * after release, assign 0 and null to members.
 * @param e
 * may be null.
 */
void DSError_release(DSError *e);

/**
 * evaluate string. if e is not null, set error info.
 * @param st
 * not null.
 * @param sourceName
 * if null, source name is treated as standard input.
 * @param source
 * not null. must be null terminated.
 * @param e
 * may be null.
 * @return
 * exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSState_eval(DSState *st, const char *sourceName, const char *source, DSError *e);

/**
 * evaluate file content. if e is not null, set error info.
 * @param st
 * not null.
 * @param sourceName
 * if null, source name is treated as standard input
 * @param fp
 * must be opened with binary mode.
 * @param e
 * may be null.
 * @return
 * exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSState_loadAndEval(DSState *st, const char *sourceName, FILE *fp, DSError *e);

/**
 * execute builtin command.
 * @param st
 * not null.
 * @param argv
 * first element must be command name.
 * last element must be null.
 * @return
 * exit status of executed command.
 * if command not found, return 1.
 */
int DSState_exec(DSState *st, char *const *argv);

/**
 * get prompt string
 * @param st
 * not null.
 * @param n
 * @return
 * if n is 1, return primary prompt.
 * if n is 2, return secondary prompt.
 * otherwise, return empty string.
 */
const char *DSState_prompt(DSState *st, unsigned int n);

/**
 * check if support D-Bus binding.
 * @return
 * if support D-Bus, return 1.
 * otherwise, return 0.
 */
int DSState_supportDBus();

// for version information
unsigned int DSState_majorVersion();
unsigned int DSState_minorVersion();
unsigned int DSState_patchVersion();

/**
 * get version string (include some build information)
 * @return
 */
const char *DSState_version();

const char *DSState_copyright();

// for feature detection
#define DS_FEATURE_LOGGING    ((unsigned int) (1 << 0))
#define DS_FEATURE_DBUS       ((unsigned int) (1 << 1))
#define DS_FEATURE_SAFE_CAST  ((unsigned int) (1 << 2))
#define DS_FEATURE_FIXED_TIME ((unsigned int) (1 << 3))

unsigned int DSState_featureBit();


// for termination hook
/**
 * status indicates execution status (DS_ERROR_KIND_ASSERTION_ERROR or DS_ERROR_KIND_EXIT).
 */
typedef void (*TerminationHook)(unsigned int status, unsigned int errorLineNum);

/**
 * when calling builtin exit command or raising assertion error, invoke hook and terminate immediately.
 * @param st
 * not null.
 * @param hook
 * if null, clear termination hook.
 */
void DSState_addTerminationHook(DSState *st, TerminationHook hook);


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
 * if buf or st is null, write 0 and null to c.
 * if c is null, do nothing.
 * call DSCandidates_release() to release candidate.
 * @param st
 * may be null.
 * @param buf
 * may be null.
 * @param cursor
 * @param c
 * may be null.
 */
void DSState_complete(DSState *st, const char *buf, size_t cursor, DSCandidates *c);

/**
 * release buffer of candidates.
 * after call it, assign 0 and null to c.
 * if c is null, do nothing.
 * @param c
 * may be null.
 */
void DSCandidates_release(DSCandidates *c);


#ifdef __cplusplus
}
#endif

#endif //YDSH_YDSH_H
