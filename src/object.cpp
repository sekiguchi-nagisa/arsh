/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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
#include "misc/num.h"

namespace ydsh {

// ######################
// ##     DSObject     ##
// ######################

void DSObject::setType(DSType *) {  // do nothing.
}

DSValue *DSObject::getFieldTable() {
    return nullptr;
}

std::string DSObject::toString(DSState &, VisitedSet *) {
    std::string str("DSObject(");
    str += std::to_string((long) this);
    str += ")";
    return str;
}

bool DSObject::equals(const DSValue &obj) {
    return (long) this == (long) obj.get();
}

DSValue DSObject::str(DSState &ctx) {
    return DSValue::create<String_Object>(getPool(ctx).getStringType(), this->toString(ctx, nullptr));
}

DSValue DSObject::interp(DSState &ctx, VisitedSet *) {
    return this->str(ctx);
}

DSValue DSObject::commandArg(DSState &ctx, VisitedSet *) {
    return this->str(ctx);
}

size_t DSObject::hash() {
    return std::hash<long>()((long) this);
}

bool DSObject::introspect(DSState &, DSType *targetType) {
    return targetType->isSameOrBaseTypeOf(*this->type);
}

// ########################
// ##     Int_Object     ##
// ########################

std::string Int_Object::toString(DSState &ctx, VisitedSet *) {
    if(*this->type == getPool(ctx).getUint32Type()) {
        return std::to_string(static_cast<unsigned int>(this->value));
    }
    return std::to_string(this->value);
}

bool Int_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Int_Object>(obj)->value;
}

size_t Int_Object::hash() {
    return std::hash<int>()(this->value);
}

// #########################
// ##     Long_Object     ##
// #########################

std::string Long_Object::toString(DSState &ctx, VisitedSet *) {
    if(*this->type == getPool(ctx).getUint64Type()) {
        return std::to_string(static_cast<unsigned long>(this->value));
    }
    return std::to_string(this->value);
}

bool Long_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Long_Object>(obj)->value;
}

size_t Long_Object::hash() {
    return std::hash<long>()(this->value);
}

// ##########################
// ##     Float_Object     ##
// ##########################

std::string Float_Object::toString(DSState &, VisitedSet *) {
    return std::to_string(this->value);
}

bool Float_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Float_Object>(obj)->value;
}

size_t Float_Object::hash() {
    return std::hash<double>()(this->value);
}


// ############################
// ##     Boolean_Object     ##
// ############################

std::string Boolean_Object::toString(DSState &, VisitedSet *) {
    return this->value ? "true" : "false";
}

bool Boolean_Object::equals(const DSValue &obj) {
    return this->value == typeAs<Boolean_Object>(obj)->value;
}

size_t Boolean_Object::hash() {
    return std::hash<bool>()(this->value);
}

// ###########################
// ##     String_Object     ##
// ###########################

void String_Object::append(DSValue &&obj) {
    this->value += typeAs<String_Object>(obj)->value;
}

std::string String_Object::toString(DSState &, VisitedSet *) {
    return this->value;
}

bool String_Object::equals(const DSValue &obj) {
    return this->value == typeAs<String_Object>(obj)->value;
}

size_t String_Object::hash() {
    return std::hash<std::string>()(this->value);
}

// ##########################
// ##     Array_Object     ##
// ##########################

static void checkCircularRef(DSState &ctx, VisitedSet * &visitedSet,
                             std::shared_ptr<VisitedSet> &newSet, const DSObject *thisPtr) {
    if(visitedSet == nullptr) {
        auto &elementTypes = static_cast<ReifiedType *>(thisPtr->getType())->getElementTypes();
        for(auto &elementType : elementTypes) {
            if(*elementType == getPool(ctx).getVariantType() ||
               *elementType == getPool(ctx).getAnyType()) {
                newSet = std::make_shared<VisitedSet>();
                visitedSet = newSet.get();
                break;
            }
        }
    } else {
        if(visitedSet->find((unsigned long) thisPtr) != visitedSet->end()) {
            throwError(ctx, getPool(ctx).getStackOverflowErrorType(), "caused by circular reference");
        }
    }
}

static void preVisit(VisitedSet *set, const DSObject *ptr) {
    if(set != nullptr) {
        set->insert((unsigned long) ptr);
    }
}

static void postVisit(VisitedSet *set, const DSObject *ptr) {
    if(set != nullptr) {
        set->erase((unsigned long) ptr);
    }
}


std::string Array_Object::toString(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    checkCircularRef(ctx, visitedSet, newSet, this);

    std::string str("[");
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }
        preVisit(visitedSet, this);
        str += this->values[i]->toString(ctx, visitedSet);
        postVisit(visitedSet, this);
    }
    str += "]";
    return str;
}

void Array_Object::append(DSValue &&obj) {
    this->values.push_back(std::move(obj));
}

