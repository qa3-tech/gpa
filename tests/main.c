#include "gpa.h"
#include "testcracks.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

typedef struct {
  int x;
  int y;
} Point;
typedef struct {
  char name[32];
  int age;
} Person;

/* Defined in second_tu.c — see that file for why this test exists. */
size_t gpa_second_tu_probe(void);

/* -------------------------------------------------------------------------
 * Suite: basics
 * ---------------------------------------------------------------------- */

static TestResult test_clean_alloc_free(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  Point *p = gpa_malloc(&a, sizeof(Point));
  TestResult r = tc_assert_not_nil(p, "malloc returns non-null");

  p->x = 10;
  p->y = 20;
  r = tc_combine(r, tc_assert_equal_int(10, p->x, "x written correctly"));
  r = tc_combine(r, tc_assert_equal_int(20, p->y, "y written correctly"));

  gpa_free(&a, p);
  bool leaked = gpa_deinit(&a);
  r = tc_combine(r, tc_assert_false(leaked, "no leaks after free"));
  return r;
}

static TestResult test_alloc_array(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  Person *people = gpa_malloc(&a, sizeof(Person) * 4);
  TestResult r = tc_assert_not_nil(people, "array alloc non-null");

  people[0].age = 30;
  people[3].age = 99;
  r = tc_combine(r, tc_assert_equal_int(30, people[0].age, "first element"));
  r = tc_combine(r, tc_assert_equal_int(99, people[3].age, "last element"));

  gpa_free(&a, people);
  bool leaked = gpa_deinit(&a);
  r = tc_combine(r, tc_assert_false(leaked, "no leaks"));
  return r;
}

static TestResult test_null_free_is_safe(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());
  gpa_free(&a, nullptr); /* must not crash */
  bool leaked = gpa_deinit(&a);
  return tc_assert_false(leaked, "null free does not leak");
}

/* -------------------------------------------------------------------------
 * Suite: leak detection
 * ---------------------------------------------------------------------- */

static TestResult test_leak_detected(void *env) {
  (void)env;
  Allocator a = gpa_init_log(libc_backing(), "gpa_leak.log");

  Point *p = gpa_malloc(&a, sizeof(Point));
  (void)p; /* intentionally not freed */

  bool leaked = gpa_deinit(&a);
  return tc_assert_true(leaked, "unreleased allocation reported as leak");
}

static TestResult test_multiple_leaks(void *env) {
  (void)env;
  Allocator a = gpa_init_log(libc_backing(), "gpa_multi_leak.log");

  gpa_malloc(&a, sizeof(Point));
  gpa_malloc(&a, sizeof(Person));
  gpa_malloc(&a, 128);

  TestResult r =
      tc_assert_equal_size(3, gpa_live_count(&a), "three live allocations");
  bool leaked = gpa_deinit(&a);
  r = tc_combine(r, tc_assert_true(leaked, "leaks reported"));
  return r;
}

static TestResult test_large_leak_detected(void *env) {
  (void)env;
  Allocator a = gpa_init_log(libc_backing(), "gpa_large_leak.log");

  gpa_malloc(&a, 8192); /* large — above the largest size class */

  bool leaked = gpa_deinit(&a);
  return tc_assert_true(leaked, "large allocation leak detected");
}

/* -------------------------------------------------------------------------
 * Suite: snapshot / check
 * ---------------------------------------------------------------------- */

static TestResult test_snapshot_no_leak(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  Point *p = gpa_malloc(&a, sizeof(Point));
  GpaSnapshot snap = gpa_snapshot(&a);

  /* nothing allocated after snapshot */
  bool leaked = gpa_check(&a, snap);
  TestResult r = tc_assert_false(leaked, "no leak since snapshot");

  gpa_free(&a, p);
  gpa_deinit(&a);
  return r;
}

static TestResult test_snapshot_catches_leak(void *env) {
  (void)env;
  Allocator a = gpa_init_log(libc_backing(), "gpa_snap.log");

  Point *pre = gpa_malloc(&a, sizeof(Point));
  GpaSnapshot snap = gpa_snapshot(&a);

  gpa_malloc(&a, sizeof(Point)); /* allocated after snap, not freed */

  bool leaked = gpa_check(&a, snap);
  TestResult r = tc_assert_true(leaked, "leak after snapshot detected");

  gpa_free(&a, pre);
  gpa_deinit(&a); /* frees remaining allocation */
  return r;
}

