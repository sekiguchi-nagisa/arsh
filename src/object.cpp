/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <memory>

#include "object.h"
#include "core.h"
#include "symbol_table.h"
#include "misc/num.h"

namespace ydsh {

// ######################
// ##     DSObject     ##
// ######################

DSValue *DSObject::getFieldTable() {
    return nullptr;
}

std::string DSObject::toString(DSState &, VisitedSet *) {
    std::string str("DSObject(");
    str += std::to_string(reinterpret_cast<long>(this));
    str += ")";
    return str;
}

bool DSObject::equals(const DSValue &obj) const {
    return reinterpret_cast<long>(this) == reinterpret_cast<long>(obj.get());
}

DSValue DSObject::str(DSState &ctx) {
    return DSValue::create<String_Object>(getPool(ctx).get(TYPE::String), this->toString(ctx, nullptr));
}

DSValue DSObject::interp(DSState &ctx, VisitedSet *) {
    return this->str(ctx);
}

DSValue DSObject::commandArg(DSState &ctx, VisitedSet *) {
    return this->str(ctx);
}

size_t DSObject::hash() const {
    return std::hash<long>()(reinterpret_cast<long>(this));
}

// ########################
// ##     Int_Object     ##
// ########################

std::string Int_Object::toString(DSState &ctx, VisitedSet *) {
    if(*this->type == getPool(ctx).get(TYPE::Uint32)) {
        return std::to_string(static_cast<unsigned int>(this->value));
    }
    return std::to_string(this->value);
}

bool Int_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Int_Object>(obj)->value;
}

size_t Int_Object::hash() const {
    return std::hash<int>()(this->value);
}

// ###########################
// ##     UnixFD_Object     ##
// ###########################

UnixFD_Object::~UnixFD_Object() {
    int fd = this->getValue();
    if(fd > STDERR_FILENO) {
        close(this->getValue());    // do not close standard io file descriptor
    }
}

std::string UnixFD_Object::toString(DSState &, ydsh::VisitedSet *) {
    std::string str = "/dev/fd/";
    str += std::to_string(this->value);
    return str;
}


// #########################
// ##     Long_Object     ##
// #########################

std::string Long_Object::toString(DSState &ctx, VisitedSet *) {
    if(*this->type == getPool(ctx).get(TYPE::Uint64)) {
        return std::to_string(static_cast<unsigned long>(this->value));
    }
    return std::to_string(this->value);
}

bool Long_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Long_Object>(obj)->value;
}

size_t Long_Object::hash() const {
    return std::hash<long>()(this->value);
}

// ##########################
// ##     Float_Object     ##
// ##########################

std::string Float_Object::toString(DSState &, VisitedSet *) {
    return std::to_string(this->value);
}

bool Float_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Float_Object>(obj)->value;
}

size_t Float_Object::hash() const {
    return std::hash<double>()(this->value);
}


// ############################
// ##     Boolean_Object     ##
// ############################

std::string Boolean_Object::toString(DSState &, VisitedSet *) {
    return this->value ? "true" : "false";
}

bool Boolean_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<Boolean_Object>(obj)->value;
}

size_t Boolean_Object::hash() const {
    return std::hash<bool>()(this->value);
}

// ###########################
// ##     String_Object     ##
// ###########################

std::string String_Object::toString(DSState &, VisitedSet *) {
    return this->value;
}

bool String_Object::equals(const DSValue &obj) const {
    return this->value == typeAs<String_Object>(obj)->value;
}

size_t String_Object::hash() const {
    return std::hash<std::string>()(this->value);
}

// ##########################
// ##     Array_Object     ##
// ##########################

static bool checkCircularRef(DSState &ctx, VisitedSet * &visitedSet,
                             std::shared_ptr<VisitedSet> &newSet, const DSObject *thisPtr) {
    if(visitedSet == nullptr) {
        auto &elementTypes = static_cast<ReifiedType *>(thisPtr->getType())->getElementTypes();
        for(auto &elementType : elementTypes) {
            if(*elementType == getPool(ctx).get(TYPE::Any)) {
                newSet = std::make_shared<VisitedSet>();
                visitedSet = newSet.get();
                break;
            }
        }
    } else {
        if(visitedSet->find((unsigned long) thisPtr) != visitedSet->end()) {
            raiseError(ctx, getPool(ctx).get(TYPE::StackOverflowError), "caused by circular reference");
            return false;
        }
    }
    return true;
}

