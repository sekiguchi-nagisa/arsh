/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#include <unistd.h>

#include <fstream>
#include <functional>
#include <sstream>

#include "../platform/platform.h"
#include "directive.h"

#include <misc/fatal.h>
#include <misc/files.h>
#include <parser.h>
#include <paths.h>
#include <type_checker.h>

namespace ydsh::directive {

#define TRY(expr)                                                                                  \
  ({                                                                                               \
    auto v = expr;                                                                                 \
    if (this->hasError()) {                                                                        \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

struct DirectiveParser : public Parser {
  explicit DirectiveParser(Lexer &lexer) : Parser(lexer) {}
  ~DirectiveParser() = default;

  std::unique_ptr<ApplyNode> operator()() {
    auto exprNode = TRY(this->parse_appliedName(false));
    auto argsNode = TRY(this->parse_arguments());
    TRY(this->expect(TokenKind::EOS));
    return std::make_unique<ApplyNode>(std::move(exprNode), std::move(argsNode));
  }
};

static bool isDirective(const std::string &line) {
  auto *ptr = line.c_str();
  return strstr(ptr, "#$test") == ptr;
}

static std::pair<std::string, unsigned int> extractDirective(std::istream &input) {
  unsigned int lineNum = 0;

  for (std::string line; std::getline(input, line);) {
    lineNum++;
    if (isDirective(line)) {
      line.erase(line.begin());
      return {line, lineNum};
    }
  }
  return {std::string(), 0};
}

class DirectiveInitializer : public TypeChecker {
private:
  std::string sourceName;
  unsigned int varCount{0}; // for scope
  using AttributeHandler = std::function<void(Node &, Directive &)>;
  using Handler = std::pair<const DSType *, AttributeHandler>;
  std::unordered_map<std::string, Handler> handlerMap;

public:
  DirectiveInitializer(const char *sourceName, const SysConfig &sysConfig, TypePool &pool,
                       Lexer &lex);
  ~DirectiveInitializer() override = default;

  void operator()(ApplyNode &node, Directive &d);

  const TypeCheckError &getError() const { return this->getErrors().front(); }

private:
  void addHandler(const char *attributeName, const DSType &type, AttributeHandler &&handler);

  void addHandler(const char *attributeName, TYPE type, AttributeHandler &&handler) {
    this->addHandler(attributeName, this->typePool.get(type), std::move(handler));
  }

  void addAlias(const char *alias, const char *attr);
  int resolveKind(const StringNode &node);

  /**
   * if not found corresponding handler, return null.
   */
  const std::pair<const DSType *, AttributeHandler> *lookupHandler(const std::string &name) const;

  bool checkNode(NodeKind kind, const Node &node);

  void setVarName(const char *name, const DSType &type);

  template <typename T>
  T *checkedCast(Node &node) {
    if (!this->checkNode(T::KIND, node)) {
      return nullptr;
    }
    return cast<T>(&node);
  }

  /**
   * return always [String : String] type
   * @return
   */
  const DSType &getMapType() {
    return *this->typePool
                .createMapType(this->typePool.get(TYPE::String), this->typePool.get(TYPE::String))
                .take();
  }

  void createError(const Node &node, const std::string &str) {
    this->errors.emplace_back(node.getToken(), "", CStrPtr(strdup(str.c_str())));
  }
};

#undef TRY
#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto v = E;                                                                                    \
    if (this->hasError()) {                                                                        \
      return;                                                                                      \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

// ##################################
// ##     DirectiveInitializer     ##
// ##################################

static bool checkDirectiveName(ApplyNode &node) {
  assert(node.getExprNode().is(NodeKind::Var));
  auto &exprNode = cast<VarNode>(node.getExprNode()); // NOLINT
  return exprNode.getVarName() == "test";
}

DirectiveInitializer::DirectiveInitializer(const char *sourceName, const SysConfig &sysConfig,
                                           TypePool &typePool, Lexer &lex)
    : TypeChecker(sysConfig, typePool, false, lex), sourceName(sourceName) {
  unsigned int modIndex = this->varCount++;
  this->curScope = NameScopePtr::create(std::ref(this->varCount), modIndex, 0);
  this->setVarName("0", this->typePool.get(TYPE::String));
}

static bool isIgnoredUser(const std::string &text) {
  return text.find('#') != std::string::npos && getuid() == 0;
}

void DirectiveInitializer::operator()(ApplyNode &node, Directive &d) {
  if (!checkDirectiveName(node)) {
    std::string str("unsupported directive: ");
    str += cast<VarNode>(node.getExprNode()).getVarName(); // NOLINT
    return this->createError(node, str);
  }

  this->addHandler("status", TYPE::Int, [&](Node &node, Directive &d) {
    int64_t value = TRY(this->checkedCast<NumberNode>(node))->getIntValue();
    if (value < INT32_MIN || value > INT32_MAX) {
      std::string str = "must be int32 value: ";
      str += std::to_string(value);
      return this->createError(node, str);
    }
    d.setStatus(static_cast<int>(value));
  });

  this->addHandler("result", TYPE::String, [&](Node &node, Directive &d) {
    auto *strNode = TRY(this->checkedCast<StringNode>(node));
    d.setKind(TRY(this->resolveKind(*strNode)));
  });

  this->addHandler("params", TYPE::StringArray, [&](Node &node, Directive &d) {
    auto *value = TRY(this->checkedCast<ArrayNode>(node));
    for (auto &e : value->getExprNodes()) {
      d.appendParam(TRY(this->checkedCast<StringNode>(*e))->getValue());
    }
  });

  this->addHandler("lineNum", TYPE::Int, [&](Node &node, Directive &d) {
    d.setLineNum(TRY(this->checkedCast<NumberNode>(node))->getIntValue());
  });

  this->addHandler("chars", TYPE::Int, [&](Node &node, Directive &d) {
    d.setChars(TRY(this->checkedCast<NumberNode>(node)->getIntValue()));
  });

  this->addHandler("errorKind", TYPE::String, [&](Node &node, Directive &d) {
    d.setErrorKind(TRY(this->checkedCast<StringNode>(node))->getValue());
  });

  this->addHandler("in", TYPE::String, [&](Node &node, Directive &d) {
    d.setIn(TRY(this->checkedCast<StringNode>(node))->getValue());
  });

  this->addHandler("out", TYPE::String, [&](Node &node, Directive &d) {
    d.setOut(TRY(this->checkedCast<StringNode>(node))->getValue());
  });

  this->addHandler("err", TYPE::String, [&](Node &node, Directive &d) {
    d.setErr(TRY(this->checkedCast<StringNode>(node))->getValue());
  });

  this->addHandler("fileName", TYPE::String, [&](Node &node, Directive &d) {
    if (node.is(NodeKind::Var) && TRY(this->checkedCast<VarNode>(node))->getVarName() == "0") {
      d.setFileName(this->sourceName.c_str());
      return;
    }

    const std::string str = TRY(this->checkedCast<StringNode>(node))->getValue();
    std::string baseDir;
    if (auto real = getRealpath(this->sourceName.c_str())) {
      StringRef ref = real.get();
      auto r = ref.lastIndexOf("/");
      assert(r != StringRef::npos);
      auto base = ref.slice(0, r != 0 ? r : r + 1);
      baseDir = base.toString();
    } else {
      baseDir = getCWD().get();
    }
    auto fullPath = expandDots(baseDir.c_str(), str.c_str());
    auto real = getRealpath(fullPath.c_str());
    if (!real) {
      std::string message = "invalid file name: ";
      message += str;
      return this->createError(node, message);
    }
    d.setFileName(real.get());
  });

  this->addHandler("envs", this->getMapType(), [&](Node &node, Directive &d) {
    auto *mapNode = TRY(this->checkedCast<MapNode>(node));
    const unsigned int size = mapNode->getKeyNodes().size();
    for (unsigned int i = 0; i < size; i++) {
      auto *keyNode = TRY(this->checkedCast<StringNode>(*mapNode->getKeyNodes()[i]));
      auto *valueNode = TRY(this->checkedCast<StringNode>(*mapNode->getValueNodes()[i]));
      d.addEnv(keyNode->getValue(), valueNode->getValue());
    }
  });
  this->addAlias("env", "envs");

  this->addHandler("ignored", TYPE::String, [&](Node &node, Directive &d) {
    auto &str = TRY(this->checkedCast<StringNode>(node))->getValue();
    d.setIgnoredPlatform(isIgnoredUser(str) || platform::contain(str));
  });

  std::unordered_set<std::string> foundAttrSet;
  for (auto &attrNode : node.getArgsNode().getNodes()) {
    auto *assignNode = TRY(this->checkedCast<AssignNode>(*attrNode));
    auto &attrName = TRY(this->checkedCast<VarNode>(assignNode->getLeftNode()))->getVarName();
    auto *pair = this->lookupHandler(attrName);
    if (pair == nullptr) {
      std::string str("unsupported attribute: ");
      str += attrName;
      return this->createError(assignNode->getLeftNode(), str);
    }

    // check duplication
    auto iter = foundAttrSet.find(attrName);
    if (iter != foundAttrSet.end()) {
      std::string str("duplicated attribute: ");
      str += attrName;
      return this->createError(assignNode->getLeftNode(), str);
    }

    // check type attribute
    this->checkType(*pair->first, assignNode->getRightNode());
    if (this->hasError()) {
      return;
    }

    // invoke handler
    (pair->second)(assignNode->getRightNode(), d);
    if (this->hasError()) {
      return;
    }
    foundAttrSet.insert(attrName);
  }
}

void DirectiveInitializer::addHandler(const char *attributeName, const DSType &type,
                                      AttributeHandler &&handler) {
  auto pair = this->handlerMap.emplace(attributeName, std::make_pair(&type, std::move(handler)));
  if (!pair.second) {
    fatal("found duplicated handler: %s\n", attributeName);
  }
}

void DirectiveInitializer::addAlias(const char *alias, const char *attr) {
  auto *handler = this->lookupHandler(attr);
  if (handler != nullptr) {
    this->addHandler(alias, *handler->first, AttributeHandler(handler->second));
  }
}

int DirectiveInitializer::resolveKind(const StringNode &node) {
  const struct {
    const char *name;
    int kind;
  } resultTable[] = {
#define DEFINE_ERROR(K) DS_ERROR_KIND_##K
      {"success", DEFINE_ERROR(SUCCESS)},
      {"parse_error", DEFINE_ERROR(PARSE_ERROR)},
      {"parse", DEFINE_ERROR(PARSE_ERROR)},
      {"type_error", DEFINE_ERROR(TYPE_ERROR)},
      {"type", DEFINE_ERROR(TYPE_ERROR)},
      {"codegen", DEFINE_ERROR(CODEGEN_ERROR)},
      {"runtime_error", DEFINE_ERROR(RUNTIME_ERROR)},
      {"runtime", DEFINE_ERROR(RUNTIME_ERROR)},
      {"throw", DEFINE_ERROR(RUNTIME_ERROR)},
      {"assertion_error", DEFINE_ERROR(ASSERTION_ERROR)},
      {"assert", DEFINE_ERROR(ASSERTION_ERROR)},
      {"exit", DEFINE_ERROR(EXIT)},
#undef DEFINE_ERROR
  };

  const auto &value = node.getValue();
  for (auto &e : resultTable) {
    if (strcasecmp(value.c_str(), e.name) == 0) {
      return e.kind;
    }
  }

  /**
   * if emtpy, ignore error
   */
  if (!value.empty()) {
    std::vector<std::string> alters;
    for (auto &e : resultTable) {
      alters.emplace_back(e.name);
    }

    std::string message = "illegal result: ";
    message += value;
    message += ", expect for ";
    unsigned int count = 0;
    for (auto &e : alters) {
      if (count++ > 0) {
        message += ", ";
      }
      message += "'";
      message += e;
      message += "'";
    }
    createError(node, message);
  }
  return -1;
}

const std::pair<const DSType *, DirectiveInitializer::AttributeHandler> *
DirectiveInitializer::lookupHandler(const std::string &name) const {
  auto iter = this->handlerMap.find(name);
  if (iter == this->handlerMap.end()) {
    return nullptr;
  }
  return &iter->second;
}

bool DirectiveInitializer::checkNode(NodeKind kind, const Node &node) {
  const char *table[] = {
#define GEN_STR(K) #K,
      EACH_NODE_KIND(GEN_STR)
#undef GEN_STR
  };

  if (!node.is(kind)) {
    std::string str = "require: ";
    str += table[static_cast<unsigned int>(kind)];
    str += "Node, but is: ";
    str += table[static_cast<unsigned int>(node.getNodeKind())];
    str += "Node";
    this->createError(node, str);
    return false;
  }
  return true;
}

void DirectiveInitializer::setVarName(const char *name, const DSType &type) {
  this->curScope->defineHandle(name, type, HandleAttr());
}

// #######################
// ##     Directive     ##
// #######################

Directive::~Directive() {
  free(this->out);
  free(this->err);
}

static void showError(const char *sourceName, Lexer &lexer, const std::string &line,
                      Token errorToken, const std::string &message, const char *errorName) {
  Token lineToken = {0, static_cast<unsigned int>(line.size())};
  std::cerr << sourceName << ":" << lexer.getMaxLineNum() << ": [" << errorName << " error] ";
  std::cerr << message << std::endl;
  std::cerr << line << std::endl;
  std::cerr << lexer.formatLineMarker(lineToken, errorToken) << std::endl;
}

static bool initDirective(const char *fileName, std::istream &input, Directive &directive) {
  auto ret = extractDirective(input);
  if (ret.first.empty()) {
    return true;
  }

  ByteBuffer buf;
  buf.append(ret.first.c_str(), ret.first.size());
  Lexer lexer(fileName, std::move(buf), nullptr);
  lexer.setLineNumOffset(ret.second);

  DirectiveParser parser(lexer);
  auto node = parser();
  if (parser.hasError()) {
    auto &e = parser.getError();
    showError(fileName, lexer, ret.first, e.getErrorToken(), e.getMessage(), "syntax");
    return false;
  }

  SysConfig sysConfig;
  TypePool pool;
  DirectiveInitializer initializer(fileName, sysConfig, pool, lexer);
  initializer(*node, directive);
  if (initializer.hasError()) {
    auto &e = initializer.getError();
    showError(fileName, lexer, ret.first, e.getToken(), e.getMessage(), "semantic");
    return false;
  }
  return true;
}

bool Directive::init(const char *fileName, Directive &d) {
  std::ifstream input(fileName);
  if (!input) {
    std::cerr << "cannot open file: " << fileName << std::endl;
    return false;
  }
  return initDirective(fileName, input, d);
}

bool Directive::init(const char *sourceName, const char *src, Directive &d) {
  std::istringstream input(src);
  return initDirective(sourceName, input, d);
}

#define EACH_DS_ERROR_KIND(E)                                                                      \
  E(DS_ERROR_KIND_SUCCESS)                                                                         \
  E(DS_ERROR_KIND_FILE_ERROR)                                                                      \
  E(DS_ERROR_KIND_PARSE_ERROR)                                                                     \
  E(DS_ERROR_KIND_TYPE_ERROR)                                                                      \
  E(DS_ERROR_KIND_CODEGEN_ERROR)                                                                   \
  E(DS_ERROR_KIND_RUNTIME_ERROR)                                                                   \
  E(DS_ERROR_KIND_ASSERTION_ERROR)                                                                 \
  E(DS_ERROR_KIND_EXIT)

const char *toString(DSErrorKind kind) {
  switch (kind) {
#define GEN_CASE(K)                                                                                \
  case DSErrorKind::K:                                                                             \
    return #K;
    EACH_DS_ERROR_KIND(GEN_CASE)
#undef GEN_CASE
  }
  return "";
}

} // namespace ydsh::directive