/*
 * TestCracks
 * Minimal C testing framework using Railway-Oriented Programming.
 *
 * Repository: https://github.com/qa3-tech/testcracks
 * License: MIT
 *
 * USAGE:
 *   #include "testcracks.h"
 *   Link with testcracks.c
 *
 * OPTIONAL DEFINES (before including):
 *   TC_NO_GETTIMEOFDAY   - Use clock() instead of gettimeofday()
 *   TC_NO_COLORS         - Disable ANSI color output
 *
 * COMPATIBILITY:
 *   C: C99 or later
 *   C++: Any version (has extern "C" guards)
 *   Platforms: Any (Linux, macOS, BSD, Windows MSVC, MinGW, embedded)
 */

#ifndef TESTCRACKS_H
#define TESTCRACKS_H

#include <stddef.h>

/* ============================================================
   PLATFORM DETECTION
   ============================================================ */

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
#ifndef TC_NO_GETTIMEOFDAY
#define TC_NO_GETTIMEOFDAY
#endif
#endif

/* ============================================================
   C++ COMPATIBILITY
   ============================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
   CONFIGURATION
   ============================================================ */

#ifndef TC_MAX_ERRORS
#define TC_MAX_ERRORS 50
#endif

#ifndef TC_MAX_MSG_LEN
#define TC_MAX_MSG_LEN 512
#endif

#ifndef TC_MAX_TESTS_PER_SUITE
#define TC_MAX_TESTS_PER_SUITE 256
#endif

#ifndef TC_MAX_SUITES
#define TC_MAX_SUITES 64
#endif

/* ============================================================
   CORE TYPES
   ============================================================ */

typedef enum { TC_PASS, TC_FAIL, TC_SKIP } ResultTag;

typedef struct {
  char message[TC_MAX_MSG_LEN];
  char expected[TC_MAX_MSG_LEN];
  char actual[TC_MAX_MSG_LEN];
} TestError;

typedef struct {
  ResultTag tag;
  TestError errors[TC_MAX_ERRORS];
  int error_count;
  double elapsed_ms;
} TestResult;

/*
 * Test function receives environment (NULL if no setup).
 * User casts to their own type.
 */
typedef TestResult (*TestFn)(void *env);

/*
 * Setup allocates environment, stores pointer in *env.
 * Returns 0 on success, non-zero to abort suite.
 */
typedef int (*SetupFn)(void **env);

/*
 * Teardown receives environment, cleans up.
 */
typedef void (*TeardownFn)(void *env);

typedef struct {
  const char *name;
  TestFn fn;
  const char *skip_reason;
} Test;

typedef struct {
  const char *name;
  Test tests[TC_MAX_TESTS_PER_SUITE];
  int test_count;
  SetupFn setup;
  TeardownFn teardown;
} Suite;

typedef struct {
  int passed;
  int failed;
  int skipped;
  int errored;
  double total_ms;
} RunSummary;

/* ============================================================
   RESULT CONSTRUCTORS
   ============================================================ */

TestResult tc_pass(void);
TestResult tc_fail(const char *msg);
TestResult tc_fail_with(const char *msg, const char *expected,
                        const char *actual);
TestResult tc_skip(const char *reason);

/* ============================================================
   RESULT PREDICATES
   ============================================================ */

int tc_is_pass(TestResult r);
int tc_is_fail(TestResult r);
int tc_is_skip(TestResult r);

/* ============================================================
   COMPOSITION
   ============================================================ */

TestResult tc_combine(TestResult a, TestResult b);

/* ============================================================
   SKIP GUARDS
   ============================================================ */

TestResult tc_skip_if(int cond, const char *reason);
TestResult tc_skip_unless(int cond, const char *reason);

/* ============================================================
   ASSERTIONS - BOOLEAN
   ============================================================ */

TestResult tc_assert_true(int cond, const char *msg);
TestResult tc_assert_false(int cond, const char *msg);

