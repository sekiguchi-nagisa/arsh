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

#include <cstdarg>

#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <DescLexer.h>
#include <constant.h>
#include <handle_info.h>
#include <misc/opt_parser.hpp>
#include <misc/parser_base.hpp>

namespace {

using namespace arsh;

HandleInfo fromNum(unsigned int num) {
  // check range
  if (num < 9) {
    switch (const auto info = static_cast<HandleInfo>(num + static_cast<int>(HandleInfo::P_N0))) {
#define GEN_CASE(ENUM) case HandleInfo::ENUM:
      EACH_HANDLE_INFO_NUM(GEN_CASE)
#undef GEN_CASE
      return info;
    default:
      break;
    }
  }
  fatal("out of range, must be 0~8\n");
}

const char *toTypeInfoName(HandleInfo info) {
  switch (info) {
#define GEN_NAME(INFO)                                                                             \
  case HandleInfo::INFO:                                                                           \
    return #INFO;
    EACH_HANDLE_INFO(GEN_NAME)
#undef GEN_NAME
  }
  return nullptr; //   normally unreachable
}

class ErrorReporter {
private:
  std::string fileName;

  unsigned int lineNum{0};

  /**
   * currently processing line
   */
  std::string curLine;

  ErrorReporter() = default;

public:
  ~ErrorReporter() = default;

  static ErrorReporter &instance() {
    static ErrorReporter e;
    return e;
  }

  void appendLine(const char *name, unsigned int ln, const std::string &line) {
    if (this->fileName.empty()) {
      this->fileName = name;
    }
    this->lineNum = ln;
    this->curLine = line;
  }

  [[noreturn]] void operator()(const char *fmt, ...) const __attribute__((format(printf, 2, 3))) {
    const unsigned int size = 512;
    char buf[size]; // error message must be under-size.

    // format message
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);

    std::cerr << this->fileName << ":" << this->lineNum << ": [error] " << buf << '\n';
    std::cerr << this->curLine << '\n' << std::flush;
    abort();
  }
};

class HandleInfoMap {
private:
  std::unordered_map<std::string, HandleInfo> name2InfoMap;
  std::vector<std::pair<HandleInfo, std::string>> info2NameMap;

  HandleInfoMap();

public:
  ~HandleInfoMap() = default;

  static HandleInfoMap &getInstance();

  const char *getName(HandleInfo info) const;

  HandleInfo getInfo(const std::string &name);

private:
  void registerName(HandleInfo info, const char *name);
};

HandleInfoMap::HandleInfoMap() {
  constexpr std::pair<HandleInfo, const char *> table[] = {
#define GEN_TABLE(E) {HandleInfo::E, #E},
      EACH_HANDLE_INFO_TYPE(GEN_TABLE) EACH_HANDLE_INFO_PTYPE(GEN_TABLE)
          EACH_HANDLE_INFO_TYPE_TEMP(GEN_TABLE) EACH_HANDLE_INFO_FUNC_TYPE(GEN_TABLE)
#undef GEN_TABLE
  };
  for (auto [e, s] : table) {
    this->registerName(e, s);
  }
}

HandleInfoMap &HandleInfoMap::getInstance() {
  static HandleInfoMap map;
  return map;
}

const char *HandleInfoMap::getName(HandleInfo info) const {
  for (auto &pair : this->info2NameMap) {
    if (pair.first == info) {
      return pair.second.c_str();
    }
  }
  fatal("not found handle info: %s\n", toTypeInfoName(info));
}

HandleInfo HandleInfoMap::getInfo(const std::string &name) {
  auto iter = this->name2InfoMap.find(name);
  if (iter == this->name2InfoMap.end()) {
    ErrorReporter::instance()("not found type name: %s", name.c_str());
  }
  return iter->second;
}

void HandleInfoMap::registerName(HandleInfo info, const char *name) {
  std::string actualName(name);
  this->info2NameMap.emplace_back(info, actualName);
  this->name2InfoMap.emplace(actualName, info);
}

class HandleInfoSerializer {
private:
  std::vector<HandleInfo> infos;

public:
  HandleInfoSerializer() = default;

  ~HandleInfoSerializer() = default;

  void add(HandleInfo info) { this->infos.push_back(info); }

  std::string toString() {
    std::string str("{");
    unsigned int size = this->infos.size();
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        str += ", ";
      }
      str += "HandleInfo::";
      str += toTypeInfoName(this->infos[i]);
    }
    str += "}";

    if (!verifyHandleInfo(this->infos)) {
      fatal("broken handle info: %s\n", str.c_str());
    }
    return str;
  }