static TestResult test_snapshot_brackets_clean_block(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  GpaSnapshot snap = gpa_snapshot(&a);
  Point *p = gpa_malloc(&a, sizeof(Point));
  gpa_free(&a, p);

  bool leaked = gpa_check(&a, snap);
  TestResult r =
      tc_assert_false(leaked, "alloc+free within snapshot window is clean");
  gpa_deinit(&a);
  return r;
}

/* -------------------------------------------------------------------------
 * Suite: realloc
 * ---------------------------------------------------------------------- */

static TestResult test_realloc_grows(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  int *arr = gpa_malloc(&a, sizeof(int) * 4);
  arr[0] = 42;

  arr = gpa_realloc(&a, arr, sizeof(int) * 16);
  TestResult r = tc_assert_not_nil(arr, "realloc returns non-null");
  r = tc_combine(
      r, tc_assert_equal_int(42, arr[0], "data preserved after realloc"));

  gpa_free(&a, arr);
  bool leaked = gpa_deinit(&a);
  r = tc_combine(r, tc_assert_false(leaked, "no leak after realloc + free"));
  return r;
}

static TestResult test_realloc_null_acts_as_malloc(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  void *p = gpa_realloc(&a, nullptr, 64);
  TestResult r = tc_assert_not_nil(p, "realloc(null) acts as malloc");

  gpa_free(&a, p);
  gpa_deinit(&a);
  return r;
}

static TestResult test_realloc_zero_acts_as_free(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  void *p = gpa_malloc(&a, 32);
  gpa_realloc(&a, p, 0); /* should free */

  bool leaked = gpa_deinit(&a);
  return tc_assert_false(leaked, "realloc(p, 0) frees — no leak");
}

/* -------------------------------------------------------------------------
 * Suite: large allocations
 * ---------------------------------------------------------------------- */

static TestResult test_large_alloc_free(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  void *big = gpa_malloc(&a, 8192);
  TestResult r = tc_assert_not_nil(big, "large alloc succeeds");
  r = tc_combine(
      r, tc_assert_equal_size(1, gpa_live_count(&a), "one live large alloc"));

  gpa_free(&a, big);
  r = tc_combine(
      r, tc_assert_equal_size(0, gpa_live_count(&a), "zero live after free"));

  bool leaked = gpa_deinit(&a);
  r = tc_combine(r, tc_assert_false(leaked, "no leaks"));
  return r;
}

/* -------------------------------------------------------------------------
 * Suite: live_count and dump
 * ---------------------------------------------------------------------- */

static TestResult test_live_count_tracks_allocs(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  TestResult r = tc_assert_equal_size(0, gpa_live_count(&a), "starts at zero");

  void *p1 = gpa_malloc(&a, 8);
  r = tc_combine(
      r, tc_assert_equal_size(1, gpa_live_count(&a), "one after first alloc"));

  void *p2 = gpa_malloc(&a, 64);
  r = tc_combine(
      r, tc_assert_equal_size(2, gpa_live_count(&a), "two after second alloc"));

  gpa_free(&a, p1);
  r = tc_combine(
      r, tc_assert_equal_size(1, gpa_live_count(&a), "back to one after free"));

  gpa_free(&a, p2);
  gpa_deinit(&a);
  return r;
}

static TestResult test_dump_runs_without_crash(void *env) {
  (void)env;
  Allocator a = gpa_init_log(libc_backing(), "gpa_dump.log");

  gpa_malloc(&a, sizeof(Point));
  gpa_malloc(&a, sizeof(Person));
  gpa_dump(&a); /* should not crash */

  gpa_deinit(&a);
  return tc_pass();
}

/* -------------------------------------------------------------------------
 * Suite: libc_allocator swap
 * ---------------------------------------------------------------------- */

static TestResult test_libc_allocator_basic(void *env) {
  (void)env;
  Allocator a = libc_allocator();

  Point *p = gpa_malloc(&a, sizeof(Point));
  TestResult r = tc_assert_not_nil(p, "libc_allocator malloc works");

  p->x = 7;
  r = tc_combine(r, tc_assert_equal_int(7, p->x, "data written"));

  gpa_free(&a, p);
  return r;
}

