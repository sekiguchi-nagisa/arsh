/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#include "handle.h"
#include "misc/util.hpp"

namespace ydsh {

std::string toString(FieldAttribute attr) {
    const char *table[] = {
#define GEN_STR(E, V) #E,
            EACH_FIELD_ATTR(GEN_STR)
#undef GEN_STR
    };

    std::string value;
    for(unsigned int i = 0; i < arraySize(table); i++) {
        if(hasFlag(attr, static_cast<FieldAttribute>(1u << i))) {
            if(!value.empty()) {
                value += " | ";
            }
            value += table[i];
        }
    }
    return value;
}

} // namespace ydsh