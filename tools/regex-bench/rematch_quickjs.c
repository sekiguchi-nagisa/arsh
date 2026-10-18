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
 * QuickJS regexp engine (libregexp) adapter for the standalone `rematch` tool (see
 * rematch_abi.h).
 *
 * libregexp is the ECMAScript-compatible regexp engine used by QuickJS. It is a plain C
 * library (`libregexp.c` + `libunicode.c`) which requires the host to provide the three
 * `lre_*` callbacks below. The subject is decoded to UTF-16 and executed with `cbuf_type = 1`
 * so `u` mode matches code points; in BMP mode a supplementary character can be matched by a
 * single `.`, matching redump's behavior (see the "non-bmp char even if bmp mode" case).
 *
 * As in tools/regex-bench/quickjs.c, a leading `(?ims)` prefix (accepted by arsh) is
 * translated into the corresponding libregexp flags because libregexp does not accept inline
 * flags.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rematch_abi.h"
#include "rematch_utf16.h"

#include <libregexp.h>

/* callbacks required by libregexp (see libregexp.h) */
bool lre_check_stack_overflow(void *opaque, size_t alloca_size) {
    (void)opaque;
    (void)alloca_size;
    return false;
}

int lre_check_timeout(void *opaque) {
    (void)opaque;
    return 0;
}

void *lre_realloc(void *opaque, void *ptr, size_t size) {
    (void)opaque;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

/*
 * Strip a leading `(?ims)` prefix (which arsh accepts) and fold its letters into `*flags`.
 * Returns a pointer to the remaining pattern and updates `*len`.
 */
static const char *splitInlineFlags(const char *pattern, int *len, int *flags) {
    if (*len >= 4 && pattern[0] == '(' && pattern[1] == '?' && pattern[3] == ')') {
        int extra = 0;
        switch (pattern[2]) {
        case 'i':
            extra = LRE_FLAG_IGNORECASE;
            break;
        case 'm':
            extra = LRE_FLAG_MULTILINE;
            break;
        case 's':
            extra = LRE_FLAG_DOTALL;
            break;
        default:
            return pattern;
        }
        *flags |= extra;
        *len -= 4;
        return pattern + 4;
    }
    return pattern;
}

int rematch_quickjs_match(const char *pattern, int patternLen,
                          const char *input, int inputLen,
                          int mode, int icase, int multiline, int dotall,
                          rematch_capture *captures, int maxCaptures,
                          char *err, int errSize) {
    char errorMsg[256];
    int flags;
    int patLen;
    int bytecodeLen;
    const char *pat;
    uint8_t *bytecode;
    uint16_t *textBuf;
    uint32_t textLen;
    int allocCount;
    int captureCount;
    uint8_t **capture;
    int ret;

    if (pattern == NULL || input == NULL || patternLen < 0 || inputLen < 0 ||
        maxCaptures <= 0 || captures == NULL) {
        if (err != NULL && errSize > 0) {
            snprintf(err, (size_t)errSize, "invalid argument");
        }
        return -1;
    }

    flags = LRE_FLAG_UNICODE;
    if (mode == REMATCH_MODE_UNICODE_SET) {
        flags |= LRE_FLAG_UNICODE_SETS;
    }
    if (icase) {
        flags |= LRE_FLAG_IGNORECASE;
    }
    if (multiline) {
        flags |= LRE_FLAG_MULTILINE;
    }
    if (dotall) {
        flags |= LRE_FLAG_DOTALL;
    }

    patLen = patternLen;
    pat = splitInlineFlags(pattern, &patLen, &flags);

    errorMsg[0] = '\0';
    bytecodeLen = 0;
    bytecode = lre_compile(&bytecodeLen, errorMsg, sizeof(errorMsg), pat, (size_t)patLen, flags,
                           NULL);
    if (bytecode == NULL) {
        if (err != NULL && errSize > 0) {
            snprintf(err, (size_t)errSize, "%s", errorMsg);
        }
        return -1;
    }

    textBuf = rematch_utf8_to_utf16(input, (uint32_t)inputLen, &textLen);
    if (textBuf == NULL) {
        lre_realloc(NULL, bytecode, 0);
        if (err != NULL && errSize > 0) {
            snprintf(err, (size_t)errSize, "out of memory");
        }
        return -2;
    }

    allocCount = lre_get_alloc_count(bytecode);
    captureCount = lre_get_capture_count(bytecode);
    capture = NULL;
    if (allocCount > 0) {
        capture = (uint8_t **)malloc(sizeof(capture[0]) * (size_t)allocCount);
    }

    ret = 0;
    if (allocCount > 0 && capture == NULL) {
        if (err != NULL && errSize > 0) {
            snprintf(err, (size_t)errSize, "out of memory");
        }
        ret = -2;
    } else {
        const int rc = lre_exec(capture, bytecode, (const uint8_t *)textBuf, 0, (int)textLen,
                                1 /* 16-bit code units */, NULL);
        if (rc < 0) {
            if (err != NULL && errSize > 0) {
                snprintf(err, (size_t)errSize, "regexp execution failed (%d)", rc);
            }
            ret = -2;
        } else if (rc == 1) {
            const int n = captureCount < maxCaptures ? captureCount : maxCaptures;
            for (int i = 0; i < n; i++) {
                const uint8_t *start = capture[2 * i];
                const uint8_t *end = capture[2 * i + 1];
                if (start == NULL || end == NULL) {
                    captures[i].offset = REMATCH_CAPTURE_UNSET;
                    captures[i].size = REMATCH_CAPTURE_UNSET;
                } else {
                    const uint32_t s = rematch_utf16_to_byte_offset(
                            textBuf, (uint32_t)(((const uint16_t *)start) - textBuf));
                    const uint32_t e = rematch_utf16_to_byte_offset(
                            textBuf, (uint32_t)(((const uint16_t *)end) - textBuf));
                    captures[i].offset = s;
                    captures[i].size = e - s;
                }
            }
            ret = n;
        }
    }

    free(capture);
    free(textBuf);
    lre_realloc(NULL, bytecode, 0);
    return ret;
}
