/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

typedef enum {
    DS_EXEC_MODE_NORMAL = 0,
    DS_EXEC_MODE_PARSE_ONLY = 1,
    DS_EXEC_MODE_CHECK_ONLY = 2,
    DS_EXEC_MODE_COMPILE_ONLY = 3,
} DSExecMode;

/**
 * create new DSState with DSExecMode
 * you can call DSState_delete() to release object.
 * @param mode
 * @return
 */
DSState *DSState_createWithMode(DSExecMode mode);

#define DSState_create() DSState_createWithMode(DS_EXEC_MODE_NORMAL)

/**
 * delete DSState. after release object, assign null to ctx.
 * @param st
 * may be null
 */
void DSState_delete(DSState **st);

void DSState_setLineNum(DSState *st, unsigned int lineNum);
unsigned int DSState_lineNum(const DSState *st);

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

/**
 *
 * @param st
 * not null
 * @param scriptPath
 * @return
 * if scriptPath is not found, return -1 and do nothing. (set errno)
 * if success, return 0.
 */
int DSState_setScriptDir(DSState *st, const char *scriptPath);

/* for internal data structure dump */
typedef enum {
    DS_DUMP_KIND_UAST = 0,  /* dump untyped abstract syntax tree */
    DS_DUMP_KIND_AST  = 1,  /* dump typed abstract syntax tree */
    DS_DUMP_KIND_CODE = 2,  /* dump byte code */
} DSDumpKind;

/**
 *
 * @param kind
 * @param fp
 * fp is not null.
 * after call it, do not close fp.
 */
void DSState_setDumpTarget(DSState *st, DSDumpKind kind, FILE *fp);


/* for option */
#define DS_OPTION_ASSERT       ((unsigned short) (1 << 1))
#define DS_OPTION_TOPLEVEL     ((unsigned short) (1 << 2))
#define DS_OPTION_TRACE_EXIT   ((unsigned short) (1 << 3))
#define DS_OPTION_HISTORY      ((unsigned short) (1 << 4))
#define DS_OPTION_INTERACTIVE  ((unsigned short) (1 << 5))

unsigned short DSState_option(const DSState *st);
void DSState_setOption(DSState *st, unsigned short optionSet);
void DSState_unsetOption(DSState *st, unsigned short optionSet);


/* for indicating error kind. */
typedef enum {
    DS_ERROR_KIND_SUCCESS        ,
    DS_ERROR_KIND_PARSE_ERROR    ,
    DS_ERROR_KIND_TYPE_ERROR     ,
    DS_ERROR_KIND_RUNTIME_ERROR  ,
    DS_ERROR_KIND_ASSERTION_ERROR,
    DS_ERROR_KIND_EXIT           ,
} DSErrorKind;

typedef struct {
    /**
     * kind of error.
     * see DS_ERROR_KIND_ * macro
     */
    DSErrorKind kind;

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
    const char *name;
} DSError;

/**
 * evaluate string. if e is not null, set error info.
 * @param st
 * not null
 * @param sourceName
 * if null, source name is treated as standard input.
 * @param data
 * not null
 * @param size
 * size of data
 * @param e
 * may be null
 * @return
 * exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 */
int DSState_eval(DSState *st, const char *sourceName, const char *data, unsigned int size, DSError *e);

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

typedef struct {
    unsigned int major;
    unsigned int minor;
    unsigned int patch;
} DSVersion;

/**
 * get version information
 * @param version
 * may be null
 * @return
 * version string
 */
const char *DSState_version(DSVersion *version);

const char *DSState_copyright();

/* for feature detection */
#define DS_FEATURE_LOGGING    ((unsigned int) (1 << 0))
#define DS_FEATURE_DBUS       ((unsigned int) (1 << 1))
#define DS_FEATURE_SAFE_CAST  ((unsigned int) (1 << 2))
#define DS_FEATURE_FIXED_TIME ((unsigned int) (1 << 3))

unsigned int DSState_featureBit();

/**
 * check if support D-Bus binding.
 * if support D-Bus, return 1.
 * otherwise, return 0.
 */
#define DSState_supportDBus() (DSState_featureBit() & DS_FEATURE_DBUS ? 1 : 0)


/* for termination hook */
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


/* for input completion */
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
 * @return
 * if completion success, return 0. otherwise return -1.
 */
int DSState_complete(const DSState *st, const char *buf, size_t cursor, DSCandidates *c);

/**
 * release buffer of candidates.
 * after call it, assign 0 and null to c.
 * if c is null, do nothing.
 * @param c
 * may be null.
 */
void DSCandidates_release(DSCandidates *c);

/* for history */
#define DS_HISTSIZE_LIMIT       ((unsigned int) 4096)
#define DS_HISTFILESIZE_LIMIT   ((unsigned int) 4096)

typedef struct {
    /**
     * initial value is 0.
     */
    unsigned int capacity;

    /**
     * initial value is 0
     */
    unsigned int size;

    /**
     * initial value is null
     */
    char **data;
} DSHistory;

/**
 * get history view
 * @param st
 * @return
 * read only history view
 */
const DSHistory *DSState_history(const DSState *st);

/**
 * synchronize history size with HISTSIZE.
 * if not set DS_OPTION_HISTORY, do nothing.
 * @param st
 */
void DSState_syncHistorySize(DSState *st);

/**
 * update history by index.
 * if index >= DSHistory.size, do nothing.
 * @param st
 * @param index
 * @param str
 */
void DSState_setHistoryAt(DSState *st, unsigned int index, const char *str);

/**
 *
 * @param st
 * @param str
 */
void DSState_addHistory(DSState *st, const char *str);

/**
 * delete history.
 * if index >= DSHistory.size, do nothing.
 * @param st
 * @param index
 */
void DSState_deleteHistoryAt(DSState *st, unsigned int index);

/**
 * clear history.
 * @param st
 */
void DSState_clearHistory(DSState *st);

/**
 * load history from file.
 * @param st
 * @param fileName
 * if null, use HISTFILE.
 */
void DSState_loadHistory(DSState *st, const char *fileName);

/**
 * save history to file.
 * @param st
 * @param fileName
 * if null, use HISTFILE.
 */
void DSState_saveHistory(const DSState *st, const char *fileName);


#ifdef __cplusplus
}
#endif

#endif /* YDSH_YDSH_H */