private:
  static int getNum(const std::vector<HandleInfo> &infos, unsigned int &index);
  static bool isType(const std::vector<HandleInfo> &infos, unsigned int &index);
  static bool isParamType(const std::vector<HandleInfo> &infos, unsigned int &index);
  static bool verifyHandleInfo(const std::vector<HandleInfo> &infos);
};

bool HandleInfoSerializer::isType(const std::vector<HandleInfo> &infos, unsigned int &index) {
#define GEN_CASE(ENUM) case HandleInfo::ENUM:
  if (index < infos.size()) {
    switch (infos[index++]) {
      EACH_HANDLE_INFO_TYPE(GEN_CASE)
      return true;
    case HandleInfo::Array:
      return getNum(infos, index) == 1 && isType(infos, index);
    case HandleInfo::Map:
      return getNum(infos, index) == 2 && isType(infos, index) && isType(infos, index);
    case HandleInfo::Tuple: {
      int num = getNum(infos, index);
      if (num < 0 || num > 8) {
        return false;
      }
      for (int i = 0; i < num; i++) {
        if (!isType(infos, index)) {
          return false;
        }
      }
      return true;
    }
    case HandleInfo::Option:
      return getNum(infos, index) == 1 && isType(infos, index);
    case HandleInfo::Func: {
      if (!isType(infos, index)) {
        return false;
      }
      int num = getNum(infos, index);
      for (int i = 0; i < num; i++) {
        if (!isType(infos, index)) {
          return false;
        }
      }
      return true;
    }
      EACH_HANDLE_INFO_NUM(GEN_CASE)
      return false;
      EACH_HANDLE_INFO_PTYPE(GEN_CASE)
      return true;
    }
  }
  return false;
#undef GEN_CASE
}

bool HandleInfoSerializer::isParamType(const std::vector<HandleInfo> &infos, unsigned int &index) {
  if (index < infos.size()) {
    switch (infos[index++]) {
#define GEN_CASE(ENUM) case HandleInfo::ENUM:
      EACH_HANDLE_INFO_PTYPE(GEN_CASE)
#undef GEN_CASE
      return true;
    default:
      return false;
    }
  }
  return false;
}

int HandleInfoSerializer::getNum(const std::vector<HandleInfo> &infos, unsigned int &index) {
#define GEN_CASE(ENUM) case HandleInfo::ENUM:
  if (index < infos.size()) {
    auto ch = infos[index++];
    switch (ch) {
      EACH_HANDLE_INFO_TYPE(GEN_CASE)
      EACH_HANDLE_INFO_TYPE_TEMP(GEN_CASE)
      EACH_HANDLE_INFO_FUNC_TYPE(GEN_CASE)
      EACH_HANDLE_INFO_PTYPE(GEN_CASE)
      return -1;
      EACH_HANDLE_INFO_NUM(GEN_CASE)
      return static_cast<int>(ch) - static_cast<int>(HandleInfo::P_N0);
    }
  }
  return -1;
#undef GEN_ENUM
}

bool HandleInfoSerializer::verifyHandleInfo(const std::vector<HandleInfo> &infos) {
  unsigned int index = 0;

  /**
   * check type constraints
   */
  int constraintSize = getNum(infos, index);
  if (constraintSize < 0 || constraintSize > 8) {
    return false;
  }
  for (int i = 0; i < constraintSize; i++) {
    if (!isParamType(infos, index)) {
      return false;
    }
    if (!isType(infos, index)) {
      return false;
    }
  }

  /**
   * check return type
   */
  if (!isType(infos, index)) {
    return false;
  }

  /**
   * check param size
   */
  int paramSize = getNum(infos, index);
  if (paramSize < 0 || paramSize > 8) {
    return false;
  }

  /**
   * check each param type
   */
  for (int i = 0; i < paramSize; i++) {
    if (!isType(infos, index)) {
      return false;
    }
  }
  return index == infos.size();
}

struct TypeToken {
  virtual ~TypeToken() = default;

  virtual void serialize(HandleInfoSerializer &s) = 0;

  virtual bool isType(HandleInfo info) = 0;

  std::string toString() {
    std::string str;
    this->toString(str);
    return str;
  }

  virtual void toString(std::string &str) = 0;
};

class CommonTypeToken : public TypeToken {
private:
  HandleInfo info;

public:
  /**
   * not call it directory.
   */
  explicit CommonTypeToken(HandleInfo info) : info(info) {}

