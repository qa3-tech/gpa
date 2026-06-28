/*
 * gpa.h — Zig-inspired General Purpose Allocator for C23
 * Copyright (c) 2026 QA3 Technologies LLC
 * SPDX-License-Identifier: MIT
 * https://github.com/qa3-tech/gpa
 *
 * Single header, no dependencies beyond standard C.
 *
 * USAGE
 *   #include "gpa.h"
 *
 * API
 *   Backing   libc_backing(void);
 *   Backing   mmap_backing(void);          // POSIX only; falls back to libc
 *
 *   Allocator gpa_init    (Backing b);
 *   Allocator gpa_init_log(Backing b, const char* log_path);
 *   bool      gpa_deinit  (Allocator* a);  // true = leaks found; frees all
 *
 *   void*  gpa_malloc (Allocator* a, size_t size);
 *   void*  gpa_realloc(Allocator* a, void* ptr, size_t new_size);
 *   void   gpa_free   (Allocator* a, void* ptr);  // no size needed
 *
 *   GpaSnapshot gpa_snapshot  (Allocator* a);
 *   bool        gpa_check     (Allocator* a, GpaSnapshot s);
 *   void        gpa_dump      (Allocator* a);
 *   size_t      gpa_live_count(Allocator* a);
 *
 *   Allocator libc_allocator(void);         // release swap, zero overhead
 *
 * GUARANTEES
 *   - Double-free (bucketed, <=2048): aborts immediately, message to
 * stderr+log.
 *   - Double-free (large, >2048): reported as unknown-pointer free; no abort.
 *   - Unknown pointer free: logs error, does not abort.
 *   - Use-after-free: freed slots are zeroed.
 *   - Leak detection: gpa_deinit() reports every unreleased allocation.
 */

#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Public types
 * ---------------------------------------------------------------------- */

typedef struct Backing Backing;
typedef struct GpaState GpaState;
typedef struct Allocator Allocator;

typedef struct {
  size_t live_count;
} GpaSnapshot;

struct Backing {
  void *(*alloc_pages)(size_t size, void *ctx);
  void (*free_pages)(void *ptr, size_t size, void *ctx);
  void *ctx;
};

struct Allocator {
  void *(*malloc)(size_t size, void *ctx);
  void *(*realloc)(void *ptr, size_t new_size, void *ctx);
  void (*free)(void *ptr, void *ctx);
  /* debug inspection — NULL in libc_allocator */
  GpaSnapshot (*snapshot)(void *ctx);
  bool (*check)(void *ctx, GpaSnapshot s);
  void (*dump)(void *ctx);
  size_t (*live_count)(void *ctx);
  void *ctx;
};

/* debug wrappers — safe to call on libc_allocator (no-ops) */
static inline GpaSnapshot gpa_snapshot(Allocator *a) {
  return a->snapshot ? a->snapshot(a->ctx) : (GpaSnapshot){0};
}
static inline bool gpa_check(Allocator *a, GpaSnapshot s) {
  return a->check ? a->check(a->ctx, s) : false;
}
static inline void gpa_dump(Allocator *a) {
  if (a->dump)
    a->dump(a->ctx);
}
static inline size_t gpa_live_count(Allocator *a) {
  return a->live_count ? a->live_count(a->ctx) : 0;
}

/* =========================================================================
 * IMPLEMENTATION — static inline throughout; safe in any number of TUs
 * ====================================================================== */

#define GPA_PAGE_SIZE 4096u
#define GPA_NUM_SIZE_CLASSES 9u
#define GPA_MAX_LOG_PATH 256u

[[maybe_unused]] static const size_t GPA_SIZE_CLASSES[GPA_NUM_SIZE_CLASSES] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048};

/* -------------------------------------------------------------------------
 * Bucket
 * ---------------------------------------------------------------------- */

typedef struct Bucket Bucket;
struct Bucket {
  Bucket *prev;
  Bucket *next;
  size_t size_class;
  size_t slot_count;
  size_t used_count;
  uint8_t used_bits[]; /* 1 bit per slot */
};

