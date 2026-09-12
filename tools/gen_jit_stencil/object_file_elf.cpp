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

#include "object_file.h"

#include <elf.h>

namespace arsh::gen_stencil {

namespace {

/**
 * the stencils are emitted into their own section with `-ffunction-sections`. clang names it
 * `.ltext.<mangled>` and gcc names it `.text.<mangled>`; the trailing dot excludes the plain
 * `.text` section so unrelated functions are skipped as well. `-fno-pic` additionally keeps the
 * stencils out of the section groups that would carry a `.rela` per group.
 */
bool isStencilSection(const char *name) {
  return StringRef(name).startsWith(".ltext") || StringRef(name).startsWith(".text.");
}

/**
 * local read-only data referenced through absolute relocations (constant pools and string literals).
 * these are copied into the code buffer, since the address is patched into the `movabs` immediate.
 */
bool isDataSection(const char *name) {
  return StringRef(name).startsWith(".lrodata") || StringRef(name).startsWith(".rodata") ||
         StringRef(name).startsWith(".ldata") || StringRef(name).startsWith(".data.rel.ro");
}

/**
 * demangle `_ZN4arsh5regex3jit12stencil_CharERNS1_10JitContextEPKNS0_4InstE` into `Char`.
 *
 * the tool assumes the Itanium ABI mangling of a free function named `stencil_<opcode>` in
 * `arsh::regex::jit`, which is exactly what `JIT_STENCIL_DEF` produces. the fixed prefix and
 * suffix are checked, so a mismatch is reported instead of silently dropping a stencil.
 */
bool extractOpcode(const StringRef mangled, std::string &out) {
  constexpr const char *PREFIX = "_ZN4arsh5regex3jit";
  constexpr const char *NAME = "stencil_";
  constexpr const char *SUFFIX = "ERNS1_10JitContextEPKNS0_4InstE";
  if (!mangled.startsWith(PREFIX) || !mangled.endsWith(SUFFIX)) {
    return false;
  }
  // the mangled name is `_ZN4arsh5regex3jit<len>stencil_<opcode>E<signature>`, where `<len>` is
  // the decimal length of `stencil_<opcode>`
  auto rest = mangled.substr(StringRef(PREFIX).size());
  if (rest.empty() || rest[0] < '0' || rest[0] > '9') {
    return false;
  }
  size_t digits = 0;
  unsigned int nameLen = 0;
  while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9') {
    nameLen = nameLen * 10 + static_cast<unsigned int>(rest[digits] - '0');
    digits++;
  }
  rest = rest.substr(digits);
  if (rest.size() < nameLen || !rest.startsWith(NAME)) {
    return false;
  }
  out = rest.substr(StringRef(NAME).size(), nameLen - StringRef(NAME).size()).toString();
  return !out.empty() && rest[nameLen] == 'E';
}

} // namespace

