/*
 * Copyright (C) 2015-2019 Nagisa Sekiguchi
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
    DS_EXEC_MODE_NORMAL       = 0,
    DS_EXEC_MODE_PARSE_ONLY   = 1,
    DS_EXEC_MODE_CHECK_ONLY   = 2,
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

/**
 * get DSExecMode.
 * @param st
 * not null
 * @return
 */
DSExecMode DSState_mode(const DSState *st);

/**
 * affect DSState_eval() result. (not affect DSState_loadAndEval())
 * @param st
 * @param lineNum
 */
void DSState_setLineNum(DSState *st, unsigned int lineNum);

/**
 * get line number after latest evaluation or setLineNum().
 * @param st
 * @return
 */
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
 * @param scriptDir
 * @return
 * if scriptDir is not found, return -1 and do nothing. (set errno)
 * if success, return 0.
 */
int DSState_setScriptDir(DSState *st, const char *scriptDir);

/**
 * get current exit status (equivalent to $?)
 * @param st
 * not null
 * @return
 */
int DSState_getExitStatus(const DSState *st);

/**
 * update exit status
 * @param st
 * not null
 * @param status
 */
void DSState_setExitStatus(DSState *st, int status);

/* for internal data structure dump */
typedef enum {
    DS_DUMP_KIND_UAST = 0,  /* dump untyped abstract syntax tree */
    DS_DUMP_KIND_AST  = 1,  /* dump typed abstract syntax tree */
    DS_DUMP_KIND_CODE = 2,  /* dump byte code */
} DSDumpKind;

/**
 *
 * @param st
 * @param kind
 * @param target
 * if null, clear dump target.
 * if empty string, treat as stdout.
 * @return
 * if success, return 0.
 * if cannot open target, do nothing and return -1.
 */
int DSState_setDumpTarget(DSState *st, DSDumpKind kind, const char *target);


/* for option */
#define DS_OPTION_ASSERT       ((unsigned short) (1u << 0u))
#define DS_OPTION_TOPLEVEL     ((unsigned short) (1u << 1u))
#define DS_OPTION_TRACE_EXIT   ((unsigned short) (1u << 2u))
#define DS_OPTION_JOB_CONTROL  ((unsigned short) (1u << 3u))
#define DS_OPTION_INTERACTIVE  ((unsigned short) (1u << 4u))

unsigned short DSState_option(const DSState *st);

/**
 * if specify DS_OPTION_JOB_CONTROL, ignore some signals
 * @param st
 * @param optionSet
 */
void DSState_setOption(DSState *st, unsigned short optionSet);

/**
 * if specify DS_OPTION_JOB_CONTROL, reset some signal setting
 * @param st
 * @param optionSet
 */
void DSState_unsetOption(DSState *st, unsigned short optionSet);


/* for indicating error kind. */
typedef enum {
    DS_ERROR_KIND_SUCCESS        ,
    DS_ERROR_KIND_FILE_ERROR     ,
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
     * file name of the error location.
     * if has no errors, will be null.
     */
    char *fileName;

    /**
     * indicate the line number of the error location.
     * if kind is, DS_ERROR_KIND_SUCCESS, it is 0.
     */
    unsigned int lineNum;

    /**
     * indicate error name.
     * if DS_ERROR_KIND_FILE, strerror()
     * if DS_ERROR_KIND_PARSE_ERROR or DS_ERROR_KIND_TYPE_ERROR, error kind.
     * if DS_ERROR_KIND_RUNTIME_ERROR, raised type name.
     * otherwise, null
     */
    char *name;
} DSError;

/**
 * release internal fields of DSError.
 * after call it, assign null to `fileName'
 * @param e
 * may be null
 */
void DSError_release(DSError *e);

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
 * open file and evaluate. if e is not null, set error info.
 * set SCRIPT_DIR to dirname of fileName.
 * before evaluation reset line number.
 * @param st
 * not null.
 * @param fileName
 * if null, file name is treated as standard input
 * @param e
 * may be null.
 * @return
 * exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 * if fileName is already loaded file, return 1.
 * if flleName is already loaded module, return always 0 and do nothing.
 */
int DSState_loadAndEval(DSState *st, const char *fileName, DSError *e);


/* for module loading option */
#define DS_MOD_FULLPATH      ((unsigned short) (1u << 0u))
#define DS_MOD_IGNORE_ENOENT ((unsigned short) (1u << 1u))

/**
 * open file as module. if e is not null, set error info.
 * before evaluation reset line number.
 * @param st
 * not null.
 * @param fileName
 * not null
 * @param varName
 * if null, treat as globally imported module.
 * @param option
 * @param e
 * may be null
 * @return
 * exit status of most recently executed command(include exit).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 * if fileName is already loaded module, return always 0 and do nothing.
 */
int DSState_loadModule(DSState *st, const char *fileName,
                       const char *varName, unsigned short option, DSError *e);

/**
 * execute command. if not DS_EXEC_MODE_NORMAL, do nothing (return always 0)
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

/**
 * get full path of system config directory (/etc/ydsh).
 * equivalent to $CONFIG_DIR
 * @return
 */
const char *DSState_configDir();

/* for feature detection */
#define DS_FEATURE_LOGGING    ((unsigned int) (1u << 0u))
#define DS_FEATURE_SAFE_CAST  ((unsigned int) (1u << 1u))
#define DS_FEATURE_FIXED_TIME ((unsigned int) (1u << 2u))

unsigned int DSState_featureBit();


/* for input completion */
struct DSCandidates;
typedef struct DSCandidates DSCandidates;

/**
 * get the candidates of possible token.
 * call DSCandidates_release() to release candidate.
 * @param st
 * may be null
 * @param buf
 * may be null
 * @param cursor
 * @return
 * return null if no candidates.
 */
DSCandidates *DSState_complete(DSState *st, const char *buf, size_t cursor);

/**
 *
 * @param c
 * @param index
 * @return
 * return null, if index out of range.
 */
const char *DSCandidates_get(const DSCandidates *c, unsigned int index);

unsigned int DSCandidates_size(const DSCandidates *c);

/**
 * release buffer of candidates.
 * if c is null, do nothing.
 * @param c
 * may be null.
 */
void DSCandidates_release(DSCandidates **c);

/* for history */
#define DS_HISTSIZE_LIMIT       ((unsigned int) 4096)
#define DS_HISTFILESIZE_LIMIT   ((unsigned int) 4096)

typedef enum {
    DS_HISTORY_SIZE,    // current history buffer size
    DS_HISTORY_GET,     // get history at index
    DS_HISTORY_SET,     // set history at index
    DS_HISTORY_DEL,     // delete history at index
    DS_HISTORY_CLEAR,   // clear all of history
    DS_HISTORY_INIT,    // add empty string to buffer
    DS_HISTORY_ADD,     // add history to buffer
    DS_HISTORY_LOAD,    // load history from file
    DS_HISTORY_SAVE,    // save history to file
    DS_HISTORY_SEARCH,  // search history
} DSHistoryOp;

/**
 * do history operation
 * @param st
 * @param op
 * @param index
 * @param buf
 * @return
 * if op is DS_HISTORY_SIZE, return size of history.
 * otherwise, return 0, if success.
 */
int DSState_historyOp(DSState *st, DSHistoryOp op, unsigned int index, const char **buf);

#ifdef __cplusplus
}
#endif

#endif /* YDSH_YDSH_H */
