/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#include "stencil_context.h"

namespace arsh::regex::jit {

/**
 * control flow and boundary assertions. none of them consume input.
 */

JIT_STENCIL_DEF(stencil_Nop) {
  JIT_NEXT_TYPE(NopIns);
}

JIT_STENCIL_DEF(stencil_Match) {
  (void)inst;
  arsh_jit_finish(&ctx);
  JIT_MATCHED();
}

JIT_STENCIL_DEF(stencil_Jump) {
  JIT_GOTO_OFF(JIT_INST(JumpIns).getTarget());
}

JIT_STENCIL_DEF(stencil_Alt) {
  const auto &ins = JIT_INST(AltIns);
  JIT_TRY(arsh_jit_push_set_ins(ctx.bts, ctx.input, ins.getSecond()));
  JIT_NEXT_TYPE(AltIns);
}

JIT_STENCIL_DEF(stencil_Start) {
  const auto &ins = JIT_INST(StartIns);
  if (ctx.input->isBegin() || (ins.multiline && isLineTerminator(ctx.input->prev()))) {
    JIT_NEXT_TYPE(StartIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_End) {
  const auto &ins = JIT_INST(EndIns);
  if (ctx.input->isEnd() || (ins.multiline && isLineTerminator(ctx.input->cur()))) {
    JIT_NEXT_TYPE(EndIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_Word) {
  const bool invert = JIT_INST(WordIns).invert;
  const bool prevIsWord = !ctx.input->isBegin() && isWord(ctx.input->prev());
  const bool curIsWord = !ctx.input->isEnd() && isWord(ctx.input->cur());
  if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
    JIT_NEXT_TYPE(WordIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_IWord) {
  const bool invert = JIT_INST(IWordIns).invert;
  const bool prevIsWord = !ctx.input->isBegin() && (arsh_jit_is_extend_word(ctx.input->prev()) != 0);
  const bool curIsWord = !ctx.input->isEnd() && (arsh_jit_is_extend_word(ctx.input->cur()) != 0);
  if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
    JIT_NEXT_TYPE(IWordIns);
  }
  JIT_BACKTRACK();
}

/**
 * single code point consumption.
 */

JIT_STENCIL_DEF(stencil_Any) {
  if (ctx.input->available()) {
    ctx.input->consumeForward();
    JIT_NEXT_TYPE(AnyIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_AnyExceptNL) {
  if (ctx.input->available()) {
    const int codePoint = ctx.input->consumeForward();
    if (!isLineTerminator(codePoint)) {
      JIT_NEXT_TYPE(AnyExceptNLIns);
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBAny) {
  if (ctx.input->availableBackward()) {
    const int codePoint = ctx.input->consumeBackward();
    if (JIT_INST(LBAnyIns).dotAll || !isLineTerminator(codePoint)) {
      JIT_NEXT_TYPE(LBAnyIns);
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_Grapheme) {
  if (ctx.input->available()) {
    const StringRef ref = ctx.input->remainForward();
    const auto size = arsh_jit_grapheme_size(ref.data(), static_cast<uint32_t>(ref.size()));
    ctx.input->setIter(ref.data() + size);
    JIT_NEXT_TYPE(GraphemeIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_Char) {
  const auto codePoint = JIT_INST(CharIns).getCodePoint();
  if (ctx.input->available() && ctx.input->consumeForward() == codePoint) {
    JIT_NEXT_TYPE(CharIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_IChar) {
  const auto codePoint = JIT_INST(ICharIns).getCodePoint();
  if (ctx.input->available() && arsh_jit_case_fold(ctx.input->consumeForward()) == codePoint) {
    JIT_NEXT_TYPE(ICharIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBChar) {
  const auto &ins = JIT_INST(LBCharIns);
  if (ctx.input->availableBackward()) {
    int codePoint = ctx.input->consumeBackward();
    if (ins.ignoreCase) {
      codePoint = arsh_jit_case_fold(codePoint);
    }
    if (codePoint == ins.getCodePoint()) {
      JIT_NEXT_TYPE(LBCharIns);
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_CharSet) {
  const auto &ins = JIT_INST(CharSetIns);
  if (ctx.input->available()) {
    const auto index = ins.getMatcherIndex();
    const bool contain = arsh_jit_matcher_contains(ctx.matchers, index, ctx.input->consumeForward());
    if (contain != ins.invert) {
      JIT_NEXT_TYPE(CharSetIns);
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_ICharSet) {
  const auto &ins = JIT_INST(ICharSetIns);
  if (ctx.input->available()) {
    const auto index = ins.getMatcherIndex();
    const bool contain = arsh_jit_matcher_contains(
        ctx.matchers, index, arsh_jit_case_fold(ctx.input->consumeForward()));
    if (contain != ins.invert) {
      JIT_NEXT_TYPE(ICharSetIns);
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBCharSet) {
  const auto &ins = JIT_INST(LBCharSetIns);
  if (ctx.input->availableBackward()) {
    const auto index = ins.getMatcherIndex();
    int codePoint = ctx.input->consumeBackward();
    if (ins.ignoreCase) {
      codePoint = arsh_jit_case_fold(codePoint);
    }
    if (arsh_jit_matcher_contains(ctx.matchers, index, codePoint) != ins.invert) {
      JIT_NEXT_TYPE(LBCharSetIns);
    }
  }
  JIT_BACKTRACK();
}

} // namespace arsh::regex::jit
