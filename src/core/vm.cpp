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

#include "vm.h"
#include "opcode.h"

namespace ydsh {
namespace core {

#define vmswitch(V) switch(static_cast<OpCode>(V))

#if 0
#define vmcase(code) case OpCode::code: {fprintf(stderr, "pc: %u, code: %s\n", ctx.pc(), #code); }
#else
#define vmcase(code) case OpCode::code:
#endif

#define CALLABLE(ctx) (ctx.callableStack().back())
#define GET_CODE(ctx) (CALLABLE(ctx)->getCode())
#define CONST_POOL(ctx) (CALLABLE(ctx)->getConstPool())

static void skipHeader(RuntimeContext &ctx) {
    ctx.pc() = 0;
    if(CALLABLE(ctx)->getCallableKind() == CallableKind::TOPLEVEL) {
        unsigned short varNum = CALLABLE(ctx)->getLocalVarNum();
        unsigned short gvarNum = CALLABLE(ctx)->getGlobalVarNum();

        ctx.reserveGlobalVar(gvarNum);
        ctx.reserveLocalVar(ctx.getLocalVarOffset() + varNum);

        ++ctx.pc(); // skip callable kind
        ctx.pc() += 2; // skip local var num
        ++ctx.pc(); // skip global var num
    } else {
        ++ctx.pc(); // skip callable kind
        ++ctx.pc(); // skip local var num
    }
}

static void mainLoop(RuntimeContext &ctx) {
    while(true) {
        vmswitch(GET_CODE(ctx)[++ctx.pc()]) {
        vmcase(NOP) {
            break;
        }
        vmcase(STOP_EVAL) {
            return;
        }
        vmcase(ASSERT) {    //FIXME not work due to call frame
            ctx.checkAssertion();
            break;
        }
        vmcase(PRINT) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.printStackTop(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(INSTANCE_OF) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.instanceOf(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(CHECK_CAST) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.checkCast(reinterpret_cast<DSType *>(v));    //FIXME exception raising
            break;
        }
        vmcase(PUSH_TRUE) {
            ctx.push(ctx.getTrueObj());
            break;
        }
        vmcase(PUSH_FALSE) {
            ctx.push(ctx.getFalseObj());
            break;
        }
        vmcase(PUSH_ESTRING) {
            ctx.push(ctx.getEmptyStrObj());
            break;
        }
        vmcase(LOAD_CONST) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.push(CONST_POOL(ctx)[index]);
            break;
        }
        vmcase(LOAD_FUNC) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadGlobal(index);

            auto *func = typeAs<FuncObject>(ctx.peek());
            if(func->getType() == nullptr) {
                auto *handle = ctx.getSymbolTable().lookupHandle(func->getCallable().getName());
                assert(handle != nullptr);
                func->setType(handle->getFieldType(ctx.getPool()));
            }
            break;
        }
        vmcase(LOAD_GLOBAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadGlobal(index);
            break;
        }
        vmcase(STORE_GLOBAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.storeGlobal(index);
            break;
        }
        vmcase(LOAD_LOCAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadLocal(index);
            break;
        }
        vmcase(STORE_LOCAL) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.storeLocal(index);
            break;
        }
        vmcase(LOAD_FIELD) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.loadField(index);
            break;
        }
        vmcase(STORE_FIELD) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.storeField(index);
            break;
        }
        vmcase(IMPORT_ENV) {
            unsigned char b = read8(GET_CODE(ctx), ++ctx.pc());
            ctx.importEnv(b > 0);
            break;
        }
        vmcase(LOAD_ENV) {
            ctx.loadEnv();
            break;
        }
        vmcase(STORE_ENV) {
            ctx.storeEnv();
            break;
        }
        vmcase(POP) {
            ctx.popNoReturn();
            break;
        }
        vmcase(DUP) {
            ctx.dup();
            break;
        }
        vmcase(DUP2) {
            ctx.dup2();
            break;
        }
        vmcase(SWAP) {
            ctx.swap();
            break;
        }
        vmcase(NEW_STRING) {
            ctx.push(DSValue::create<String_Object>(ctx.getPool().getStringType()));
            break;
        }
        vmcase(APPEND_STRING) {
            DSValue v(ctx.pop());
            typeAs<String_Object>(ctx.peek())->append(std::move(v));
            break;
        }
        vmcase(NEW_ARRAY) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.push(DSValue::create<Array_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(APPEND_ARRAY) {
            DSValue v(ctx.pop());
            typeAs<Array_Object>(ctx.peek())->append(std::move(v));
            break;
        }
        vmcase(NEW_MAP) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.push(DSValue::create<Map_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(APPEND_MAP) {
            DSValue value(ctx.pop());
            DSValue key(ctx.pop());
            typeAs<Map_Object>(ctx.peek())->add(std::make_pair(std::move(key), std::move(value)));
            break;
        }
        vmcase(NEW_TUPLE) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.push(DSValue::create<Tuple_Object>(*reinterpret_cast<DSType *>(v)));
            break;
        }
        vmcase(NEW) {
            unsigned long v = read64(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 8;
            ctx.newDSObject(reinterpret_cast<DSType *>(v));
            break;
        }
        vmcase(CALL_INIT) {
            unsigned short paramSize = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.callConstructor(paramSize);
            break;
        }
        vmcase(CALL_METHOD) {
            unsigned short index = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            unsigned short paramSize = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.callMethod(index, paramSize);   //FIXME exception raising
            break;
        }
        vmcase(CALL_FUNC) {
            unsigned short paramSize = read16(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() += 2;
            ctx.applyFuncObject(paramSize);
            skipHeader(ctx);
            break;
        }
        vmcase(RETURN) {
            ctx.restoreStackState();
            ctx.callableStack().pop_back();
            break;
        }
        vmcase(RETURN_V) {
            DSValue v(ctx.pop());
            ctx.restoreStackState();
            ctx.callableStack().pop_back();
            ctx.push(std::move(v));
            break;
        }
        vmcase(BRANCH) {
            unsigned short offset = read16(GET_CODE(ctx), ctx.pc() + 1);
            if(typeAs<Boolean_Object>(ctx.pop())->getValue()) {
                ctx.pc() += 2;
            } else {
                ctx.pc() += offset - 1;
            }
            break;
        }
        vmcase(GOTO) {
            unsigned int index = read32(GET_CODE(ctx), ctx.pc() + 1);
            ctx.pc() = index - 1;
            break;
        }
        vmcase(THROW) {
            ctx.throwException();
        }
        vmcase(ENTER_FINALLY) {
            const unsigned int index = read32(GET_CODE(ctx), ctx.pc() + 1);
            const unsigned int savedIndex = ctx.pc() + 4;
            ctx.push(DSValue::createNum(savedIndex));
            ctx.pc() = index - 1;
            break;
        }
        vmcase(EXIT_FINALLY) {
            switch(ctx.peek().kind()) {
            case DSValueKind::OBJECT: {
                ctx.throwException();
                break;
            }
            case DSValueKind::NUMBER: {
                unsigned int index = static_cast<unsigned int>(ctx.pop().value());
                ctx.pc() = index;
                break;
            }
            }
            break;
        }
        }
    }
}

/**
 * if found exception handler, return true.
 * otherwise return false.
 */
static bool handleException(RuntimeContext &ctx) {
    while(!ctx.callableStack().empty()) {
        if(ctx.pc() == 0) { // from native method
            ctx.restoreStackState();
            continue;
        }

        // search exception entry
        const unsigned int occurredPC = ctx.pc();
        const DSType *occurredType = ctx.getThrownObject()->getType();

        for(unsigned int i = 0; CALLABLE(ctx)->getExceptionEntries()[i].type != nullptr; i++) {
            const ExceptionEntry &entry = CALLABLE(ctx)->getExceptionEntries()[i];
            if(occurredPC >= entry.begin && occurredPC < entry.end
               && entry.type->isSameOrBaseTypeOf(*occurredType)) {
                ctx.pc() = entry.dest - 1;
                ctx.loadThrownObject();
                return true;
            }
        }

        // unwind stack
        if(ctx.callableStack().size() > 1) {
            ctx.restoreStackState();
        }
        ctx.callableStack().pop_back();
    }
    return false;
}

bool vmEval(RuntimeContext &ctx, Callable &callable) {
    ctx.resetState();

    ctx.callableStack().push_back(&callable);
    skipHeader(ctx);

    while(true) {
        try {
            mainLoop(ctx);
            ctx.callableStack().clear();
            return true;
        } catch(const DSExcepton &) {
            if(handleException(ctx)) {
                continue;
            }
            return false;
        }
    }
}

} // namespace core
} // namespace ydsh
