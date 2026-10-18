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

#ifndef ARSH_TEST_REGEX_REMATCH_ABI_H
#define ARSH_TEST_REGEX_REMATCH_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Common interface implemented by each embedded engine adapter (see
 * rematch_hermes.cpp / rematch_quickjs.c / rematch_srell.cpp).
 *
 * `rematch` is a standalone version of the arsh `redump -m` dump tool which embeds the
 * JavaScript-compatible engines used by tools/regex-bench and selects one with `-e`.
 * The adapters translate the arsh-like mode / modifier flags into each engine's own flags
 * and report the first match with UTF-8 byte offsets.
 */

/* the regex mode, mirroring arsh's regex::Mode. */
#define REMATCH_MODE_BMP 0         /* '' */
#define REMATCH_MODE_UNICODE 1     /* 'u' */
#define REMATCH_MODE_UNICODE_SET 2 /* 'v' */

/* offset/size of an unmatched capture group. */
#define REMATCH_CAPTURE_UNSET UINT32_MAX

typedef struct {
    uint32_t offset; /* UTF-8 byte offset */
    uint32_t size;   /* UTF-8 byte size */
} rematch_capture;

/**
 * Try to match `pattern` against `input` once (the first match only, like redump -m).
 *
 * @param captures output buffer with room for `maxCaptures` entries (group 0 first)
 * @param err output buffer for a compile error message (may be null)
 * @return the number of captures (>= 1) on match, 0 if there is no match,
 *         -1 on a compile error and -2 on a runtime error.
 */
int rematch_hermes_match(const char *pattern, int patternLen,
                         const char *input, int inputLen,
                         int mode, int icase, int multiline, int dotall,
                         rematch_capture *captures, int maxCaptures, char *err, int errSize);

int rematch_quickjs_match(const char *pattern, int patternLen,
                          const char *input, int inputLen,
                          int mode, int icase, int multiline, int dotall,
                          rematch_capture *captures, int maxCaptures, char *err, int errSize);

int rematch_srell_match(const char *pattern, int patternLen,
                        const char *input, int inputLen,
                        int mode, int icase, int multiline, int dotall,
                        rematch_capture *captures, int maxCaptures, char *err, int errSize);

#ifdef __cplusplus
}
#endif

#endif // ARSH_TEST_REGEX_REMATCH_ABI_H
