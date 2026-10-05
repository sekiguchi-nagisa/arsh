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
 * See memory_tracker.h.
 *
 * The tracker replaces the global C++ operator new/delete and the C malloc/calloc/realloc/free and
 * forwards them to the real libc allocators. Each live block is accounted by its usable size, so
 * the reported number matches the allocator's real footprint.
 *
 * It is only enabled on glibc, where the real allocators are the `__libc_*` aliases and
 * malloc_usable_size() reports the block size. On other platforms it compiles to a no-op so the
 * benchmark is unaffected.
 */

#include "memory_tracker.h"

#include <stdlib.h>

#if defined(__GLIBC__)
#define REGEX_BENCH_TRACK_MEMORY 1
#endif

#ifdef REGEX_BENCH_TRACK_MEMORY
#include <errno.h>
#include <malloc.h>
#include <new>

#include <atomic>

extern "C" void *__libc_malloc(size_t);
extern "C" void *__libc_calloc(size_t, size_t);
extern "C" void *__libc_realloc(void *, size_t);
extern "C" void __libc_free(void *);
extern "C" void *__libc_memalign(size_t, size_t);
#endif

namespace {

#ifdef REGEX_BENCH_TRACK_MEMORY

std::atomic<size_t> liveBytes{0};
std::atomic<size_t> peakBytes{0};

size_t usableSize(const void *ptr) {
  return ptr == nullptr ? 0 : malloc_usable_size(const_cast<void *>(ptr));
}

void addLive(long long delta) {
  const size_t value = liveBytes.fetch_add(static_cast<size_t>(delta)) + static_cast<size_t>(delta);
  size_t current = peakBytes.load();
  while (value > current && !peakBytes.compare_exchange_weak(current, value)) {
  }
}

void onAlloc(void *ptr, size_t requested) {
  if (ptr == nullptr) {
    return;
  }
  const size_t tracked = usableSize(ptr);
  addLive(static_cast<long long>(tracked != 0 ? tracked : requested));
}

void onFree(void *ptr) {
  const size_t tracked = usableSize(ptr);
  if (tracked != 0) {
    addLive(-static_cast<long long>(tracked));
  }
}

#endif // REGEX_BENCH_TRACK_MEMORY

} // namespace

extern "C" void memory_tracker_init(void) {
  /* the real allocators are resolved statically here; kept for API symmetry */
}

extern "C" size_t memory_tracker_live(void) {
#ifdef REGEX_BENCH_TRACK_MEMORY
  return liveBytes.load();
#else
  return 0;
#endif
}

extern "C" void memory_tracker_reset_peak(void) {
#ifdef REGEX_BENCH_TRACK_MEMORY
  peakBytes.store(liveBytes.load());
#endif
}

extern "C" size_t memory_tracker_peak(void) {
#ifdef REGEX_BENCH_TRACK_MEMORY
  return peakBytes.load();
#else
  return 0;
#endif
}

#ifdef REGEX_BENCH_TRACK_MEMORY

extern "C" void *malloc(size_t size) {
  void *ptr = __libc_malloc(size);
  onAlloc(ptr, size);
  return ptr;
}

extern "C" void *calloc(size_t count, size_t size) {
  void *ptr = __libc_calloc(count, size);
  onAlloc(ptr, count * size);
  return ptr;
}

extern "C" void *realloc(void *ptr, size_t size) {
  if (size == 0) {
    free(ptr);
    return nullptr;
  }
  const size_t oldUsable = usableSize(ptr);
  void *next = __libc_realloc(ptr, size);
  if (next != nullptr) {
    onAlloc(next, size);
    if (oldUsable != 0) {
      addLive(-static_cast<long long>(oldUsable));
    }
  }
  return next;
}

extern "C" void free(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  onFree(ptr);
  __libc_free(ptr);
}

extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size) {
  void *ptr = __libc_memalign(alignment, size);
  if (ptr == nullptr) {
    return ENOMEM;
  }
  *memptr = ptr;
  onAlloc(ptr, size);
  return 0;
}

extern "C" void *aligned_alloc(size_t alignment, size_t size) {
  void *ptr = __libc_memalign(alignment, size);
  onAlloc(ptr, size);
  return ptr;
}

void *operator new(size_t size) {
  void *ptr = malloc(size);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void *operator new[](size_t size) { return ::operator new(size); }

void *operator new(size_t size, const std::nothrow_t &) noexcept { return malloc(size); }

void *operator new[](size_t size, const std::nothrow_t &) noexcept { return malloc(size); }

void operator delete(void *ptr) noexcept { free(ptr); }

void operator delete[](void *ptr) noexcept { free(ptr); }

void operator delete(void *ptr, size_t) noexcept { free(ptr); }

void operator delete[](void *ptr, size_t) noexcept { free(ptr); }

void operator delete(void *ptr, const std::nothrow_t &) noexcept { free(ptr); }

void operator delete[](void *ptr, const std::nothrow_t &) noexcept { free(ptr); }

#endif // REGEX_BENCH_TRACK_MEMORY