  ~CommonTypeToken() override = default;

  void serialize(HandleInfoSerializer &s) override { s.add(this->info); }

  bool isType(HandleInfo i) override { return this->info == i; }

  static std::unique_ptr<CommonTypeToken> newTypeToken(const std::string &name);

  void toString(std::string &str) override {
    str += HandleInfoMap::getInstance().getName(this->info);
  }
};

std::unique_ptr<CommonTypeToken> CommonTypeToken::newTypeToken(const std::string &name) {
  return std::make_unique<CommonTypeToken>(HandleInfoMap::getInstance().getInfo(name));
}

class ReifiedTypeToken : public TypeToken {
private:
  std::unique_ptr<CommonTypeToken> typeTemp;

  /**
   * element size. must be equivalent to this->elements.size().
   * if requiredSize is 0, allow any elements
   */
  unsigned int requiredSize;

  std::vector<std::unique_ptr<TypeToken>> elements;

  ReifiedTypeToken(std::unique_ptr<CommonTypeToken> &&type, unsigned int elementSize)
      : typeTemp(type.release()), requiredSize(elementSize) {}

public:
  ~ReifiedTypeToken() override = default;

  void addElement(std::unique_ptr<TypeToken> &&type) { this->elements.push_back(std::move(type)); }

  void serialize(HandleInfoSerializer &s) override;

  bool isType(HandleInfo info) override { return this->typeTemp->isType(info); }

  static std::unique_ptr<ReifiedTypeToken> newReifiedTypeToken(const std::string &name);

  void toString(std::string &str) override {
    if (this->typeTemp->isType(HandleInfo::Array)) {
      assert(this->elements.size() == 1);
      str += "[";
      this->elements[0]->toString(str);
      str += "]";
    } else if (this->typeTemp->isType(HandleInfo::Map)) {
      assert(this->elements.size() == 2);
      str += "[";
      this->elements[0]->toString(str);
      str += " : ";
      this->elements[1]->toString(str);
      str += "]";
    } else if (this->typeTemp->isType(HandleInfo::Tuple)) {
      str += "(";
      for (unsigned int i = 0; i < this->elements.size(); i++) {
        if (i > 0) {
          str += ", ";
        }
        this->elements[i]->toString(str);
      }
      str += ")";
    } else if (this->typeTemp->isType(HandleInfo::Option)) {
      assert(this->elements.size() == 1);
      bool isFuncType = this->elements[0]->isType(HandleInfo::Func);
      if (isFuncType) {
        str += "(";
      }
      this->elements[0]->toString(str);
      if (isFuncType) {
        str += ")";
      }
      str += "?";
    } else {
      std::string tmp;
      this->typeTemp->toString(tmp);
      fatal("unsupported reified type: %s\n", tmp.c_str());
    }
  }
};

void ReifiedTypeToken::serialize(HandleInfoSerializer &s) {
  // check element size
  unsigned int elementSize = this->elements.size();
  if (this->requiredSize > 0) {
    if (this->requiredSize != elementSize) {
      ErrorReporter::instance()("require %d, but is %zu", this->requiredSize,
                                this->elements.size());
    }
  }

  typeTemp->serialize(s);
  s.add(fromNum(elementSize));
  for (auto &tok : this->elements) {
    tok->serialize(s);
  }
}

std::unordered_map<std::string, std::pair<unsigned int, HandleInfo>> initTypeMap() {
  std::unordered_map<std::string, std::pair<unsigned int, HandleInfo>> map = {
      {TYPE_ARRAY, {1, HandleInfo::Array}},
      {TYPE_MAP, {2, HandleInfo::Map}},
      {TYPE_TUPLE, {0, HandleInfo::Tuple}},
      {TYPE_OPTION, {1, HandleInfo::Option}},
  };
  return map;
}

std::unique_ptr<ReifiedTypeToken> ReifiedTypeToken::newReifiedTypeToken(const std::string &name) {
  static const auto typeMap = initTypeMap();
  auto iter = typeMap.find(name);
  if (iter == typeMap.end()) {
    ErrorReporter::instance()("unsupported type template: %s", name.c_str());
  }
  auto tok = std::make_unique<CommonTypeToken>(iter->second.second);
  unsigned int size = iter->second.first;
  return std::unique_ptr<ReifiedTypeToken>(new ReifiedTypeToken(std::move(tok), size));
}