/* ============================================================
   ASSERTIONS - EQUALITY
   ============================================================ */

TestResult tc_assert_equal_int(int expected, int actual, const char *msg);
TestResult tc_assert_not_equal_int(int unexpected, int actual, const char *msg);

TestResult tc_assert_equal_long(long expected, long actual, const char *msg);
TestResult tc_assert_not_equal_long(long unexpected, long actual,
                                    const char *msg);

TestResult tc_assert_equal_size(size_t expected, size_t actual,
                                const char *msg);
TestResult tc_assert_not_equal_size(size_t unexpected, size_t actual,
                                    const char *msg);

TestResult tc_assert_equal_double(double expected, double actual,
                                  const char *msg);
TestResult tc_assert_not_equal_double(double unexpected, double actual,
                                      const char *msg);

TestResult tc_assert_equal_str(const char *expected, const char *actual,
                               const char *msg);
TestResult tc_assert_not_equal_str(const char *unexpected, const char *actual,
                                   const char *msg);

TestResult tc_assert_equal_ptr(const void *expected, const void *actual,
                               const char *msg);
TestResult tc_assert_not_equal_ptr(const void *unexpected, const void *actual,
                                   const char *msg);

/* ============================================================
   ASSERTIONS - NIL/NULL
   ============================================================ */

TestResult tc_assert_nil(const void *ptr, const char *msg);
TestResult tc_assert_not_nil(const void *ptr, const char *msg);

/* ============================================================
   ASSERTIONS - NUMERIC COMPARISONS
   ============================================================ */

TestResult tc_assert_greater_int(int actual, int than, const char *msg);
TestResult tc_assert_greater_or_equal_int(int actual, int than,
                                          const char *msg);
TestResult tc_assert_less_int(int actual, int than, const char *msg);
TestResult tc_assert_less_or_equal_int(int actual, int than, const char *msg);

TestResult tc_assert_greater_double(double actual, double than,
                                    const char *msg);
TestResult tc_assert_greater_or_equal_double(double actual, double than,
                                             const char *msg);
TestResult tc_assert_less_double(double actual, double than, const char *msg);
TestResult tc_assert_less_or_equal_double(double actual, double than,
                                          const char *msg);
TestResult tc_assert_in_delta(double expected, double actual, double delta,
                              const char *msg);

/* ============================================================
   ASSERTIONS - COLLECTIONS (int arrays)
   ============================================================ */

TestResult tc_assert_empty_int(const int *arr, int len, const char *msg);
TestResult tc_assert_not_empty_int(const int *arr, int len, const char *msg);
TestResult tc_assert_len(int expected, int actual, const char *msg);
TestResult tc_assert_contains_int(int elem, const int *arr, int len,
                                  const char *msg);
TestResult tc_assert_not_contains_int(int elem, const int *arr, int len,
                                      const char *msg);

/* ============================================================
   SUITE CONSTRUCTION
   ============================================================ */

Suite tc_suite(const char *name, Test *tests);
Suite tc_suite_with(const char *name, SetupFn setup, TeardownFn teardown,
                    Test *tests);
Test tc_skip_test(const char *name, const char *reason);

/* ============================================================
   RUNNERS
   ============================================================ */

TestResult tc_run_test(Test *test, void *env);
RunSummary tc_run_suite(Suite *suite);
RunSummary tc_run_all(Suite **suites);
int tc_main(int argc, char **argv, Suite **suites);

/* ============================================================
   OUTPUT
   ============================================================ */

void tc_print_result(const char *name, TestResult *result);
int tc_print_summary(RunSummary summary);

/* ============================================================
   JUNIT XML OUTPUT
   ============================================================ */

int tc_write_junit_xml(const char *filename, Suite **suites,
                       RunSummary summary);

#ifdef __cplusplus
}
#endif

#endif /* TESTCRACKS_H */