static TestResult test_libc_allocator_debug_noop(void *env) {
  (void)env;
  Allocator a = libc_allocator();

  /* these must not crash when called on libc_allocator */
  GpaSnapshot snap = gpa_snapshot(&a);
  (void)snap;
  gpa_check(&a, snap);
  gpa_dump(&a);
  size_t n = gpa_live_count(&a);

  return tc_assert_equal_size(0, n, "live_count returns 0 on libc_allocator");
}

/* -------------------------------------------------------------------------
 * Suite: linkage
 *
 * gpa.h is static inline throughout, so it may be included in any number
 * of translation units. second_tu.c also includes it; this binary links
 * both. If gpa.h regresses to external linkage, the build fails to link
 * before this test ever runs — which is exactly the guard we want.
 * ---------------------------------------------------------------------- */

static TestResult test_multi_tu_link(void *env) {
  (void)env;
  size_t live = gpa_second_tu_probe();
  return tc_assert_equal_size(0, live, "second TU allocates and frees cleanly");
}

/* -------------------------------------------------------------------------
 * Suite: robustness
 *
 * These guard the bucket-layout fix. The fill-page test writes every byte of
 * every slot and packs a class to capacity, then overflows into a second
 * bucket — exactly the pattern that, under the old slot count, let the top
 * slot stomp the page's bucket header. The boundary test pins routing: 2048
 * is the largest bucket class, 2049 and up go to the large list.
 * ---------------------------------------------------------------------- */

static TestResult test_fill_page_no_header_corruption(void *env) {
  (void)env;
  Allocator a = gpa_init(libc_backing());

  const size_t sc = 64;
  const size_t n = _gpa_slots_for(sc); /* white-box: exact slots per page */

  void *slots[512];
  TestResult r =
      tc_assert_true(n > 0 && n <= 512, "slot count within test buffer");

  /* pack the page and scribble the whole of each slot */
  for (size_t i = 0; i < n; i++) {
    slots[i] = gpa_malloc(&a, sc);
    r = tc_combine(r, tc_assert_not_nil(slots[i], "packed slot non-null"));
    memset(slots[i], 0xAB, sc);
  }
  r = tc_combine(r, tc_assert_equal_size(n, gpa_live_count(&a),
                                         "page packed to capacity"));

  /* one more must spill into a fresh bucket, not overrun this one */
  void *extra = gpa_malloc(&a, sc);
  r = tc_combine(r, tc_assert_not_nil(extra, "overflow allocation succeeds"));
  memset(extra, 0xCD, sc);
  r = tc_combine(
      r, tc_assert_equal_size(n + 1, gpa_live_count(&a), "overflow tracked"));

  for (size_t i = 0; i < n; i++)
    gpa_free(&a, slots[i]);
  gpa_free(&a, extra);
  r = tc_combine(
      r, tc_assert_equal_size(0, gpa_live_count(&a), "all slots freed"));

  bool leaked = gpa_deinit(&a); /* corrupt header => abort or leak here */
  r = tc_combine(r, tc_assert_false(leaked, "clean after full-page churn"));
  return r;
}

static TestResult test_bucket_header_alignment(void *env) {
  (void)env;
  /* Every bucket header sits at the page end; it must stay aligned to
     _Alignof(Bucket) or its size_t/pointer fields are misaligned (UB). One
     allocation per class forces that class's bucket into existence; we then
     recover the header and check its address and that no slot runs into it.
     Guards the page-layout fix without needing a sanitizer build. */
  Allocator a = gpa_init(libc_backing());
  GpaState *s = (GpaState *)a.ctx;

  TestResult r = tc_pass();
  for (size_t i = 0; i < GPA_NUM_SIZE_CLASSES; i++) {
    void *p = gpa_malloc(&a, GPA_SIZE_CLASSES[i]);
    r = tc_combine(r, tc_assert_not_nil(p, "class allocation non-null"));

    size_t sci;
    Bucket *b = _gpa_bucket_owning(s, p, &sci);
    r = tc_combine(r, tc_assert_not_nil(b, "owning bucket found"));
    r = tc_combine(
        r, tc_assert_equal_size(0, (size_t)((uintptr_t)b % _Alignof(Bucket)),
                                "header is aligned"));
    r = tc_combine(r, tc_assert_true(b->slot_count * b->size_class <=
                                         _gpa_bucket_offset(b->size_class),
                                     "slots stay below the header"));
    gpa_free(&a, p);
  }

  bool leaked = gpa_deinit(&a);
  return tc_combine(r, tc_assert_false(leaked, "alignment sweep clean"));
}

