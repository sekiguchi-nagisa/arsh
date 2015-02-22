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

#include <parser/TokenKind.h>

static const char *TOKEN_KIND_STRING[] = {
#define GEN_NAME(ENUM) #ENUM,
        EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
};

const char *getTokenName(TokenKind kind) {
    return TOKEN_KIND_STRING[kind];
}


