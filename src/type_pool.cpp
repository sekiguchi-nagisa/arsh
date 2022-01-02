/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#include "type_pool.h"
#include "bind.h"

namespace ydsh {

// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool() {
  // initialize type
  this->initBuiltinType(TYPE::_ProcGuard, "process guard%%", false,
                        info_Dummy()); // pseudo base type
  this->initBuiltinType(TYPE::_Root, "pseudo top%%", false, TYPE::_ProcGuard,
                        info_Dummy()); // pseudo base type

  this->initBuiltinType(TYPE::Any, "Any", true, TYPE::_Root, info_AnyType());
  this->initBuiltinType(TYPE::Void, "Void", false, info_Dummy());
  this->initBuiltinType(TYPE::Nothing, "Nothing", false, info_Dummy());

  /**
   * hidden from script.
   */
  this->initBuiltinType(TYPE::_Value, "Value%%", true, TYPE::Any, info_Dummy());

  this->initBuiltinType(TYPE::Int, "Int", false, TYPE::_Value, info_IntType());
  this->initBuiltinType(TYPE::Float, "Float", false, TYPE::_Value, info_FloatType());
  this->initBuiltinType(TYPE::Boolean, "Boolean", false, TYPE::_Value, info_BooleanType());
  this->initBuiltinType(TYPE::String, "String", false, TYPE::_Value, info_StringType());

  this->initBuiltinType(TYPE::Regex, "Regex", false, TYPE::Any, info_RegexType());
  this->initBuiltinType(TYPE::Signal, "Signal", false, TYPE::_Value, info_SignalType());
  this->initBuiltinType(TYPE::Signals, "Signals", false, TYPE::Any, info_SignalsType());
  this->initBuiltinType(TYPE::Error, "Error", true, TYPE::Any, info_ErrorType());
  this->initBuiltinType(TYPE::Job, "Job", false, TYPE::Any, info_JobType());
  this->initBuiltinType(TYPE::Func, "Func", false, TYPE::Any, info_Dummy());
  this->initBuiltinType(TYPE::Module, "Module", false, TYPE::Any, info_ModuleType());
  this->initBuiltinType(TYPE::StringIter, "StringIter%%", false, TYPE::Any, info_StringIterType());
  this->initBuiltinType(TYPE::UnixFD, "UnixFD", false, TYPE::Any, info_UnixFDType());

  // initialize type template
  std::vector<const DSType *> elements = {&this->get(TYPE::Any)};
  this->initTypeTemplate(this->arrayTemplate, TYPE_ARRAY, std::move(elements), info_ArrayType());

  elements = {&this->get(TYPE::_Value), &this->get(TYPE::Any)};
  this->initTypeTemplate(this->mapTemplate, TYPE_MAP, std::move(elements), info_MapType());

  elements = std::vector<const DSType *>();
  this->initTypeTemplate(this->tupleTemplate, TYPE_TUPLE, std::move(elements),
                         info_TupleType()); // pseudo template.

  elements = std::vector<const DSType *>();
  this->initTypeTemplate(this->optionTemplate, TYPE_OPTION, std::move(elements),
                         info_OptionType()); // pseudo template

  // init string array type(for command argument)
  {
    std::vector<const DSType *> types = {&this->get(TYPE::String)};
    auto checked =
        this->createReifiedType(this->getArrayTemplate(), std::move(types)); // TYPE::StringArray
    (void)checked;
    assert(checked);
  }

  // init some error type
  this->initErrorType(TYPE::ArithmeticError, "ArithmeticError");
  this->initErrorType(TYPE::OutOfRangeError, "OutOfRangeError");
  this->initErrorType(TYPE::KeyNotFoundError, "KeyNotFoundError");
  this->initErrorType(TYPE::TypeCastError, "TypeCastError");
  this->initErrorType(TYPE::SystemError, "SystemError");
  this->initErrorType(TYPE::StackOverflowError, "StackOverflowError");
  this->initErrorType(TYPE::RegexSyntaxError, "RegexSyntaxError");
  this->initErrorType(TYPE::GlobbingError, "GlobbingError");
  this->initErrorType(TYPE::UnwrappingError, "UnwrappingError");
  this->initErrorType(TYPE::IllegalAccessError, "IllegalAccessError");
  this->initErrorType(TYPE::InvalidOperationError, "InvalidOperationError");

