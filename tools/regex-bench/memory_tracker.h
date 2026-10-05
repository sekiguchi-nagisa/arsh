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

#ifndef REGEX_BENCH_MEMORY_TRACKER_H
#define REGEX_BENCH_MEMORY_TRACKER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Process-wide heap accounting used by the regex-performance adapters to report how much memory an
 * engine needs.
 *
 * The tracker is implemented in memory_tracker.cpp by replacing the global
 * malloc/calloc/realloc/free and C++ operator new/delete with wrappers that forward to the real
 * libc allocators. Because the engines are linked into the benchmark executable, all of their heap
 * usage is observed, while the allocations made deep inside libc itself are not.
 *
 * It is only active on glibc; on other platforms every accessor returns 0 so the benchmark still
 * runs, just without memory numbers.
 *
 * `memory_tracker_live()` returns the current live bytes and `memory_tracker_peak()` the high-water
 * mark since the last `memory_tracker_reset_peak()`, so a caller measures an engine as a delta:
 *
 *   size_t base = memory_tracker_live();
 *   ... build the engine instance ...
 *   size_t instance_bytes = memory_tracker_live() - base;
 *
 *   memory_tracker_reset_peak();
 *   size_t scan_base = memory_tracker_live();
 *   ... run the engine ...
 *   size_t runtime_bytes = memory_tracker_peak() - scan_base;
 *
 * The counters track the usable block size on glibc (i.e. the allocator's actual footprint,
 * including its rounding). The benchmark is single-threaded; the counters are atomic only to stay
 * well-defined if an engine happens to allocate from a worker thread.
 */

/**
 * Resolve the real libc allocators. Optional: the tracker resolves them statically, but calling
 * this before measuring keeps the API explicit for the adapters.
 */
void memory_tracker_init(void);

/** @return the number of bytes currently live (allocated through the tracked allocator). */
size_t memory_tracker_live(void);

/** Set the peak watermark to the current live size. */
void memory_tracker_reset_peak(void);

/** @return the maximum live size observed since the last memory_tracker_reset_peak(). */
size_t memory_tracker_peak(void);

#ifdef __cplusplus
}
#endif

#endif // REGEX_BENCH_MEMORY_TRACKER_H
