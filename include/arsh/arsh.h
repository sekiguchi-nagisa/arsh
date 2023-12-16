/*
 * Copyright (C) 2015-2022 Nagisa Sekiguchi
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

#if defined(__GNUC__)
#if defined _WIN32 || defined __CYGWIN__
#define DS_PUBLIC_API(type) __attribute__((dllexport)) type
#else
#define DS_PUBLIC_API(type) __attribute__((visibility("default"))) type
#endif
#else
#define DS_PUBLIC_API(type) type
#endif

struct DSState;
typedef struct DSState DSState;

typedef enum {
  DS_EXEC_MODE_NORMAL,
  DS_EXEC_MODE_PARSE_ONLY,
  DS_EXEC_MODE_CHECK_ONLY,
  DS_EXEC_MODE_COMPILE_ONLY,
} DSExecMode;

/**
 * create new DSState with DSExecMode
 * you can call DSState_delete() to release object.
 * @param mode
 * @return
 * if specified invalid mode, return null
 */
DS_PUBLIC_API(DSState *) DSState_createWithMode(DSExecMode mode);

static inline DSState *DSState_create() { return DSState_createWithMode(DS_EXEC_MODE_NORMAL); }

/**
 * delete DSState. before call destructor, call TERM_HOOK
 * after release object, assign null to ctx.
 * @param st
 * may be null
 */
DS_PUBLIC_API(void) DSState_delete(DSState **st);

/**
 * get DSExecMode.
 * @param st
 * may be null
 * @return
 * if st is null, return always DS_EXEC_MODE_NORMAL
 */
DS_PUBLIC_API(DSExecMode) DSState_mode(const DSState *st);

/**
 * affect DSState_eval() result. (not affect DSState_loadModule())
 * @param st
 * @param lineNum
 * if st is null, do nothing
 */
DS_PUBLIC_API(void) DSState_setLineNum(DSState *st, unsigned int lineNum);

/**
 * get line number after latest DSState_eval() or setLineNum().
 * @param st
 * @return
 * if st is null, return always 0
 */
DS_PUBLIC_API(unsigned int) DSState_lineNum(const DSState *st);

/**
 * set shell name ($0).
 * @param st
 * if null, do nothing.
 * @param shellName
 * if null, do nothing.
 */
DS_PUBLIC_API(void) DSState_setShellName(DSState *st, const char *shellName);

/**
 * set arguments ($@).
 * @param st
 * if null, do nothing
 * @param args
 * if null, clear '@'
 * @return
 * if args size reaches limit, return -1 and clear '@'
 * otherwise return 0
 */
DS_PUBLIC_API(int) DSState_setArguments(DSState *st, char *const *args);

/**
 * set full path of current executable path (in linux, /proc/self/exe)
 * @param st
 * @return
 * if cannot resolve path, return null.
 * if st is null, return null
 */
DS_PUBLIC_API(const char *) DSState_initExecutablePath(DSState *st);

/**
 * get current exit status ($? & 0xFF)
 * @param st
 * @return
 * if null, return always 0
 */
DS_PUBLIC_API(int) DSState_exitStatus(const DSState *st);

