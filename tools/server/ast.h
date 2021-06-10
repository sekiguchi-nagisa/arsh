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

class Source { // FIXME: apply changes to buffer
private:
  std::string fileName; // must be full path
  std::string content;

public:
  Source(const uri::URI &uri, std::string &&content)
      : fileName(uri.getPath()), content(std::move(content)) {}

  const std::string &getFileName() const { return this->fileName; }

  const std::string &getContent() const { return this->content; }
};

class ASTContext : public RefCount<ASTContext> {
private:
  Source source;
  IntrusivePtr<NameScope> scope;
  TypePool pool;
  unsigned int gvarCount{0};

public:
  ASTContext(unsigned int modID, Source &&source);

  const Source &getSource() const { return this->source; }

  const IntrusivePtr<NameScope> &getScope() const { return this->scope; }

  const TypePool &getPool() const { return this->pool; }

  TypePool &getPool() { return this->pool; }
};

using ASTContextPtr = IntrusivePtr<ASTContext>;

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_SERVER_AST_H