  // init internal status type
  this->initBuiltinType(TYPE::_InternalStatus, "internal status%%", false, TYPE::_Root,
                        info_Dummy());
  this->initBuiltinType(TYPE::_ShellExit, "Shell Exit", false, TYPE::_InternalStatus, info_Dummy());
  this->initBuiltinType(TYPE::_AssertFail, "Assertion Error", false, TYPE::_InternalStatus,
                        info_Dummy());
}

TypePool::~TypePool() {
  for (auto &e : this->typeTable) {
    e->destroy();
  }

  for (auto &e : this->methodMap) {
    const_cast<Key &>(e.first).dispose();
  }
}

DSType *TypePool::addType(DSType *type) {
  assert(type != nullptr);
  auto pair = this->nameMap.emplace(type->getNameRef(), type->typeId());
  if (pair.second) {
    this->typeTable.push_back(type);
    if (this->typeTable.size() == MAX_TYPE_NUM) {
      fatal("type id reaches limit(%u)\n", MAX_TYPE_NUM);
    }
    return type;
  }
  return nullptr;
}

TypeOrError TypePool::getType(StringRef typeName) const {
  auto *type = this->get(typeName);
  if (type == nullptr) {
    RAISE_TL_ERROR(UndefinedType, typeName.data());
  }
  return Ok(type);
}

void TypePool::discard(const TypeDiscardPoint point) {
  for (unsigned int i = point.typeIdOffset; i < this->typeTable.size(); i++) {
    this->typeTable[i]->destroy();
  }
  this->typeTable.erase(this->typeTable.begin() + point.typeIdOffset, this->typeTable.end());

  for (auto iter = this->nameMap.begin(); iter != this->nameMap.end();) {
    if (iter->second >= point.typeIdOffset) {
      iter = this->nameMap.erase(iter);
    } else {
      ++iter;
    }
  }

  assert(point.typeIdOffset == this->typeTable.size());

  // abort method handle
  this->methodIdCount = point.methodIdOffset;
  for (auto iter = this->methodMap.begin(); iter != this->methodMap.end();) {
    if (iter->first.id >= point.typeIdOffset) { // discard all method of discarded type
      const_cast<Key &>(iter->first).dispose();
      iter = this->methodMap.erase(iter);
      continue;
    } else if (iter->second && iter->second.commitId() >= point.methodIdOffset) {
      // only discard instantiated method handle
      auto *ptr = iter->second.handle();
      assert(ptr);
      iter->second = Value(ptr->getMethodIndex());
      assert(!iter->second);
    }
    ++iter;
  }
}

TypeTempOrError TypePool::getTypeTemplate(const std::string &typeName) const {
  auto iter = this->templateMap.find(typeName);
  if (iter == this->templateMap.end()) {
    RAISE_TL_ERROR(NotTemplate, typeName.c_str());
  }
  return Ok(iter->second);
}

TypeOrError TypePool::createReifiedType(const TypeTemplate &typeTemplate,
                                        std::vector<const DSType *> &&elementTypes) {
  if (this->tupleTemplate.getName() == typeTemplate.getName()) {
    return this->createTupleType(std::move(elementTypes));
  }
  if (this->optionTemplate.getName() == typeTemplate.getName()) {
    return this->createOptionType(std::move(elementTypes));
  }

  // check each element type
  if (auto checked = checkElementTypes(typeTemplate, elementTypes); !checked) {
    return checked;
  }

  std::string typeName(this->toReifiedTypeName(typeTemplate, elementTypes));
  auto *type = this->get(typeName);
  if (type == nullptr) {
    if (this->arrayTemplate.getName() == typeTemplate.getName()) {
      auto *arrayType = this->newType<ArrayType>(typeName, typeTemplate.getInfo(),
                                                 this->get(TYPE::Any), *elementTypes[0]);
      assert(arrayType);
      this->registerHandles(*arrayType);
      type = arrayType;
    } else if (this->mapTemplate.getName() == typeTemplate.getName()) {
      auto *mapType = this->newType<MapType>(typeName, typeTemplate.getInfo(), this->get(TYPE::Any),
                                             *elementTypes[0], *elementTypes[1]);
      assert(mapType);
      this->registerHandles(*mapType);
      type = mapType;
    }
  }
  return Ok(type);
}

TypeOrError TypePool::createOptionType(std::vector<const DSType *> &&elementTypes) {
  if (elementTypes.size() != 1) {
    unsigned int size = elementTypes.size();
    RAISE_TL_ERROR(UnmatchElement, this->optionTemplate.getName().c_str(), 1, size);
  }
  auto *elementType = elementTypes[0];
  if (elementType->isVoidType() || elementType->isNothingType()) {
    RAISE_TL_ERROR(InvalidElement, elementType->getName());
  } else if (elementType->isOptionType()) {
    return Ok(elementType);
  }

  auto typeName = this->toReifiedTypeName(this->optionTemplate, elementTypes);
  auto *type = this->get(typeName);
  if (type == nullptr) {
    type = this->newType<OptionType>(typeName, *elementType);
    assert(type);
  }
  return Ok(type);
}

