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
 * Adapter which registers PCRE2 into the `rust-leipzig/regex-performance` benchmark tool, both
 * without and with the JIT (`pcre2` and `pcre2-jit`).
 *
 * It replaces the adapter that ships with the harness so that the compiled instance and the scan
 * are measured with the shared heap tracker (see memory_tracker.h), like the other adapters used
 * by this directory. The patterns are compiled with `PCRE2_MULTILINE` like the upstream harness,
 * plus `PCRE2_UTF`: without it PCRE2 reads one byte per character, so the matches of a multi-byte
 * character are lost (e.g. `\p{Sm}` on `\u221E`). The 8-bit code unit width still applies, but
 * the subject is decoded as UTF-8.
 *
 * In UTF mode `pcre2_match` re-validates the subject on every call, and this adapter calls it once
 * per match, which makes the scan quadratic in the input size (`[a-zA-Z]+ing` on `3200.txt` grows
 * from ~0.6 s to ~285 s). The subject is therefore validated once up front and the matches are
 * scanned with `PCRE2_NO_UTF_CHECK`. That is safe here because the pointer is only ever advanced
 * by the byte length of whole code points, and the option is only set when the subject is valid
 * UTF-8; an invalid subject is scanned with the check enabled, so PCRE2 reports the
 * `PCRE2_ERROR_UTF8_ERR*` failure instead of running off into undefined behaviour.
 *
 * See `pcre2_find_all` / `pcre2_jit_find_all` for the benchmark entry points (see main.h).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "memory_tracker.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/* upper bound for the capture vector; the benchmark patterns have few groups */
#define PCRE2_MAX_CAPTURES 32

/* generous JIT stack so that the heavier patterns do not hit PCRE2_ERROR_JIT_STACKLIMIT */
#define PCRE2_JIT_STACK_SIZE (1024 * 1024)

/* the byte length of the UTF-8 sequence that starts at `s[i]` (1 for a malformed byte) */
static int utf8SequenceLength(const unsigned char *s, int len, int i) {
    const unsigned char b = s[i];
    int size = 1;
    if (b >= 0xF0) {
        size = 4;
    } else if (b >= 0xE0) {
        size = 3;
    } else if (b >= 0xC0) {
        size = 2;
    }
    return i + size > len ? 1 : size;
}

/**
 * Check that the subject is valid UTF-8, with the same rules PCRE2 applies (RFC 3629: no
 * overlong forms, no surrogate code points, nothing above U+10FFFF).
 */
static int isValidUTF8(const unsigned char *s, int len) {
    int i = 0;
    while (i < len) {
        const unsigned char b = s[i];
        unsigned char lo = 0x80, hi = 0xBF; /* allowed range of the second byte */
        int size;
        if (b < 0x80) {
            i++;
            continue;
        } else if (b < 0xC2) { /* a continuation byte, or an overlong 2-byte form */
            return 0;
        } else if (b < 0xE0) {
            size = 2;
        } else if (b < 0xF0) {
            size = 3;
            if (b == 0xE0) {
                lo = 0xA0; /* overlong */
            } else if (b == 0xED) {
                hi = 0x9F; /* surrogate */
            }
        } else if (b < 0xF5) {
            size = 4;
            if (b == 0xF0) {
                lo = 0x90; /* overlong */
            } else if (b == 0xF4) {
                hi = 0x8F; /* above U+10FFFF */
            }
        } else {
            return 0;
        }
        if (i + size > len || s[i + 1] < lo || s[i + 1] > hi) {
            return 0;
        }
        for (int k = 2; k < size; k++) {
            if ((s[i + k] & 0xC0) != 0x80) {
                return 0;
            }
        }
        i += size;
    }
    return 1;
}

/**
 * Scan the whole input once, counting all non-overlapping matches (emulating the global (g)
 * flag) by repeatedly matching from the end of the previous match. The pattern is compiled with
 * `PCRE2_UTF`, so the input is advanced by one code point on an empty match (never into the
 * middle of a multi-byte character).
 *
 * `matchOpts` is `PCRE2_NO_UTF_CHECK` when the subject has already been validated (see
 * isValidUTF8) and 0 otherwise. A subject that is not valid UTF-8 is only matched by the
 * interpreter, so that it is reported as an error instead of hitting the unchecked JIT path.
 */
static int scanAll(pcre2_code *re, pcre2_match_data *match_data, pcre2_match_context *match_ctx,
                   int useJit, uint32_t matchOpts, const char *subject, int subjectLen,
                   long long *found) {
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    const char *ptr = subject;
    int len = subjectLen;
    long long count = 0;

    for (;;) {
        int rc;
        if (useJit && matchOpts != 0) {
            rc = pcre2_jit_match(re, (PCRE2_SPTR)ptr, (PCRE2_SIZE)len, 0, matchOpts, match_data,
                                 match_ctx);
        } else {
            rc = pcre2_match(re, (PCRE2_SPTR)ptr, (PCRE2_SIZE)len, 0, matchOpts, match_data,
                             match_ctx);
        }
        if (rc == PCRE2_ERROR_NOMATCH) {
            break;
        }
        if (rc < 0) {
            fprintf(stderr, "PCRE2 matching failed with: %d\n", rc);
            return -1;
        }
        count++;

        const PCRE2_SIZE start = ovector[0];
        const PCRE2_SIZE end = ovector[1];
        ptr += end;
        len -= (int)end;
        if (end == start) { /* empty match, avoid infinite loop */
            if (len <= 0) {
                break;
            }
            const int size = utf8SequenceLength((const unsigned char *)ptr, len, 0);
            ptr += size;
            len -= size;
        }
    }
    *found = count;
    return 0;
}