static void preVisit(VisitedSet *set, const DSObject *ptr) {
    if(set != nullptr) {
        set->insert(reinterpret_cast<unsigned long>(ptr));
    }
}

static void postVisit(VisitedSet *set, const DSObject *ptr) {
    if(set != nullptr) {
        set->erase(reinterpret_cast<unsigned long>(ptr));
    }
}

static bool checkInvalid(DSState &st, DSValue &v) {
    if(v.kind() == DSValueKind::INVALID) {
        raiseError(st, getPool(st).get(TYPE::UnwrappingError), "invalid value");
        return false;
    }
    return true;
}

#define TRY(T, E) \
({ auto v = E; if(hasError(ctx)) { return T(); } std::forward<decltype(v)>(v); })


std::string Array_Object::toString(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(std::string, checkCircularRef(ctx, visitedSet, newSet, this));

    std::string str("[");
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        TRY(std::string, checkInvalid(ctx, this->values[i]));
        preVisit(visitedSet, this);
        str += TRY(std::string, this->values[i]->toString(ctx, visitedSet));
        postVisit(visitedSet, this);
    }
    str += "]";
    return str;
}

const DSValue &Array_Object::nextElement() {
    unsigned int index = this->curIndex++;
    assert(index < this->values.size());
    return this->values[index];
}

DSValue Array_Object::interp(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(DSValue, checkCircularRef(ctx, visitedSet, newSet, this));

    if(this->values.size() == 1) {
        TRY(DSValue, checkInvalid(ctx, this->values[0]));
        preVisit(visitedSet, this);
        auto v = TRY(DSValue, this->values[0]->interp(ctx, visitedSet));
        postVisit(visitedSet, this);
        return v;
    }

    std::string str;
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += " ";
        }
        TRY(DSValue, checkInvalid(ctx, this->values[i]));
        preVisit(visitedSet, this);
        auto ret = TRY(DSValue, this->values[i]->interp(ctx, visitedSet));
        str += typeAs<String_Object>(ret)->getValue();
        postVisit(visitedSet, this);
    }
    return DSValue::create<String_Object>(getPool(ctx).get(TYPE::String), std::move(str));
}

DSValue Array_Object::commandArg(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(DSValue, checkCircularRef(ctx, visitedSet, newSet, this));

    auto result = DSValue::create<Array_Object>(getPool(ctx).get(TYPE::StringArray));
    for(auto &e : this->values) {
        TRY(DSValue, checkInvalid(ctx, e));
        preVisit(visitedSet, this);
        auto temp = TRY(DSValue, e->commandArg(ctx, visitedSet));
        postVisit(visitedSet, this);

        DSType *tempType = temp->getType();
        if(*tempType == getPool(ctx).get(TYPE::String)) {
            typeAs<Array_Object>(result)->values.push_back(std::move(temp));
        } else {
            assert(*tempType == getPool(ctx).get(TYPE::StringArray));
            auto *tempArray = typeAs<Array_Object>(temp);
            for(auto &tempValue : tempArray->values) {
                typeAs<Array_Object>(result)->values.push_back(tempValue);
            }
        }
    }
    return result;
}

// ########################
// ##     Map_Object     ##
// ########################

DSValue Map_Object::nextElement(DSState &ctx) {
    std::vector<DSType *> types(2);
    types[0] = this->iter->first->getType();
    types[1] = this->iter->second->getType();

    auto entry = DSValue::create<Tuple_Object>(getPool(ctx).createTupleType(std::move(types)));
    typeAs<Tuple_Object>(entry)->set(0, this->iter->first);
    typeAs<Tuple_Object>(entry)->set(1, this->iter->second);
    ++this->iter;

    return entry;
}

std::string Map_Object::toString(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(std::string, checkCircularRef(ctx, visitedSet, newSet, this));

    std::string str("[");
    unsigned int count = 0;
    for(auto &iter : this->valueMap) {
        if(count++ > 0) {
            str += ", ";
        }
        str += iter.first->toString(ctx, nullptr);
        str += " : ";

        TRY(std::string, checkInvalid(ctx, iter.second));
        preVisit(visitedSet, this);
        str += TRY(std::string, iter.second->toString(ctx, visitedSet));
        postVisit(visitedSet, this);
    }
    str += "]";
    return str;
}

