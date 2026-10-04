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

#ifndef ARSH_TEST_REGEX_REMATCH_UTF16_H
#define ARSH_TEST_REGEX_REMATCH_UTF16_H

#include <stdint.h>
#include <stdlib.h>

/**
 * Small UTF-8 <-> UTF-16 helpers shared by the Hermes and QuickJS adapters.
 *
 * Both engines operate on UTF-16 and report capture offsets in UTF-16 code units, while
 * `rematch` reports UTF-8 byte offsets (like redump). The input is decoded once and the
 * reported code unit offsets are converted back to byte offsets.
 *
 * A non-BMP code point is stored as a surrogate pair, so a surrogate pair counts as two
 * UTF-16 code units and four UTF-8 bytes.
 */

/* decode one UTF-8 code point (a malformed/truncated sequence is treated as one byte) */
static inline uint32_t rematch_utf8_next(const unsigned char *s, uint32_t len, uint32_t *index) {
    const uint32_t i = *index;
    const unsigned char b = s[i];
    uint32_t cp = b;
    uint32_t size = 1;
    if (b >= 0xF0) {
        cp = b & 0x07u;
        size = 4;
    } else if (b >= 0xE0) {
        cp = b & 0x0Fu;
        size = 3;
    } else if (b >= 0xC0) {
        cp = b & 0x1Fu;
        size = 2;
    }
    if (i + size > len) { /* truncated sequence, treat as a single byte */
        *index = i + 1;
        return b;
    }
    for (uint32_t k = 1; k < size; k++) {
        cp = (cp << 6) | (s[i + k] & 0x3Fu);
    }
    *index = i + size;
    return cp;
}

/* decode a UTF-8 string into a newly allocated UTF-16 buffer (surrogate pairs are kept) */
static inline uint16_t *rematch_utf8_to_utf16(const char *input, uint32_t inputLen,
                                              uint32_t *outLen) {
    uint16_t *out = (uint16_t *)malloc(sizeof(uint16_t) * (inputLen + 1));
    if (out == NULL) {
        *outLen = 0;
        return NULL;
    }
    const unsigned char *s = (const unsigned char *)input;
    uint32_t j = 0;
    for (uint32_t i = 0; i < inputLen;) {
        uint32_t cp = rematch_utf8_next(s, inputLen, &i);
        if (cp > 0xFFFF) {
            cp -= 0x10000;
            out[j++] = (uint16_t)(0xD800 + (cp >> 10));
            out[j++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            out[j++] = (uint16_t)cp;
        }
    }
    *outLen = j;
    return out;
}

/*
 * Convert a UTF-16 code unit index into a UTF-8 byte offset.
 *
 * A surrogate pair is always counted as a whole code point (4 bytes), even if `unitIndex`
 * points between the high and low surrogate. This keeps the byte ranges on code point
 * boundaries when an engine that matches code units (QuickJS in BMP mode) returns a
 * boundary inside a supplementary character, matching the offset redump reports.
 */
static inline uint32_t rematch_utf16_to_byte_offset(const uint16_t *units, uint32_t unitIndex) {
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < unitIndex;) {
        const uint32_t cu = units[i];
        if (cu >= 0xD800 && cu <= 0xDBFF) { /* surrogate pair, count as one code point */
            bytes += 4;
            i += 2;
        } else if (cu < 0x80) {
            bytes += 1;
            i += 1;
        } else if (cu < 0x800) {
            bytes += 2;
            i += 1;
        } else {
            bytes += 3;
            i += 1;
        }
    }
    return bytes;
}

#endif // ARSH_TEST_REGEX_REMATCH_UTF16_H
