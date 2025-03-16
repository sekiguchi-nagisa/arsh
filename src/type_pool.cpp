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

namespace arsh {

// ######################
// ##     TypePool     ##
// ######################

TypePool::TypePool() {
  // initialize type

  /**
   * for type error
   */
  this->initBuiltinType(TYPE::Unresolved_, "<unresolved>", info_Dummy());

  /**
   * pseudo base type
   */
  this->initBuiltinType(TYPE::ProcGuard_, "process guard%%", info_Dummy());

  this->initBuiltinType(TYPE::Any, "Any", TYPE::ProcGuard_, info_AnyType());
  this->initBuiltinType(TYPE::Void, "Void", info_Dummy());
  this->initBuiltinType(TYPE::Nothing, "Nothing", info_Dummy());

  /**
   * hidden from script.
   */
  this->initBuiltinType(TYPE::Value_, "Value%%", TYPE::Any, info_Dummy());

  this->initBuiltinType(TYPE::Int, "Int", TYPE::Value_, info_IntType());
  this->initBuiltinType(TYPE::Float, "Float", TYPE::Value_, info_FloatType());
  this->initBuiltinType(TYPE::Bool, "Bool", TYPE::Value_, info_BoolType());
  this->initBuiltinType(TYPE::String, "String", TYPE::Value_, info_StringType());

  this->initBuiltinType(TYPE::Regex, "Regex", TYPE::Any, info_RegexType());
  this->initBuiltinType(TYPE::RegexMatch, "RegexMatch", TYPE::Any, info_RegexMatchType());
  this->initBuiltinType(TYPE::Signal, "Signal", TYPE::Value_, info_SignalType());
  this->initBuiltinType(TYPE::Signals, "Signals", TYPE::Any, info_SignalsType());
  this->initBuiltinType(TYPE::Throwable, "Throwable", TYPE::Any, info_ThrowableType());
  this->initBuiltinType(TYPE::Error, "Error", TYPE::Throwable, info_ErrorType());
  this->initBuiltinType(TYPE::Job, "Job", TYPE::Any, info_JobType());
  this->initBuiltinType(TYPE::Jobs, "Jobs", TYPE::Any, info_JobsType());
  this->initBuiltinType(TYPE::Module, "Module", TYPE::Any, info_ModuleType());
  this->initBuiltinType(TYPE::StringIter, "StringIter%%", TYPE::Any, info_StringIterType());
  this->initBuiltinType(TYPE::FD, "FD", TYPE::Any, info_FDType());
  this->initBuiltinType(TYPE::ProcSubst, "ProcSubst", TYPE::FD, info_ProcSubstType());
  this->initBuiltinType(TYPE::Reader, "Reader%%", TYPE::Any, info_ReaderType());
  this->initBuiltinType(TYPE::Command, "Command", TYPE::Any, info_CommandType());
  this->initBuiltinType(TYPE::LineEditor, "LineEditor", TYPE::Any, info_LineEditorType());
  this->initBuiltinType(TYPE::CLI, "CLI", TYPE::Any, info_CLIType());
  this->initBuiltinType(TYPE::Candidates, "Candidates", TYPE::Any, info_CandidatesType());

  // initialize type template
  this->initTypeTemplate(this->arrayTemplate, TypeTemplate::Kind::Array, {&this->get(TYPE::Any)},
                         info_ArrayType());
  this->initTypeTemplate(this->mapTemplate, TypeTemplate::Kind::Map,
                         {&this->get(TYPE::Value_), &this->get(TYPE::Any)}, info_MapType());

  this->initTypeTemplate(this->tupleTemplate, TypeTemplate::Kind::Tuple, {}, info_TupleType());

  this->initTypeTemplate(this->optionTemplate, TypeTemplate::Kind::Option, {}, info_OptionType());

  this->initTypeTemplate(this->funcTemplate, TypeTemplate::Kind::Func, {}, info_FuncType());

  // init string array type(for command argument)
  {
    auto checked = this->createArrayType(this->get(TYPE::String)); // TYPE::StringArray
    (void)checked;
    assert(checked);
  }

  // init optional nothing type (for dummy invalid value)
  {
    auto checked = this->createOptionType(this->get(TYPE::Nothing));
    (void)checked;
    assert(checked);
  }

  // init some error type
  this->initDerivedErrorType(TYPE::ArithmeticError, "ArithmeticError");
  this->initDerivedErrorType(TYPE::OutOfRangeError, "OutOfRangeError");
  this->initDerivedErrorType(TYPE::KeyNotFoundError, "KeyNotFoundError");
  this->initDerivedErrorType(TYPE::TypeCastError, "TypeCastError");
  this->initDerivedErrorType(TYPE::SystemError, "SystemError");
  this->initDerivedErrorType(TYPE::StackOverflowError, "StackOverflowError");
  this->initDerivedErrorType(TYPE::RegexSyntaxError, "RegexSyntaxError");
  this->initDerivedErrorType(TYPE::RegexMatchError, "RegexMatchError");
  this->initDerivedErrorType(TYPE::TildeError, "TildeError");
  this->initDerivedErrorType(TYPE::GlobError, "GlobError");
  this->initDerivedErrorType(TYPE::UnwrapError, "UnwrapError");
  this->initDerivedErrorType(TYPE::IllegalAccessError, "IllegalAccessError");
  this->initDerivedErrorType(TYPE::InvalidOperationError, "InvalidOperationError");
  this->initDerivedErrorType(TYPE::ExecError, "ExecError");
  this->initDerivedErrorType(TYPE::CLIError, "CLIError");
  this->initDerivedErrorType(TYPE::ArgumentError, "ArgumentError");

  this->initBuiltinType(TYPE::ShellExit_, "ShellExit", TYPE::Throwable, info_Dummy());
  this->initBuiltinType(TYPE::AssertFail_, "AssertionFailed", TYPE::Throwable, info_Dummy());
}

TypePool::~TypePool() {
  for (auto &e : this->typeTable) {
    e->destroy();
  }
}

Type *TypePool::addType(Type *type) {
  assert(type != nullptr);
  auto pair = this->nameMap.emplace(type->getNameRef(), type->typeId());
  if (pair.second) {
    this->typeTable.push_back(type);
    if (this->typeTable.size() == MAX_TYPE_NUM) {
      fatal("type id reaches limit(%u)\n", MAX_TYPE_NUM);
    }
    return type;
  }
  type->destroy();
  return nullptr;
}

const Type *TypePool::getType(StringRef typeName) const { return this->get(typeName); }

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
      iter = this->methodMap.erase(iter);
      continue;
    } else if (iter->second && iter->second.commitId() >= point.methodIdOffset) {
      // only discard instantiated method handle
      auto *ptr = iter->second.handle();
      assert(ptr);
      iter->second = Value(ptr->getIndex());
      assert(!iter->second);
    }
    ++iter;
  }
}