/* for internal data structure dump */
typedef enum {
  DS_DUMP_KIND_UAST, /* dump untyped abstract syntax tree */
  DS_DUMP_KIND_AST,  /* dump typed abstract syntax tree */
  DS_DUMP_KIND_CODE, /* dump byte code */
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
 * if cannot open target or invalid parameter, do nothing and return -1.
 */
DS_PUBLIC_API(int) DSState_setDumpTarget(DSState *st, DSDumpKind kind, const char *target);

/* for option */
#define DS_OPTION_ASSERT ((unsigned int)(1u << 0u))
#define DS_OPTION_INTERACTIVE ((unsigned int)(1u << 1u))
#define DS_OPTION_TRACE_EXIT ((unsigned int)(1u << 2u))
#define DS_OPTION_JOB_CONTROL ((unsigned int)(1u << 3u))
#define DS_OPTION_XTRACE ((unsigned int)(1u << 4u))

DS_PUBLIC_API(unsigned int) DSState_option(const DSState *st);

/**
 * if specify DS_OPTION_JOB_CONTROL, ignore some signals
 * @param st
 * @param optionSet
 */
DS_PUBLIC_API(void) DSState_setOption(DSState *st, unsigned int optionSet);

/**
 * if specify DS_OPTION_JOB_CONTROL, reset some signal setting
 * @param st
 * @param optionSet
 */
DS_PUBLIC_API(void) DSState_unsetOption(DSState *st, unsigned int optionSet);

/* for indicating error kind. */
typedef enum {
  DS_ERROR_KIND_SUCCESS,
  DS_ERROR_KIND_FILE_ERROR,
  DS_ERROR_KIND_PARSE_ERROR,
  DS_ERROR_KIND_TYPE_ERROR,
  DS_ERROR_KIND_CODEGEN_ERROR,
  DS_ERROR_KIND_RUNTIME_ERROR,
  DS_ERROR_KIND_ASSERTION_ERROR,
  DS_ERROR_KIND_EXIT,
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
   * if kind is DS_ERROR_KIND_SUCCESS, it is 0.
   */
  unsigned int lineNum;

  /**
   * indicates the number of characters in error line
   * if kind is not S_ERROR_KIND_PARSE_ERROR, DS_ERROR_KIND_TYPE_ERROR or
   * DS_ERROR_KIND_CODEGEN_ERROR, always is 0
   */
  unsigned int chars;

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
DS_PUBLIC_API(void) DSError_release(DSError *e);

/**
 * evaluate string. if e is not null, set error info.
 * SCRIPT_DIR is always current working directory
 * @param st
 * @param sourceName
 * if null, source name is treated as standard input.
 * @param data
 * not null
 * @param size
 * size of data
 * @param e
 * may be null
 * @return
 * exit status of most recently executed command(include exit, 0~255).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 * if st or data is null, return -1 and not set error
 */
DS_PUBLIC_API(int)
DSState_eval(DSState *st, const char *sourceName, const char *data, size_t size, DSError *e);

/* for module loading option */
/**
 * load module as fullpath. so not allow cascading module search
 * (not search from LOCAL_MOD_DIR and SYSTEM_MOD_DIR)
 */
#define DS_MOD_FULLPATH ((unsigned int)(1u << 0u))

/**
 * ignore ENOENT error
 */
#define DS_MOD_IGNORE_ENOENT ((unsigned int)(1u << 1u))

/**
 * evaluate module in separate module context
 */
#define DS_MOD_SEPARATE_CTX ((unsigned int)(1u << 2u))

/**
 * open file as module. if e is not null, set error info.
 * before evaluation reset line number.
 * @param st
 * not null.
 * @param fileName
 * not null
 * @param option
 * @param e
 * may be null
 * @return
 * exit status of most recently executed command(include exit, 0~255).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 * if fileName is already loaded module, return always 0 and do nothing.
 * if st or fileName is null, return -1 and not set error
 */
DS_PUBLIC_API(int)
DSState_loadModule(DSState *st, const char *fileName, unsigned int option, DSError *e);

/**
 * open file and evaluate. if e is not null, set error info.
 * set SCRIPT_DIR to dirname of fileName.
 * before evaluation reset line number.
 * equivalent to DSState_loadModule(DS_MOD_FULLPATH | DS_MOD_SEPARATE_CTX)
 * @param st
 * @param fileName
 * not null
 * @param e
 * may be null.
 * @return
 * exit status of most recently executed command(include exit, 0~255).
 * if terminated by some errors(exception, assertion, syntax or semantic error), return always 1.
 * if fileName is already loaded file, return 1.
 * if fileName is already loaded module, return always 0 and do nothing.
 * if st is null, return -1 and not set error
 */
static inline int DSState_loadAndEval(DSState *st, const char *sourceName, DSError *e) {
  return DSState_loadModule(st, sourceName, DS_MOD_FULLPATH | DS_MOD_SEPARATE_CTX, e);
}

/**
 * execute command. if not DS_EXEC_MODE_NORMAL, do nothing (return always 0)
 * @param st
 * not null.
 * @param argv
 * first element must be command name.
 * last element must be null.
 * @return
 * exit status of executed command (0~255).
 * if command not found, return 1.
 * if st or argv is null, return -1
 */
DS_PUBLIC_API(int) DSState_exec(DSState *st, char *const *argv);

typedef enum {
  DS_CONFIG_COMPILER,
  DS_CONFIG_REGEX,
  DS_CONFIG_VERSION,
  DS_CONFIG_OSTYPE,
  DS_CONFIG_MACHTYPE,
  DS_CONFIG_CONFIG_HOME,
  DS_CONFIG_DATA_HOME,
  DS_CONFIG_MODULE_HOME,
  DS_CONFIG_DATA_DIR,
  DS_CONFIG_MODULE_DIR,
  DS_CONFIG_UNICODE,
} DSConfig;

/**
 * get runtime system configurations
 * @param st
 * not null
 * @param config
 * @return
 * if not found, return null
 */
DS_PUBLIC_API(const char *) DSState_config(const DSState *st, DSConfig config);

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
DS_PUBLIC_API(const char *) DSState_version(DSVersion *version);

DS_PUBLIC_API(const char *) DSState_copyright();

/* for feature detection */
#define DS_FEATURE_LOGGING ((unsigned int)(1u << 0u))
#define DS_FEATURE_SAFE_CAST ((unsigned int)(1u << 1u))

DS_PUBLIC_API(unsigned int) DSState_featureBit();

/**
 * read lines from stdin
 * also provide line edit capability
 * customize line edit behavior via `LINE_EDIT` global variable
 * @param st
 * must not be null
 * @param buf
 * output buffer for read data (after read, will be null terminated)
 * @param bufSize
 * @param e
 * may be null
 * @return
 * if has erro or reach end of strean, return -1
 * if canceled, return -1 and set EAGAIN
 * otherwise, return size of read data (not include last null character)
 */
DS_PUBLIC_API(ssize_t) DSState_readLine(DSState *st, char *buf, size_t bufSize, DSError *e);

#ifdef __cplusplus
}
#endif

#endif /* YDSH_YDSH_H */