static inline size_t _gpa_slots_for(size_t sc) {
  /* Largest slot count whose slots + Bucket header + used_bits all fit one
     page:  n*sc + sizeof(Bucket) + ceil(n/8) <= GPA_PAGE_SIZE.
     With ceil(n/8) <= (n+7)/8, solving for n gives the closed form below.
     No clamp to 1: a class too large to seat even one slot must be left out
     of GPA_SIZE_CLASSES (the largest kept class, 2048, yields exactly 1). */
  size_t avail = GPA_PAGE_SIZE - sizeof(Bucket);
  return (8u * avail - 7u) / (8u * sc + 1u);
}

static inline size_t _gpa_used_bytes(size_t slot_count) {
  return (slot_count + 7) / 8;
}

static inline size_t _gpa_bucket_offset(size_t sc) {
  size_t n = _gpa_slots_for(sc);
  return GPA_PAGE_SIZE - (sizeof(Bucket) + _gpa_used_bytes(n));
}

static inline Bucket *_gpa_bucket_from_page(void *page, size_t sc) {
  return (Bucket *)((uint8_t *)page + _gpa_bucket_offset(sc));
}

static inline void *_gpa_page_from_bucket(Bucket *b) {
  return (uint8_t *)b - _gpa_bucket_offset(b->size_class);
}

static inline void *_gpa_slot_ptr(Bucket *b, size_t idx) {
  return (uint8_t *)_gpa_page_from_bucket(b) + idx * b->size_class;
}

static inline size_t _gpa_slot_index(Bucket *b, void *ptr) {
  return ((uint8_t *)ptr - (uint8_t *)_gpa_page_from_bucket(b)) / b->size_class;
}

static inline bool _bit_get(uint8_t *bits, size_t i) {
  return (bits[i / 8] >> (i % 8)) & 1;
}
static inline void _bit_set(uint8_t *bits, size_t i) {
  bits[i / 8] |= (uint8_t)(1u << (i % 8));
}
static inline void _bit_clear(uint8_t *bits, size_t i) {
  bits[i / 8] &= (uint8_t)~(1u << (i % 8));
}

/* -------------------------------------------------------------------------
 * Large allocation list
 * ---------------------------------------------------------------------- */

typedef struct LargeAlloc LargeAlloc;
struct LargeAlloc {
  void *ptr;
  size_t size;
  LargeAlloc *next;
};

/* -------------------------------------------------------------------------
 * GpaState
 * ---------------------------------------------------------------------- */

struct GpaState {
  Backing backing;
  Bucket *buckets[GPA_NUM_SIZE_CLASSES];
  LargeAlloc *large_head;
  size_t live_count;
  FILE *log;
  char log_path[GPA_MAX_LOG_PATH];
};

/* -------------------------------------------------------------------------
 * Logging
 * ---------------------------------------------------------------------- */

static inline void _gpa_log(GpaState *s, const char *fmt, ...) {
  if (!s->log)
    return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(s->log, fmt, ap);
  va_end(ap);
  fflush(s->log);
}

static inline void _gpa_log_init(GpaState *s) {
  if (!s->log)
    return;
  time_t t = time(nullptr);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
  fprintf(s->log, "[gpa] init: %s\n", buf);
  fflush(s->log);
}

/* -------------------------------------------------------------------------
 * Backing implementations
 * ---------------------------------------------------------------------- */

static inline void *_libc_alloc_pages(size_t size, void *ctx) {
  (void)ctx;
  return malloc(size);
}
static inline void _libc_free_pages(void *ptr, size_t size, void *ctx) {
  (void)ctx;
  (void)size;
  free(ptr);
}