class FuncTypeToken : public TypeToken {
private:
  std::unique_ptr<CommonTypeToken> typeTemp;

  std::unique_ptr<TypeToken> returnType;

  std::vector<std::unique_ptr<TypeToken>> paramTypes;

public:
  explicit FuncTypeToken(std::unique_ptr<TypeToken> &&returnType)
      : typeTemp(CommonTypeToken::newTypeToken(TYPE_FUNC)), returnType(std::move(returnType)) {}

  ~FuncTypeToken() override = default;

  void addParamType(std::unique_ptr<TypeToken> &&type) {
    this->paramTypes.push_back(std::move(type));
  }

  void serialize(HandleInfoSerializer &s) override;

  bool isType(HandleInfo info) override { return this->typeTemp->isType(info); }

  void toString(std::string &str) override {
    str += "(";
    for (unsigned int i = 0; i < this->paramTypes.size(); i++) {
      if (i > 0) {
        str += ", ";
      }
      this->paramTypes[i]->toString(str);
    }
    str += ") -> ";
    this->returnType->toString(str);
  }
};

void FuncTypeToken::serialize(HandleInfoSerializer &s) {
  this->typeTemp->serialize(s);
  this->returnType->serialize(s);
  s.add(fromNum(this->paramTypes.size()));
  for (auto &tok : this->paramTypes) {
    tok->serialize(s);
  }
}

/**
 * first: typeParam
 * second: requiredType
 */
using TypeConstraint = std::pair<std::unique_ptr<TypeToken>, std::unique_ptr<TypeToken>>;

class Element {
protected:
  std::string funcName;

  /**
   * if true, this function is operator
   */
  bool op;

  std::vector<TypeConstraint> constraints;

  /**
   * owner type which this element belongs to
   */
  std::unique_ptr<TypeToken> ownerType;

  /**
   * if this element represents for constructor, returnType is always void.
   */
  std::unique_ptr<TypeToken> returnType;

  /**
   * not contains receiver type if this element represents for function
   */
  std::vector<std::unique_ptr<TypeToken>> paramTypes;

  /**
   * contains parameter name
   */
  std::vector<std::string> paramNames;

  /**
   * name of binding function
   */
  std::string actualFuncName;

public:
  Element(std::string &&funcName, bool op) : funcName(std::move(funcName)), op(op) {}

  virtual ~Element() = default;

  void addConstraint(std::unique_ptr<TypeToken> &&typeParam, std::unique_ptr<TypeToken> &&req) {
    this->constraints.emplace_back(std::move(typeParam), std::move(req));
  }

  const std::vector<TypeConstraint> &getConstraints() const { return this->constraints; }

  void addParam(std::string &&name, std::unique_ptr<TypeToken> &&type) {
    if (!this->ownerType) { // treat first param type as receiver
      this->ownerType = std::move(type);
      return;
    }
    this->paramNames.emplace_back(std::move(name));
    this->paramTypes.push_back(std::move(type));
  }

  void setReturnType(std::unique_ptr<TypeToken> &&type) { this->returnType = std::move(type); }

  void setActualFuncName(std::string &&name) { this->actualFuncName = std::move(name); }

  bool isOwnerType(HandleInfo info) const { return this->ownerType->isType(info); }

  std::string toSerializedHandle() const {
    HandleInfoSerializer s;
    s.add(fromNum(this->constraints.size()));
    for (auto &c : this->constraints) {
      c.first->serialize(s);
      c.second->serialize(s);
    }
    this->returnType->serialize(s);
    s.add(fromNum(this->paramTypes.size() + 1));
    this->ownerType->serialize(s);
    for (const std::unique_ptr<TypeToken> &t : this->paramTypes) {
      t->serialize(s);
    }
    return s.toString();
  }

  std::string toParamNames() const {
    std::string value = "\"";
    unsigned int count = 0;
    for (auto &name : this->paramNames) {
      if (count++ > 0) {
        value += ";";
      }
      value += name;
    }
    value += '"';
    return value;
  }

  std::string toFuncName() const {
    if (this->op) {
      return this->funcName;
    }

    std::string str("\"");
    str += this->funcName;
    str += "\"";
    return str;
  }

  const char *getActualFuncName() const { return this->actualFuncName.c_str(); }

  std::string emit() const {
    std::string str("{");
    str += this->toFuncName();
    str += ", ";
    str += this->toParamNames();
    str += ", ";
    str += this->toSerializedHandle();
    str += "}";
    return str;
  }

