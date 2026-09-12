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

#ifndef ARSH_TOOLS_GEN_JIT_STENCIL_OBJECT_FILE_H
#define ARSH_TOOLS_GEN_JIT_STENCIL_OBJECT_FILE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "misc/string_ref.hpp"

namespace arsh::gen_stencil {

/**
 * a relocation site within an extracted stencil function.
 *
 * the copy-and-patch compiler overwrites 8 bytes at `offset` (relative to the start of the function
 * code) with an absolute address.
 */
struct StencilHole {
  uint32_t offset{0};
  std::string symbol; // referenced symbol name, or the section name for local data
  int64_t addend{0};
  bool isSection{false}; // true if `symbol` names a section (local constant pool)
};

/**
 * an extracted stencil function.
 */
struct StencilFunction {
  std::string opcode; // e.g. `Char` (derived from `stencil_Char`)
  std::vector<uint8_t> code;
  std::vector<StencilHole> holes;
};

/**
 * a local data section referenced by the stencils (e.g. a constant pool).
 */
struct DataSection {
  uint32_t alignment{1};
  std::vector<uint8_t> data;
};

struct StencilObject {
  std::vector<StencilFunction> functions;

  // referenced local data sections (name -> bytes). the compiler copies them into the code buffer.
  std::map<std::string, DataSection> dataSections;
};

/**
 * read a whole file into memory.
 */
bool readFile(const StringRef path, std::vector<char> &out, std::string &error);

/**
 * parse a stencil translation unit object file and extract the `stencil_*` functions.
 *
 * only ELF64 / EM_X86_64 is supported (see the JIT scope in the plan).
 *
 * @param path
 * @param out
 * @param error set on failure
 * @return
 */
bool parseStencilObject(const StringRef path, StencilObject &out, std::string &error);

} // namespace arsh::gen_stencil

#endif // ARSH_TOOLS_GEN_JIT_STENCIL_OBJECT_FILE_H
