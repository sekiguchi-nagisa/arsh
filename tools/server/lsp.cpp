/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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

#include "lsp.h"

namespace ydsh::lsp {

const char *toString(TraceSetting setting) {
    switch(setting) {
#define GEN_CASE(E) case TraceSetting::E: return #E;
        EACH_TRACE_SETTING(GEN_CASE)
#undef GEN_CASE
    default:
        return "off";
    }
}

bool toEnum(const char *str, TraceSetting &setting) {
    StringRef ref = str;
    TraceSetting settings[] = {
#define GEN_ENUM(E) TraceSetting::E,
            EACH_TRACE_SETTING(GEN_ENUM)
#undef GEN_ENUM
    };
    for(auto &e : settings) {
        if(ref == toString(e)) {
            setting = e;
            return true;
        }
    }
    setting = TraceSetting::off;
    return false;
}

const char *toString(CodeActionKind kind) {
    switch(kind) {
#define GEN_CASE(E, V) case CodeActionKind::E: return V;
        EACH_CODE_ACTION_KIND(GEN_CASE)
#undef GEN_CASE
    default:
        return "";
    }
}

bool toEnum(const char *str, CodeActionKind &kind) {
    StringRef ref = str;
    CodeActionKind kinds[] = {
#define GEN_ENUM(E, V) CodeActionKind::E,
            EACH_CODE_ACTION_KIND(GEN_ENUM)
#undef GEN_ENUM
    };
    for(auto &e : kinds) {
        if(ref == toString(e)) {
            kind = e;
            return true;
        }
    }
    kind = CodeActionKind::Empty;
    return false;
}


} // namespace ydsh::lsp