static inline Backing libc_backing(void) {
  return (Backing){.alloc_pages = _libc_alloc_pages,
                   .free_pages = _libc_free_pages};
}

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
static inline void *_mmap_alloc_pages(size_t size, void *ctx) {
  (void)ctx;
  void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (p == MAP_FAILED) ? nullptr : p;
}
static inline void _mmap_free_pages(void *ptr, size_t size, void *ctx) {
  (void)ctx;
  munmap(ptr, size);
}
static inline Backing mmap_backing(void) {
  return (Backing){.alloc_pages = _mmap_alloc_pages,
                   .free_pages = _mmap_free_pages};
}
#else
static inline Backing mmap_backing(void) { return libc_backing(); }
#endif

/* -------------------------------------------------------------------------
 * Size class lookup
 * ---------------------------------------------------------------------- */

static inline int _gpa_sc_index(size_t size) {
  for (int i = 0; i < (int)GPA_NUM_SIZE_CLASSES; i++)
    if (GPA_SIZE_CLASSES[i] >= size)
      return i;
  return -1;
}

/* -------------------------------------------------------------------------
 * Bucket management
 * ---------------------------------------------------------------------- */

static inline Bucket *_gpa_bucket_alloc(GpaState *s, size_t sc_idx) {
  void *page = s->backing.alloc_pages(GPA_PAGE_SIZE, s->backing.ctx);
  if (!page)
    return nullptr;
  memset(page, 0, GPA_PAGE_SIZE);
  size_t sc = GPA_SIZE_CLASSES[sc_idx];
  Bucket *b = _gpa_bucket_from_page(page, sc);
  b->size_class = sc;
  b->slot_count = _gpa_slots_for(sc);
  b->used_count = 0;
  b->prev = b->next = b;
  return b;
}

static inline void _gpa_bucket_free(GpaState *s, Bucket *b) {
  s->backing.free_pages(_gpa_page_from_bucket(b), GPA_PAGE_SIZE,
                        s->backing.ctx);
}

static inline Bucket *_gpa_bucket_with_slot(GpaState *s, size_t sc_idx) {
  Bucket *head = s->buckets[sc_idx];
  if (head && head->used_count < head->slot_count)
    return head;
  if (head) {
    Bucket *cur = head->next;
    while (cur != head) {
      if (cur->used_count < cur->slot_count) {
        s->buckets[sc_idx] = cur;
        return cur;
      }
      cur = cur->next;
    }
  }
  Bucket *nb = _gpa_bucket_alloc(s, sc_idx);
  if (!nb)
    return nullptr;
  if (head) {
    nb->next = head;
    nb->prev = head->prev;
    head->prev->next = nb;
    head->prev = nb;
  }
  s->buckets[sc_idx] = nb;
  return nb;
}

static inline Bucket *_gpa_bucket_owning(GpaState *s, void *ptr,
                                         size_t *sc_idx_out) {
  for (size_t i = 0; i < GPA_NUM_SIZE_CLASSES; i++) {
    Bucket *head = s->buckets[i];
    if (!head)
      continue;
    Bucket *cur = head;
    do {
      void *page = _gpa_page_from_bucket(cur);
      size_t payload = cur->slot_count * cur->size_class;
      if ((uint8_t *)ptr >= (uint8_t *)page &&
          (uint8_t *)ptr < (uint8_t *)page + payload) {
        if (sc_idx_out)
          *sc_idx_out = i;
        return cur;
      }
      cur = cur->next;
    } while (cur != head);
  }
  return nullptr;
}

/* -------------------------------------------------------------------------
 * Large allocation helpers
 * ---------------------------------------------------------------------- */

static inline void *_gpa_large_alloc(GpaState *s, size_t size) {
  size_t pages = (size + GPA_PAGE_SIZE - 1) / GPA_PAGE_SIZE;
  size_t total = pages * GPA_PAGE_SIZE;
  void *ptr = s->backing.alloc_pages(total, s->backing.ctx);
  if (!ptr)
    return nullptr;
  LargeAlloc *la = (LargeAlloc *)malloc(sizeof(LargeAlloc));
  if (!la) {
    s->backing.free_pages(ptr, total, s->backing.ctx);
    return nullptr;
  }
  la->ptr = ptr;
  la->size = total;
  la->next = s->large_head;
  s->large_head = la;
  return ptr;
}