static TestResult test_size_class_boundary(void *env) {
  (void)env;
  /* white-box: the table tops out at 2048; nothing larger is bucketed */
  TestResult r =
      tc_assert_equal_size(9, GPA_NUM_SIZE_CLASSES, "nine size classes");
  r = tc_combine(
      r, tc_assert_equal_size(2048, GPA_SIZE_CLASSES[GPA_NUM_SIZE_CLASSES - 1],
                              "largest class is 2048"));
  r = tc_combine(r,
                 tc_assert_true(_gpa_sc_index(2048) >= 0, "2048 is bucketed"));
  r = tc_combine(
      r, tc_assert_true(_gpa_sc_index(2049) < 0, "2049 routes to large list"));

  /* behavioural: a bucketed and a large allocation coexist and free cleanly */
  Allocator a = gpa_init(libc_backing());
  void *bucketed = gpa_malloc(&a, 2048);
  void *large = gpa_malloc(&a, 2049);
  r = tc_combine(r, tc_assert_not_nil(bucketed, "2048 alloc ok"));
  r = tc_combine(r, tc_assert_not_nil(large, "2049 alloc ok"));
  r = tc_combine(r, tc_assert_equal_size(2, gpa_live_count(&a), "two live"));

  gpa_free(&a, bucketed);
  gpa_free(&a, large);
  r = tc_combine(
      r, tc_assert_equal_size(0, gpa_live_count(&a), "zero live after free"));

  bool leaked = gpa_deinit(&a);
  r = tc_combine(r, tc_assert_false(leaked, "boundary churn clean"));
  return r;
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv) {
  Suite basics = tc_suite(
      "Basics",
      (Test[]){{.name = "clean alloc and free", .fn = test_clean_alloc_free},
               {.name = "allocate array", .fn = test_alloc_array},
               {.name = "null free is safe", .fn = test_null_free_is_safe},
               {0}});

  Suite leaks = tc_suite(
      "Leak Detection",
      (Test[]){
          {.name = "single leak detected", .fn = test_leak_detected},
          {.name = "multiple leaks detected", .fn = test_multiple_leaks},
          {.name = "large allocation leak", .fn = test_large_leak_detected},
          {0}});

  Suite snaps = tc_suite(
      "Snapshot / Check",
      (Test[]){
          {.name = "snapshot: no leak", .fn = test_snapshot_no_leak},
          {.name = "snapshot: catches leak", .fn = test_snapshot_catches_leak},
          {.name = "snapshot: clean block",
           .fn = test_snapshot_brackets_clean_block},
          {0}});

  Suite reallocs = tc_suite(
      "Realloc",
      (Test[]){
          {.name = "grows and preserves data", .fn = test_realloc_grows},
          {.name = "null acts as malloc",
           .fn = test_realloc_null_acts_as_malloc},
          {.name = "zero acts as free", .fn = test_realloc_zero_acts_as_free},
          {0}});

  Suite large = tc_suite(
      "Large Allocations",
      (Test[]){{.name = "large alloc and free", .fn = test_large_alloc_free},
               {0}});

  Suite inspection =
      tc_suite("Inspection", (Test[]){{.name = "live_count tracks allocations",
                                       .fn = test_live_count_tracks_allocs},
                                      {.name = "dump runs without crash",
                                       .fn = test_dump_runs_without_crash},
                                      {0}});

  Suite swap = tc_suite("libc_allocator Swap",
                        (Test[]){{.name = "basic malloc and free",
                                  .fn = test_libc_allocator_basic},
                                 {.name = "debug functions are no-ops",
                                  .fn = test_libc_allocator_debug_noop},
                                 {0}});

  Suite linkage =
      tc_suite("Linkage", (Test[]){{.name = "multi-TU include links and runs",
                                    .fn = test_multi_tu_link},
                                   {0}});

  Suite robustness = tc_suite(
      "Robustness", (Test[]){{.name = "full page write leaves header intact",
                              .fn = test_fill_page_no_header_corruption},
                             {.name = "bucket headers stay aligned",
                              .fn = test_bucket_header_alignment},
                             {.name = "size-class boundary at 2048",
                              .fn = test_size_class_boundary},
                             {0}});

  Suite *all[] = {&basics,     &leaks, &snaps,   &reallocs,   &large,
                  &inspection, &swap,  &linkage, &robustness, nullptr};
  return tc_main(argc, argv, all);
}