const TypeTemplate *TypePool::getTypeTemplate(StringRef typeName) const {
  auto iter = this->templateMap.find(typeName);
  if (iter == this->templateMap.end()) {
    return nullptr;
  }
  return iter->second;
}

TypeOrError TypePool::createReifiedType(const TypeTemplate &typeTemplate,
                                        std::vector<const Type *> &&elementTypes) {
  if (this->tupleTemplate == typeTemplate) {
    return this->createTupleType(std::move(elementTypes));
  }
  if (this->optionTemplate == typeTemplate) {
    return this->createOptionType(std::move(elementTypes));
  }

  // check each element type
  if (auto checked = checkElementTypes(typeTemplate, elementTypes); !checked) {
    return checked;
  }

  std::string typeName(this->toReifiedTypeName(typeTemplate, elementTypes));
  auto *type = this->get(typeName);
  if (type == nullptr) {
    if (this->arrayTemplate == typeTemplate) {
      auto *arrayType = this->newType<ArrayType>(typeName, typeTemplate.getInfo(),
                                                 this->get(TYPE::Any), *elementTypes[0]);
      assert(arrayType);
      this->registerHandles(*arrayType);
      type = arrayType;
    } else if (this->mapTemplate == typeTemplate) {
      auto *mapType = this->newType<MapType>(typeName, typeTemplate.getInfo(), this->get(TYPE::Any),
                                             *elementTypes[0], *elementTypes[1]);
      assert(mapType);
      this->registerHandles(*mapType);
      type = mapType;
    }
  }
  return Ok(type);
}

