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

#include "jit_code.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../instruction.h"
#include "../regex.h"
#include "jit_context.h"
#include "stencil_data.h"
#include "stencil_runtime.h"

namespace arsh::regex::jit {

namespace {

constexpr const char *OPCODE_NAMES[] = {
#define GEN_NAME(E) #E,
    EACH_RE_OPCODE(GEN_NAME)
#undef GEN_NAME
};

const StencilData *findStencil(const char *opcode) {
  for (const auto &stencil : STENCILS) {
    if (strcmp(stencil.opcode, opcode) == 0) {
      return &stencil;
    }
  }
  return nullptr;
}

/**
 * one instruction of the bytecode, with the block it was compiled to.
 *
 * `offset` is the byte offset within the instruction sequence, which is also the key the backtrack
 * stack stores its targets under (`Backtrack::setIns.target`), so it is what a resolved target is
 * looked up by.
 */
struct Instruction {
  size_t offset{0};
  size_t blockOffset{0};
  const StencilData *stencil{nullptr};
};

size_t alignUp(const size_t value, const size_t align) { return (value + align - 1) & ~(align - 1); }

template <typename T>
const T &as(const Inst *inst) {
  return *reinterpret_cast<const T *>(inst);
}

/**
 * what a hole is filled with.
 *
 * an *operand* is written as a plain value into the `movabs` the compiler emitted for the
 * placeholder reference. a *code address* is the address of the block for a bytecode offset, which
 * is what the second pass resolves once the layout is known.
 */
struct HoleValue {
  uint64_t imm{0};       // the operand, or the bytecode offset of a code address
  bool isCodeAddress{false};
};

/**
 * the `BeginLoop` a loop instruction refers to.
 *
 * `BeginLoop` is its own; `EndLoop` stores the distance back to it, exactly like the interpreter
 * computes it (see `Backtrack::backtrack`).
 */
const Inst *resolveLoopInst(const Inst *inst, const char *instSeqBase) {
  if (inst->op == OpCode::BeginLoop) {
    return inst;
  }
  // `EndLoop` stores the *absolute* byte offset of its `BeginLoop` (see `vm.cpp`:
  // `inst = bts.getStartInst() + cast<EndLoopIns>(*inst).getTarget()`)
  return reinterpret_cast<const Inst *>(const_cast<char *>(instSeqBase) +
                                        as<EndLoopIns>(inst).getTarget());
}

/**
 * decode the operand of `inst` that the placeholder `symbol` stands for.
 *
 * every target is a bytecode offset: `resolveHole` is handed the *base* of the sequence so that the
 * distance fields (`EndLoop`'s and the radix ones) and the absolute ones (`Jump`, `Alt`, ...) come
 * out the same way, which is what `vm.cpp` relies on too.
 */
bool resolveHole(const Inst *inst, const char *instSeqBase, const char *symbol, HoleValue &out) {
  // ---- operands read straight out of the instruction ----
  if (strcmp(symbol, "jit_imm_code_point") == 0) {
    switch (inst->op) {
    case OpCode::Char:
      out.imm = static_cast<uint32_t>(as<CharIns>(inst).getCodePoint());
      return true;
    case OpCode::IChar:
      out.imm = static_cast<uint32_t>(as<ICharIns>(inst).getCodePoint());
      return true;
    case OpCode::LBChar:
      out.imm = static_cast<uint32_t>(as<LBCharIns>(inst).getCodePoint());
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_ignore_case") == 0) {
    switch (inst->op) {
    case OpCode::LBChar:
      out.imm = as<LBCharIns>(inst).ignoreCase ? 1 : 0;
      return true;
    case OpCode::LBCharSet:
      out.imm = as<LBCharSetIns>(inst).ignoreCase ? 1 : 0;
      return true;
    case OpCode::LBBackRef:
      out.imm = as<LBBackRefIns>(inst).ignoreCase ? 1 : 0;
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_dot_all") == 0) {
    if (inst->op != OpCode::LBAny) {
      return false;
    }
    out.imm = as<LBAnyIns>(inst).dotAll ? 1 : 0;
    return true;
  }
  if (strcmp(symbol, "jit_imm_multiline") == 0) {
    switch (inst->op) {
    case OpCode::Start:
      out.imm = as<StartIns>(inst).multiline ? 1 : 0;
      return true;
    case OpCode::End:
      out.imm = as<EndIns>(inst).multiline ? 1 : 0;
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_invert") == 0) {
    switch (inst->op) {
    case OpCode::Word:
      out.imm = as<WordIns>(inst).invert ? 1 : 0;
      return true;
    case OpCode::IWord:
      out.imm = as<IWordIns>(inst).invert ? 1 : 0;
      return true;
    case OpCode::CharSet:
      out.imm = as<CharSetIns>(inst).invert ? 1 : 0;
      return true;
    case OpCode::ICharSet:
      out.imm = as<ICharSetIns>(inst).invert ? 1 : 0;
      return true;
    case OpCode::LBCharSet:
      out.imm = as<LBCharSetIns>(inst).invert ? 1 : 0;
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_matcher_index") == 0) {
    switch (inst->op) {
    case OpCode::CharSet:
      out.imm = as<CharSetIns>(inst).getMatcherIndex();
      return true;
    case OpCode::ICharSet:
      out.imm = as<ICharSetIns>(inst).getMatcherIndex();
      return true;
    case OpCode::LBCharSet:
      out.imm = as<LBCharSetIns>(inst).getMatcherIndex();
      return true;
    case OpCode::String:
      out.imm = as<StringIns>(inst).getIndex();
      return true;
    case OpCode::LBString:
      out.imm = as<LBStringIns>(inst).getIndex();
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_capture_index") == 0) {
    switch (inst->op) {
    case OpCode::BeginCapture:
      out.imm = as<BeginCaptureIns>(inst).getCaptureIndex();
      return true;
    case OpCode::EndCapture:
      out.imm = as<EndCaptureIns>(inst).getCaptureIndex();
      return true;
    case OpCode::LBEndCapture:
      out.imm = as<LBEndCaptureIns>(inst).getCaptureIndex();
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_first_index") == 0) {
    if (inst->op != OpCode::ResetCaptures) {
      return false;
    }
    out.imm = as<ResetCapturesIns>(inst).getFirstIndex();
    return true;
  }
  if (strcmp(symbol, "jit_imm_last_index") == 0) {
    if (inst->op != OpCode::ResetCaptures) {
      return false;
    }
    out.imm = as<ResetCapturesIns>(inst).getLastIndex();
    return true;
  }
  if (strcmp(symbol, "jit_imm_ref_index") == 0) {
    switch (inst->op) {
    case OpCode::BackRef:
      out.imm = as<BackRefIns>(inst).getRefIndex();
      return true;
    case OpCode::IBackRef:
      out.imm = as<IBackRefIns>(inst).getRefIndex();
      return true;
    case OpCode::LBBackRef:
      out.imm = as<LBBackRefIns>(inst).getRefIndex();
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_named") == 0) {
    switch (inst->op) {
    case OpCode::BackRef:
      out.imm = as<BackRefIns>(inst).named ? 1 : 0;
      return true;
    case OpCode::IBackRef:
      out.imm = as<IBackRefIns>(inst).named ? 1 : 0;
      return true;
    case OpCode::LBBackRef:
      out.imm = as<LBBackRefIns>(inst).named ? 1 : 0;
      return true;
    default:
      return false;
    }
  }
  if (strcmp(symbol, "jit_imm_negate") == 0) {
    if (inst->op != OpCode::BeginLookAround) {
      return false;
    }
    out.imm = as<BeginLookAroundIns>(inst).negate ? 1 : 0;
    return true;
  }
  if (strcmp(symbol, "jit_imm_lookaround_target") == 0) {
    if (inst->op != OpCode::BeginLookAround) {
      return false;
    }
    out.imm = as<BeginLookAroundIns>(inst).getTarget();
    return true;
  }

  // ---- jump operands ----
  //
  // a target that goes through the backtrack stack keeps being an offset (the trampoline carries it
  // back into the code), while a target the compiler already knows is patched as a block address.
  if (strcmp(symbol, "jit_imm_alt_second") == 0) {
    if (inst->op != OpCode::Alt) {
      return false;
    }
    out.imm = as<AltIns>(inst).getSecond();
    return true;
  }
  if (strcmp(symbol, "jit_target_jump") == 0) {
    if (inst->op != OpCode::Jump) {
      return false;
    }
    out.imm = as<JumpIns>(inst).getTarget();
    out.isCodeAddress = true;
    return true;
  }

  // ---- loop operands ----
  //
  // both loop stencils describe the same loop, so they share every operand: `EndLoop` only stores
  // the distance back to its `BeginLoop` (see `resolveLoopInst`).
  if (strncmp(symbol, "jit_imm_loop_", 13) == 0 || strcmp(symbol, "jit_target_loop_body") == 0 ||
      strcmp(symbol, "jit_target_loop_outer") == 0) {
    if (inst->op != OpCode::BeginLoop && inst->op != OpCode::EndLoop) {
      return false;
    }
    const Inst *loopInst = resolveLoopInst(inst, instSeqBase);
    const auto &loop = as<BeginLoopIns>(loopInst);
    if (strcmp(symbol, "jit_imm_loop_index") == 0) {
      out.imm = loop.getLoopIndex();
      return true;
    }
    if (strcmp(symbol, "jit_imm_loop_min") == 0) {
      out.imm = loop.getMin();
      return true;
    }
    if (strcmp(symbol, "jit_imm_loop_max") == 0) {
      out.imm = loop.getMax();
      return true;
    }
    if (strcmp(symbol, "jit_imm_loop_greedy") == 0) {
      out.imm = loop.greedy ? 1 : 0;
      return true;
    }
    // the loop step is handed the offset of the `BeginLoop` so that a backtrack into the loop can
    // re-enter the loop step (not the `BeginLoop` stencil, which would reset the counter)
    if (strcmp(symbol, "jit_imm_loop_begin") == 0) {
      out.imm = static_cast<uint64_t>(reinterpret_cast<const char *>(loopInst) - instSeqBase);
      return true;
    }
    if (strcmp(symbol, "jit_imm_loop_outer") == 0) {
      out.imm = loop.getOuter();
      return true;
    }
    if (strcmp(symbol, "jit_target_loop_body") == 0) {
      // the body is the instruction right after the `BeginLoop`
      out.imm = static_cast<uint64_t>(reinterpret_cast<const char *>(loopInst) - instSeqBase) +
                getInstSize(loopInst);
      out.isCodeAddress = true;
      return true;
    }
    if (strcmp(symbol, "jit_target_loop_outer") == 0) {
      out.imm = loop.getOuter();
      out.isCodeAddress = true;
      return true;
    }
    return false;
  }

  // ---- radix operands ----
  //
  // `Prepare*Radix` is immediately followed by the radix instruction, which holds every operand and
  // the two distances, so both entry points resolve through it.
  const bool isPrepare = inst->op == OpCode::PrepareRadix || inst->op == OpCode::PrepareLBRadix;
  const bool isRadixBody = inst->op == OpCode::RadixOrEmoji || inst->op == OpCode::LBRadixOrEmoji;
  if (isPrepare || isRadixBody) {
    const Inst *radixInst = isPrepare
                                ? reinterpret_cast<const Inst *>(
                                      reinterpret_cast<const char *>(inst) + sizeof(PrepareRadixIns))
                                : inst;
    const bool backward = radixInst->op == OpCode::LBRadixOrEmoji;
    if (!backward && radixInst->op != OpCode::RadixOrEmoji) {
      return false;
    }
    const auto index = backward ? as<LBRadixOrEmojiIns>(radixInst).getIndex()
                                : as<RadixOrEmojiIns>(radixInst).getIndex();
    const auto emoji = backward ? toUnderlying(as<LBRadixOrEmojiIns>(radixInst).emoji)
                                : toUnderlying(as<RadixOrEmojiIns>(radixInst).emoji);
    const auto hasRadix = backward ? as<LBRadixOrEmojiIns>(radixInst).hasRadix
                                   : as<RadixOrEmojiIns>(radixInst).hasRadix;
    const auto nextOffset = backward ? as<LBRadixOrEmojiIns>(radixInst).nextOffset
                                     : as<RadixOrEmojiIns>(radixInst).nextOffset;
    if (strcmp(symbol, "jit_imm_radix_index") == 0) {
      out.imm = index;
      return true;
    }
    if (strcmp(symbol, "jit_imm_radix_has") == 0) {
      out.imm = hasRadix ? 1 : 0;
      return true;
    }
    if (strcmp(symbol, "jit_imm_radix_emoji") == 0) {
      out.imm = emoji;
      return true;
    }
    if (strcmp(symbol, "jit_imm_radix_next") == 0) {
      out.imm = nextOffset ? 1 : 0;
      return true;
    }
    // a backtrack into the radix re-runs the radix instruction, so both entry points point at it
    if (strcmp(symbol, "jit_imm_radix_offset") == 0) {
      out.imm = static_cast<uint64_t>(reinterpret_cast<const char *>(radixInst) - instSeqBase);
      return true;
    }
    if (strcmp(symbol, "jit_target_radix_match") == 0) {
      out.imm = static_cast<uint64_t>(reinterpret_cast<const char *>(radixInst) - instSeqBase) +
                sizeof(RadixOrEmojiIns) + nextOffset;
      out.isCodeAddress = true;
      return true;
    }
    // the chain edge of `Prepare*Radix` skips the radix instruction itself
    if (strcmp(symbol, "jit_target_radix_next") == 0) {
      if (!isPrepare) {
        return false;
      }
      out.imm = static_cast<uint64_t>(reinterpret_cast<const char *>(radixInst) - instSeqBase) +
                sizeof(RadixOrEmojiIns);
      out.isCodeAddress = true;
      return true;
    }
    return false;
  }

  return false;
}

bool compileTo(JitCode &code, const Regex &regex) {
  const auto &instSeq = regex.getInstSeq();
  const char *const instSeqBase = reinterpret_cast<const char *>(instSeq.data());
  const size_t instSeqSize = instSeq.size();

  // pass 1: walk the bytecode and lay out one block per instruction
  std::vector<Instruction> instructions;
  std::map<size_t, size_t> blockByOffset;
  size_t cursor = 0;
  size_t codeSize = 0;
  while (cursor < instSeqSize) {
    const auto *inst = reinterpret_cast<const Inst *>(instSeqBase + cursor);
    const auto op = static_cast<size_t>(inst->op);
    if (op >= std::size(OPCODE_NAMES)) {
      return false; // unknown opcode
    }
    const auto *stencil = findStencil(OPCODE_NAMES[op]);
    if (!stencil) {
      return false; // no stencil for this instruction: fall back to the interpreter
    }
    const size_t size = getInstSize(inst);
    if (size == 0) {
      return false;
    }

    Instruction entry;
    entry.offset = cursor;
    entry.stencil = stencil;
    entry.blockOffset = alignUp(codeSize, 16);
    codeSize = entry.blockOffset + stencil->size;
    instructions.push_back(entry);
    blockByOffset[cursor] = entry.blockOffset;
    cursor += size;
  }
  if (instructions.empty()) {
    return false;
  }

  // the local constant pools the stencils refer to (they are copied with the code)
  std::map<std::string, size_t> dataOffsets;
  for (const auto &section : STENCIL_DATA_SECTIONS) {
    codeSize = alignUp(codeSize, section.alignment ? section.alignment : 1);
    dataOffsets[section.name] = codeSize;
    codeSize += section.size;
  }

  // pass 2: copy the stencils and patch the holes
  if (!code.buffer.allocate(codeSize)) {
    return false;
  }
  auto *const base = code.buffer.data();
  for (const auto &section : STENCIL_DATA_SECTIONS) {
    memcpy(base + dataOffsets[section.name], section.data, section.size);
  }

  // a code address is the block of the instruction at that bytecode offset
  const auto resolveBlock = [&](const size_t byteOffset) -> const uint8_t * {
    auto iter = blockByOffset.find(byteOffset);
    return iter == blockByOffset.end() ? nullptr : base + iter->second;
  };

  for (size_t i = 0; i < instructions.size(); i++) {
    const auto &entry = instructions[i];
    const auto *inst = reinterpret_cast<const Inst *>(instSeqBase + entry.offset);
    memcpy(base + entry.blockOffset, entry.stencil->code, entry.stencil->size);
    const uint8_t *successor =
        i + 1 < instructions.size() ? base + instructions[i + 1].blockOffset : nullptr;

    for (size_t h = 0; h < entry.stencil->holeCount; h++) {
      const auto &hole = entry.stencil->holes[h];
      const uint8_t *address = nullptr;
      if (hole.isSection) {
        auto iter = dataOffsets.find(hole.symbol);
        if (iter == dataOffsets.end()) {
          return false;
        }
        address = base + iter->second + hole.addend;
      } else if (strcmp(hole.symbol, "jit_next") == 0) {
        if (!successor) {
          return false; // nothing to continue with: fall back to the interpreter
        }
        address = successor;
      } else if (strcmp(hole.symbol, "jit_backtrack") == 0) {
        address = reinterpret_cast<const uint8_t *>(&jit_backtrack);
      } else {
        HoleValue resolved;
        if (resolveHole(inst, instSeqBase, hole.symbol, resolved)) {
          if (resolved.isCodeAddress) {
            address = resolveBlock(resolved.imm);
            if (!address) {
              return false; // a target outside the sequence: fall back to the interpreter
            }
          } else {
            // an operand: written as a 64-bit value into the `movabs` the compiler emitted
            memcpy(base + entry.blockOffset + hole.offset, &resolved.imm, sizeof(resolved.imm));
            continue;
          }
        } else {
          // a call into the runtime: bound through the lookup table of `stencil_runtime.cpp`, which
          // is the same code the interpreter runs
          address = static_cast<const uint8_t *>(lookupJitRuntimeSymbol(hole.symbol));
          if (!address) {
            return false; // an unknown helper: fall back to the interpreter
          }
        }
      }
      memcpy(base + entry.blockOffset + hole.offset, &address, sizeof(address));
    }
  }

  code.codeOffsets.assign(instSeqSize, 0);
  for (const auto &entry : instructions) {
    code.codeOffsets[entry.offset] = static_cast<uint32_t>(entry.blockOffset);
  }
  return true;
}

} // namespace

std::shared_ptr<JitCode> compile(const Regex &regex) {
  auto code = std::make_shared<JitCode>();
  if (!compileTo(*code, regex)) {
    return nullptr;
  }
  if (!code->buffer.makeExecutable()) {
    return nullptr;
  }
  return code;
}

} // namespace arsh::regex::jit