  std::string toString() const {
    const bool isInit = this->op && this->funcName == "OP_INIT";
    std::string str;
    if (isInit) {
      str += "type ";
      str += this->returnType->toString();
    } else {
      str += "function ";
      if (this->op) {
        str += "%";
      }
      str += this->funcName;
    }

    str += "(";
    for (unsigned int i = 0; i < this->paramNames.size(); i++) {
      if (i > 0) {
        str += ", ";
      }
      str += this->paramNames[i];
      str += ": ";
      str += this->paramTypes[i]->toString();
    }
    if (isInit) {
      str += ")";
    } else {
      str += "): ";
      str += this->returnType->toString();
    }

    if (!this->constraints.empty()) {
      str += " where ";
      for (unsigned int i = 0; i < this->constraints.size(); i++) {
        if (i > 0) {
          str += ", ";
        }
        str += this->constraints[i].first->toString();
        str += " : ";
        str += this->constraints[i].second->toString();
      }
    }
    if (!isInit) {
      str += " for ";
      this->ownerType->toString(str);
    }
    return str;
  }
};

using ParseError = arsh::ParseErrorBase<DescTokenKind>;

#define CUR_KIND() (this->curKind)

#define TRY(expr)                                                                                  \
  ({                                                                                               \
    auto v = expr;                                                                                 \
    if (this->hasError()) {                                                                        \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

class Parser : public arsh::ParserBase<DescTokenKind, DescLexer> {
public:
  Parser() = default;

  ~Parser() = default;

  std::vector<std::unique_ptr<Element>> operator()(const char *fileName);

private:
  static bool isDescriptor(const std::string &line);

  std::string toName(const Token &token) const {
    Token t = token;
    t.pos++;
    t.size--;
    return this->lexer->toTokenText(t);
  }

  void init(DescLexer &lexer) {
    this->lexer = &lexer;
    this->fetchNext();
  }

  std::unique_ptr<Element> parse_descriptor(const std::string &line);

  std::unique_ptr<Element> parse_funcDesc();

  /**
   *
   * @param element
   * @return
   * always null
   */
  std::unique_ptr<Element> parse_params(std::unique_ptr<Element> &element);

  std::unique_ptr<TypeToken> parse_type();

  /**
   *
   * @param line
   * @param element
   * @return
   * always null
   */
  std::unique_ptr<Element> parse_funcDecl(const std::string &line,
                                          std::unique_ptr<Element> &element);
};

std::vector<std::unique_ptr<Element>> Parser::operator()(const char *fileName) {
  std::ifstream input(fileName);
  if (!input) {
    fatal("cannot open file: %s\n", fileName);
  }

  unsigned int lineNum = 0;
  std::string line;
  bool foundDesc = false;

  std::vector<std::unique_ptr<Element>> elements;
  std::unique_ptr<Element> element;
  while (std::getline(input, line)) {
    lineNum++;
    ErrorReporter::instance().appendLine(fileName, lineNum, line);

    if (foundDesc) {
      this->parse_funcDecl(line, element);
      if (this->hasError()) {
        goto ERR;
      }
      elements.push_back(std::move(element));
      foundDesc = false;
      continue;
    }
    if (isDescriptor(line)) {
      foundDesc = true;
      element = this->parse_descriptor(line);
      if (this->hasError()) {
        goto ERR;
      }
    }

  ERR:
    if (this->hasError()) {
      auto &e = this->getError();
      std::cerr << fileName << ":" << lineNum << ": [error] " << e.getMessage() << '\n';
      std::cerr << line << '\n';
      Token lineToken{.pos = 0, .size = static_cast<unsigned int>(line.size())};
      std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << '\n'
                << std::flush;
      exit(EXIT_FAILURE);
    }
  }

  return elements;
}

bool Parser::isDescriptor(const std::string &line) {
  // skip space
  unsigned int pos = 0;
  unsigned int size = line.size();
  for (; pos < size; pos++) {
    char ch = line[pos];
    if (ch != ' ' && ch != '\t') {
      break;
    }
  }

  const char *prefix = "//!bind:";
  for (unsigned int i = 0; prefix[i] != '\0'; i++) {
    if (pos >= size) {
      return false;
    }
    if (line[pos++] != prefix[i]) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<Element> Parser::parse_descriptor(const std::string &line) {
  DescLexer lexer(line.c_str());
  this->init(lexer);

  TRY(this->expect(DescTokenKind::DESC_PREFIX));

  switch (CUR_KIND()) {
  case DescTokenKind::FUNC:
    return this->parse_funcDesc();
  default:
    const DescTokenKind alters[] = {
        DescTokenKind::FUNC,
    };
    this->reportNoViableAlterError(alters);
    return nullptr;
  }
}

std::unique_ptr<Element> Parser::parse_funcDesc() {
  TRY(this->expect(DescTokenKind::FUNC));

  std::unique_ptr<Element> element;

  // parse function name
  switch (CUR_KIND()) {
  case DescTokenKind::IDENTIFIER: {
    Token token = TRY(this->expect(DescTokenKind::IDENTIFIER));
    element = std::make_unique<Element>(this->lexer->toTokenText(token), false);
    break;
  }
  case DescTokenKind::VAR_NAME: {
    Token token = TRY(this->expect(DescTokenKind::VAR_NAME));
    element = std::make_unique<Element>(this->toName(token), true);
    break;
  }
  default: {
    const DescTokenKind alters[] = {
        DescTokenKind::IDENTIFIER,
        DescTokenKind::VAR_NAME,
    };
    this->reportNoViableAlterError(alters);
    return nullptr;
  }
  }

  // parse parameter decl
  TRY(this->expect(DescTokenKind::LP));
  TRY(this->parse_params(element));
  TRY(this->expect(DescTokenKind::RP));

  TRY(this->expect(DescTokenKind::COLON));
  element->setReturnType(TRY(this->parse_type()));

  // parse constraints
  if (CUR_KIND() == DescTokenKind::WHERE) {
    do {
      this->consume();
      auto typeParam = TRY(this->parse_type());
      TRY(this->expect(DescTokenKind::COLON));
      auto reqType = TRY(this->parse_type());
      element->addConstraint(std::move(typeParam), std::move(reqType));
    } while (CUR_KIND() == DescTokenKind::COMMA);
  }
  TRY(this->expect(DescTokenKind::EOS));
  return element;
}

std::unique_ptr<Element> Parser::parse_params(std::unique_ptr<Element> &element) {
  int count = 0;
  do {
    if (count++ > 0) {
      TRY(this->expect(DescTokenKind::COMMA));
    }

    Token token = TRY(this->expect(DescTokenKind::VAR_NAME));
    TRY(this->expect(DescTokenKind::COLON));
    auto type = TRY(this->parse_type());

    element->addParam(this->toName(token), std::move(type));
  } while (CUR_KIND() == DescTokenKind::COMMA);

  return nullptr;
}

bool isFunc(const std::string &str) { return str == TYPE_FUNC; }

std::unique_ptr<TypeToken> Parser::parse_type() {
  Token token = TRY(this->expect(DescTokenKind::IDENTIFIER));
  if (CUR_KIND() != DescTokenKind::TYPE_OPEN) {
    return CommonTypeToken::newTypeToken(this->lexer->toTokenText(token));
  }

  auto str = this->lexer->toTokenText(token);
  if (isFunc(str)) {
    TRY(this->expect(DescTokenKind::TYPE_OPEN));
    auto retType = TRY(this->parse_type());
    std::unique_ptr<FuncTypeToken> funcType(new FuncTypeToken(std::move(retType)));

    if (CUR_KIND() != DescTokenKind::TYPE_CLOSE) {
      TRY(this->expect(DescTokenKind::COMMA));
      TRY(this->expect(DescTokenKind::PTYPE_OPEN));
      unsigned int count = 0;
      do {
        if (count++ > 0) {
          TRY(this->expect(DescTokenKind::COMMA));
        }
        funcType->addParamType(TRY(this->parse_type()));
      } while (CUR_KIND() == DescTokenKind::COMMA);
      TRY(this->expect(DescTokenKind::PTYPE_CLOSE));
    }

    TRY(this->expect(DescTokenKind::TYPE_CLOSE));

    return funcType;
  } else {
    auto type(ReifiedTypeToken::newReifiedTypeToken(str));
    TRY(this->expect(DescTokenKind::TYPE_OPEN));

    if (CUR_KIND() != DescTokenKind::TYPE_CLOSE) {
      unsigned int count = 0;
      do {
        if (count++ > 0) {
          TRY(this->expect(DescTokenKind::COMMA));
        }
        type->addElement(TRY(this->parse_type()));
      } while (CUR_KIND() == DescTokenKind::COMMA);
    }

    TRY(this->expect(DescTokenKind::TYPE_CLOSE));

    return std::unique_ptr<TypeToken>(type.release());
  }
}

std::unique_ptr<Element> Parser::parse_funcDecl(const std::string &line,
                                                std::unique_ptr<Element> &element) {
  DescLexer lexer(line.c_str());
  this->init(lexer);

  const bool isDecl = CUR_KIND() == DescTokenKind::ARSH_METHOD_DECL;
  if (isDecl) {
    TRY(this->expect(DescTokenKind::ARSH_METHOD_DECL));
  } else {
    TRY(this->expect(DescTokenKind::ARSH_METHOD));
  }

  Token token = TRY(this->expect(DescTokenKind::IDENTIFIER));
  std::string str(this->lexer->toTokenText(token));
  element->setActualFuncName(std::move(str));

  TRY(this->expect(DescTokenKind::LP));
  TRY(this->expect(DescTokenKind::RCTX));
  TRY(this->expect(DescTokenKind::AND));
  TRY(this->expect(DescTokenKind::IDENTIFIER));
  TRY(this->expect(DescTokenKind::RP));
  TRY(this->expect(isDecl ? DescTokenKind::SEMI_COLON : DescTokenKind::LBC));
  TRY(this->expect(DescTokenKind::EOS));
  return nullptr;
}

#define OUT(fmt, ...)                                                                              \
  do {                                                                                             \
    fprintf(fp, fmt, ##__VA_ARGS__);                                                               \
  } while (false)

struct TypeBind {
  HandleInfo info;
  std::string name;

  std::vector<Element *> funcElements;

  explicit TypeBind(HandleInfo info)
      : info(info), name(HandleInfoMap::getInstance().getName(info)) {}

  ~TypeBind() = default;
};

std::vector<TypeBind *> genTypeBinds(std::vector<std::unique_ptr<Element>> &elements) {
  std::vector<TypeBind *> binds;
#define GEN_BIND(ENUM) binds.push_back(new TypeBind(HandleInfo::ENUM));
  EACH_HANDLE_INFO_TYPE(GEN_BIND)
  EACH_HANDLE_INFO_TYPE_TEMP(GEN_BIND)
  EACH_HANDLE_INFO_FUNC_TYPE(GEN_BIND)
#undef GEN_BIND

  for (const std::unique_ptr<Element> &element : elements) {
    bool matched = false;
    for (TypeBind *bind : binds) {
      if (element->isOwnerType(bind->info)) {
        matched = true;
        bind->funcElements.push_back(element.get());
        break;
      }
    }
    if (!matched) {
      fatal("invalid owner type\n");
    }
  }
  return binds;
}

bool isDisallowType(HandleInfo info) {
  const HandleInfo list[] = {HandleInfo::Void, HandleInfo::Value_};
  return std::any_of(std::begin(list), std::end(list), [&](auto &e) { return e == info; });
}

void genHeaderFile(const char *fileName, const std::vector<TypeBind *> &binds) {
  FILE *fp = fopen(fileName, "w");
  if (fp == nullptr) {
    fatal("cannot open output file: %s\n", fileName);
  }

  // write header
  OUT("// this is an auto-generated file. not change it directly\n");
  OUT("#ifndef ARSH_BIND_H\n");
  OUT("#define ARSH_BIND_H\n");
  OUT("\n");
  OUT("#include <constant.h>\n");
  OUT("\n");
  OUT("namespace arsh {\n");
  OUT("\n");

  // generate NativeFuncInfo table
  OUT("static constexpr NativeFuncInfo infoTable[] = {\n");
  OUT("    {nullptr, nullptr, {}},\n");
  for (TypeBind *bind : binds) {
    for (Element *e : bind->funcElements) {
      OUT("    %s,\n", e->emit().c_str());
    }
  }
  OUT("};\n");
  OUT("const NativeFuncInfo *nativeFuncInfoTable() {\n"
      "    return infoTable;\n"
      "}\n");
  OUT("\n");

  // generate dummy
  OUT("static native_type_info_t info_Dummy() {\n");
  OUT("    return { .offset = 0, .methodSize = 0 };\n");
  OUT("}\n");
  OUT("\n");

  unsigned int offsetCount = 1;

  // generate each native_type_info_t
  for (TypeBind *bind : binds) {
    if (isDisallowType(bind->info)) {
      continue; // skip Void due to having no elements.
    }

    unsigned int methodSize = bind->funcElements.size();

    OUT("static native_type_info_t info_%sType() {\n", bind->name.c_str());
    OUT("    return { .offset = %u, .methodSize = %u };\n",
        bind->funcElements.empty() ? 0 : offsetCount, methodSize);
    OUT("}\n");
    OUT("\n");

    offsetCount += methodSize;
  }

  OUT("} // namespace arsh\n");
  OUT("\n");
  OUT("#endif //ARSH_BIND_H\n");
  fclose(fp);
}

void genSourceFile(const char *fileName, const std::vector<TypeBind *> &binds) {
  FILE *fp = fopen(fileName, "w");
  if (fp == nullptr) {
    fatal("cannot open output file: %s\n", fileName);
  }

  // write header
  OUT("// this is an auto-generated file. not change it directly\n");
  OUT("#include <builtin.h>\n");
  OUT("\n");
  OUT("namespace arsh {\n");
  OUT("\n");

  // generate NativeFuncPtrTable
  OUT("static constexpr native_func_t ptrTable[] = {\n");
  OUT("    nullptr,\n");
  for (TypeBind *bind : binds) {
    for (Element *e : bind->funcElements) {
      OUT("    %s,\n", e->getActualFuncName());
    }
  }
  OUT("};\n");
  OUT("const native_func_t *nativeFuncPtrTable() {\n"
      "    return ptrTable;\n"
      "}\n");
  OUT("\n");

  OUT("} // namespace arsh\n");
  OUT("\n");
  fclose(fp);
}

void gencode(const char *headerFileName, const char *outFileName,
             const std::vector<TypeBind *> &binds) {
  genHeaderFile(headerFileName, binds);
  genSourceFile(outFileName, binds);
}

void gendoc(const char *outFileName, const std::vector<TypeBind *> &binds) {
  FILE *fp = fopen(outFileName, "w");
  if (fp == nullptr) {
    fatal("cannot open output file: %s\n", outFileName);
  }

  OUT("# Standard Library Interface Definition\n");
  for (auto &bind : binds) {
    if (bind->funcElements.empty()) {
      continue;
    }

    OUT("## %s type\n", bind->name.c_str());
    OUT("```");

    for (auto &e : bind->funcElements) {
      OUT("\n");
      OUT("%s\n", e->toString().c_str());
    }
    OUT("```\n\n");
  }

  fclose(fp);
}

enum class OptionSet : unsigned char {
  DOC,
  BIND,
  HEADER,
  HELP,
};

const OptParser<OptionSet>::Option options[] = {
    {OptionSet::DOC, 0, "doc", OptParseOp::NO_ARG, "generate interface documentation"},
    {OptionSet::BIND, 0, "bind", OptParseOp::NO_ARG, "generate function binding"},
    {OptionSet::HEADER, 0, "header", OptParseOp::HAS_ARG,
     "generated header file (only available --bind)"},
    {OptionSet::HELP, 'h', "help", OptParseOp::NO_ARG, "show help message"},
};

void usage(FILE *fp, char **argv) {
  fprintf(fp, "usage: %s ([option..]) [target source] [generated code]\n", argv[0]);
}

} // namespace

int main(int argc, char **argv) {
  auto parser = createOptParser(options);

  auto begin = argv + 1;
  auto end = argv + argc;
  OptParseResult<OptionSet> result;

  StringRef headerFileName;
  bool doc = false;
  while ((result = parser(begin, end))) {
    switch (result.getOpt()) {
    case OptionSet::DOC:
      doc = true;
      break;
    case OptionSet::BIND:
      doc = false;
      break;
    case OptionSet::HELP:
      usage(stdout, argv);
      printf("%s\n", parser.formatOptions().c_str());
      exit(0);
    case OptionSet::HEADER:
      headerFileName = result.getValue();
      break;
    }
  }
  if (result.isError()) {
    fprintf(stderr, "%s\n%s\n", result.formatError().c_str(), parser.formatOptions().c_str());
    exit(1);
  }

  if (begin == end || begin + 1 == end) {
    usage(stderr, argv);
    exit(1);
  }
  if (!doc) {
    if (!headerFileName.data()) {
      fprintf(stderr, "require --header option\n%s\n", parser.formatOptions().c_str());
      exit(1);
    }
  }

  const char *inputFileName = *begin;
  const char *outputFileName = *(++begin);

  auto elements = Parser()(inputFileName);
  std::vector<TypeBind *> binds(genTypeBinds(elements));

  if (doc) {
    gendoc(outputFileName, binds);
  } else {
    gencode(headerFileName.toString().c_str(), outputFileName, binds);
  }

  exit(0);
}
