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

#include "compiler.h"

namespace ydsh {

// ######################
// ##     Compiler     ##
// ######################

Compiler::Compiler(DefaultModuleProvider &moduleProvider, std::unique_ptr<FrontEnd::Context> &&ctx,
                   CompileOption compileOption, const CompileDumpTarget *dumpTarget)
    : compileOption(compileOption), provider(moduleProvider),
      frontEnd(this->provider, std::move(ctx), toOption(this->compileOption)),
      uastDumper(dumpTarget ? dumpTarget->fps[DS_DUMP_KIND_UAST] : nullptr),
      astDumper(dumpTarget ? dumpTarget->fps[DS_DUMP_KIND_AST] : nullptr),
      codegen(this->provider.getPool()),
      codeDumper(dumpTarget ? dumpTarget->fps[DS_DUMP_KIND_CODE] : nullptr,
                 this->provider.getPool()) {
  if (this->uastDumper) {
    this->frontEnd.setUASTDumper(this->uastDumper);
  }
  if (this->astDumper) {
    this->frontEnd.setASTDumper(this->astDumper);
  }
}

int Compiler::operator()(ObjPtr<FuncObject> &func) {
  this->frontEnd.setupASTDump();
  if (!this->frontEndOnly()) {
    this->codegen.initialize(this->frontEnd.getCurModId(), this->frontEnd.getCurrentLexer());
  }
  while (this->frontEnd) {
    auto ret = this->frontEnd();
    if (!ret) {
      return 1;
    }

    if (this->frontEndOnly()) {
      continue;
    }

    switch (ret.kind) {
    case FrontEndResult::ENTER_MODULE:
      this->codegen.enterModule(this->frontEnd.getCurModId(), this->frontEnd.getCurrentLexer());
      break;
    case FrontEndResult::EXIT_MODULE:
      if (!this->codegen.exitModule(cast<SourceNode>(*ret.node))) {
        goto END;
      }
      break;
    case FrontEndResult::IN_MODULE:
      if (!this->codegen.generate(*ret.node)) {
        goto END;
      }
      break;
    default:
      break;
    }
  }
  this->frontEnd.teardownASTDump();
  assert(this->frontEnd.getContext().size() == 1);
  {
    auto &modType = this->provider.newModTypeFromCurContext(this->frontEnd.getContext());
    if (!this->frontEndOnly()) {
      func = this->codegen.finalize(this->frontEnd.getMaxLocalVarIndex(), modType);
    }
  }

END:
  if (this->codegen.hasError()) {
    auto &e = this->codegen.getError();
    this->errorListener &&this->errorListener->handleCodeGenError(this->frontEnd.getContext(), e);
    return 1;
  }
  if (hasFlag(this->compileOption, CompileOption::LOAD_TO_ROOT)) {
    auto ret = this->provider.getPool().getModTypeById(this->frontEnd.getCurModId());
    assert(ret);
    auto msg = this->provider.getScope()->importForeignHandles(
        this->provider.getPool(), cast<ModType>(*ret.asOk()), ImportedModKind::GLOBAL);
    if (!msg.empty()) {
      auto node = std::make_unique<EmptyNode>(Token{0, 0});
      auto error = createTCError<ConflictSymbol>(
          *node, msg.c_str(), this->frontEnd.getCurrentLexer().getSourceName().c_str());
      this->errorListener &&this->errorListener->handleTypeError(this->frontEnd.getContext(),
                                                                 error);
      return 1;
    }
  }

  // dump code
  if (this->codeDumper && func) {
    this->codeDumper(func->getCode(), this->provider.getScope()->getMaxGlobalVarIndex());
  }
  return 0;
}

} // namespace ydsh