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

/**
 * Adapter which registers the Hermes regex engine (the one used by the Hermes
 * JavaScript engine) into the `rust-leipzig/regex-performance` benchmark tool.
 *
 * Hermes splits the engine into a compiler (`hermes::regex::Regex`, templated on the character
 * traits) and a bytecode executor (`hermes::regex::searchWithBytecode`). Like the VM itself, the
 * pattern is compiled once with `UTF16RegexTraits` and then executed with `ASCIIRegexTraits` when
 * the subject is 7-bit ASCII, or `UTF16RegexTraits` otherwise.
 *
 * See `hermes_find_all` for the benchmark entry point required by the harness (see main.h).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "main.h"
#include "memory_tracker.h"

#include <hermes/Regex/Executor.h>
#include <hermes/Regex/Regex.h>
#include <hermes/Regex/RegexTraits.h>

#include <llvh/ADT/ArrayRef.h>

using namespace hermes::regex;

static bool isAllASCII(const char *subject, int subjectLen) {
    for (int i = 0; i < subjectLen; i++) {
        if (static_cast<unsigned char>(subject[i]) >= 0x80) {
            return false;
        }
    }
    return true;
}

/* decode a UTF-8 string into UTF-16 (surrogate pairs are kept as two code units) */
static std::u16string toUTF16(const char *subject, int subjectLen) {
    std::u16string out;
    out.reserve(static_cast<size_t>(subjectLen));
    int i = 0;
    while (i < subjectLen) {
        const unsigned char b = static_cast<unsigned char>(subject[i]);
        unsigned int cp = b;
        int size = 1;
        if (b >= 0xF0) {
            cp = b & 0x07;
            size = 4;
        } else if (b >= 0xE0) {
            cp = b & 0x0F;
            size = 3;
        } else if (b >= 0xC0) {
            cp = b & 0x1F;
            size = 2;
        }
        if (i + size > subjectLen) { /* truncated sequence, treat as a single byte */
            cp = b;
            size = 1;
        } else {
            for (int k = 1; k < size; k++) {
                cp = (cp << 6) | (static_cast<unsigned char>(subject[i + k]) & 0x3F);
            }
        }
        i += size;
        if (cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

/**
 * Some benchmark subjects start with an inline `(?i)` modifier while others pass no flag at all.
 * Hermes only accepts flags as a separate string, so translate the leading `(?ims)` prefix (if
 * any) to the equivalent flags and return the pattern without it. Unicode (`u`) mode is always
 * enabled because the benchmark patterns use `\p{...}` and match in Unicode mode.
 */
static std::u16string splitInlineFlags(const char *pattern, std::u16string &flags) {
    flags = u"u";
    const size_t len = strlen(pattern);
    if (len >= 4 && pattern[0] == '(' && pattern[1] == '?' && pattern[3] == ')' &&
        strchr("ims", pattern[2]) != nullptr) {
        flags.push_back(static_cast<char16_t>(pattern[2]));
        return toUTF16(pattern + 4, static_cast<int>(len - 4));
    }
    return toUTF16(pattern, static_cast<int>(len));
}

/* advance `index` by one code point (a surrogate pair counts as one) */
static uint32_t nextCodePoint(const std::u16string &text, uint32_t index) {
    const uint32_t len = static_cast<uint32_t>(text.size());
    if (index < len) {
        const char16_t c = text[index];
        if (c >= 0xD800 && c <= 0xDBFF && index + 1 < len) {
            return index + 2;
        }
    }
    return index + 1;
}

/**
 * Count all non-overlapping matches (emulating the global (g) flag) by repeatedly searching from
 * the end of the previous match. The engine itself searches forward from the given offset, so the
 * input is advanced by this function.
 */
static long long countUTF16Matches(const std::vector<uint8_t> &bytecode, const std::u16string &text) {
    const uint32_t len = static_cast<uint32_t>(text.size());
    std::vector<CapturedRange> captures;
    long long found = 0;
    for (uint32_t lastIndex = 0; lastIndex <= len;) {
        const auto result = searchWithBytecode(
                bytecode, text.data(), lastIndex, len, &captures, constants::matchDefault);
        if (result == MatchRuntimeResult::StackOverflow) {
            return -1;
        }
        if (result != MatchRuntimeResult::Match) {
            break;
        }
        found++;
        const uint32_t start = captures[0].start;
        const uint32_t end = captures[0].end;
        lastIndex = end;
        if (end == start) { /* empty match, avoid infinite loop */
            if (end >= len) {
                break;
            }
            lastIndex = nextCodePoint(text, end);
        }
    }
    return found;
}

static long long countASCIIMatches(const std::vector<uint8_t> &bytecode, const char *subject,
                                   uint32_t subjectLen) {
    std::vector<CapturedRange> captures;
    long long found = 0;
    for (uint32_t lastIndex = 0; lastIndex <= subjectLen;) {
        const auto result = searchWithBytecode(bytecode, subject, lastIndex, subjectLen, &captures,
                                               constants::matchDefault | constants::matchInputAllAscii);
        if (result == MatchRuntimeResult::StackOverflow) {
            return -1;
        }
        if (result != MatchRuntimeResult::Match) {
            break;
        }
        found++;
        const uint32_t start = captures[0].start;
        const uint32_t end = captures[0].end;
        lastIndex = end;
        if (end == start) { /* empty match, avoid infinite loop */
            if (end >= subjectLen) {
                break;
            }
            lastIndex = end + 1;
        }
    }
    return found;
}

extern "C" int hermes_find_all(char *pattern, char *subject, int subject_len, int repeat,
                               struct result *res) {
    if (pattern == nullptr || subject == nullptr || subject_len < 0 || repeat <= 0 || res == nullptr) {
        return -1;
    }

    std::u16string flags;
    const auto pattern16 = splitInlineFlags(pattern, flags);
    memory_tracker_init();
    const size_t memBase = memory_tracker_live();
    Regex<UTF16RegexTraits> regex(llvh::ArrayRef<char16_t>(pattern16.data(), pattern16.size()),
                                  llvh::ArrayRef<char16_t>(flags.data(), flags.size()));
    if (!regex.valid()) {
        printf("Hermes compilation failed: %s (%s)\n", pattern,
               constants::messageForError(regex.getError()));
        return -1;
    }
    const auto bytecode = regex.compile();
    /* the compiled bytecode is what the engine keeps for execution */
    res->mem_instance = memory_tracker_live() - memBase;

    /* decode the subject once; the conversion is not part of the measured region */
    const bool ascii = isAllASCII(subject, subject_len);
    const std::u16string text16 = ascii ? std::u16string() : toUTF16(subject, subject_len);

    double *times = static_cast<double *>(calloc(static_cast<size_t>(repeat), sizeof(double)));
    if (!times) {
        return -1;
    }
    const int timesLen = repeat;

    memory_tracker_reset_peak();
    const size_t memScanBase = memory_tracker_live();

    long long found = 0;
    do {
        TIME_TYPE start, end;
        GET_TIME(start);
        found = ascii ? countASCIIMatches(bytecode, subject, static_cast<uint32_t>(subject_len))
                      : countUTF16Matches(bytecode, text16);
        GET_TIME(end);
        times[repeat - 1] = TIME_DIFF_IN_MS(start, end);
        if (found < 0) {
            free(times);
            return -1;
        }
    } while (--repeat > 0);

    res->mem_runtime = memory_tracker_peak() - memScanBase;
    res->matches = static_cast<int>(found);
    get_mean_and_derivation(times, static_cast<uint32_t>(timesLen), res);
    free(times);
    return 0;
}
