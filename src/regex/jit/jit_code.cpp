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

#include "../instruction.h"
#include "../regex.h"
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
 * one instruction of the bytecode walk, with its compiled block.
 */
struct Instruction {
  size_t offset{0};      // byte offset within the instruction sequence
  size_t blockOffset{0}; // offset of the compiled block within the code buffer
  const StencilData *stencil{nullptr};
};

size_t alignUp(const size_t value, const size_t align) { return (value + align - 1) & ~(align - 1); }

/**
 * `jit_next` is patched with the successor block, so every stencil must be followed by another one.
 * the terminal `Match` does not continue, so it is the only instruction allowed to be last.
 */
bool isTerminal(const OpCode op) { return op == OpCode::Match; }

bool compileTo(JitCode &code, const Regex &regex) {
  const auto &instSeq = regex.getInstSeq();
  const auto *const begin = instSeq.data();
  const size_t instSeqSize = instSeq.size();

  // pass 1: walk the bytecode and lay out one block per instruction
  std::vector<Instruction> instructions;
  size_t cursor = 0;
  size_t codeSize = 0;
  while (cursor < instSeqSize) {
    const auto *inst = reinterpret_cast<const Inst *>(reinterpret_cast<const char *>(begin) + cursor);
    const auto op = static_cast<size_t>(inst->op);
    if (op >= std::size(OPCODE_NAMES)) {
      return false; // unknown opcode
    }
    const auto *stencil = findStencil(OPCODE_NAMES[op]);
    if (!stencil) {
      return false; // no stencil for this instruction: fall back to the interpreter
    }
    const size_t size = getInstSize(inst);
    if (!size) {
      return false;
    }

    Instruction entry;
    entry.offset = cursor;
    entry.stencil = stencil;
    entry.blockOffset = alignUp(codeSize, 16);
    codeSize = entry.blockOffset + stencil->size;
    instructions.push_back(entry);
    cursor += size;
  }
  if (instructions.empty()) {
    return false;
  }

  // every instruction but the last must have a successor block to patch `jit_next` with
  for (size_t i = 0; i + 1 < instructions.size(); i++) {
    const auto *inst =
        reinterpret_cast<const Inst *>(reinterpret_cast<const char *>(begin) + instructions[i].offset);
    if (isTerminal(inst->op)) {
      return false; // a terminal instruction in the middle of the sequence: unexpected
    }
  }

  // lay out the local constant pools after the code
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

  const auto *const trampoline = reinterpret_cast<const uint8_t *>(&jit_goto);
  for (size_t i = 0; i < instructions.size(); i++) {
    const auto &entry = instructions[i];
    memcpy(base + entry.blockOffset, entry.stencil->code, entry.stencil->size);

    const uint8_t *successor = nullptr;
    if (i + 1 < instructions.size()) {
      successor = base + instructions[i + 1].blockOffset;
    }

    for (size_t h = 0; h < entry.stencil->holeCount; h++) {
      const auto &hole = entry.stencil->holes[h];
      const uint8_t *value = nullptr;
      if (hole.isSection) {
        auto iter = dataOffsets.find(hole.symbol);
        if (iter == dataOffsets.end()) {
          return false;
        }
        value = base + iter->second + hole.addend;
      } else if (strcmp(hole.symbol, "jit_next") == 0) {
        if (!successor) {
          return false; // nothing to continue with: fall back to the interpreter
        }
        value = successor;
      } else if (strcmp(hole.symbol, "jit_goto") == 0) {
        value = trampoline;
      } else {
        value = static_cast<const uint8_t *>(lookupJitRuntimeSymbol(hole.symbol));
        if (!value) {
          return false; // unknown helper: fall back to the interpreter
        }
      }
      memcpy(base + entry.blockOffset + hole.offset, &value, sizeof(value));
    }
  }

  // the re-entry table: bytecode byte offset -> block offset within the buffer
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