void Array_Object::append(const DSValue &obj) {
    this->values.push_back(obj);
}

void Array_Object::set(unsigned int index, const DSValue &obj) {
    this->values[index] = obj;
}

const DSValue &Array_Object::nextElement() {
    unsigned int index = this->curIndex++;
    assert(index < this->values.size());
    return this->values[index];
}

DSValue Array_Object::interp(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    checkCircularRef(ctx, visitedSet, newSet, this);

    if(this->values.size() == 1) {
        preVisit(visitedSet, this);
        auto v = this->values[0]->interp(ctx, visitedSet);
        postVisit(visitedSet, this);
        return v;
    }

    std::string str;
    unsigned int size = this->values.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += " ";
        }
        preVisit(visitedSet, this);
        str += typeAs<String_Object>(this->values[i]->interp(ctx, visitedSet))->getValue();
        postVisit(visitedSet, this);
    }
    return DSValue::create<String_Object>(getPool(ctx).getStringType(), std::move(str));
}

DSValue Array_Object::commandArg(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    checkCircularRef(ctx, visitedSet, newSet, this);

    DSValue result(new Array_Object(getPool(ctx).getStringArrayType()));
    for(auto &e : this->values) {
        preVisit(visitedSet, this);
        DSValue temp(e->commandArg(ctx, visitedSet));
        postVisit(visitedSet, this);

        DSType *tempType = temp->getType();
        if(*tempType == getPool(ctx).getStringType()) {
            typeAs<Array_Object>(result)->values.push_back(std::move(temp));
        } else {
            assert(*tempType == getPool(ctx).getStringArrayType());
            Array_Object *tempArray = typeAs<Array_Object>(temp);
            for(auto &tempValue : tempArray->values) {
                typeAs<Array_Object>(result)->values.push_back(tempValue);
            }
        }
    }
    return result;
}

bool KeyCompare::operator()(const DSValue &x, const DSValue &y) const {
    return x->equals(y);
}

std::size_t GenHash::operator()(const DSValue &key) const {
    return key->hash();
}

// ########################
// ##     Map_Object     ##
// ########################

void Map_Object::set(const DSValue &key, const DSValue &value) {
    this->valueMap[key] = value;
}

void Map_Object::add(std::pair<DSValue, DSValue> &&entry) {
    this->valueMap.insert(std::move(entry));
}

void Map_Object::initIterator() {
    this->iter = this->valueMap.cbegin();
}

DSValue Map_Object::nextElement(DSState &ctx) {
    std::vector<DSType *> types(2);
    types[0] = this->iter->first->getType();
    types[1] = this->iter->second->getType();

    DSValue entry(new Tuple_Object(getPool(ctx).createTupleType(std::move(types))));
    typeAs<Tuple_Object>(entry)->set(0, this->iter->first);
    typeAs<Tuple_Object>(entry)->set(1, this->iter->second);
    ++this->iter;

    return entry;
}

bool Map_Object::hasNext() {
    return this->iter != this->valueMap.cend();
}

std::string Map_Object::toString(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    checkCircularRef(ctx, visitedSet, newSet, this);

    std::string str("[");
    unsigned int count = 0;
    for(auto &iter : this->valueMap) {
        if(count++ > 0) {
            str += ", ";
        }
        str += iter.first->toString(ctx, nullptr);
        str += " : ";

        preVisit(visitedSet, this);
        str += iter.second->toString(ctx, visitedSet);
        postVisit(visitedSet, this);
    }
    str += "]";
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
    checkCircularRef(ctx, visitedSet, newSet, this);

    std::string str("(");
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ", ";
        }

        preVisit(visitedSet, this);
        str += this->fieldTable[i]->toString(ctx, visitedSet);
        postVisit(visitedSet, this);
    }
    str += ")";
    return str;
}

void Tuple_Object::set(unsigned int elementIndex, const DSValue &obj) {
    this->fieldTable[elementIndex] = obj;
}

const DSValue &Tuple_Object::get(unsigned int elementIndex) {
    return this->fieldTable[elementIndex];
}

DSValue Tuple_Object::interp(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    checkCircularRef(ctx, visitedSet, newSet, this);

    std::string str;
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += " ";
        }
        preVisit(visitedSet, this);
        str += typeAs<String_Object>(this->fieldTable[i]->interp(ctx, visitedSet))->getValue();
        postVisit(visitedSet, this);
    }
    return DSValue::create<String_Object>(getPool(ctx).getStringType(), std::move(str));
}

