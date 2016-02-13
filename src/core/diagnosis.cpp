/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#include "diagnosis.h"

namespace ydsh {
namespace core {

const char *getTLErrorKind(TLError e) {
    const char *table[] = {
#define GEN_KIND(K, M) #K,
            EACH_TL_ERROR(GEN_KIND)
#undef GEN_KIND
    };
    return table[static_cast<unsigned int>(e)];
}

static std::string formatMessage(const char *fmt, const std::string **v) {
    unsigned int c = 0;
    std::string str;
    for(unsigned int i = 0; fmt[i] != '\0'; i++) {
        char ch = fmt[i];
        if(ch == '%') {
            str += *v[c++];
        } else {
            str += ch;
        }
    }
    return str;
}

namespace __detail_tl_error {

TypeLookupError createErrorImpl(TLError e, const std::string **v) {
    return TypeLookupError(getTLErrorKind(e), formatMessage(getTLErrorMessage(e), v));
}

} // namespace __detail_tl_error

} // namespace core
} // namespace ydsh


namespace ydsh {
namespace parser {

const char *getTCErrorKind(TCError e) {
    const char *table[] = {
#define GEN_KIND(K, M) #K,
            EACH_TC_ERROR(GEN_KIND)
#undef GEN_KIND
    };
    return table[static_cast<unsigned int>(e)];
}

namespace __detail_tc_error {

TypeCheckError createErrorImpl(TCError e, const ast::Node &node, const std::string **v) {
    return TypeCheckError(node.getToken(), getTCErrorKind(e), core::formatMessage(getTCErrorMessage(e), v));
}

} // namespace __detail_tc_error

} // namespace parser
} // namespace ydsh