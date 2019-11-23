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

#include <cassert>

#include "symbol_table.h"
#include "handle.h"
#include "misc/fatal.h"
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

// ##########################
// ##     MethodHandle     ##
// ##########################

MethodHandle::~MethodHandle() {
    delete this->next;
}

void MethodHandle::addParamType(DSType &type) {
    this->paramTypes.push_back(&type);
}

class TypeDecoder {
private:
    SymbolTable &symbolTable;
    const HandleInfo *cursor;
    const std::vector<DSType *> *types;

public:
    TypeDecoder(SymbolTable &pool, const HandleInfo *pos, const std::vector<DSType *> *types) :
            symbolTable(pool), cursor(pos), types(types) {}
    ~TypeDecoder() = default;

    TypeOrError decode();

    unsigned int decodeNum() {
        return static_cast<unsigned int>(static_cast<int>(*(this->cursor++)) - static_cast<int>(HandleInfo::P_N0));
    }
};

#define TRY(E) ({ auto value = E; if(!value) { return value; } value.take(); })

TypeOrError TypeDecoder::decode() {
    switch(*(this->cursor++)) {
#define GEN_CASE(ENUM) case HandleInfo::ENUM: return Ok(&this->symbolTable.get(TYPE::ENUM));
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
    case HandleInfo::Array: {
        auto &t = this->symbolTable.getArrayTemplate();
        unsigned int size = this->decodeNum();
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = TRY(decode());
        return this->symbolTable.createReifiedType(t, std::move(elementTypes));
    }
    case HandleInfo::Map: {
        auto &t = this->symbolTable.getMapTemplate();
        unsigned int size = this->decodeNum();
        assert(size == 2);
        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = TRY(this->decode());
        }
        return this->symbolTable.createReifiedType(t, std::move(elementTypes));
    }
    case HandleInfo::Tuple: {
        unsigned int size = this->decodeNum();
        if(size == 0) { // variable length type
            size = this->types->size();
            std::vector<DSType *> elementTypes(size);
            for(unsigned int i = 0; i < size; i++) {
                elementTypes[i] = (*this->types)[i];
            }
            return this->symbolTable.createTupleType(std::move(elementTypes));
        }

        std::vector<DSType *> elementTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            elementTypes[i] = TRY(this->decode());
        }
        return this->symbolTable.createTupleType(std::move(elementTypes));
    }
    case HandleInfo::Option: {
        auto &t = this->symbolTable.getOptionTemplate();
        unsigned int size = this->decodeNum();
        assert(size == 1);
        std::vector<DSType *> elementTypes(size);
        elementTypes[0] = TRY(this->decode());
        return this->symbolTable.createReifiedType(t, std::move(elementTypes));
    }
    case HandleInfo::Func: {
        auto *retType = TRY(this->decode());
        unsigned int size = this->decodeNum();
        std::vector<DSType *> paramTypes(size);
        for(unsigned int i = 0; i < size; i++) {
            paramTypes[i] = TRY(this->decode());
        }
        return this->symbolTable.createFuncType(retType, std::move(paramTypes));
    }
    case HandleInfo::P_N0:
    case HandleInfo::P_N1:
    case HandleInfo::P_N2:
    case HandleInfo::P_N3:
    case HandleInfo::P_N4:
    case HandleInfo::P_N5:
    case HandleInfo::P_N6:
    case HandleInfo::P_N7:
    case HandleInfo::P_N8:
        fatal("must be type\n");
    case HandleInfo::T0:
        return Ok((*this->types)[0]);
    case HandleInfo::T1:
        return Ok((*this->types)[1]);
    default:
        return Ok(static_cast<DSType *>(nullptr)); // normally unreachable due to suppress gcc warning
    }
}

#define TRY2(E) ({ auto value = E; if(!value) { return false; } value.take(); })

// FIXME: error reporting
bool MethodHandle::init(SymbolTable &symbolTable, const NativeFuncInfo &info,
                        const std::vector<DSType *> *types) {
    TypeDecoder decoder(symbolTable, info.handleInfo, types);

    // check type parameter constraint
    const unsigned int constraintSize = decoder.decodeNum();
    for(unsigned int i = 0; i < constraintSize; i++) {
        auto *typeParam = TRY2(decoder.decode());
        auto *reqType = TRY2(decoder.decode());
        if(!reqType->isSameOrBaseTypeOf(*typeParam)) {
            return false;
        }
    }

    auto *returnType = TRY2(decoder.decode());    // init return type
    const unsigned int paramSize = decoder.decodeNum();
    auto *recvType = TRY2(decoder.decode());
    std::vector<DSType *> paramTypes(paramSize - 1);
    for(unsigned int i = 1; i < paramSize; i++) {   // init param types
        paramTypes[i - 1] = TRY2(decoder.decode());
    }

    this->returnType = returnType;
    this->recvType = recvType;
    this->paramTypes = std::move(paramTypes);
    return true;
}

} // namespace ydsh