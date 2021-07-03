/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_SERVER_AST_H
#define YDSH_TOOLS_SERVER_AST_H

#include <node.h>
#include <scope.h>
#include <type_pool.h>

#include "../tools/uri/uri.h"

namespace ydsh::lsp {

class Source {
private:
  std::string fileName;
  std::string content;

public:
  Source(const uri::URI &uri, std::string &&content)
      : fileName(uri.getPath()), content(std::move(content)) {}

  const std::string &getFileName() const {
    return this->fileName;
  }

  const std::string &getContent() const {
    return this->content;
  }

  void setContent(std::string &&c) {
    this->content = std::move(c);
  }

  ByteBuffer toContentBuf() const {
    const char *ptr = this->content.c_str();
    return ByteBuffer(ptr, ptr + strlen(ptr));
  }
};

class ASTContext : public RefCount<ASTContext> {
private:
  Source source;
  IntrusivePtr<NameScope> scope;
  TypePool pool;
  std::vector<std::unique_ptr<Node>> nodes;
  unsigned int gvarCount{0};
  TypeDiscardPoint typeDiscardPoint;
  ScopeDiscardPoint scopeDiscardPoint;

public:
  ASTContext(unsigned int modID, Source &&source);

  const Source &getSource() const { return this->source; }

  const IntrusivePtr<NameScope> &getScope() const { return this->scope; }

  const TypePool &getPool() const { return this->pool; }

  TypePool &getPool() { return this->pool; }

  void updateContent(std::string &&c) {
    this->source.setContent(std::move(c));
  }

  unsigned int getModId() const {
    return this->scope->modId;
  }

  void addNode(std::unique_ptr<Node> &&node) {
    this->nodes.push_back(std::move(node));
  }

  const std::vector<std::unique_ptr<Node>> &getNodes() const {
    return this->nodes;
  }
};

using ASTContextPtr = IntrusivePtr<ASTContext>;

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_SERVER_AST_H
