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
 * Adapter which registers the QuickJS regexp engine (libregexp) into the
 * `rust-leipzig/regex-performance` benchmark tool.
 *
 * libregexp is the ECMAScript-compatible regexp engine used by QuickJS. It is a plain C library
 * (`libregexp.c` + `libunicode.c`) which requires the host to provide the three `lre_*` callbacks
 * below. See `quickjs_find_all` for the benchmark entry point required by the harness (see main.h).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "memory_tracker.h"

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

static const char *skipInlineFlags(const char *pattern, int *flags) {
    *flags = LRE_FLAG_UNICODE;
    if (strlen(pattern) >= 4 && pattern[0] == '(' && pattern[1] == '?' && pattern[3] == ')') {
        switch (pattern[2]) {
        case 'i':
            *flags |= LRE_FLAG_IGNORECASE;
            return pattern + 4;
        case 'm':
            *flags |= LRE_FLAG_MULTILINE;
            return pattern + 4;
        case 's':
            *flags |= LRE_FLAG_DOTALL;
            return pattern + 4;
        default:
            break;
        }
    }
    return pattern;
}

/* advance `index` (a byte offset into a UTF-8 string) by one code point */
static int nextCodePoint(const char *subject, int len, int index) {
    const unsigned char b = (unsigned char)subject[index];
    unsigned int size = 1;
    if (b >= 0xF0) {
        size = 4;
    } else if (b >= 0xE0) {
        size = 3;
    } else if (b >= 0xC0) {
        size = 2;
    }
    if (index + (int)size > len) {
        size = 1;
    }
    return index + (int)size;
}

static long long countMatches(const uint8_t *bc, const char *subject, int subjectLen) {
    const int allocCount = lre_get_alloc_count(bc);
    uint8_t **capture = NULL;
    if (allocCount > 0) {
        capture = (uint8_t **)malloc(sizeof(capture[0]) * allocCount);
        if (!capture) {
            return -1;
        }
    }
    const uint8_t *base = (const uint8_t *)subject;
    long long found = 0;
    for (int lastIndex = 0; lastIndex <= subjectLen;) {
        const int ret = lre_exec(capture, bc, base, lastIndex, subjectLen, 0, NULL);
        if (ret != 1) {
            break;
        }
        if (ret < 0) { /* LRE_RET_MEMORY_ERROR/TIMEOUT/BYTECODE_ERROR */
            free(capture);
            return -1;
        }
        const int start = (int)(capture[0] - base);
        const int end = (int)(capture[1] - base);
        found++;
        lastIndex = end;
        if (end == start) { /* empty match, avoid infinite loop */
            if (end >= subjectLen) {
                break;
            }
            lastIndex = nextCodePoint(subject, subjectLen, end);
        }
    }
    free(capture);
    return found;
}

extern int quickjs_find_all(char *pattern, char *subject, int subject_len, int repeat,
                            struct result *res) {
    TIME_TYPE start = 0, end = 0;
    long long found = 0;
    int flags = 0;
    char errMsg[256];
    int bytecodeLen = 0;
    uint8_t *bytecode = NULL;

    memory_tracker_init();
    const size_t memBase = memory_tracker_live();

    const char *pat = skipInlineFlags(pattern, &flags);
    errMsg[0] = '\0';
    bytecode = lre_compile(&bytecodeLen, errMsg, sizeof(errMsg), pat, strlen(pat), flags, NULL);
    if (!bytecode) {
        printf("QuickJS compilation failed: %s (%s)\n", pattern, errMsg);
        return -1;
    }
    /* the compiled bytecode is the regex instance (lre_exec keeps no per-instance state) */
    res->mem_instance = memory_tracker_live() - memBase;

    double *times = (double *)calloc((size_t)repeat, sizeof(double));
    if (!times) {
        lre_realloc(NULL, bytecode, 0);
        return -1;
    }
    const int timesLen = repeat;

    memory_tracker_reset_peak();
    const size_t memScanBase = memory_tracker_live();

    do {
        GET_TIME(start);
        found = countMatches(bytecode, subject, subject_len);
        GET_TIME(end);
        times[repeat - 1] = TIME_DIFF_IN_MS(start, end);
    } while (--repeat > 0);

    if (found < 0) {
        free(times);
        lre_realloc(NULL, bytecode, 0);
        return -1;
    }

    res->mem_runtime = memory_tracker_peak() - memScanBase;
    res->matches = (int)found;
    get_mean_and_derivation(times, (uint32_t)timesLen, res);
    free(times);
    lre_realloc(NULL, bytecode, 0);
    return 0;
}