// ########################
// ##     Job_Object     ##
// ########################

std::string Job_Object::toString(DSState &, VisitedSet *) {
    std::string str = "%";
    str += std::to_string(this->entry->jobID());
    return str;
}

// ########################
// ##     BaseObject     ##
// ########################

BaseObject::~BaseObject() {
    delete[] this->fieldTable;
}

DSValue *BaseObject::getFieldTable() {
    return this->fieldTable;
}

// ##########################
// ##     Tuple_Object     ##
// ##########################

std::string Tuple_Object::toString(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(std::string, checkCircularRef(ctx, visitedSet, newSet, this));

    std::string str("(");
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }

        TRY(std::string, checkInvalid(ctx, this->fieldTable[i]));
        preVisit(visitedSet, this);
        str += TRY(std::string, this->fieldTable[i]->toString(ctx, visitedSet));
        postVisit(visitedSet, this);
    }
    str += ")";
    return str;
}

DSValue Tuple_Object::interp(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(DSValue, checkCircularRef(ctx, visitedSet, newSet, this));

    std::string str;
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += " ";
        }
        TRY(DSValue, checkInvalid(ctx, this->fieldTable[i]));
        preVisit(visitedSet, this);
        auto ret = TRY(DSValue, this->fieldTable[i]->interp(ctx, visitedSet));
        str += typeAs<String_Object>(ret)->getValue();
        postVisit(visitedSet, this);
    }
    return DSValue::create<String_Object>(getPool(ctx).get(TYPE::String), std::move(str));
}

DSValue Tuple_Object::commandArg(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    TRY(DSValue, checkCircularRef(ctx, visitedSet, newSet, this));

    auto result = DSValue::create<Array_Object>(getPool(ctx).get(TYPE::StringArray));
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        TRY(DSValue, checkInvalid(ctx, this->fieldTable[i]));
        preVisit(visitedSet, this);
        auto temp = TRY(DSValue, this->fieldTable[i]->commandArg(ctx, visitedSet));
        postVisit(visitedSet, this);

        DSType *tempType = temp->getType();
        if(*tempType == getPool(ctx).get(TYPE::String)) {
            typeAs<Array_Object>(result)->append(std::move(temp));
        } else {
            assert(*tempType == getPool(ctx).get(TYPE::StringArray));
            auto *tempArray = typeAs<Array_Object>(temp);
            for(auto &tempValue : tempArray->getValues()) {
                typeAs<Array_Object>(result)->append(tempValue);
            }
        }
    }
    return result;
}

// ##########################
// ##     Error_Object     ##
// ##########################

std::string Error_Object::toString(DSState &ctx, VisitedSet *) {
    std::string str(getPool(ctx).getTypeName(*this->type));
    str += ": ";
    str += typeAs<String_Object>(this->message)->getValue();
    return str;
}

void Error_Object::printStackTrace(DSState &ctx) {
    // print header
    fprintf(stderr, "%s\n", this->toString(ctx, nullptr).c_str());

    // print stack trace
    for(auto &s : this->stackTrace) {
        fprintf(stderr, "    from %s:%d '%s()'\n",
                s.getSourceName().c_str(), s.getLineNum(), s.getCallerName().c_str());
    }
}

DSValue Error_Object::newError(const DSState &ctx, DSType &type, DSValue &&message) {
    DSValue obj(new Error_Object(type, std::move(message)));
    typeAs<Error_Object>(obj)->createStackTrace(ctx);
    typeAs<Error_Object>(obj)->name = DSValue::create<String_Object>(
            getPool(ctx).get(TYPE::String), getPool(ctx).getTypeName(type));
    return obj;
}

void Error_Object::createStackTrace(const DSState &ctx) {
    fillInStackTrace(ctx, this->stackTrace);
}

unsigned int getLineNum(const LineNumEntry *entries, unsigned int index) {  //FIXME binary search
    unsigned int i = 0;
    for(; entries[i].address > 0; i++) {
        if(index < entries[i].address) {
            break;
        }
    }
    return entries[i > 0 ? i - 1 : 0].lineNum;
}

// ########################
// ##     FuncObject     ##
// ########################

std::string FuncObject::toString(DSState &, VisitedSet *) {
    std::string str = this->code.is(CodeKind::FUNCTION) ? "function(" : "module(";
    str += this->code.getName();
    str += ")";
    return str;
}

} // namespace ydsh