static std::unique_ptr<TypeLookupError>
createInvalidElementError(const TypeTemplate &t, unsigned int index, const char *name) {
  std::unique_ptr<TypeLookupError> error;
  switch (t.getKind()) {
  case TypeTemplate::Kind::Array:
    error = createTLError<InvalidArrayElement>(name);
    break;
  case TypeTemplate::Kind::Map:
    if (index == 0) {
      error = createTLError<InvalidMapKey>(name);
    } else {
      error = createTLError<InvalidMapValue>(name);
    }
    break;
  case TypeTemplate::Kind::Tuple:
    error = createTLError<InvalidTupleElement>(name);
    break;
  case TypeTemplate::Kind::Option:
    break;
  case TypeTemplate::Kind::Func:
    error = createTLError<InvalidFuncParam>(name);
    break;
  }
  if (!error) {
    error = createTLError<InvalidElement>(name);
  }
  error->setElementIndex(index);
  return error;
}

TypeOrError TypePool::createOptionType(std::vector<const Type *> &&elementTypes) {
  if (elementTypes.size() != 1) {
    unsigned int size = elementTypes.size();
    RAISE_TL_ERROR(UnmatchElement, this->optionTemplate.getName(), 1, size);
  }
  auto *elementType = elementTypes[0];
  if (elementType->isVoidType() || elementType->isUnresolved()) {
    return Err(createInvalidElementError(this->optionTemplate, 0, elementType->getName()));
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

TypeOrError TypePool::createTupleType(std::vector<const Type *> &&elementTypes) {
  auto checked = checkElementTypes(this->tupleTemplate, elementTypes, SYS_LIMIT_TUPLE_NUM);
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

TypeOrError TypePool::createFuncType(const Type &returnType,
                                     std::vector<const Type *> &&paramTypes) {
  auto checked = checkElementTypes(this->funcTemplate, paramTypes, SYS_LIMIT_FUNC_PARAM_NUM);
  if (!checked) {
    return checked;
  }

  std::string typeName(toFunctionTypeName(returnType, paramTypes));
  auto *type = this->get(typeName);
  if (type == nullptr) {
    type = this->newType<FunctionType>(typeName, this->get(TYPE::Any), returnType,
                                       std::move(paramTypes));
    assert(type);
  }
  assert(type->isFuncType());
  return Ok(type);
}

TypeOrError TypePool::createErrorType(const std::string &typeName, const Type &superType,
                                      ModId belongedModId) {
  if (!this->get(TYPE::Error).isSameOrBaseTypeOf(superType)) {
    RAISE_TL_ERROR(InvalidElement, superType.getName());
  }
  std::string name = toQualifiedTypeName(typeName, belongedModId);
  auto *type = this->newType<DerivedErrorType>(name, superType);
  if (type) {
    return Ok(type);
  } else {
    RAISE_TL_ERROR(DefinedType, typeName.c_str());
  }
}

TypeOrError TypePool::createRecordType(const std::string &typeName, ModId belongedModId) {
  std::string name = toQualifiedTypeName(typeName, belongedModId);
  auto *type = this->newType<RecordType>(name, this->get(TYPE::Any));
  if (type) {
    return Ok(type);
  } else {
    RAISE_TL_ERROR(DefinedType, typeName.c_str());
  }
}

TypeOrError TypePool::createCLIRecordType(const std::string &typeName, ModId belongedModId,
                                          CLIRecordType::Attr attr, std::string &&desc) {
  std::string name = toQualifiedTypeName(typeName, belongedModId);
  auto *type = this->newType<CLIRecordType>(name, this->get(TYPE::CLI), attr, std::move(desc));
  if (type) {
    return Ok(type);
  } else {
    RAISE_TL_ERROR(DefinedType, typeName.c_str());
  }
}

TypeOrError TypePool::finalizeRecordType(const RecordType &recordType,
                                         std::unordered_map<std::string, HandlePtr> &&handles) {
  unsigned int fieldCount = 0;
  for (auto &e : handles) {
    if (!e.second->isMethodHandle() && !e.second->is(HandleKind::TYPE_ALIAS)) {
      fieldCount++;
      auto &type = this->get(e.second->getTypeId());
      if (type.isVoidType() || type.isNothingType() || type.isUnresolved()) {
        RAISE_TL_ERROR(InvalidElement, type.getName());
      }
    }
  }
  if (fieldCount > SYS_LIMIT_TUPLE_NUM) {
    RAISE_TL_ERROR(ElementLimit);
  }
  auto *newRecordType = cast<RecordType>(this->getMut(recordType.typeId()));
  assert(!newRecordType->isFinalized());
  newRecordType->finalize(fieldCount, std::move(handles));
  return Ok(newRecordType);
}

TypeOrError TypePool::finalizeCLIRecordType(const CLIRecordType &recordType,
                                            std::unordered_map<std::string, HandlePtr> &&handles,
                                            std::vector<ArgEntry> &&entries) {
  auto ret = this->finalizeRecordType(recordType, std::move(handles));
  if (ret) {
    auto *newRecordType = cast<CLIRecordType>(this->getMut(recordType.typeId()));
    assert(newRecordType->isFinalized());
    newRecordType->finalizeArgEntries(std::move(entries));
  }
  return ret;
}

const ModType &TypePool::createModType(ModId modId,
                                       std::unordered_map<std::string, HandlePtr> &&handles,
                                       FlexBuffer<ModType::Imported> &&children, unsigned int index,
                                       ModAttr modAttr) {
  auto name = toModTypeName(modId);
  auto *type = this->get(name);
  if (type == nullptr) {
    type = this->newType<ModType>(this->get(TYPE::Module), modId, std::move(handles),
                                  std::move(children), index, modAttr);
    assert(type);
  } else { // re-open (only allow root module)
    assert(type->isModType());
    auto &modType = cast<ModType>(*this->getMut(type->typeId()));
    assert(isRootMod(modType.getModId()));
    modType.reopen(std::move(handles), std::move(children), modAttr);
  }
  return cast<ModType>(*type);
}

const ModType *TypePool::getModTypeById(ModId modId) const {
  auto name = toModTypeName(modId);
  auto *type = this->getType(name);
  if (type) {
    assert(type->isModType());
    return cast<ModType>(type);
  }
  return nullptr;
}

class TypeDecoder {
private:
  TypePool &pool;
  const HandleInfo *cursor;
  const std::vector<const Type *> types;

public:
  TypeDecoder(TypePool &pool, const HandleInfo *pos, std::vector<const Type *> &&types)
      : pool(pool), cursor(pos), types(std::move(types)) {}

  ~TypeDecoder() = default;

  void reset(const HandleInfo *ptr) { this->cursor = ptr; }

  TypeOrError decode();

  unsigned int decodeNum() {
    return static_cast<unsigned int>(static_cast<int>(*(this->cursor++)) -
                                     static_cast<int>(HandleInfo::P_N0));
  }

  /**
   * decode and check type parameter constraint
   * @return
   */
  bool decodeConstraint();
};

#undef TRY
#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto value = E;                                                                                \
    if (unlikely(!value)) {                                                                        \
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
    std::vector<const Type *> elementTypes(size);
    elementTypes[0] = TRY(decode());
    return this->pool.createReifiedType(t, std::move(elementTypes));
  }
  case HandleInfo::Map: {
    auto &t = this->pool.getMapTemplate();
    unsigned int size = this->decodeNum();
    assert(size == 2);
    std::vector<const Type *> elementTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      elementTypes[i] = TRY(this->decode());
    }
    return this->pool.createReifiedType(t, std::move(elementTypes));
  }
  case HandleInfo::Tuple: {
    unsigned int size = this->decodeNum();
    std::vector<const Type *> elementTypes(size);
    for (unsigned int i = 0; i < size; i++) {
      elementTypes[i] = TRY(this->decode());
    }
    return this->pool.createTupleType(std::move(elementTypes));
  }
  case HandleInfo::Option: {
    auto &t = this->pool.getOptionTemplate();
    unsigned int size = this->decodeNum();
    assert(size == 1);
    std::vector<const Type *> elementTypes(size);
    elementTypes[0] = TRY(this->decode());
    return this->pool.createReifiedType(t, std::move(elementTypes));
  }
  case HandleInfo::Func: {
    auto *retType = TRY(this->decode());
    unsigned int size = this->decodeNum();
    std::vector<const Type *> paramTypes(size);
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
  }
  return Ok(static_cast<Type *>(nullptr)); // normally unreachable due to suppress gcc warning
}

#define TRY2(E)                                                                                    \
  ({                                                                                               \
    auto value = E;                                                                                \
    if (unlikely(!value)) {                                                                        \
      return false;                                                                                \
    }                                                                                              \
    std::move(value).take();                                                                       \
  })

bool TypeDecoder::decodeConstraint() {
  const unsigned int constraintSize = this->decodeNum();
  for (unsigned int i = 0; i < constraintSize; i++) {
    auto *typeParam = TRY2(this->decode());
    auto *reqType = TRY2(this->decode());
    if (!reqType->isSameOrBaseTypeOf(*typeParam)) {
      return false;
    }
  }
  return true;
}

#undef TRY2
#define TRY2(E)                                                                                    \
  ({                                                                                               \
    auto value = E;                                                                                \
    if (unlikely(!value)) {                                                                        \
      return nullptr;                                                                              \
    }                                                                                              \
    std::move(value).take();                                                                       \
  })

std::unique_ptr<MethodHandle> TypePool::allocNativeMethodHandle(const Type &recv,
                                                                unsigned int methodIndex) {
  auto types = recv.getTypeParams(*this);
  auto info = nativeFuncInfoTable()[methodIndex];
  TypeDecoder decoder(*this, info.handleInfo, std::move(types));

  // check type parameter constraint
  bool r = decoder.decodeConstraint();
  (void)r;
  assert(r);

  auto *returnType = TRY2(decoder.decode()); // init return type
  const unsigned int paramSize = decoder.decodeNum();
  assert(paramSize > 0);
  const unsigned int actualParamSize = paramSize - 1;
  auto *recvType = TRY2(decoder.decode());
  assert(*recvType == recv);

  std::array<const Type *, HandleInfoParamNumMax()> paramTypes{};
  assert(actualParamSize < paramTypes.size());
  for (unsigned int i = 0; i < actualParamSize; i++) {
    paramTypes[i] = TRY2(decoder.decode());
  }
  return MethodHandle::native(*recvType, methodIndex, *returnType, actualParamSize, paramTypes);
}

const MethodHandle *TypePool::lookupMethod(const Type &recvType, const std::string &methodName) {
  const bool isInit = methodName == OP_INIT;
  for (auto *type = &recvType; type != nullptr; type = type->getSuperType()) {
    Key key(*type, methodName);
    auto iter = this->methodMap.find(key);
    if (iter != this->methodMap.end()) {
      if (!iter->second) {
        assert(methodName == nativeFuncInfoTable()[iter->second.index()].funcName);
        const unsigned int index = iter->second.index();
        auto handle = this->allocNativeMethodHandle(*type, index);
        if (!handle) {
          return nullptr;
        }
        const auto commitId = this->methodIdCount++;
        iter->second = Value(commitId, handle.release());
      }
      return iter->second.handle();
    }
    if (!type->isRecordOrDerived() && !type->isDerivedErrorType() && isInit) {
      break;
    }
  }
  return nullptr;
}

bool TypePool::hasMethod(const Type &recvType, const std::string &methodName) const {
  for (auto *type = &recvType; type != nullptr; type = type->getSuperType()) {
    Key key(*type, methodName);
    auto iter = this->methodMap.find(key);
    if (iter != this->methodMap.end()) {
      return true;
    }
  }
  return false;
}

std::string TypePool::toReifiedTypeName(const TypeTemplate &typeTemplate,
                                        const std::vector<const Type *> &elementTypes) const {
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
    str += "?";
    return str;
  } else {
    std::string str = typeTemplate.getName();
    str += '<';
    unsigned int c = 0;
    for (auto &e : elementTypes) {
      if (c++ > 0) {
        str += ", ";
      }
      str += e->getNameRef();
    }
    str += '>';
    return str;
  }
}