bool parseStencilObject(const StringRef path, StencilObject &out, std::string &error) {
  std::vector<char> data;
  if (!readFile(path, data, error)) {
    return false;
  }

  if (data.size() < sizeof(Elf64_Ehdr)) {
    error = "not an ELF64 file";
    return false;
  }
  const auto *ehdr = reinterpret_cast<const Elf64_Ehdr *>(data.data());
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    error = "not an ELF64 file";
    return false;
  }
  if (ehdr->e_machine != EM_X86_64) {
    error = "unsupported machine: only x86-64 (ELF64) is supported";
    return false;
  }
  if (ehdr->e_type != ET_REL) {
    error = "not a relocatable object file";
    return false;
  }

  const auto *shdrs = reinterpret_cast<const Elf64_Shdr *>(data.data() + ehdr->e_shoff);
  const auto sectionName = [&](const Elf64_Shdr &shdr) -> const char * {
    return data.data() + shdrs[ehdr->e_shstrndx].sh_offset + shdr.sh_name;
  };

  const Elf64_Shdr *symtab = nullptr;
  for (unsigned int i = 0; i < ehdr->e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) {
      symtab = &shdrs[i];
      break;
    }
  }
  if (!symtab) {
    error = "no symbol table";
    return false;
  }
  const auto *strtab = &shdrs[symtab->sh_link];
  const auto *symBegin = reinterpret_cast<const Elf64_Sym *>(data.data() + symtab->sh_offset);
  const auto symCount = symtab->sh_size / sizeof(Elf64_Sym);
  const auto symbolName = [&](const Elf64_Sym &sym) -> const char * {
    return data.data() + strtab->sh_offset + sym.st_name;
  };

  // the section index -> unique name map for the local constant pools we actually carry. a section
  // is named `<object>#<section>` so that several stencil translation units can be merged without
  // colliding on the generic `.lrodata.cst4` names, and only referenced sections are kept.
  std::map<unsigned int, std::string> carriedSections;
  const auto uniqueName = [&](const unsigned int sectionIndex) -> std::string {
    if (auto iter = carriedSections.find(sectionIndex); iter != carriedSections.end()) {
      return iter->second;
    }
    std::string name = path.toString();
    if (auto pos = name.find_last_of('/'); pos != std::string::npos) { // keep the output stable
      name = name.substr(pos + 1);
    }
    name += '#';
    name += sectionName(shdrs[sectionIndex]);
    carriedSections[sectionIndex] = name;
    return name;
  };

  for (unsigned int i = 0; i < ehdr->e_shnum; i++) {
    const auto &codeShdr = shdrs[i];
    if (codeShdr.sh_type != SHT_PROGBITS || !isStencilSection(sectionName(codeShdr))) {
      continue;
    }
    const auto *codeBegin = reinterpret_cast<const uint8_t *>(data.data() + codeShdr.sh_offset);

    // a translation unit may split the relocations that apply to this code section across several
    // `.rela.*` sections, so every matching one is considered.
    for (unsigned int j = 0; j < ehdr->e_shnum; j++) {
      if (shdrs[j].sh_type == SHT_REL && shdrs[j].sh_info == i) {
        error = "unexpected relocation format (SHT_REL is not supported)";
        return false;
      }
    }

    for (unsigned int j = 0; j < symCount; j++) {
      const auto &sym = symBegin[j];
      if (sym.st_shndx != i || ELF64_ST_TYPE(sym.st_info) != STT_FUNC || sym.st_size == 0) {
        continue;
      }
      std::string opcode;
      if (!extractOpcode(symbolName(sym), opcode)) {
        continue;
      }

      StencilFunction fn;
      fn.opcode = std::move(opcode);
      fn.code.assign(codeBegin + sym.st_value, codeBegin + sym.st_value + sym.st_size);

      for (unsigned int k = 0; k < ehdr->e_shnum; k++) {
        const auto &relaShdr = shdrs[k];
        if (relaShdr.sh_info != i || relaShdr.sh_type != SHT_RELA) {
          continue;
        }
        const auto *relaBegin =
            reinterpret_cast<const Elf64_Rela *>(data.data() + relaShdr.sh_offset);
        const auto count = relaShdr.sh_size / sizeof(Elf64_Rela);
        for (size_t m = 0; m < count; m++) {
          const auto &rela = relaBegin[m];
          if (rela.r_offset < sym.st_value || rela.r_offset >= sym.st_value + sym.st_size) {
            continue;
          }
          if (ELF64_R_TYPE(rela.r_info) != R_X86_64_64) {
            error = "unexpected relocation type " +
                    std::to_string(ELF64_R_TYPE(rela.r_info)) + " in stencil " + fn.opcode;
            return false;
          }
          const auto &target = symBegin[ELF64_R_SYM(rela.r_info)];
          StencilHole hole;
          hole.offset = static_cast<uint32_t>(rela.r_offset - sym.st_value);
          hole.addend = rela.r_addend;
          if (target.st_shndx == SHN_UNDEF) {
            hole.symbol = symbolName(target);
          } else {
            // a local constant lives at `st_value` within its section, so it has to be folded into
            // the addend: the patched value is `sectionBase + (addend + st_value)`.
            hole.symbol = uniqueName(target.st_shndx);
            hole.addend += static_cast<int64_t>(target.st_value);
            hole.isSection = true;
          }
          fn.holes.push_back(std::move(hole));
        }
      }
      out.functions.push_back(std::move(fn));
    }
  }

  // now materialize the referenced local constant pools
  for (const auto &[sectionIndex, name] : carriedSections) {
    const auto &shdr = shdrs[sectionIndex];
    // the section is copied verbatim, so any reference it makes to another section would be
    // silently wrong (its relative distance changes). constant pools are self-contained, so a
    // relocation here means we would miscompile: fail loudly instead.
    for (unsigned int k = 0; k < ehdr->e_shnum; k++) {
      if (shdrs[k].sh_type == SHT_RELA && shdrs[k].sh_info == sectionIndex) {
        error = "carried data section " + name +
                " has relocations (unsupported: it may be a jump table referencing code)";
        return false;
      }
    }
    if (!isDataSection(sectionName(shdr))) {
      error = "referenced local section " + name + " is not a supported data section";
      return false;
    }
    DataSection section;
    section.alignment = shdr.sh_addralign ? shdr.sh_addralign : 1;
    const auto *begin = reinterpret_cast<const uint8_t *>(data.data() + shdr.sh_offset);
    section.data.assign(begin, begin + shdr.sh_size);
    out.dataSections[name] = std::move(section);
  }

  if (out.functions.empty()) {
    error = "no stencil function found (did the stencils get inlined?)";
    return false;
  }
  return true;
}

} // namespace arsh::gen_stencil