static inline bool _gpa_large_free(GpaState *s, void *ptr) {
  LargeAlloc **cur = &s->large_head;
  while (*cur) {
    if ((*cur)->ptr == ptr) {
      LargeAlloc *la = *cur;
      *cur = la->next;
      s->backing.free_pages(la->ptr, la->size, s->backing.ctx);
      free(la);
      return true;
    }
    cur = &(*cur)->next;
  }
  return false;
}

/* -------------------------------------------------------------------------
 * Core vtable functions
 * ---------------------------------------------------------------------- */

static inline void *_gpa_malloc(size_t size, void *ctx) {
  if (!size)
    return nullptr;
  GpaState *s = (GpaState *)ctx;
  int idx = _gpa_sc_index(size);
  if (idx < 0) {
    void *p = _gpa_large_alloc(s, size);
    if (p) {
      s->live_count++;
      _gpa_log(s, "[gpa] malloc large: %p  size %zu\n", p, size);
    }
    return p;
  }
  Bucket *b = _gpa_bucket_with_slot(s, (size_t)idx);
  if (!b)
    return nullptr;
  size_t ubytes = _gpa_used_bytes(b->slot_count);
  for (size_t byte = 0; byte < ubytes; byte++) {
    if (b->used_bits[byte] == 0xFF)
      continue;
    for (size_t bit = 0; bit < 8; bit++) {
      size_t slot = byte * 8 + bit;
      if (slot >= b->slot_count)
        break;
      if (!_bit_get(b->used_bits, slot)) {
        _bit_set(b->used_bits, slot);
        b->used_count++;
        s->live_count++;
        void *p = _gpa_slot_ptr(b, slot);
        _gpa_log(s, "[gpa] malloc: %p  size %zu  class %zu  slot %zu\n", p,
                 size, GPA_SIZE_CLASSES[idx], slot);
        return p;
      }
    }
  }
  return nullptr;
}

static inline void _gpa_free(void *ptr, void *ctx) {
  if (!ptr)
    return;
  GpaState *s = (GpaState *)ctx;
  if (_gpa_large_free(s, ptr)) {
    s->live_count--;
    _gpa_log(s, "[gpa] free large: %p\n", ptr);
    return;
  }
  size_t sc_idx;
  Bucket *b = _gpa_bucket_owning(s, ptr, &sc_idx);
  if (!b) {
    _gpa_log(s, "[gpa] ERROR: free of unknown pointer %p\n", ptr);
    fprintf(stderr, "[gpa] ERROR: free of unknown pointer %p\n", ptr);
    return;
  }
  size_t slot = _gpa_slot_index(b, ptr);
  if (!_bit_get(b->used_bits, slot)) {
    _gpa_log(s, "[gpa] DOUBLE-FREE: %p  class %zu  slot %zu\n", ptr,
             b->size_class, slot);
    fprintf(stderr, "[gpa] DOUBLE-FREE: %p  class %zu  slot %zu\n", ptr,
            b->size_class, slot);
    abort();
  }
  _bit_clear(b->used_bits, slot);
  b->used_count--;
  s->live_count--;
  memset(ptr, 0, b->size_class); /* zero on free — catches use-after-free */
  _gpa_log(s, "[gpa] free: %p  class %zu  slot %zu\n", ptr, b->size_class,
           slot);
}

