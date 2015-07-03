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

#include "../ast/Node.h"

#define IN_SRC_FILE
#include "TypeCheckError.h"


namespace ydsh {
namespace parser {

struct MsgTuple1 {
    const char *kindStr;
    const char *msg1;
};

static MsgTuple1 errorTuple0[] = {
#define GEN_MSG(ENUM, S1) {#ENUM, S1},
    EACH_TC_ERROR0(GEN_MSG)
#undef GEN_MSG
};

static MsgTuple1 errorTuple1[] = {
#define GEN_MSG(ENUM, S1) {#ENUM, S1},
        EACH_TC_ERROR1(GEN_MSG)
#undef GEN_MSG
};

struct MsgTuple2 {
    const char *kindStr;
    const char *msg1;
    const char *msg2;
};

static MsgTuple2 errorTuple2[] = {
#define GEN_MSG(ENUM, S1, S2) {#ENUM, S1, S2},
        EACH_TC_ERROR2(GEN_MSG)
#undef GEN_MSG
};

struct MsgTuple3 {
    const char *kindStr;
    const char *msg1;
    const char *msg2;
    const char *msg3;
};

static MsgTuple3 errorTuple3[] = {
#define GEN_MSG(ENUM, S1, S2, S3) {#ENUM, S1, S2, S3},
        EACH_TC_ERROR3(GEN_MSG)
#undef GEN_MSG
};


// ############################
// ##     TypeCheckError     ##
// ############################

TypeCheckError::TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind0 k) :
        lineNum(lineNum), kind(errorTuple0[k].kindStr), message(errorTuple0[k].msg1) {
}

TypeCheckError::TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind1 k, const std::string &arg1) :
        lineNum(lineNum), kind(errorTuple1[k].kindStr), message(errorTuple1[k].msg1) {
    this->message += arg1;
}

TypeCheckError::TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind2 k, const std::string &arg1,
                               const std::string &arg2) :
        lineNum(lineNum), kind(errorTuple2[k].kindStr), message(errorTuple2[k].msg1) {
    this->message += arg1;
    this->message += errorTuple2[k].msg2;
    this->message += arg2;
}

TypeCheckError::TypeCheckError(unsigned int lineNum, TypeCheckError::ErrorKind3 k, const std::string &arg1,
                               const std::string &arg2, const std::string &arg3) :
        lineNum(lineNum), kind(errorTuple3[k].kindStr), message(errorTuple3[k].msg1) {
    this->message += arg1;
    this->message += errorTuple3[k].msg2;
    this->message == arg2;
    this->message += errorTuple3[k].msg3;
    this->message += arg3;
}

TypeCheckError::TypeCheckError(unsigned int lineNum, const core::TypeLookupError &e) :
        lineNum(lineNum), kind(e.getKind()), message(e.getMessage()) {
}

std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind0 kind) {
    stream << errorTuple0[kind].kindStr;
    return stream;
}

std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind1 kind) {
    stream << errorTuple1[kind].kindStr;
    return stream;
}

std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind2 kind) {
    stream << errorTuple2[kind].kindStr;
    return stream;
}

std::ostream &operator<<(std::ostream &stream, TypeCheckError::ErrorKind3 kind) {
    stream << errorTuple3[kind].kindStr;
    return stream;
}

// ##########################
// ##     ErrorRaiser0     ##
// ##########################

void ErrorRaiser0::operator()(ast::Node *node) throw(TypeCheckError) {
    throw TypeCheckError(node->getLineNum(), this->kind);
}

// ##########################
// ##     ErrorRaiser1     ##
// ##########################

void ErrorRaiser1::operator()(ast::Node *node, const std::string &arg1) throw(TypeCheckError) {
    throw TypeCheckError(node->getLineNum(), this->kind, arg1);
}

// ##########################
// ##     ErrorRaiser2     ##
// ##########################
void ErrorRaiser2::operator()(ast::Node *node, const std::string &arg1,
                              const std::string &arg2) throw(TypeCheckError) {
    throw TypeCheckError(node->getLineNum(), this->kind, arg1, arg2);
}

// ##########################
// ##     ErrorRaiser3     ##
// ##########################
void ErrorRaiser3::operator()(ast::Node *node, const std::string &arg1,
                              const std::string &arg2, const std::string &arg3) throw(TypeCheckError) {
    throw TypeCheckError(node->getLineNum(), this->kind, arg1, arg2, arg3);
}

#define GEN_VAR(ENUM, S1) ErrorRaiser0 E_##ENUM = { TypeCheckError::ENUM };
    EACH_TC_ERROR0(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1) ErrorRaiser1 E_##ENUM = { TypeCheckError::ENUM };
    EACH_TC_ERROR1(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2) ErrorRaiser2 E_##ENUM = { TypeCheckError::ENUM };
    EACH_TC_ERROR2(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2, S3) ErrorRaiser3 E_##ENUM = { TypeCheckError::ENUM };
    EACH_TC_ERROR3(GEN_VAR)
#undef GEN_VAR


#undef IN_SRC_FILE

} // namespace parser
} // namespace ydsh
