#include "testcracks.h"
#include "gpa.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

typedef struct { int x; int y; } Point;
typedef struct { char name[32]; int age; } Person;

/* -------------------------------------------------------------------------
 * Suite: basics
 * ---------------------------------------------------------------------- */

static TestResult test_clean_alloc_free(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    Point* p = gpa_malloc(&a, sizeof(Point));
    TestResult r = tc_assert_not_nil(p, "malloc returns non-null");

    p->x = 10; p->y = 20;
    r = tc_combine(r, tc_assert_equal_int(10, p->x, "x written correctly"));
    r = tc_combine(r, tc_assert_equal_int(20, p->y, "y written correctly"));

    gpa_free(&a, p);
    bool leaked = gpa_deinit(&a);
    r = tc_combine(r, tc_assert_false(leaked, "no leaks after free"));
    return r;
}

static TestResult test_alloc_array(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    Person* people = gpa_malloc(&a, sizeof(Person) * 4);
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

static TestResult test_null_free_is_safe(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());
    gpa_free(&a, nullptr);   /* must not crash */
    bool leaked = gpa_deinit(&a);
    return tc_assert_false(leaked, "null free does not leak");
}

/* -------------------------------------------------------------------------
 * Suite: leak detection
 * ---------------------------------------------------------------------- */

static TestResult test_leak_detected(void* env) {
    (void)env;
    Allocator a = gpa_init_log(libc_backing(), "gpa_leak.log");

    Point* p = gpa_malloc(&a, sizeof(Point));
    (void)p;   /* intentionally not freed */

    bool leaked = gpa_deinit(&a);
    return tc_assert_true(leaked, "unreleased allocation reported as leak");
}

static TestResult test_multiple_leaks(void* env) {
    (void)env;
    Allocator a = gpa_init_log(libc_backing(), "gpa_multi_leak.log");

    gpa_malloc(&a, sizeof(Point));
    gpa_malloc(&a, sizeof(Person));
    gpa_malloc(&a, 128);

    TestResult r = tc_assert_equal_size(3, gpa_live_count(&a), "three live allocations");
    bool leaked = gpa_deinit(&a);
    r = tc_combine(r, tc_assert_true(leaked, "leaks reported"));
    return r;
}

static TestResult test_large_leak_detected(void* env) {
    (void)env;
    Allocator a = gpa_init_log(libc_backing(), "gpa_large_leak.log");

    gpa_malloc(&a, 8192);   /* large — above GPA_LARGE_THRESHOLD */

    bool leaked = gpa_deinit(&a);
    return tc_assert_true(leaked, "large allocation leak detected");
}

/* -------------------------------------------------------------------------
 * Suite: snapshot / check
 * ---------------------------------------------------------------------- */

static TestResult test_snapshot_no_leak(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    Point* p = gpa_malloc(&a, sizeof(Point));
    GpaSnapshot snap = gpa_snapshot(&a);

    /* nothing allocated after snapshot */
    bool leaked = gpa_check(&a, snap);
    TestResult r = tc_assert_false(leaked, "no leak since snapshot");

    gpa_free(&a, p);
    gpa_deinit(&a);
    return r;
}

static TestResult test_snapshot_catches_leak(void* env) {
    (void)env;
    Allocator a = gpa_init_log(libc_backing(), "gpa_snap.log");

    Point* pre = gpa_malloc(&a, sizeof(Point));
    GpaSnapshot snap = gpa_snapshot(&a);

    gpa_malloc(&a, sizeof(Point));   /* allocated after snap, not freed */

    bool leaked = gpa_check(&a, snap);
    TestResult r = tc_assert_true(leaked, "leak after snapshot detected");

    gpa_free(&a, pre);
    gpa_deinit(&a);   /* frees remaining allocation */
    return r;
}

static TestResult test_snapshot_brackets_clean_block(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    GpaSnapshot snap = gpa_snapshot(&a);
    Point* p = gpa_malloc(&a, sizeof(Point));
    gpa_free(&a, p);

    bool leaked = gpa_check(&a, snap);
    TestResult r = tc_assert_false(leaked, "alloc+free within snapshot window is clean");
    gpa_deinit(&a);
    return r;
}

/* -------------------------------------------------------------------------
 * Suite: realloc
 * ---------------------------------------------------------------------- */

static TestResult test_realloc_grows(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    int* arr = gpa_malloc(&a, sizeof(int) * 4);
    arr[0] = 42;

    arr = gpa_realloc(&a, arr, sizeof(int) * 16);
    TestResult r = tc_assert_not_nil(arr, "realloc returns non-null");
    r = tc_combine(r, tc_assert_equal_int(42, arr[0], "data preserved after realloc"));

    gpa_free(&a, arr);
    bool leaked = gpa_deinit(&a);
    r = tc_combine(r, tc_assert_false(leaked, "no leak after realloc + free"));
    return r;
}

static TestResult test_realloc_null_acts_as_malloc(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    void* p = gpa_realloc(&a, nullptr, 64);
    TestResult r = tc_assert_not_nil(p, "realloc(null) acts as malloc");

    gpa_free(&a, p);
    gpa_deinit(&a);
    return r;
}