TypeOrError TypePool::createTupleType(std::vector<const DSType *> &&elementTypes) {
  auto checked = checkElementTypes(elementTypes, SYS_LIMIT_TUPLE_NUM);
  if (!checked) {
    return checked;
  }

  assert(!elementTypes.empty());

  std::string typeName(toTupleTypeName(elementTypes));
  auto *type = this->get(typeName);
  if (type == nullptr) {
    auto &superType = this->get(TYPE::Any);
    auto *tuple = this->newType<TupleType>(typeName, this->tupleTemplate.getInfo(), superType,
                                           std::move(elementTypes));
    assert(tuple);
    this->registerHandles(*tuple);
    type = tuple;
  }
  return Ok(type);
}

TypeOrError TypePool::createFuncType(const DSType &returnType,
                                     std::vector<const DSType *> &&paramTypes) {
  auto checked = checkElementTypes(paramTypes, SYS_LIMIT_FUNC_PARAM_NUM);
  if (!checked) {
    return checked;
  }

  std::string typeName(toFunctionTypeName(returnType, paramTypes));
  auto *type = this->get(typeName);
  if (type == nullptr) {
    type = this->newType<FunctionType>(typeName, this->get(TYPE::Func), returnType,
                                       std::move(paramTypes));
    assert(type);
  }
  assert(type->isFuncType());
  return Ok(type);
}

TypeOrError TypePool::createErrorType(const std::string &typeName, const DSType &superType,
                                      unsigned short belongedModId) {
  if (!this->get(TYPE::Error).isSameOrBaseTypeOf(superType)) {
    RAISE_TL_ERROR(InvalidElement, superType.getName());
  }
  std::string name = toModTypeName(belongedModId);
  name += ".";
  name += typeName;
  auto *type = this->newType<ErrorType>(name, superType);
  if (type) {
    return Ok(type);
  } else {
    RAISE_TL_ERROR(DefinedType, typeName.c_str());
  }
}

const ModType &TypePool::createModType(unsigned short modID,
                                       std::unordered_map<std::string, FieldHandle> &&handles,
                                       FlexBuffer<ModType::Imported> &&children,
                                       unsigned int index) {
  auto name = toModTypeName(modID);
  auto *type = this->get(name);
  if (type == nullptr) {
    type = this->newType<ModType>(this->get(TYPE::Module), modID, std::move(handles),
                                  std::move(children), index);
    assert(type);
  } else { // re-open (only allow root module)
    assert(type->isModType());
    auto &modType = const_cast<ModType &>(cast<ModType>(*type)); // FIXME: replace const_cast?
    assert(modType.isRoot());
    modType.reopen(std::move(handles), std::move(children));
  }
  return cast<ModType>(*type);
}

TypeOrError TypePool::getModTypeById(unsigned short modId) const {
  auto name = toModTypeName(modId);
  return this->getType(name);
}

class TypeDecoder {
private:
  TypePool &pool;
  const HandleInfo *cursor;
  const std::vector<const DSType *> types;

public:
  TypeDecoder(TypePool &pool, const HandleInfo *pos, std::vector<const DSType *> &&types)
      : pool(pool), cursor(pos), types(std::move(types)) {}

  ~TypeDecoder() = default;

  TypeOrError decode();

  unsigned int decodeNum() {
    return static_cast<unsigned int>(static_cast<int>(*(this->cursor++)) -
                                     static_cast<int>(HandleInfo::P_N0));
  }
};

#undef TRY
#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto value = E;                                                                                \
    if (!value) {                                                                                  \
      return value;                                                                                \
    }                                                                                              \
    std::move(value).take();                                                                       \
  })