static int pcre2FindAll(char *pattern, char *subject, int subject_len, int repeat, int useJit,
                        struct result *res) {
    TIME_TYPE start = 0, end = 0;
    int errCode = 0;
    PCRE2_SIZE errOffset = 0;
    pcre2_jit_stack *stack = NULL;

    memory_tracker_init();
    const size_t memBase = memory_tracker_live();

    pcre2_compile_context *compCtx = pcre2_compile_context_create(NULL);
    if (!compCtx) {
        printf("PCRE2 cannot allocate compile context\n");
        return -1;
    }
    pcre2_set_newline(compCtx, PCRE2_NEWLINE_ANYCRLF);
    pcre2_code *re = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
                                   PCRE2_MULTILINE | PCRE2_UTF, &errCode, &errOffset, compCtx);
    pcre2_compile_context_free(compCtx);
    if (!re) {
        printf("PCRE2 compilation failed at offset %d: [%d]\n", (int)errOffset, errCode);
        return -1;
    }
    /* the compiled pattern plus, for the JIT variant, the generated code */
    res->mem_instance = memory_tracker_live() - memBase;

    pcre2_match_context *matchCtx = pcre2_match_context_create(NULL);
    if (!matchCtx) {
        printf("PCRE2 cannot allocate match context\n");
        pcre2_code_free(re);
        return -1;
    }
    if (useJit) {
        if (pcre2_jit_compile(re, PCRE2_JIT_COMPLETE) != 0) {
            printf("PCRE2 JIT compilation failed\n");
            pcre2_match_context_free(matchCtx);
            pcre2_code_free(re);
            return -1;
        }
        stack = pcre2_jit_stack_create(PCRE2_JIT_STACK_SIZE, PCRE2_JIT_STACK_SIZE, NULL);
        if (!stack) {
            printf("PCRE2 cannot allocate JIT stack\n");
            pcre2_match_context_free(matchCtx);
            pcre2_code_free(re);
            return -1;
        }
        pcre2_jit_stack_assign(matchCtx, NULL, stack);
    }

    pcre2_match_data *matchData = pcre2_match_data_create(PCRE2_MAX_CAPTURES, NULL);
    if (!matchData) {
        printf("PCRE2 cannot allocate match data\n");
        if (stack) {
            pcre2_jit_stack_free(stack);
        }
        pcre2_match_context_free(matchCtx);
        pcre2_code_free(re);
        return -1;
    }

    double *times = (double *)calloc((size_t)repeat, sizeof(double));
    if (!times) {
        pcre2_match_data_free(matchData);
        if (stack) {
            pcre2_jit_stack_free(stack);
        }
        pcre2_match_context_free(matchCtx);
        pcre2_code_free(re);
        return -1;
    }
    const int timesLen = repeat;

    memory_tracker_reset_peak();
    const size_t memScanBase = memory_tracker_live();

    /* validate the subject once: this is what lets the scan skip the per-call check */
    const uint32_t matchOpts =
            isValidUTF8((const unsigned char *)subject, subject_len) ? PCRE2_NO_UTF_CHECK : 0;

    long long found = 0;
    do {
        GET_TIME(start);
        const int rc =
                scanAll(re, matchData, matchCtx, useJit, matchOpts, subject, subject_len, &found);
        GET_TIME(end);
        times[repeat - 1] = TIME_DIFF_IN_MS(start, end);
        if (rc != 0) {
            free(times);
            pcre2_match_data_free(matchData);
            if (stack) {
                pcre2_jit_stack_free(stack);
            }
            pcre2_match_context_free(matchCtx);
            pcre2_code_free(re);
            return -1;
        }
    } while (--repeat > 0);

    res->mem_runtime = memory_tracker_peak() - memScanBase;
    res->matches = (int)found;
    get_mean_and_derivation(times, (uint32_t)timesLen, res);

    free(times);
    pcre2_match_data_free(matchData);
    if (stack) {
        pcre2_jit_stack_free(stack);
    }
    pcre2_match_context_free(matchCtx);
    pcre2_code_free(re);
    return 0;
}

int pcre2_find_all(char *pattern, char *subject, int subject_len, int repeat, struct result *res) {
    return pcre2FindAll(pattern, subject, subject_len, repeat, 0, res);
}

int pcre2_jit_find_all(char *pattern, char *subject, int subject_len, int repeat,
                       struct result *res) {
    return pcre2FindAll(pattern, subject, subject_len, repeat, 1, res);
}
