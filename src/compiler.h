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

#ifndef YDSH_COMPILER_H
#define YDSH_COMPILER_H

#include "codegen.h"
#include "error_report.h"
#include "frontend.h"
#include "misc/flag_util.hpp"

namespace ydsh {

enum class CompileOption : unsigned short {
  LOAD_TO_ROOT = 1u << 1u,
  PARSE_ONLY = 1u << 2u,
  CHECK_ONLY = 1u << 3u,
  PRINT_TOPLEVEL = 1u << 4u,
};

template <>
struct allow_enum_bitop<CompileOption> : std::true_type {};

inline FrontEndOption toOption(CompileOption option) {
  FrontEndOption op{};
  if (hasFlag(option, CompileOption::PARSE_ONLY)) {
    setFlag(op, FrontEndOption::PARSE_ONLY);
  }
  if (hasFlag(option, CompileOption::PRINT_TOPLEVEL)) {
    setFlag(op, FrontEndOption::TOPLEVEL);
  }
  return op;
}

struct CompileDumpTarget {
  FILE *fps[3];
};

class Compiler {
private:
  CompileOption compileOption;
  DefaultModuleProvider &provider;
  FrontEnd frontEnd;
  ObserverPtr<ErrorListener> errorListener;
  NodeDumper uastDumper;
  NodeDumper astDumper;
  ByteCodeGenerator codegen;
  ByteCodeDumper codeDumper;

public:
  Compiler(DefaultModuleProvider &moduleProvider, std::unique_ptr<FrontEnd::Context> &&ctx,
           CompileOption compileOption, const CompileDumpTarget *dumpTarget);

  bool frontEndOnly() const {
    return hasFlag(this->compileOption, CompileOption::PARSE_ONLY) ||
           hasFlag(this->compileOption, CompileOption::CHECK_ONLY);
  }

  void setErrorListener(ErrorListener &r) {
    this->errorListener.reset(&r);
    this->frontEnd.setErrorListener(r);
  }

  unsigned int lineNum() const { return this->frontEnd.getRootLineNum(); }

  int operator()(ObjPtr<FuncObject> &func);
};

} // namespace ydsh

#endif // YDSH_COMPILER_H
