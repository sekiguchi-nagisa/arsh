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
 *
 * every operand is read from a placeholder, so the stencil never touches the bytecode. each of them
 * also keeps the state it does not use in registers for the successor.
 */

JIT_STENCIL_DEF(stencil_Nop) {
  JIT_NEXT();
}

JIT_STENCIL_DEF(stencil_Match) {
  (void)bts;
  (void)loops;
  (void)matchers;
  // capture 0 is the whole match, which starts where the current attempt started
  arsh_jit_finish(ctx->ctx, input, captures, ctx->matchStart);
  JIT_MATCHED();
}

JIT_STENCIL_DEF(stencil_Jump) {
  JIT_TAIL(jit_target_jump);
}

JIT_STENCIL_DEF(stencil_Alt) {
  // the second branch is only needed if the first one fails, so it is not patched as a code address
  const auto second = JIT_IMM_I32(jit_imm_alt_second);
  JIT_TRY(arsh_jit_push_set_ins(bts, input, static_cast<uint32_t>(second)));
  JIT_NEXT();
}

JIT_STENCIL_DEF(stencil_Start) {
  const bool multiline = JIT_IMM_BOOL(jit_imm_multiline);
  if (input->isBegin() || (multiline && isLineTerminator(input->prev()))) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_End) {
  const bool multiline = JIT_IMM_BOOL(jit_imm_multiline);
  if (input->isEnd() || (multiline && isLineTerminator(input->cur()))) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_Word) {
  const bool invert = JIT_IMM_BOOL(jit_imm_invert);
  const bool prevIsWord = !input->isBegin() && isWord(input->prev());
  const bool curIsWord = !input->isEnd() && isWord(input->cur());
  if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_IWord) {
  const bool invert = JIT_IMM_BOOL(jit_imm_invert);
  const bool prevIsWord = !input->isBegin() && (arsh_jit_is_extend_word(input->prev()) != 0);
  const bool curIsWord = !input->isEnd() && (arsh_jit_is_extend_word(input->cur()) != 0);
  if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

/**
 * single code point consumption.
 */

JIT_STENCIL_DEF(stencil_Any) {
  if (input->available()) {
    input->consumeForward();
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_AnyExceptNL) {
  if (input->available()) {
    const int codePoint = input->consumeForward();
    if (!isLineTerminator(codePoint)) {
      JIT_NEXT();
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBAny) {
  const bool dotAll = JIT_IMM_BOOL(jit_imm_dot_all);
  if (input->availableBackward()) {
    const int codePoint = input->consumeBackward();
    if (dotAll || !isLineTerminator(codePoint)) {
      JIT_NEXT();
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_Grapheme) {
  if (input->available()) {
    const StringRef ref = input->remainForward();
    const auto size = arsh_jit_grapheme_size(ref.data(), static_cast<uint32_t>(ref.size()));
    input->setIter(ref.data() + size);
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_Char) {
  const auto codePoint = JIT_IMM_I32(jit_imm_code_point);
  if (input->available() && input->consumeForward() == codePoint) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_IChar) {
  const auto codePoint = JIT_IMM_I32(jit_imm_code_point);
  if (input->available() && arsh_jit_case_fold(input->consumeForward()) == codePoint) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBChar) {
  const auto codePoint = JIT_IMM_I32(jit_imm_code_point);
  const bool ignoreCase = JIT_IMM_BOOL(jit_imm_ignore_case);
  if (input->availableBackward()) {
    int c = input->consumeBackward();
    if (ignoreCase) {
      c = arsh_jit_case_fold(c);
    }
    if (c == codePoint) {
      JIT_NEXT();
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_CharSet) {
  const auto index = JIT_IMM_U16(jit_imm_matcher_index);
  const bool invert = JIT_IMM_BOOL(jit_imm_invert);
  if (input->available()) {
    const bool contain = arsh_jit_matcher_contains(matchers, index, input->consumeForward()) != 0;
    if (contain != invert) {
      JIT_NEXT();
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_ICharSet) {
  const auto index = JIT_IMM_U16(jit_imm_matcher_index);
  const bool invert = JIT_IMM_BOOL(jit_imm_invert);
  if (input->available()) {
    const bool contain =
        arsh_jit_matcher_contains(matchers, index, arsh_jit_case_fold(input->consumeForward())) != 0;
    if (contain != invert) {
      JIT_NEXT();
    }
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBCharSet) {
  const auto index = JIT_IMM_U16(jit_imm_matcher_index);
  const bool invert = JIT_IMM_BOOL(jit_imm_invert);
  const bool ignoreCase = JIT_IMM_BOOL(jit_imm_ignore_case);
  if (input->availableBackward()) {
    int codePoint = input->consumeBackward();
    if (ignoreCase) {
      codePoint = arsh_jit_case_fold(codePoint);
    }
    if ((arsh_jit_matcher_contains(matchers, index, codePoint) != 0) != invert) {
      JIT_NEXT();
    }
  }
  JIT_BACKTRACK();
}

} // namespace arsh::regex::jit