static inline void *_gpa_realloc(void *ptr, size_t new_size, void *ctx) {
  if (!ptr)
    return _gpa_malloc(new_size, ctx);
  if (!new_size) {
    _gpa_free(ptr, ctx);
    return nullptr;
  }
  GpaState *s = (GpaState *)ctx;

  size_t old_size = 0;
  for (LargeAlloc *la = s->large_head; la; la = la->next)
    if (la->ptr == ptr) {
      old_size = la->size;
      break;
    }
  if (!old_size) {
    Bucket *b = _gpa_bucket_owning(s, ptr, nullptr);
    if (b)
      old_size = b->size_class;
  }
  if (!old_size) {
    _gpa_log(s, "[gpa] ERROR: realloc of unknown pointer %p\n", ptr);
    fprintf(stderr, "[gpa] ERROR: realloc of unknown pointer %p\n", ptr);
    return nullptr;
  }

  /* same size class — no-op */
  if (_gpa_sc_index(old_size) == _gpa_sc_index(new_size) &&
      _gpa_sc_index(new_size) >= 0)
    return ptr;

  void *np = _gpa_malloc(new_size, ctx);
  if (!np)
    return nullptr;
  memcpy(np, ptr, old_size < new_size ? old_size : new_size);
  _gpa_free(ptr, ctx);
  _gpa_log(s, "[gpa] realloc: %p -> %p  old %zu  new %zu\n", ptr, np, old_size,
           new_size);
  return np;
}

/* -------------------------------------------------------------------------
 * Debug vtable functions
 * ---------------------------------------------------------------------- */

static inline GpaSnapshot _gpa_snapshot(void *ctx) {
  return (GpaSnapshot){.live_count = ((GpaState *)ctx)->live_count};
}

static inline bool _gpa_check(void *ctx, GpaSnapshot snap) {
  GpaState *s = (GpaState *)ctx;
  bool leaked = s->live_count > snap.live_count;
  if (leaked) {
    size_t n = s->live_count - snap.live_count;
    _gpa_log(s, "[gpa] check: %zu live allocation(s) since snapshot\n", n);
    fprintf(stderr, "[gpa] check: %zu live allocation(s) since snapshot\n", n);
  }
  return leaked;
}

static inline void _gpa_dump(void *ctx) {
  GpaState *s = (GpaState *)ctx;
  _gpa_log(s, "[gpa] dump: %zu live allocation(s)\n", s->live_count);
  fprintf(stderr, "[gpa] dump: %zu live allocation(s)\n", s->live_count);
  for (size_t i = 0; i < GPA_NUM_SIZE_CLASSES; i++) {
    Bucket *head = s->buckets[i];
    if (!head)
      continue;
    Bucket *cur = head;
    do {
      size_t ubytes = _gpa_used_bytes(cur->slot_count);
      for (size_t byte = 0; byte < ubytes; byte++) {
        if (!cur->used_bits[byte])
          continue;
        for (size_t bit = 0; bit < 8; bit++) {
          size_t slot = byte * 8 + bit;
          if (slot >= cur->slot_count)
            break;
          if (_bit_get(cur->used_bits, slot)) {
            void *p = _gpa_slot_ptr(cur, slot);
            _gpa_log(s, "[gpa] live: %p  class %zu  slot %zu\n", p,
                     cur->size_class, slot);
            fprintf(stderr, "[gpa] live: %p  class %zu  slot %zu\n", p,
                    cur->size_class, slot);
          }
        }
      }
      cur = cur->next;
    } while (cur != head);
  }
  for (LargeAlloc *la = s->large_head; la; la = la->next) {
    _gpa_log(s, "[gpa] live large: %p  size %zu\n", la->ptr, la->size);
    fprintf(stderr, "[gpa] live large: %p  size %zu\n", la->ptr, la->size);
  }
}

static inline size_t _gpa_live_count(void *ctx) {
  return ((GpaState *)ctx)->live_count;
}

/* -------------------------------------------------------------------------
 * Public init / deinit
 * ---------------------------------------------------------------------- */

static inline Allocator _gpa_make(GpaState *s) {
  return (Allocator){
      .malloc = _gpa_malloc,
      .realloc = _gpa_realloc,
      .free = _gpa_free,
      .snapshot = _gpa_snapshot,
      .check = _gpa_check,
      .dump = _gpa_dump,
      .live_count = _gpa_live_count,
      .ctx = s,
  };
}