TypeOrError TypeDecoder::decode() {
  switch (*(this->cursor++)) {
#define GEN_CASE(ENUM)                                                                             \
  case HandleInfo::ENUM:                                                                           \
    return Ok(&this->pool.get(TYPE::ENUM));
    EACH_HANDLE_INFO_TYPE(GEN_CASE)
#undef GEN_CASE
  case HandleInfo::Array: {
    auto &t = this->pool.getArrayTemplate();
    unsigned int size = this->decodeNum();
    assert(size == 1);
    std::vector<const DSType *> elementTypes(size);
    elementTypes[0] = TRY(decode());
    return this->pool.createReifiedType(t, std::move(elementTypes));
  }
  case HandleInfo::Map: {
    auto &t = this->pool.getMapTemplate();
    unsigned int size = this->decodeNum();
    assert(size == 2);
    std::vector<const DSType *> elementTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      elementTypes[i] = TRY(this->decode());
    }
    return this->pool.createReifiedType(t, std::move(elementTypes));
  }
  case HandleInfo::Tuple: {
    unsigned int size = this->decodeNum();
    if (size == 0) { // variable length type
      size = this->types.size();
      std::vector<const DSType *> elementTypes(size);
      for (unsigned int i = 0; i < size; i++) {
        elementTypes[i] = this->types[i];
      }
      return this->pool.createTupleType(std::move(elementTypes));
    }

    std::vector<const DSType *> elementTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      elementTypes[i] = TRY(this->decode());
    }
    return this->pool.createTupleType(std::move(elementTypes));
  }
  case HandleInfo::Option: {
    auto &t = this->pool.getOptionTemplate();
    unsigned int size = this->decodeNum();
    assert(size == 1);
    std::vector<const DSType *> elementTypes(size);
    elementTypes[0] = TRY(this->decode());
    return this->pool.createReifiedType(t, std::move(elementTypes));
  }
  case HandleInfo::Func: {
    auto *retType = TRY(this->decode());
    unsigned int size = this->decodeNum();
    std::vector<const DSType *> paramTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      paramTypes[i] = TRY(this->decode());
    }
    return this->pool.createFuncType(*retType, std::move(paramTypes));
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
    return Ok(this->types[0]);
  case HandleInfo::T1:
    return Ok(this->types[1]);
  default:
    return Ok(static_cast<DSType *>(nullptr)); // normally unreachable due to suppress gcc warning
  }
}

#define TRY2(E)                                                                                    \
  ({                                                                                               \
    auto value = E;                                                                                \
    if (!value) {                                                                                  \
      return false;                                                                                \
    }                                                                                              \
    std::move(value).take();                                                                       \
  })

// FIXME: error reporting
bool TypePool::allocMethodHandle(const DSType &recv, MethodMap::iterator iter) {
  assert(!iter->second);
  unsigned int index = iter->second.index();
  auto types = recv.getTypeParams();
  auto info = nativeFuncInfoTable()[index];
  TypeDecoder decoder(*this, info.handleInfo, std::move(types));

  // check type parameter constraint
  const unsigned int constraintSize = decoder.decodeNum();
  for (unsigned int i = 0; i < constraintSize; i++) {
    auto *typeParam = TRY2(decoder.decode());
    auto *reqType = TRY2(decoder.decode());
    if (!reqType->isSameOrBaseTypeOf(*typeParam)) {
      return false;
    }
  }

  auto *returnType = TRY2(decoder.decode()); // init return type
  const unsigned int paramSize = decoder.decodeNum();
  assert(paramSize > 0);
  auto *recvType = TRY2(decoder.decode());
  assert(*recvType == recv);

  const auto commitId = this->methodIdCount++;
  auto handle = MethodHandle::create(*recvType, index, *returnType, paramSize - 1);
  for (unsigned int i = 1; i < paramSize; i++) { // init param types
    handle->paramTypes[i - 1] = TRY2(decoder.decode());
  }
  iter->second = Value(commitId, handle.release());
  return true;
}

const MethodHandle *TypePool::lookupMethod(const DSType &recvType, const std::string &methodName) {
  for (auto *type = &recvType; type != nullptr; type = type->getSuperType()) {
    Key key(*type, methodName);
    auto iter = this->methodMap.find(key);
    if (iter != this->methodMap.end()) {
      if (!iter->second) {
        assert(methodName == nativeFuncInfoTable()[iter->second.index()].funcName);
        if (!this->allocMethodHandle(*type, iter)) {
          return nullptr;
        }
      }
      return iter->second.handle();
    }
  }
  return nullptr;
}

