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

#include <ydsh/ydsh.h>
#include "exe/Shell.h"

using namespace ydsh;

struct DSContext {
    Shell *shell;

    DSContext() : shell(Shell::createShell()) { }

    ~DSContext() {
        delete this->shell;
    }
};

struct DSStatus {
    /**
     * kind of execution status.
     */
    unsigned int type;

    /**
     * for error location.
     */
    unsigned int lineNum;

    const char *errorKind;

    DSStatus(unsigned int type, unsigned int lineNum, const char *errorKind) :
            type(type), lineNum(lineNum), errorKind(errorKind) { }

    ~DSStatus() = default;
};


// #######################
// ##     DSContext     ##
// #######################

DSContext *DSContext_create() {
    return new DSContext();
}

void DSContext_delete(DSContext **ctx) {
    if(ctx != nullptr) {
        delete (*ctx);
        *ctx = nullptr;
    }
}

static int createStatus(ExecStatus s, Shell *shell, DSStatus **status) {
    static char empty[] = "";

    unsigned int type = DS_STATUS_SUCCESS;
    int ret = 0;
    unsigned int lineNum = 0;
    const char *errorKind = empty;

    switch(s) {
    case ExecStatus::SUCCESS:
        type = DS_STATUS_SUCCESS;
        ret = 0;
        break;
    case ExecStatus::PARSE_ERROR:
        type = DS_STATUS_PARSE_ERROR;
        ret = 1;
        lineNum = shell->getReportingListener().getLineNum();
        errorKind = shell->getReportingListener().getMessageKind();
        break;
    case ExecStatus::TYPE_ERROR:
        type = DS_STATUS_TYPE_ERROR;
        ret = 1;
        lineNum = shell->getReportingListener().getLineNum();
        errorKind = shell->getReportingListener().getMessageKind();
        break;
    case ExecStatus::RUNTIME_ERROR:
        type = DS_STATUS_RUNTIME_ERROR;
        ret = 1;
        break;
    case ExecStatus::ASSERTION_ERROR:
        type = DS_STATUS_ASSERTION_ERROR;
        ret = 1;
        break;
    case ExecStatus::EXIT:
        type = DS_STATUS_EXIT;
        ret = shell->getExitStatus();
        break;
    }

    if(status != nullptr) {
        *status = new DSStatus(type, lineNum, errorKind);
    }
    return ret;
}

int DSContext_eval(DSContext *ctx, const char *source, DSStatus **status) {
    ExecStatus s = ctx->shell->eval(source);
    return createStatus(s, ctx->shell, status); //FIXME:
}

int DSContext_loadAndEval(DSContext *ctx, const char *sourceName, FILE *fp, DSStatus **status) {
    ExecStatus s = ctx->shell->eval(sourceName, fp);
    return createStatus(s, ctx->shell, status); //FIXME:
}

void DSContext_setLineNum(DSContext *ctx, unsigned int lineNum) {
    ctx->shell->setLineNum(lineNum);
}

unsigned int DSContext_getLineNum(DSContext *ctx) {
    return ctx->shell->getLineNum();
}

void DSContext_setArguments(DSContext *ctx, const char **args) {
    std::vector<const char *> argList;
    for(unsigned int i = 0; args[i] != nullptr; i++) {
        argList.push_back(args[i]);
    }

    ctx->shell->setArguments(argList);
}

const char *DSContext_getWorkingDir(DSContext *ctx) {
    return ctx->shell->getWorkingDir().c_str();
}

int DSContext_getExitStatus(DSContext *ctx) {
    return ctx->shell->getExitStatus();
}

static void setOptionImpl(Shell *shell, flag32_set_t flagSet, bool set) {
    if(hasFlag(flagSet, DS_OPTION_DUMP_UAST)) {
        shell->setDumpUntypedAST(set);
    }
    if(hasFlag(flagSet, DS_OPTION_DUMP_AST)) {
        shell->setDumpTypedAST(set);
    }
    if(hasFlag(flagSet, DS_OPTION_PARSE_ONLY)) {
        shell->setParseOnly(set);
    }
    if(hasFlag(flagSet, DS_OPTION_ASSERT)) {
        shell->setAssertion(set);
    }
    if(hasFlag(flagSet, DS_OPTION_TOPLEVEL)) {
        shell->setToplevelprinting(set);
    }
    if(hasFlag(flagSet, DS_OPTION_TRACE_EXIT)) {
        shell->setTraceExit(set);
    }
}

void DSContext_setOption(DSContext *ctx, unsigned int optionSet) {
    setOptionImpl(ctx->shell, optionSet, true);
}

void DSContext_unsetOption(DSContext *ctx, unsigned int optionSet) {
    setOptionImpl(ctx->shell, optionSet, false);
}


// ######################
// ##     DSStatus     ##
// ######################

void DSStatus_free(DSStatus **status) {
    if(status != nullptr) {
        delete (*status);
        *status = nullptr;
    }
}

unsigned int DSStatus_getType(DSStatus *status) {
    return status->type;
}

unsigned int DSStatus_getErrorLineNum(DSStatus *status) {
    return status->lineNum;
}

const char *DSStatus_getErrorKind(DSStatus *status) {
    return status->errorKind;
}