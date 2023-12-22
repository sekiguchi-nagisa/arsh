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

#ifndef ARSH_ARSH_H
#define ARSH_ARSH_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#if defined _WIN32 || defined __CYGWIN__
#define AR_PUBLIC_API(type) __attribute__((dllexport)) type
#else
#define AR_PUBLIC_API(type) __attribute__((visibility("default"))) type
#endif
#else
#define AR_PUBLIC_API(type) type
#endif

struct ARState;
typedef struct ARState ARState;

typedef enum {
  AR_EXEC_MODE_NORMAL,
  AR_EXEC_MODE_PARSE_ONLY,
  AR_EXEC_MODE_CHECK_ONLY,
  AR_EXEC_MODE_COMPILE_ONLY,
} ARExecMode;

/**
 * create new ARState with ARExecMode
 * you can call ARState_delete() to release object.
 * @param mode
 * @return
 * if specified invalid mode, return null
 */
AR_PUBLIC_API(ARState *) ARState_createWithMode(ARExecMode mode);

static inline ARState *ARState_create() { return ARState_createWithMode(AR_EXEC_MODE_NORMAL); }

/**
 * delete ARState. before call destructor, call TERM_HOOK
 * after release object, assign null to ctx.
 * @param st
 * may be null
 */
AR_PUBLIC_API(void) ARState_delete(ARState **st);

/**
 * get ARExecMode.
 * @param st
 * may be null
 * @return
 * if st is null, return always AR_EXEC_MODE_NORMAL
 */
AR_PUBLIC_API(ARExecMode) ARState_mode(const ARState *st);

/**
 * affect ARState_eval() result. (not affect ARState_loadModule())
 * @param st
 * @param lineNum
 * if st is null, do nothing
 */
AR_PUBLIC_API(void) ARState_setLineNum(ARState *st, unsigned int lineNum);

/**
 * get line number after latest ARState_eval() or setLineNum().
 * @param st
 * @return
 * if st is null, return always 0
 */
AR_PUBLIC_API(unsigned int) ARState_lineNum(const ARState *st);

/**
 * set shell name ($0).
 * @param st
 * if null, do nothing.
 * @param shellName
 * if null, do nothing.
 */
AR_PUBLIC_API(void) ARState_setShellName(ARState *st, const char *shellName);

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
AR_PUBLIC_API(int) ARState_setArguments(ARState *st, char *const *args);

/**
 * set full path of current executable path (in linux, /proc/self/exe)
 * @param st
 * @return
 * if cannot resolve path, return null.
 * if st is null, return null
 */
AR_PUBLIC_API(const char *) ARState_initExecutablePath(ARState *st);

/**
 * get current exit status ($? & 0xFF)
 * @param st
 * @return
 * if null, return always 0
 */
AR_PUBLIC_API(int) ARState_exitStatus(const ARState *st);

/* for internal data structure dump */
typedef enum {
  AR_DUMP_KIND_UAST, /* dump untyped abstract syntax tree */
  AR_DUMP_KIND_AST,  /* dump typed abstract syntax tree */
  AR_DUMP_KIND_CODE, /* dump byte code */
} ARDumpKind;

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
AR_PUBLIC_API(int) ARState_setDumpTarget(ARState *st, ARDumpKind kind, const char *target);

/* for option */
#define AR_OPTION_ASSERT ((unsigned int)(1u << 0u))
#define AR_OPTION_INTERACTIVE ((unsigned int)(1u << 1u))
#define AR_OPTION_TRACE_EXIT ((unsigned int)(1u << 2u))
#define AR_OPTION_JOB_CONTROL ((unsigned int)(1u << 3u))
#define AR_OPTION_XTRACE ((unsigned int)(1u << 4u))

AR_PUBLIC_API(unsigned int) ARState_option(const ARState *st);

/**
 * if specify AR_OPTION_JOB_CONTROL, ignore some signals
 * @param st
 * @param optionSet
 */
AR_PUBLIC_API(void) ARState_setOption(ARState *st, unsigned int optionSet);

/**
 * if specify AR_OPTION_JOB_CONTROL, reset some signal setting
 * @param st
 * @param optionSet
 */
AR_PUBLIC_API(void) ARState_unsetOption(ARState *st, unsigned int optionSet);

/* for indicating error kind. */
typedef enum {
  AR_ERROR_KIND_SUCCESS,
  AR_ERROR_KIND_FILE_ERROR,
  AR_ERROR_KIND_PARSE_ERROR,
  AR_ERROR_KIND_TYPE_ERROR,
  AR_ERROR_KIND_CODEGEN_ERROR,
  AR_ERROR_KIND_RUNTIME_ERROR,
  AR_ERROR_KIND_ASSERTION_ERROR,
  AR_ERROR_KIND_EXIT,
} ARErrorKind;

