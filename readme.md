# gpa.h

Zig-inspired General Purpose Allocator for C23. Single header. No dependencies beyond standard C.

---

## Motivation

Zig's `std.heap.DebugAllocator` gives you leak detection, double-free detection,
and use-after-free hardening as a composable, swappable allocator — not a global
tool bolted on externally. `gpa.h` brings that pattern to C23.

The core idea: your code receives an `Allocator` and calls `gpa_malloc` / `gpa_free`
through it. In development you pass a `gpa_init(...)` allocator and get full
diagnostics. In release you swap in `libc_allocator()` — one line, zero other changes,
zero overhead.

Every function is `static inline`, so you can `#include "gpa.h"` from any number of
translation units with no separate `.c` to compile and no duplicate-symbol link errors.

---

## Files

| File                            | Purpose                                                               |
| ------------------------------- | --------------------------------------------------------------------- |
| `gpa.h`                         | Allocator — drop into your project                                    |
| `tests/main.c`                  | Test entry point (requires TestCracks)                                |
| `tests/second_tu.c`             | Second TU — guards multi-TU linkage                                   |
| `testcracks.h` / `testcracks.c` | Test framework ([TestCracks](https://github.com/qa3-tech/TestCracks)) |

---

## Quick Start

```c
#include "gpa.h"

typedef struct { int x; int y; } Point;

int main(void) {
    Allocator alloc = gpa_init(libc_backing());

    Point* p = gpa_malloc(&alloc, sizeof(Point));
    p->x = 10;
    p->y = 20;
    gpa_free(&alloc, p);

    gpa_deinit(&alloc);
    return 0;
}
```

Compile:

```
gcc -std=c23 -o myapp myapp.c
```

With log file:

```c
Allocator alloc = gpa_init_log(libc_backing(), "gpa.log");
```

---

## API

### Init / deinit

```c
Allocator gpa_init    (Backing b);
Allocator gpa_init_log(Backing b, const char* log_path);
bool      gpa_deinit  (Allocator* a);   // true = leaks found; always frees all memory
```

### Alloc / free

```c
void*  gpa_malloc (Allocator* a, size_t size);
void*  gpa_realloc(Allocator* a, void* ptr, size_t new_size);
void   gpa_free   (Allocator* a, void* ptr);   // no size needed
```

### Backing (page source)

```c
Backing libc_backing(void);   // malloc/free — universal
Backing mmap_backing(void);   // mmap/munmap — POSIX; falls back to libc elsewhere
```

### Debug inspection

```c
GpaSnapshot gpa_snapshot  (Allocator* a);              // mark a known-clean point
bool        gpa_check     (Allocator* a, GpaSnapshot); // true = leaks since snapshot
void        gpa_dump      (Allocator* a);              // log every live allocation
size_t      gpa_live_count(Allocator* a);
```

All four are safe to call on `libc_allocator()` — they become no-ops.

### Release swap

```c
Allocator libc_allocator(void);   // identical vtable, malloc/free, zero overhead
```

---

## Guarantees

| Situation                     | Behaviour                                                   |
| ----------------------------- | ----------------------------------------------------------- |
| Leak at `gpa_deinit`          | Logged to stderr and log file; memory freed; returns `true` |
| Double-free (bucketed, ≤2048) | Logged; `abort()` called immediately                        |
| Double-free (large, >2048)    | Reported as an unknown-pointer free; execution continues    |
| Free of unknown pointer       | Logged to stderr; execution continues                       |
| Use-after-free                | Freed slots zeroed — stale reads return zeros, not garbage  |

Double-free detection differs by allocation size. A bucketed allocation carries
a per-slot used-bit, so freeing it twice is caught and aborts. A large
allocation is removed from its list on the first free, so a second free can no
longer be distinguished from a stray pointer — it is logged as
`free of unknown pointer` and execution continues rather than aborting.

---

## Leak Remediation Workflow

When `gpa_deinit` reports leaks, narrow them down with `gpa_snapshot` / `gpa_check`:

```c
Allocator alloc = gpa_init_log(libc_backing(), "gpa.log");

// 1. mark a known-clean point
GpaSnapshot snap = gpa_snapshot(&alloc);

// 2. run the suspect block
Point* p = gpa_malloc(&alloc, sizeof(Point));
// ... forgot to free p ...

// 3. check for leaks since the snapshot
if (gpa_check(&alloc, snap)) {
    // 4. dump every live allocation: address, size-class, slot
    gpa_dump(&alloc);
}

// 5. deinit always frees everything, leaked or not
gpa_deinit(&alloc);
```

The log file records every `malloc`, `free`, `realloc`, and leak with addresses
and size-class info. Cross-reference addresses with your allocation calls to
find the culprit.

For tighter bracketing, wrap smaller code blocks in their own snapshot/check pairs
until the leak is isolated to a single allocation.

---

## Release Swap

```c
// development
Allocator alloc = gpa_init_log(libc_backing(), "gpa.log");

// release — one line change, nothing else
Allocator alloc = libc_allocator();
```

All `gpa_malloc` / `gpa_free` / `gpa_realloc` calls work identically.
Debug functions (`gpa_snapshot`, `gpa_check`, `gpa_dump`, `gpa_live_count`)
become no-ops automatically.

---

## Size Classes

Small allocations map to the smallest size class >= the requested size:

```
8  16  32  64  128  256  512  1024  2048
```

Allocations above 2048 bytes go to a separate large-allocation list, rounded up
to a whole number of 4096-byte pages. `gpa_free` recovers the size from the
bucket — you never pass a size to `free`.

2048 is the ceiling for a reason. Each bucket stores its header (slot bitmap
and bookkeeping) at the end of the same page as its slots, so the slots and the
header must share 4096 bytes. The slot count is sized to leave room for that
header — which is why the largest class fits exactly one 2048-byte slot per
page (the other ~2KB holds the header and unavoidable slack). A 4096-byte class
would need the whole page for a single slot with nothing left for the header, so
those sizes route to the large list instead.

---

## Alignment

Alignment is determined by the size class and the backing — there is no
`aligned_alloc`-style request, and over-aligned types (alignment greater than
what the size class provides) are not supported.

| Backing          | Guaranteed alignment                                                                              |
| ---------------- | ------------------------------------------------------------------------------------------------- |
| `libc_backing()` | at least `min(size_class, 16)` — 8 B for the 8-byte class, 16 B otherwise; large allocations 16 B |
| `mmap_backing()` | the full size class (page base is page-aligned); large allocations page-aligned                   |

The guarantee is the floor. In practice the C library often hands back a more
strongly aligned page, so you may observe higher alignment under `libc_backing`,
but do not rely on more than the table above. 16 bytes matches `max_align_t` on
common 64-bit ABIs, so ordinary scalar and struct types are fine; SIMD or
cache-line-aligned types that need 32/64-byte alignment are not guaranteed under
`libc_backing`.

`gpa_malloc` does not zero the memory it returns — initialize what you allocate.
(Freed slots are currently zeroed as a use-after-free aid, which can make fresh
allocations happen to read as zero, but that is an internal detail, not a
contract.)

---

## Interpreting Leak Output

When `gpa_deinit` or `gpa_check` reports a leak, each line tells you three things:

```
[gpa] leak: 0x64ae2ad4e7e0  class 8  slot 0
```

**address** — the exact pointer that was never freed. Cross-reference against your
code to identify the variable.

**class** — the size bucket the allocation landed in. Always the smallest power of
two >= your requested size:

| You asked for | Class |
| ------------- | ----- |
| 1 – 8 bytes   | 8     |
| 9 – 16 bytes  | 16    |
| 17 – 32 bytes | 32    |
| 33 – 64 bytes | 64    |
| … and so on   | …     |

So `class 8` means something 1–8 bytes — likely a small struct or scalar. `class 64`
means something 33–64 bytes — a medium struct.

**slot** — the position of that allocation within the class's page, in allocation
order. Slot 0 was the first allocation of that class, slot 1 the second, and so on.
Multiple leaks in the same class with consecutive slots means they were allocated
together — a strong hint they came from the same code path.

Large allocations (> 2048 bytes) bypass the bucket system and report differently:

```
[gpa] leak large: 0x64ae2ad4e7e0  size 8192
```

Here `size` is the actual page-rounded allocation size, not a class.

### Narrowing down a leak

Use `gpa_snapshot` / `gpa_check` to bracket suspect code:

```c
GpaSnapshot snap = gpa_snapshot(&alloc);

// suspect block
Point* p = gpa_malloc(&alloc, sizeof(Point));

if (gpa_check(&alloc, snap)) {
    gpa_dump(&alloc);   // prints every live address + class + slot
}
```

Wrap progressively smaller blocks until the leak isolates to a single allocation.
The slot number gives you allocation order within that class — useful when the
same struct type leaks multiple times.

---

## Building Tests

```
# fetch TestCracks
curl -sL https://raw.githubusercontent.com/qa3-tech/TestCracks/main/include/testcracks.h -o testcracks.h
curl -sL https://raw.githubusercontent.com/qa3-tech/TestCracks/main/src/testcracks.c    -o testcracks.c

# build and run — main.c and second_tu.c both include gpa.h
gcc -std=c23 -Wall -Wextra -Werror -o test_gpa main.c second_tu.c testcracks.c
./test_gpa
```

Linking `main.c` and `second_tu.c` together is itself the regression guard for the
`static inline` design: if `gpa.h` ever reverts to external linkage, this build fails
with `multiple definition of ...` before any test runs.

---

## Limitations

- **Single-threaded.** An `Allocator` holds mutable state with no locking. Do not
  share one across threads; give each thread its own, or add external
  synchronization.
- **No over-alignment.** Alignment follows the size class and backing (see
  [Alignment](#alignment)); there is no `aligned_alloc`-style request.
- **Large double-free is not caught.** Freeing a >2048-byte allocation twice is
  reported as an unknown-pointer free, not a double-free, and does not abort (see
  [Guarantees](#guarantees)).
- **Bookkeeping uses libc.** Internal metadata (the allocator state and
  large-allocation nodes) comes from `malloc`/`calloc` regardless of the backing;
  only the pages themselves come from the backing.
- POSIX only for `mmap_backing()`; `libc_backing()` works everywhere.
- No stack-trace capture on alloc/free (requires platform unwinding — out of scope).
