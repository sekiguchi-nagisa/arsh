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
    }
    ctx.pc() += CALLABLE(ctx)->getCodeOffset() - 1;
}

static bool isSpace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

static bool isFieldSep(const char *ifs, int ch) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(ifs[i] == ch) {
            return true;
        }
    }
    return false;
}

static bool hasSpace(const char *ifs) {
    for(unsigned int i = 0; ifs[i] != '\0'; i++) {
        if(isSpace(ifs[i])) {
            return true;
        }
    }
    return false;
}

static void forkAndCapture(bool isStr, RuntimeContext &ctx) {
    const unsigned short offset = read16(GET_CODE(ctx), ctx.pc() + 1);

    // capture stdout
    pid_t pipefds[2];

    if(pipe(pipefds) < 0) {
        perror("pipe creation failed\n");
        exit(1);    //FIXME: throw exception
    }

    pid_t pid = xfork();
    if(pid > 0) {   // parent process
        close(pipefds[WRITE_PIPE]);

        DSValue obj;

        if(isStr) {  // capture stdout as String
            static const int bufSize = 256;
            char buf[bufSize + 1];
            std::string str;
            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }
                buf[readSize] = '\0';
                str += buf;
            }

            // remove last newlines
            std::string::size_type pos = str.find_last_not_of('\n');
            if(pos == std::string::npos) {
                str.clear();
            } else {
                str.erase(pos + 1);
            }

            obj = DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str));
        } else {    // capture stdout as String Array
            const char *ifs = ctx.getIFS();
            unsigned int skipCount = 1;

            static const int bufSize = 256;
            char buf[bufSize];
            std::string str;
            obj = DSValue::create<Array_Object>(ctx.getPool().getStringArrayType());
            Array_Object *array = typeAs<Array_Object>(obj);

            while(true) {
                int readSize = read(pipefds[READ_PIPE], buf, bufSize);
                if(readSize == -1 && (errno == EINTR || errno == EAGAIN)) {
                    continue;
                }
                if(readSize <= 0) {
                    break;
                }

                for(int i = 0; i < readSize; i++) {
                    char ch = buf[i];
                    bool fieldSep = isFieldSep(ifs, ch);
                    if(fieldSep && skipCount > 0) {
                        if(isSpace(ch)) {
                            continue;
                        }
                        if(--skipCount == 1) {
                            continue;
                        }
                    }
                    skipCount = 0;
                    if(fieldSep) {
                        array->append(DSValue::create<String_Object>(
                                ctx.getPool().getStringType(), std::move(str)));
                        str = "";
                        skipCount = isSpace(ch) ? 2 : 1;
                        continue;
                    }
                    str += ch;
                }
            }

            // remove last newline
            while(!str.empty() && str.back() == '\n') {
                str.pop_back();
            }

            // append remain
            if(!str.empty() || !hasSpace(ifs)) {
                array->append(DSValue::create<String_Object>(
                        ctx.getPool().getStringType(), std::move(str)));
            }
        }
        close(pipefds[READ_PIPE]);

        // wait exit
        int status;
        ctx.xwaitpid(pid, status, 0);
        if(WIFEXITED(status)) {
            ctx.updateExitStatus(WEXITSTATUS(status));
        }
        if(WIFSIGNALED(status)) {
            ctx.updateExitStatus(WTERMSIG(status));
        }

        // push object
        ctx.push(std::move(obj));

        ctx.pc() += offset - 1;
    } else if(pid == 0) {   // child process
        dup2(pipefds[WRITE_PIPE], STDOUT_FILENO);
        close(pipefds[READ_PIPE]);
        close(pipefds[WRITE_PIPE]);

        ctx.pc() += 2;
    } else {
        perror("fork failed");
        exit(1);    //FIXME: throw exception
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
            ctx.callMethod(index, paramSize);
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
            ctx.throwException(ctx.pop());
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
                ctx.throwException(ctx.pop());
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
        vmcase(COPY_INT) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            int v = typeAs<Int_Object>(ctx.pop())->getValue();
            ctx.push(DSValue::create<Int_Object>(*type, v));
            break;
        }
        vmcase(TO_BYTE) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            v &= 0xFF;  // fill higher bits (8th ~ 31) with 0
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getByteType(), v));
            break;
        }
        vmcase(TO_U16) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getUint16Type(), v));
            break;
        }
        vmcase(TO_I16) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            v &= 0xFFFF;    // fill higher bits (16th ~ 31th) with 0
            if(v & 0x8000) {    // if 15th bit is 1, fill higher bits with 1
                v |= 0xFFFF0000;
            }
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getInt16Type(), v));
            break;
        }
        vmcase(NEW_LONG) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            unsigned long l = v;
            ctx.push(DSValue::create<Long_Object>(*type, l));
            break;
        }
        vmcase(COPY_LONG) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            long v = typeAs<Long_Object>(ctx.pop())->getValue();
            ctx.push(DSValue::create<Long_Object>(*type, v));
            break;
        }
        vmcase(I_NEW_LONG) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            int v = typeAs<Int_Object>(ctx.pop())->getValue();
            long l = v;
            ctx.push(DSValue::create<Long_Object>(*type, l));
            break;
        }
        vmcase(NEW_INT) {
            DSType *type = ctx.getPool().getByNumTypeIndex(read8(GET_CODE(ctx), ++ctx.pc()));
            unsigned long l = typeAs<Long_Object>(ctx.pop())->getValue();
            unsigned int v = static_cast<unsigned int>(l);
            ctx.push(DSValue::create<Int_Object>(*type, v));
            break;
        }
        vmcase(U32_TO_D) {
            unsigned int v = typeAs<Int_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(I32_TO_D) {
            int v = typeAs<Int_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(U64_TO_D) {
            unsigned long v = typeAs<Long_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(I64_TO_D) {
            long v = typeAs<Long_Object>(ctx.pop())->getValue();
            double d = static_cast<double>(v);
            ctx.push(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), d));
            break;
        }
        vmcase(D_TO_U32) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            unsigned int v = static_cast<unsigned int>(d);
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), v));
            break;
        }
        vmcase(D_TO_I32) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            int v = static_cast<int>(d);
            ctx.push(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), v));
            break;
        }
        vmcase(D_TO_U64) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            unsigned long v = static_cast<unsigned long>(d);
            ctx.push(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), v));
            break;
        }
        vmcase(D_TO_I64) {
            double d = typeAs<Float_Object>(ctx.pop())->getValue();
            long v = static_cast<long>(d);
            ctx.push(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), v));
            break;
        }
        vmcase(EXIT_CHILD) {
            if(ctx.peek()) {
                ctx.handleUncaughtException(ctx.pop());
                exit(1);
            } else {
                exit(ctx.getExitStatus());
            }
        }
        vmcase(CAPTURE_STR) {
            forkAndCapture(true, ctx);
            break;
        }
        vmcase(CAPTURE_ARRAY) {
            forkAndCapture(false, ctx);
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
