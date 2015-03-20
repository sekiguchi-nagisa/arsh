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

#include <core/DSObject.h>

struct ProcContext : public DSObject {   //FIXME: redirect option
    typedef enum {
        NORMAL,
        INTR,
    } ExitKind;


    std::string cmdName;
    std::vector<std::shared_ptr<String_Object>> params;

    /**
     * actual command parameter. the last element must be NULL.
     */
    char **argv;

    int pid;
    ExitKind exitKind;
    int exitStatus;

    ProcContext(const std::string &cmdName);
    ~ProcContext();

    DSType *getType();  // override
    int getFieldSize(); // override
    DSObject *lookupField(int fieldIndex);  // override
    std::string toString(); // override

    void addParam(const std::shared_ptr<DSObject> &value);

    /**
     * init argv.
     */
    void prepare();
};

struct ProcGroup {
    unsigned int procSize;
    std::shared_ptr<ProcContext> *procs;

    ProcGroup(unsigned int procSize);
    ~ProcGroup();

    void addProc(unsigned int index, const std::shared_ptr<ProcContext> &ctx);

    /**
     * execute all process.
     */
    int execProcs();
};

#endif /* CORE_PROCCONTEXT_H_ */
