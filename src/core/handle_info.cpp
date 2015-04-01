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

#include <core/handle_info.h>

typedef struct {
    const char *handleInfo;
    unsigned int pos;
} context_t;

static bool isType(context_t *ctx) {
    if(ctx->handleInfo[ctx->pos] != '\0') {
        switch(ctx->handleInfo[ctx->pos++]) {
        case VOID_T:
        case ANY_T:
        case INT_T:
        case FLOAT_T:
        case BOOL_T:
        case STRING_T:
        case ARRAY_T:
        case MAP_T:
            return true;
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return false;
        case T0:
        case T1:
            return true;
        }
    }
    return false;
}

static int getNum(context_t *ctx) {
    if(ctx->handleInfo[ctx->pos] != '\0') {
        char ch = ctx->handleInfo[ctx->pos++];
        switch(ch) {
        case VOID_T:
        case ANY_T:
        case INT_T:
        case FLOAT_T:
        case BOOL_T:
        case STRING_T:
        case ARRAY_T:
        case MAP_T:
            return -1;
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return (int) (ch - P_N0);
        case T0:
        case T1:
            return -1;
        }
    }
    return -1;
}


bool verifyHandleInfo(char *handleInfo) {
    context_t ctx = {handleInfo, 0};

    /**
     * check return type
     */
    if(!isType(&ctx)) {
        return false;
    }

    /**
     * check param size
     */
    int paramSize = getNum(&ctx);
    if(paramSize < 0 || paramSize > 8) {
        return false;
    }

    /**
     * check param types
     */
    for(int i = 0; i < paramSize; i++) {
        if(!isType(&ctx)) {
            return false;
        }
    }

    /**
     * check null terminate
     */
    return ctx.handleInfo[ctx.pos] == '\0';
}
