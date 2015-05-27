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

#ifndef CORE_PROCCONTEXT_H_
#define CORE_PROCCONTEXT_H_

#include "DSObject.h"

namespace ydsh {
namespace core {

struct RuntimeContext;

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

#define EACH_RedirectOP(OP) \
    OP(IN_2_FILE, "<") \
    OP(OUT_2_FILE, "1>") \
    OP(OUT_2_FILE_APPEND, "1>>") \
    OP(ERR_2_FILE, "2>") \
    OP(ERR_2_FILE_APPEND, "2>>") \
    OP(MERGE_ERR_2_OUT_2_FILE, "&>") \
    OP(MERGE_ERR_2_OUT_2_FILE_APPEND, "&>>") \
    OP(MERGE_ERR_2_OUT, "2>&1")

enum RedirectOP : unsigned int {
#define GEN_ENUM(ENUM, STR) ENUM,
    EACH_RedirectOP(GEN_ENUM)
#undef GEN_ENUM
};

struct ProcContext : public DSObject {   //FIXME: redirect option
    typedef enum {
        NORMAL,
        INTR,
    } ExitKind;


    /**
     * not call destructor.
     */
    RuntimeContext *ctx;

    std::string cmdName;
    std::vector<std::shared_ptr<String_Object>> params;
    std::vector<std::pair<RedirectOP, std::shared_ptr<String_Object>>> redirs;

    /**
     * actual command parameter. the last element must be NULL.
     */
    char **argv;

    int pid;
    ExitKind exitKind;
    int exitStatus;

    ProcContext(RuntimeContext &ctx, const std::string &cmdName);

    ~ProcContext();

    void addParam(std::shared_ptr<DSObject> &&value, bool skipEmptyString);
    void addRedirOption(RedirectOP op, std::shared_ptr<DSObject> &&value);

    /**
     * init argv.
     */
    void prepare();
};

struct ProcGroup {
    unsigned int procSize;
    std::shared_ptr<ProcContext> *procs;

    explicit ProcGroup(unsigned int procSize);

    ~ProcGroup();

    void addProc(unsigned int index, const std::shared_ptr<ProcContext> &ctx);

    /**
     * execute all process.
     */
    int execProcs();
};

} // namespace core
} // namespace ydsh

#endif /* CORE_PROCCONTEXT_H_ */