std::string TypePool::toReifiedTypeName(const ydsh::TypeTemplate &typeTemplate,
                                        const std::vector<const DSType *> &elementTypes) const {
  if (typeTemplate == this->getArrayTemplate()) {
    std::string str = "[";
    str += elementTypes[0]->getNameRef();
    str += "]";
    return str;
  } else if (typeTemplate == this->getMapTemplate()) {
    std::string str = "[";
    str += elementTypes[0]->getNameRef();
    str += " : ";
    str += elementTypes[1]->getNameRef();
    str += "]";
    return str;
  } else if (typeTemplate == this->getOptionTemplate()) {
    auto *type = elementTypes[0];
    std::string str;
    if (type->isFuncType()) {
      str += "(";
    }
    str += type->getNameRef();
    if (type->isFuncType()) {
      str += ")";
    }
    str += "!";
    return str;
  } else {
    unsigned int elementSize = elementTypes.size();
    std::string str = typeTemplate.getName();
    str += "<";
    for (unsigned int i = 0; i < elementSize; i++) {
      if (i > 0) {
        str += ",";
      }
      str += elementTypes[i]->getNameRef();
    }
    str += ">";
    return str;
  }
}

std::string TypePool::toTupleTypeName(const std::vector<const DSType *> &elementTypes) {
  std::string str = "(";
  for (unsigned int i = 0; i < elementTypes.size(); i++) {
    if (i > 0) {
      str += ", ";
    }
    str += elementTypes[i]->getNameRef();
  }
  if (elementTypes.size() == 1) {
    str += ",";
  }
  str += ")";
  return str;
}

std::string TypePool::toFunctionTypeName(const DSType &returnType,
                                         const std::vector<const DSType *> &paramTypes) {
  std::string funcTypeName = "(";
  for (unsigned int i = 0; i < paramTypes.size(); i++) {
    if (i > 0) {
      funcTypeName += ", ";
    }
    funcTypeName += paramTypes[i]->getNameRef();
  }
  funcTypeName += ") -> ";
  funcTypeName += returnType.getNameRef();
  return funcTypeName;
}

TypeOrError TypePool::checkElementTypes(const std::vector<const DSType *> &elementTypes,
                                        size_t limit) {
  if (elementTypes.size() > limit) {
    RAISE_TL_ERROR(ElementLimit);
  }
  for (auto &type : elementTypes) {
    if (type->isVoidType() || type->isNothingType()) {
      RAISE_TL_ERROR(InvalidElement, type->getName());
    }
  }
  return Ok(static_cast<DSType *>(nullptr));
}

TypeOrError TypePool::checkElementTypes(const TypeTemplate &t,
                                        const std::vector<const DSType *> &elementTypes) {
  const unsigned int size = elementTypes.size();

  // check element type size
  if (t.getElementTypeSize() != size) {
    RAISE_TL_ERROR(UnmatchElement, t.getName().c_str(), t.getElementTypeSize(), size);
  }

  for (unsigned int i = 0; i < size; i++) {
    auto *acceptType = t.getAcceptableTypes()[i];
    auto *elementType = elementTypes[i];
    if (acceptType->isSameOrBaseTypeOf(*elementType) && !elementType->isNothingType()) {
      continue;
    }
    if (acceptType->is(TYPE::Any) && elementType->isOptionType()) {
      continue;
    }
    RAISE_TL_ERROR(InvalidElement, elementType->getName());
  }
  return Ok(static_cast<DSType *>(nullptr));
}

void TypePool::initBuiltinType(ydsh::TYPE t, const char *typeName, bool, const ydsh::DSType *super,
                               ydsh::native_type_info_t info) {
  // create and register type
  auto *type = this->newType<BuiltinType>(typeName, super, info);
  assert(type);
  this->registerHandles(*type);
  (void)t;
  assert(type->is(t));
}

void TypePool::initTypeTemplate(TypeTemplate &temp, const char *typeName,
                                std::vector<const DSType *> &&elementTypes,
                                native_type_info_t info) {
  temp = TypeTemplate(std::string(typeName), std::move(elementTypes), info);
  this->templateMap.emplace(typeName, &temp);
}

void TypePool::initErrorType(TYPE t, const char *typeName) {
  auto *type = this->newType<ErrorType>(typeName, this->get(TYPE::Error));
  assert(type);
  (void)type;
  (void)t;
  assert(type->is(t));
}

void TypePool::registerHandle(const BuiltinType &recv, const char *name, unsigned int index) {
  auto ret = this->methodMap.emplace(Key(recv, strdup(name)), Value(index));
  (void)ret;
  assert(ret.second);
}

void TypePool::registerHandles(const BuiltinType &type) {
  // init method handle
  auto info = type.getNativeTypeInfo();
  for (unsigned int i = 0; i < info.methodSize; i++) {
    const NativeFuncInfo *funcInfo = &info.getMethodInfo(i);
    unsigned int methodIndex = info.getActualMethodIndex(i);
    this->registerHandle(type, funcInfo->funcName, methodIndex);
  }
}

} // namespace ydsh