static TestResult test_realloc_zero_acts_as_free(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    void* p = gpa_malloc(&a, 32);
    gpa_realloc(&a, p, 0);   /* should free */

    bool leaked = gpa_deinit(&a);
    return tc_assert_false(leaked, "realloc(p, 0) frees — no leak");
}

/* -------------------------------------------------------------------------
 * Suite: large allocations
 * ---------------------------------------------------------------------- */

static TestResult test_large_alloc_free(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    void* big = gpa_malloc(&a, 8192);
    TestResult r = tc_assert_not_nil(big, "large alloc succeeds");
    r = tc_combine(r, tc_assert_equal_size(1, gpa_live_count(&a), "one live large alloc"));

    gpa_free(&a, big);
    r = tc_combine(r, tc_assert_equal_size(0, gpa_live_count(&a), "zero live after free"));

    bool leaked = gpa_deinit(&a);
    r = tc_combine(r, tc_assert_false(leaked, "no leaks"));
    return r;
}

/* -------------------------------------------------------------------------
 * Suite: live_count and dump
 * ---------------------------------------------------------------------- */

static TestResult test_live_count_tracks_allocs(void* env) {
    (void)env;
    Allocator a = gpa_init(libc_backing());

    TestResult r = tc_assert_equal_size(0, gpa_live_count(&a), "starts at zero");

    void* p1 = gpa_malloc(&a, 8);
    r = tc_combine(r, tc_assert_equal_size(1, gpa_live_count(&a), "one after first alloc"));

    void* p2 = gpa_malloc(&a, 64);
    r = tc_combine(r, tc_assert_equal_size(2, gpa_live_count(&a), "two after second alloc"));

    gpa_free(&a, p1);
    r = tc_combine(r, tc_assert_equal_size(1, gpa_live_count(&a), "back to one after free"));

    gpa_free(&a, p2);
    gpa_deinit(&a);
    return r;
}

static TestResult test_dump_runs_without_crash(void* env) {
    (void)env;
    Allocator a = gpa_init_log(libc_backing(), "gpa_dump.log");

    gpa_malloc(&a, sizeof(Point));
    gpa_malloc(&a, sizeof(Person));
    gpa_dump(&a);   /* should not crash */

    gpa_deinit(&a);
    return tc_pass();
}

/* -------------------------------------------------------------------------
 * Suite: libc_allocator swap
 * ---------------------------------------------------------------------- */

static TestResult test_libc_allocator_basic(void* env) {
    (void)env;
    Allocator a = libc_allocator();

    Point* p = gpa_malloc(&a, sizeof(Point));
    TestResult r = tc_assert_not_nil(p, "libc_allocator malloc works");

    p->x = 7;
    r = tc_combine(r, tc_assert_equal_int(7, p->x, "data written"));

    gpa_free(&a, p);
    return r;
}

static TestResult test_libc_allocator_debug_noop(void* env) {
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
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char** argv) {
    Suite basics = tc_suite("Basics", (Test[]){
        {.name = "clean alloc and free", .fn = test_clean_alloc_free},
        {.name = "allocate array", .fn = test_alloc_array},
        {.name = "null free is safe", .fn = test_null_free_is_safe},
        {0}
    });

    Suite leaks = tc_suite("Leak Detection", (Test[]){
        {.name = "single leak detected", .fn = test_leak_detected},
        {.name = "multiple leaks detected", .fn = test_multiple_leaks},
        {.name = "large allocation leak", .fn = test_large_leak_detected},
        {0}
    });

    Suite snaps = tc_suite("Snapshot / Check", (Test[]){
        {.name = "snapshot: no leak", .fn = test_snapshot_no_leak},
        {.name = "snapshot: catches leak", .fn = test_snapshot_catches_leak},
        {.name = "snapshot: clean block", .fn = test_snapshot_brackets_clean_block},
        {0}
    });

    Suite reallocs = tc_suite("Realloc", (Test[]){
        {.name = "grows and preserves data", .fn = test_realloc_grows},
        {.name = "null acts as malloc", .fn = test_realloc_null_acts_as_malloc},
        {.name = "zero acts as free", .fn = test_realloc_zero_acts_as_free},
        {0}
    });

    Suite large = tc_suite("Large Allocations", (Test[]){
        {.name = "large alloc and free", .fn = test_large_alloc_free},
        {0}
    });

    Suite inspection = tc_suite("Inspection", (Test[]){
        {.name = "live_count tracks allocations", .fn = test_live_count_tracks_allocs},
        {.name = "dump runs without crash", .fn = test_dump_runs_without_crash},
        {0}
    });

    Suite swap = tc_suite("libc_allocator Swap", (Test[]){
        {.name = "basic malloc and free", .fn = test_libc_allocator_basic},
        {.name = "debug functions are no-ops", .fn = test_libc_allocator_debug_noop},
        {0}
    });

    Suite* all[] = { &basics, &leaks, &snaps, &reallocs, &large, &inspection, &swap, nullptr };
    return tc_main(argc, argv, all);
}