DSValue Tuple_Object::commandArg(DSState &ctx, VisitedSet *visitedSet) {
    std::shared_ptr<VisitedSet> newSet;
    checkCircularRef(ctx, visitedSet, newSet, this);

    DSValue result(new Array_Object(getPool(ctx).getStringArrayType()));
    unsigned int size = this->getElementSize();
    for(unsigned int i = 0; i < size; i++) {
        preVisit(visitedSet, this);
        DSValue temp(this->fieldTable[i]->commandArg(ctx, visitedSet));
        postVisit(visitedSet, this);

        DSType *tempType = temp->getType();
        if(*tempType == getPool(ctx).getStringType()) {
            typeAs<Array_Object>(result)->append(std::move(temp));
        } else {
            assert(*tempType == getPool(ctx).getStringArrayType());
            Array_Object *tempArray = typeAs<Array_Object>(temp);
            for(auto &tempValue : tempArray->getValues()) {
                typeAs<Array_Object>(result)->append(tempValue);
            }
        }
    }
    return result;
}

// ###############################
// ##     StackTraceElement     ##
// ###############################

std::ostream &operator<<(std::ostream &stream, const StackTraceElement &e) {
    return stream << "from " << e.getSourceName() << ":"
           << e.getLineNum() << " '" << e.getCallerName() << "()'";
}

unsigned int getOccuredLineNum(const std::vector<StackTraceElement> &elements) {
    return elements.empty() ? 0 : elements.front().getLineNum();
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
    std::cerr << this->toString(ctx, nullptr) << std::endl;

    // print stack trace
    for(auto &s : this->stackTrace) {
        std::cerr << "    " << s << std::endl;
    }
}

const DSValue &Error_Object::getName(DSState &ctx) {
    if(!this->name) {
        this->name = DSValue::create<String_Object>(
                getPool(ctx).getStringType(), getPool(ctx).getTypeName(*this->type));
    }
    return this->name;
}

DSValue Error_Object::newError(const DSState &ctx, DSType &type, const DSValue &message) {
    auto obj = DSValue::create<Error_Object>(type, message);
    typeAs<Error_Object>(obj)->createStackTrace(ctx);
    return obj;
}

DSValue Error_Object::newError(const DSState &ctx, DSType &type, DSValue &&message) {
    auto obj = DSValue::create<Error_Object>(type, std::move(message));
    typeAs<Error_Object>(obj)->createStackTrace(ctx);
    return obj;
}

void Error_Object::createStackTrace(const DSState &ctx) {
    fillInStackTrace(ctx, this->stackTrace);
}

unsigned int getSourcePos(const SourcePosEntry *const entries, unsigned int index) {  //FIXME binary search
    unsigned int i = 0;
    for(; entries[i].address > 0; i++) {
        if(index < entries[i].address) {
            break;
        }
    }
    return entries[i > 0 ? i - 1 : 0].pos;
}

// ########################
// ##     FuncObject     ##
// ########################

void FuncObject::setType(DSType *type) {
    if(this->type == nullptr) {
        assert(dynamic_cast<FunctionType *>(type) != nullptr);
        this->type = type;
    }
}

std::string FuncObject::toString(DSState &, VisitedSet *) {
    std::string str("function(");
    str += this->code.getName();
    str += ")";
    return str;
}

static std::string toHexString(unsigned long v) {
    constexpr unsigned int size = sizeof(v) * 2 + 3;
    char buf[size];
    snprintf(buf, size, "0x%lx", v);
    return std::string(buf);
}

std::string encodeMethodDescriptor(const char *methodName, const MethodHandle *handle) {
    const unsigned long num = reinterpret_cast<unsigned long>(handle);
    std::string str(toHexString(num));
    str += ';';
    str += methodName;
    return str;
}

std::pair<const char *, const MethodHandle *> decodeMethodDescriptor(const char *desc) {
    const char *delim = strchr(desc, ';');
    std::string ptr(desc, delim - desc);
    int s = 0;
    unsigned long v = convertToUint64(ptr.c_str(), s);
    assert(s == 0);
    return std::make_pair(delim + 1, reinterpret_cast<const MethodHandle *>(v));
}

std::string encodeFieldDescriptor(const DSType &recvType, const char *fieldName, const DSType &fieldType) {
    std::string str(toHexString(reinterpret_cast<unsigned long>(&fieldType)));
    str += ';';
    str += toHexString(reinterpret_cast<unsigned long>(&recvType));
    str += ';';
    str += fieldName;
    return str;
}

std::tuple<const DSType *, const char *, const DSType *> decodeFieldDescriptor(const char *desc) {
    const char *delim = strchr(desc, ';');
    std::string ptr(desc, delim - desc);
    int s = 0;
    const DSType *fieldType = reinterpret_cast<const DSType *>(convertToUint64(ptr.c_str(), s));
    assert(s == 0);

    desc = delim + 1;
    delim = strchr(desc, ';');
    ptr = std::string(desc, delim - desc);
    const DSType *recvType = reinterpret_cast<const DSType *>(convertToUint64(ptr.c_str(), s));
    assert(s == 0);
    return std::make_tuple(recvType, delim + 1, fieldType);
}

} // namespace ydsh