static inline GpaState *_gpa_state_new(Backing b) {
  GpaState *s = (GpaState *)calloc(1, sizeof(GpaState));
  if (s)
    s->backing = b;
  return s;
}

static inline Allocator gpa_init(Backing b) {
  return _gpa_make(_gpa_state_new(b));
}

static inline Allocator gpa_init_log(Backing b, const char *log_path) {
  GpaState *s = _gpa_state_new(b);
  if (s && log_path) {
    s->log = fopen(log_path, "w");
    if (s->log) {
      strncpy(s->log_path, log_path, GPA_MAX_LOG_PATH - 1);
      _gpa_log_init(s);
    }
  }
  return _gpa_make(s);
}

static inline bool gpa_deinit(Allocator *a) {
  if (!a || !a->ctx)
    return false;
  GpaState *s = (GpaState *)a->ctx;
  bool any = false;

  for (size_t i = 0; i < GPA_NUM_SIZE_CLASSES; i++) {
    Bucket *head = s->buckets[i];
    if (!head)
      continue;
    /* collect all buckets in this class before freeing */
    Bucket *cur = head;
    Bucket *stop = head;
    do {
      Bucket *next = cur->next;
      size_t ubytes = _gpa_used_bytes(cur->slot_count);
      for (size_t byte = 0; byte < ubytes; byte++) {
        if (!cur->used_bits[byte])
          continue;
        for (size_t bit = 0; bit < 8; bit++) {
          size_t slot = byte * 8 + bit;
          if (slot >= cur->slot_count)
            break;
          if (_bit_get(cur->used_bits, slot)) {
            void *p = _gpa_slot_ptr(cur, slot);
            _gpa_log(s, "[gpa] leak: %p  class %zu  slot %zu\n", p,
                     cur->size_class, slot);
            fprintf(stderr, "[gpa] leak: %p  class %zu  slot %zu\n", p,
                    cur->size_class, slot);
            any = true;
          }
        }
      }
      _gpa_bucket_free(s, cur);
      cur = next;
    } while (cur != stop);
  }

  for (LargeAlloc *la = s->large_head; la;) {
    _gpa_log(s, "[gpa] leak large: %p  size %zu\n", la->ptr, la->size);
    fprintf(stderr, "[gpa] leak large: %p  size %zu\n", la->ptr, la->size);
    any = true;
    LargeAlloc *next = la->next;
    s->backing.free_pages(la->ptr, la->size, s->backing.ctx);
    free(la);
    la = next;
  }

  if (any) {
    _gpa_log(s, "[gpa] deinit: leaks detected — all memory freed\n");
    fprintf(stderr, "[gpa] deinit: leaks detected — all memory freed\n");
  } else {
    _gpa_log(s, "[gpa] deinit: clean\n");
  }

  if (s->log)
    fclose(s->log);
  free(s);
  a->ctx = nullptr;
  return any;
}

/* -------------------------------------------------------------------------
 * Convenience wrappers
 * ---------------------------------------------------------------------- */

static inline void *gpa_malloc(Allocator *a, size_t size) {
  return a->malloc(size, a->ctx);
}
static inline void *gpa_realloc(Allocator *a, void *p, size_t new_size) {
  return a->realloc(p, new_size, a->ctx);
}
static inline void gpa_free(Allocator *a, void *p) { a->free(p, a->ctx); }

/* -------------------------------------------------------------------------
 * libc_allocator — release swap
 * ---------------------------------------------------------------------- */

static inline void *_la_malloc(size_t s, void *ctx) {
  (void)ctx;
  return malloc(s);
}
static inline void *_la_realloc(void *p, size_t s, void *ctx) {
  (void)ctx;
  return realloc(p, s);
}
static inline void _la_free(void *p, void *ctx) {
  (void)ctx;
  free(p);
}

static inline Allocator libc_allocator(void) {
  return (Allocator){
      .malloc = _la_malloc, .realloc = _la_realloc, .free = _la_free,
      /* debug fields intentionally null — gpa_* wrappers handle nullptr */
  };
}
