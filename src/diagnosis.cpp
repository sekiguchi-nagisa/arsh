/*
 * Copyright (C) 2016-2017 Nagisa Sekiguchi
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

#include <cstdarg>

#include "diagnosis.h"

namespace ydsh {

TypeLookupError createTLError(TLError, const char *kind, const char *fmt, ...) {
    va_list arg;

    va_start(arg, fmt);
    char *str = nullptr;
    vasprintf(&str, fmt, arg);
    va_end(arg);

    TypeLookupError error(kind, str);
    free(str);
    return error;
}

TypeCheckError createTCError(TCError, const Node &node, const char *kind, const char *fmt, ...) {
    va_list arg;

    va_start(arg, fmt);
    char *str = nullptr;
    vasprintf(&str, fmt, arg);
    va_end(arg);

    TypeCheckError error(node.getToken(), kind, str);
    free(str);
    return error;
}

} // namespace ydsh