std::string TypePool::toTupleTypeName(const std::vector<const Type *> &elementTypes) {
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

std::string TypePool::toFunctionTypeName(const Type &returnType,
                                         const std::vector<const Type *> &paramTypes) {
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

TypeOrError TypePool::checkElementTypes(const TypeTemplate &t,
                                        const std::vector<const Type *> &elementTypes,
                                        size_t limit) {
  if (elementTypes.size() > limit) {
    RAISE_TL_ERROR(ElementLimit);
  }
  unsigned int index = 0;
  for (auto &type : elementTypes) {
    if (type->isVoidType() || type->isNothingType() || type->isUnresolved()) {
      return Err(createInvalidElementError(t, index, type->getName()));
    }
    index++;
  }
  return Ok(static_cast<Type *>(nullptr));
}

TypeOrError TypePool::checkElementTypes(const TypeTemplate &t,
                                        const std::vector<const Type *> &elementTypes) {
  assert(t.getElementTypeSize() != 0);
  const unsigned int size = elementTypes.size();

  // check element type size
  if (t.getElementTypeSize() != size) {
    RAISE_TL_ERROR(UnmatchElement, t.getName(), t.getElementTypeSize(), size);
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
    return Err(createInvalidElementError(t, i, elementType->getName()));
  }
  return Ok(static_cast<Type *>(nullptr));
}

void TypePool::initBuiltinType(TYPE t, const char *typeName, const Type *super,
                               native_type_info_t info) {
  // create and register type
  auto *type = this->newType<BuiltinType>(typeName, super, info);
  assert(type);
  this->registerHandles(*type);
  (void)t;
  assert(type->is(t));
}

void TypePool::initTypeTemplate(TypeTemplate &temp, TypeTemplate::Kind kind,
                                std::vector<const Type *> &&elementTypes, native_type_info_t info) {
  temp = TypeTemplate(kind, std::move(elementTypes), info);
  StringRef key = temp.getName();
  this->templateMap.emplace(key, &temp);
}

void TypePool::initDerivedErrorType(TYPE t, const char *typeName) {
  auto *type = this->newType<DerivedErrorType>(typeName, this->get(TYPE::Error));
  assert(type);
  (void)type;
  (void)t;
  assert(type->is(t));
}

void TypePool::registerHandle(const BuiltinType &recv, const char *name, unsigned int index) {
  auto ret = this->methodMap.emplace(Key(recv, name), Value(index));
  (void)ret;
  assert(ret.second);
}

void TypePool::registerHandles(const BuiltinType &type) {
  // init method handle
  auto types = type.getTypeParams(*this);
  TypeDecoder decoder(*this, nullptr, std::move(types));
  auto info = type.getNativeTypeInfo();
  for (unsigned int i = 0; i < info.methodSize; i++) {
    const NativeFuncInfo *funcInfo = &info.getMethodInfo(i);
    decoder.reset(funcInfo->handleInfo);
    if (!decoder.decodeConstraint()) {
      continue;
    }
    unsigned int methodIndex = info.getActualMethodIndex(i);
    this->registerHandle(type, funcInfo->funcName, methodIndex);
  }
}

} // namespace arsh