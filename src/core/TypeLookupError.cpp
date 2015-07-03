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

#define IN_SRC_FILE
#include "TypeLookupError.h"

namespace ydsh {
namespace core {

// for message
struct MsgTuple1 {
    const char *kindStr;
    const char *msg1;
};

static MsgTuple1 errorTuple0[] = {
#define GEN_MSG(ENUM, S1) {#ENUM, S1},
        EACH_TL_ERROR0(GEN_MSG)
#undef GEN_MSG
};

static MsgTuple1 errorTuple1[] = {
#define GEN_MSG(ENUM, S1) {#ENUM, S1},
        EACH_TL_ERROR1(GEN_MSG)
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
        EACH_TL_ERROR3(GEN_MSG)
#undef GEN_MSG
};


// #############################
// ##     TypeLookupError     ##
// #############################

TypeLookupError::TypeLookupError(TypeLookupError::ErrorKind0 k) :
        kind(errorTuple0[k].kindStr), message(errorTuple0[k].msg1) {
}

TypeLookupError::TypeLookupError(TypeLookupError::ErrorKind1 k, const std::string &arg1) :
        kind(errorTuple1[k].kindStr), message(errorTuple1[k].msg1) {
    this->message += arg1;
}

TypeLookupError::TypeLookupError(TypeLookupError::ErrorKind3 k, const std::string &arg1,
                                 const std::string &arg2, const std::string &arg3) :
        kind(errorTuple3[k].kindStr), message(errorTuple3[k].msg1) {
    this->message += arg1;
    this->message += errorTuple3[k].msg2;
    this->message += arg2;
    this->message += errorTuple3[k].msg3;
    this->message += arg3;

}

std::ostream &operator<<(std::ostream &stream, TypeLookupError::ErrorKind0 kind) {
    stream << errorTuple0[kind].kindStr;
    return stream;
}

std::ostream &operator<<(std::ostream &stream, TypeLookupError::ErrorKind1 kind) {
    stream << errorTuple1[kind].kindStr;
    return stream;
}

std::ostream &operator<<(std::ostream &stream, TypeLookupError::ErrorKind3 kind) {
    stream << errorTuple3[kind].kindStr;
    return stream;
}

#define GEN_VAR(ENUM, S1) ErrorRaiser0 E_##ENUM = { TypeLookupError::ENUM };
    EACH_TL_ERROR0(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1) ErrorRaiser1 E_##ENUM = { TypeLookupError::ENUM };
    EACH_TL_ERROR1(GEN_VAR)
#undef GEN_VAR

#define GEN_VAR(ENUM, S1, S2, S3) ErrorRaiser3 E_##ENUM = { TypeLookupError::ENUM };
EACH_TL_ERROR3(GEN_VAR)
#undef GEN_VAR


#undef IN_SRC_FILE

} // namespace core
} // namespace ydsh