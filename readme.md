# gpa.h

Zig-inspired General Purpose Allocator for C23.  
Single header. No dependencies beyond standard C.

---

## Motivation

Zig's `std.heap.DebugAllocator` gives you leak detection, double-free detection,
and use-after-free hardening as a composable, swappable allocator — not a global
tool bolted on externally. `gpa.h` brings that pattern to C23.

The core idea: your code receives an `Allocator` and calls `gpa_malloc` / `gpa_free`
through it. In development you pass a `gpa_init(...)` allocator and get full
diagnostics. In release you swap in `libc_allocator()` — one line, zero other changes,
zero overhead.

---

## Files

| File | Purpose |
|---|---|
| `gpa.h` | Allocator — drop into your project |
| `test_gpa.c` | Tests (requires TestCracks) |
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

    gpa_deinit(&alloc);   // prints "[gpa] deinit: clean"
    return 0;
}
```

Compile:
```sh
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
GpaSnapshot gpa_snapshot  (Allocator* a);             // mark a known-clean point
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

| Situation | Behaviour |
|---|---|
| Leak at `gpa_deinit` | Logged to stderr and log file; memory freed; returns `true` |
| Double-free | Logged; `abort()` called immediately |
| Free of unknown pointer | Logged to stderr; execution continues |
| Use-after-free | Freed slots zeroed — stale reads return zeros, not garbage |

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
8  16  32  64  128  256  512  1024  2048  4096
```

Allocations above 2048 bytes go to a large-allocation list (page-aligned,
tracked separately). `gpa_free` recovers the size from the bucket — you
never pass a size to `free`.

---

## Building Tests

```sh
# fetch TestCracks
curl -sL https://raw.githubusercontent.com/qa3-tech/TestCracks/main/include/testcracks.h -o testcracks.h
curl -sL https://raw.githubusercontent.com/qa3-tech/TestCracks/main/src/testcracks.c    -o testcracks.c

# build and run
gcc -std=c23 -Wall -Wextra -Werror -o test_gpa test_gpa.c testcracks.c
./test_gpa
```

Expected output:
```
=== Basics ===
  ✓ clean alloc and free
  ✓ allocate array
  ✓ null free is safe

=== Leak Detection ===
  ✓ single leak detected
  ✓ multiple leaks detected
  ✓ large allocation leak

... (17 tests total)

17/17 passed, 0 failed, 0 skipped
```

---

## Using with Zig build system

```zig
// build.zig
exe.addCSourceFile(.{
    .file  = b.path("src/myapp.c"),
    .flags = &[_][]const u8{"-std=c23"},
});
exe.linkLibC();
```

No special configuration needed — `gpa.h` is a single header.

---

## Limitations

- POSIX only for `mmap_backing()`; `libc_backing()` works everywhere
- No stack trace capture on alloc/free (requires platform unwinding — out of scope)