typedef struct {
  /**
   * kind of error.
   * see AR_ERROR_KIND_ * macro
   */
  ARErrorKind kind;

  /**
   * file name of the error location.
   * if has no errors, will be null.
   */
  char *fileName;

  /**
   * indicate the line number of the error location.
   * if kind is AR_ERROR_KIND_SUCCESS, it is 0.
   */
  unsigned int lineNum;

  /**
   * indicates the number of characters in error line
   * if kind is not S_ERROR_KIND_PARSE_ERROR, AR_ERROR_KIND_TYPE_ERROR or
   * AR_ERROR_KIND_CODEGEN_ERROR, always is 0
   */
  unsigned int chars;

  /**
   * indicate error name.
   * if AR_ERROR_KIND_FILE, strerror()
   * if AR_ERROR_KIND_PARSE_ERROR or AR_ERROR_KIND_TYPE_ERROR, error kind.
   * if AR_ERROR_KIND_RUNTIME_ERROR, raised type name.
   * otherwise, null
   */
  char *name;
} ARError;

/**
 * release internal fields of ARError.
 * after call it, assign null to `fileName'
 * @param e
 * may be null
 */
AR_PUBLIC_API(void) ARError_release(ARError *e);

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
AR_PUBLIC_API(int)
ARState_eval(ARState *st, const char *sourceName, const char *data, size_t size, ARError *e);

/* for module loading option */
/**
 * load module as fullpath. so not allow cascading module search
 * (not search from LOCAL_MOD_DIR and SYSTEM_MOD_DIR)
 */
#define AR_MOD_FULLPATH ((unsigned int)(1u << 0u))

/**
 * ignore ENOENT error
 */
#define AR_MOD_IGNORE_ENOENT ((unsigned int)(1u << 1u))

/**
 * evaluate module in separate module context
 */
#define AR_MOD_SEPARATE_CTX ((unsigned int)(1u << 2u))

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
AR_PUBLIC_API(int)
ARState_loadModule(ARState *st, const char *fileName, unsigned int option, ARError *e);

/**
 * open file and evaluate. if e is not null, set error info.
 * set SCRIPT_DIR to dirname of fileName.
 * before evaluation reset line number.
 * equivalent to ARState_loadModule(AR_MOD_FULLPATH | AR_MOD_SEPARATE_CTX)
 * @param st
 * @param sourceName
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
static inline int ARState_loadAndEval(ARState *st, const char *sourceName, ARError *e) {
  return ARState_loadModule(st, sourceName, AR_MOD_FULLPATH | AR_MOD_SEPARATE_CTX, e);
}

/**
 * execute command. if not AR_EXEC_MODE_NORMAL, do nothing (return always 0)
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
AR_PUBLIC_API(int) ARState_exec(ARState *st, char *const *argv);

typedef enum {
  AR_CONFIG_COMPILER,
  AR_CONFIG_REGEX,
  AR_CONFIG_VERSION,
  AR_CONFIG_OSTYPE,
  AR_CONFIG_MACHTYPE,
  AR_CONFIG_CONFIG_HOME,
  AR_CONFIG_DATA_HOME,
  AR_CONFIG_MODULE_HOME,
  AR_CONFIG_DATA_DIR,
  AR_CONFIG_MODULE_DIR,
  AR_CONFIG_UNICODE,
} ARConfig;

/**
 * get runtime system configurations
 * @param st
 * not null
 * @param config
 * @return
 * if not found, return null
 */
AR_PUBLIC_API(const char *) ARState_config(const ARState *st, ARConfig config);

typedef struct {
  unsigned int major;
  unsigned int minor;
  unsigned int patch;
} ARVersion;

/**
 * get version information
 * @param version
 * may be null
 * @return
 * version string
 */
AR_PUBLIC_API(const char *) ARState_version(ARVersion *version);

AR_PUBLIC_API(const char *) ARState_copyright();

/* for feature detection */
#define AR_FEATURE_LOGGING ((unsigned int)(1u << 0u))
#define AR_FEATURE_SAFE_CAST ((unsigned int)(1u << 1u))

AR_PUBLIC_API(unsigned int) ARState_featureBit();

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
 * if has error or reach end of stream, return -1
 * if canceled, return -1 and set EAGAIN
 * otherwise, return size of read data (not include last null character)
 */
AR_PUBLIC_API(ssize_t) ARState_readLine(ARState *st, char *buf, size_t bufSize, ARError *e);

#ifdef __cplusplus
}
#endif

#endif /* ARSH_ARSH_H */
