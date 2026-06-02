#include "testcracks.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TC_NO_GETTIMEOFDAY
#include <sys/time.h>
#else
#include <time.h>
#endif

/* ============================================================
   INTERNAL HELPERS
   ============================================================ */

static double tc__get_time_ms(void) {
#ifndef TC_NO_GETTIMEOFDAY
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
#else
  return (double)clock() / CLOCKS_PER_SEC * 1000.0;
#endif
}

static void tc__safe_copy(char *dst, const char *src, size_t max) {
  size_t len;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  len = strlen(src);
  if (len >= max)
    len = max - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static void tc__append_error(TestResult *r, const char *msg,
                             const char *expected, const char *actual) {
  if (r->error_count < TC_MAX_ERRORS) {
    tc__safe_copy(r->errors[r->error_count].message, msg, TC_MAX_MSG_LEN);
    tc__safe_copy(r->errors[r->error_count].expected, expected, TC_MAX_MSG_LEN);
    tc__safe_copy(r->errors[r->error_count].actual, actual, TC_MAX_MSG_LEN);
    r->error_count++;
  }
}

#ifdef TC_NO_COLORS
#define TC__GREEN ""
#define TC__RED ""
#define TC__YELLOW ""
#define TC__RESET ""
#else
#define TC__GREEN "\033[32m"
#define TC__RED "\033[31m"
#define TC__YELLOW "\033[33m"
#define TC__RESET "\033[0m"
#endif

/* ============================================================
   RESULT CONSTRUCTORS
   ============================================================ */

TestResult tc_pass(void) {
  TestResult r;
  memset(&r, 0, sizeof(r));
  r.tag = TC_PASS;
  return r;
}

TestResult tc_fail(const char *msg) {
  TestResult r;
  memset(&r, 0, sizeof(r));
  r.tag = TC_FAIL;
  tc__append_error(&r, msg, "", "");
  return r;
}

TestResult tc_fail_with(const char *msg, const char *expected,
                        const char *actual) {
  TestResult r;
  memset(&r, 0, sizeof(r));
  r.tag = TC_FAIL;
  tc__append_error(&r, msg, expected, actual);
  return r;
}

TestResult tc_skip(const char *reason) {
  TestResult r;
  memset(&r, 0, sizeof(r));
  r.tag = TC_SKIP;
  tc__append_error(&r, reason, "", "");
  return r;
}

/* ============================================================
   RESULT PREDICATES
   ============================================================ */

int tc_is_pass(TestResult r) { return r.tag == TC_PASS; }
int tc_is_fail(TestResult r) { return r.tag == TC_FAIL; }
int tc_is_skip(TestResult r) { return r.tag == TC_SKIP; }

/* ============================================================
   COMPOSITION
   ============================================================ */

TestResult tc_combine(TestResult a, TestResult b) {
  int i;
  TestResult r;

  if (a.tag == TC_SKIP)
    return a;
  if (b.tag == TC_SKIP)
    return b;
  if (a.tag == TC_PASS && b.tag == TC_PASS)
    return a;

  memset(&r, 0, sizeof(r));
  r.tag = TC_FAIL;

  for (i = 0; i < a.error_count && r.error_count < TC_MAX_ERRORS; i++) {
    r.errors[r.error_count] = a.errors[i];
    r.error_count++;
  }
  for (i = 0; i < b.error_count && r.error_count < TC_MAX_ERRORS; i++) {
    r.errors[r.error_count] = b.errors[i];
    r.error_count++;
  }

  return r;
}

/* ============================================================
   SKIP GUARDS
   ============================================================ */

TestResult tc_skip_if(int cond, const char *reason) {
  return cond ? tc_skip(reason) : tc_pass();
}

TestResult tc_skip_unless(int cond, const char *reason) {
  return cond ? tc_pass() : tc_skip(reason);
}

/* ============================================================
   ASSERTIONS - BOOLEAN
   ============================================================ */

TestResult tc_assert_true(int cond, const char *msg) {
  if (cond)
    return tc_pass();
  return tc_fail_with(msg, "true", "false");
}

TestResult tc_assert_false(int cond, const char *msg) {
  if (!cond)
    return tc_pass();
  return tc_fail_with(msg, "false", "true");
}

/* ============================================================
   ASSERTIONS - EQUALITY (int)
   ============================================================ */

TestResult tc_assert_equal_int(int expected, int actual, const char *msg) {
  char exp[32], act[32];
  if (expected == actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "%d", expected);
  snprintf(act, sizeof(act), "%d", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_not_equal_int(int unexpected, int actual,
                                   const char *msg) {
  char exp[32], act[32];
  if (unexpected != actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "not %d", unexpected);
  snprintf(act, sizeof(act), "%d", actual);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - EQUALITY (long)
   ============================================================ */

TestResult tc_assert_equal_long(long expected, long actual, const char *msg) {
  char exp[32], act[32];
  if (expected == actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "%ld", expected);
  snprintf(act, sizeof(act), "%ld", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_not_equal_long(long unexpected, long actual,
                                    const char *msg) {
  char exp[32], act[32];
  if (unexpected != actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "not %ld", unexpected);
  snprintf(act, sizeof(act), "%ld", actual);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - EQUALITY (size_t)
   ============================================================ */

TestResult tc_assert_equal_size(size_t expected, size_t actual,
                                const char *msg) {
  char exp[32], act[32];
  if (expected == actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "%zu", expected);
  snprintf(act, sizeof(act), "%zu", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_not_equal_size(size_t unexpected, size_t actual,
                                    const char *msg) {
  char exp[32], act[32];
  if (unexpected != actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "not %zu", unexpected);
  snprintf(act, sizeof(act), "%zu", actual);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - EQUALITY (double)
   ============================================================ */

TestResult tc_assert_equal_double(double expected, double actual,
                                  const char *msg) {
  char exp[32], act[32];
  if (expected == actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "%g", expected);
  snprintf(act, sizeof(act), "%g", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_not_equal_double(double unexpected, double actual,
                                      const char *msg) {
  char exp[32], act[32];
  if (unexpected != actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "not %g", unexpected);
  snprintf(act, sizeof(act), "%g", actual);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - EQUALITY (string)
   ============================================================ */

TestResult tc_assert_equal_str(const char *expected, const char *actual,
                               const char *msg) {
  const char *exp_str = expected ? expected : "(null)";
  const char *act_str = actual ? actual : "(null)";
  if (expected == NULL && actual == NULL)
    return tc_pass();
  if (expected == NULL || actual == NULL) {
    return tc_fail_with(msg, exp_str, act_str);
  }
  if (strcmp(expected, actual) == 0)
    return tc_pass();
  return tc_fail_with(msg, exp_str, act_str);
}

TestResult tc_assert_not_equal_str(const char *unexpected, const char *actual,
                                   const char *msg) {
  char exp[TC_MAX_MSG_LEN];
  const char *act_str = actual ? actual : "(null)";
  if (unexpected == NULL && actual == NULL) {
    snprintf(exp, sizeof(exp), "not (null)");
    return tc_fail_with(msg, exp, act_str);
  }
  if (unexpected == NULL || actual == NULL)
    return tc_pass();
  if (strcmp(unexpected, actual) != 0)
    return tc_pass();
  snprintf(exp, sizeof(exp), "not \"%s\"", unexpected);
  return tc_fail_with(msg, exp, act_str);
}

/* ============================================================
   ASSERTIONS - EQUALITY (pointer)
   ============================================================ */

TestResult tc_assert_equal_ptr(const void *expected, const void *actual,
                               const char *msg) {
  char exp[32], act[32];
  if (expected == actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "%p", expected);
  snprintf(act, sizeof(act), "%p", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_not_equal_ptr(const void *unexpected, const void *actual,
                                   const char *msg) {
  char exp[32], act[32];
  if (unexpected != actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "not %p", unexpected);
  snprintf(act, sizeof(act), "%p", actual);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - NIL/NULL
   ============================================================ */

TestResult tc_assert_nil(const void *ptr, const char *msg) {
  char act[32];
  if (ptr == NULL)
    return tc_pass();
  snprintf(act, sizeof(act), "%p", ptr);
  return tc_fail_with(msg, "NULL", act);
}

TestResult tc_assert_not_nil(const void *ptr, const char *msg) {
  if (ptr != NULL)
    return tc_pass();
  return tc_fail_with(msg, "non-NULL", "NULL");
}

/* ============================================================
   ASSERTIONS - NUMERIC COMPARISONS (int)
   ============================================================ */

TestResult tc_assert_greater_int(int actual, int than, const char *msg) {
  char exp[32], act[32];
  if (actual > than)
    return tc_pass();
  snprintf(exp, sizeof(exp), "> %d", than);
  snprintf(act, sizeof(act), "%d", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_greater_or_equal_int(int actual, int than,
                                          const char *msg) {
  char exp[32], act[32];
  if (actual >= than)
    return tc_pass();
  snprintf(exp, sizeof(exp), ">= %d", than);
  snprintf(act, sizeof(act), "%d", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_less_int(int actual, int than, const char *msg) {
  char exp[32], act[32];
  if (actual < than)
    return tc_pass();
  snprintf(exp, sizeof(exp), "< %d", than);
  snprintf(act, sizeof(act), "%d", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_less_or_equal_int(int actual, int than, const char *msg) {
  char exp[32], act[32];
  if (actual <= than)
    return tc_pass();
  snprintf(exp, sizeof(exp), "<= %d", than);
  snprintf(act, sizeof(act), "%d", actual);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - NUMERIC COMPARISONS (double)
   ============================================================ */

TestResult tc_assert_greater_double(double actual, double than,
                                    const char *msg) {
  char exp[32], act[32];
  if (actual > than)
    return tc_pass();
  snprintf(exp, sizeof(exp), "> %g", than);
  snprintf(act, sizeof(act), "%g", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_greater_or_equal_double(double actual, double than,
                                             const char *msg) {
  char exp[32], act[32];
  if (actual >= than)
    return tc_pass();
  snprintf(exp, sizeof(exp), ">= %g", than);
  snprintf(act, sizeof(act), "%g", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_less_double(double actual, double than, const char *msg) {
  char exp[32], act[32];
  if (actual < than)
    return tc_pass();
  snprintf(exp, sizeof(exp), "< %g", than);
  snprintf(act, sizeof(act), "%g", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_less_or_equal_double(double actual, double than,
                                          const char *msg) {
  char exp[32], act[32];
  if (actual <= than)
    return tc_pass();
  snprintf(exp, sizeof(exp), "<= %g", than);
  snprintf(act, sizeof(act), "%g", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_in_delta(double expected, double actual, double delta,
                              const char *msg) {
  char exp[64], act[64];
  double diff = fabs(expected - actual);
  if (diff <= delta)
    return tc_pass();
  snprintf(exp, sizeof(exp), "%g +/- %g", expected, delta);
  snprintf(act, sizeof(act), "%g (diff: %g)", actual, diff);
  return tc_fail_with(msg, exp, act);
}

/* ============================================================
   ASSERTIONS - COLLECTIONS (int arrays)
   ============================================================ */

TestResult tc_assert_empty_int(const int *arr, int len, const char *msg) {
  char act[32];
  (void)arr;
  if (len == 0)
    return tc_pass();
  snprintf(act, sizeof(act), "%d elements", len);
  return tc_fail_with(msg, "empty", act);
}

TestResult tc_assert_not_empty_int(const int *arr, int len, const char *msg) {
  (void)arr;
  if (len > 0)
    return tc_pass();
  return tc_fail_with(msg, "non-empty", "0 elements");
}

TestResult tc_assert_len(int expected, int actual, const char *msg) {
  char exp[32], act[32];
  if (expected == actual)
    return tc_pass();
  snprintf(exp, sizeof(exp), "length %d", expected);
  snprintf(act, sizeof(act), "length %d", actual);
  return tc_fail_with(msg, exp, act);
}

TestResult tc_assert_contains_int(int elem, const int *arr, int len,
                                  const char *msg) {
  char exp[32];
  int i;
  for (i = 0; i < len; i++) {
    if (arr[i] == elem)
      return tc_pass();
  }
  snprintf(exp, sizeof(exp), "contains %d", elem);
  return tc_fail_with(msg, exp, "not found");
}

TestResult tc_assert_not_contains_int(int elem, const int *arr, int len,
                                      const char *msg) {
  char exp[32], act[64];
  int i;
  for (i = 0; i < len; i++) {
    if (arr[i] == elem) {
      snprintf(exp, sizeof(exp), "not contains %d", elem);
      snprintf(act, sizeof(act), "found at index %d", i);
      return tc_fail_with(msg, exp, act);
    }
  }
  return tc_pass();
}

/* ============================================================
   SUITE CONSTRUCTION
   ============================================================ */

Suite tc_suite(const char *name, Test *tests) {
  return tc_suite_with(name, NULL, NULL, tests);
}

Suite tc_suite_with(const char *name, SetupFn setup, TeardownFn teardown,
                    Test *tests) {
  Suite s;
  int i = 0;

  memset(&s, 0, sizeof(s));
  s.name = name;
  s.setup = setup;
  s.teardown = teardown;

  while (tests[i].name != NULL && i < TC_MAX_TESTS_PER_SUITE) {
    s.tests[i] = tests[i];
    i++;
  }
  s.test_count = i;

  return s;
}

Test tc_skip_test(const char *name, const char *reason) {
  Test t;
  memset(&t, 0, sizeof(t));
  t.name = name;
  t.fn = NULL;
  t.skip_reason = reason;
  return t;
}

/* ============================================================
   RUNNERS
   ============================================================ */

TestResult tc_run_test(Test *test, void *env) {
  TestResult r;
  double start;

  if (test->fn == NULL) {
    return tc_skip(test->skip_reason ? test->skip_reason : "skipped");
  }

  start = tc__get_time_ms();
  r = test->fn(env);
  r.elapsed_ms = tc__get_time_ms() - start;

  return r;
}

void tc_print_result(const char *name, TestResult *result) {
  const char *icon;
  const char *color;
  int i;

  switch (result->tag) {
  case TC_PASS:
    icon = "\xe2\x9c\x93";
    color = TC__GREEN;
    break; /* ✓ */
  case TC_FAIL:
    icon = "\xe2\x9c\x97";
    color = TC__RED;
    break; /* ✗ */
  case TC_SKIP:
    icon = "\xe2\x97\x8b";
    color = TC__YELLOW;
    break; /* ○ */
  default:
    icon = "?";
    color = "";
    break;
  }

  printf("  %s%s%s %s (%.2fms)\n", color, icon, TC__RESET, name,
         result->elapsed_ms);

  if (result->tag == TC_FAIL) {
    for (i = 0; i < result->error_count; i++) {
      printf("      %s\n", result->errors[i].message);
      if (result->errors[i].expected[0] != '\0') {
        printf("        Expected: %s\n", result->errors[i].expected);
        printf("        Actual:   %s\n", result->errors[i].actual);
      }
    }
  } else if (result->tag == TC_SKIP && result->error_count > 0) {
    printf("      [%s]\n", result->errors[0].message);
  }
}

/* Storage for test results (needed for XML output) */
static TestResult tc__results[TC_MAX_SUITES][TC_MAX_TESTS_PER_SUITE];
static int tc__result_counts[TC_MAX_SUITES];

static RunSummary tc__run_suite_ex(Suite *suite, int suite_idx) {
  RunSummary summary;
  int idx = (suite_idx >= 0 && suite_idx < TC_MAX_SUITES) ? suite_idx : 0;
  double start;
  int i;
  void *env = NULL;

  memset(&summary, 0, sizeof(summary));
  start = tc__get_time_ms();

  if (suite->setup) {
    int ret = suite->setup(&env);
    if (ret != 0) {
      summary.total_ms = tc__get_time_ms() - start;
      printf("\n=== %s (%.2fms) ===\n", suite->name, summary.total_ms);
      printf("  %s\xe2\x9c\x97%s Setup failed (returned %d)\n", TC__RED,
             TC__RESET, ret);
      summary.errored = suite->test_count;
      return summary;
    }
  }

  for (i = 0; i < suite->test_count; i++) {
    tc__results[idx][i] = tc_run_test(&suite->tests[i], env);
    tc__result_counts[idx] = i + 1;

    switch (tc__results[idx][i].tag) {
    case TC_PASS:
      summary.passed++;
      break;
    case TC_FAIL:
      summary.failed++;
      break;
    case TC_SKIP:
      summary.skipped++;
      break;
    }
  }

  if (suite->teardown) {
    suite->teardown(env);
  }

  summary.total_ms = tc__get_time_ms() - start;

  printf("\n=== %s (%.2fms) ===\n", suite->name, summary.total_ms);
  for (i = 0; i < suite->test_count; i++) {
    tc_print_result(suite->tests[i].name, &tc__results[idx][i]);
  }

  return summary;
}

RunSummary tc_run_suite(Suite *suite) { return tc__run_suite_ex(suite, -1); }

static RunSummary tc__run_all_ex(Suite **suites, int store_results) {
  RunSummary total;
  RunSummary s;
  double start;
  int i;

  memset(&total, 0, sizeof(total));
  memset(tc__result_counts, 0, sizeof(tc__result_counts));
  start = tc__get_time_ms();

  for (i = 0; suites[i] != NULL; i++) {
    s = tc__run_suite_ex(suites[i], store_results ? i : -1);
    total.passed += s.passed;
    total.failed += s.failed;
    total.skipped += s.skipped;
    total.errored += s.errored;
  }

  total.total_ms = tc__get_time_ms() - start;
  return total;
}

RunSummary tc_run_all(Suite **suites) { return tc__run_all_ex(suites, 1); }

int tc_print_summary(RunSummary summary) {
  int total =
      summary.passed + summary.failed + summary.skipped + summary.errored;

  printf("\n%d/%d passed, %d failed, %d skipped, %d errored (Total: %.2fms)\n",
         summary.passed, total, summary.failed, summary.skipped,
         summary.errored, summary.total_ms);

  return (summary.failed > 0 || summary.errored > 0) ? 1 : 0;
}

/* ============================================================
   JUNIT XML OUTPUT
   ============================================================ */

static void tc__xml_escape(char *dst, const char *src, size_t max) {
  size_t di = 0;
  size_t si;
  if (!src) {
    dst[0] = '\0';
    return;
  }

  for (si = 0; src[si] != '\0' && di < max - 6; si++) {
    switch (src[si]) {
    case '&':
      memcpy(dst + di, "&amp;", 5);
      di += 5;
      break;
    case '<':
      memcpy(dst + di, "&lt;", 4);
      di += 4;
      break;
    case '>':
      memcpy(dst + di, "&gt;", 4);
      di += 4;
      break;
    case '"':
      memcpy(dst + di, "&quot;", 6);
      di += 6;
      break;
    case '\'':
      memcpy(dst + di, "&apos;", 6);
      di += 6;
      break;
    default:
      dst[di++] = src[si];
      break;
    }
  }
  dst[di] = '\0';
}

int tc_write_junit_xml(const char *filename, Suite **suites,
                       RunSummary summary) {
  FILE *f;
  int i, j, suite_count;
  int total_tests, total_failures, total_skipped;
  char escaped[TC_MAX_MSG_LEN * 2];

  f = fopen(filename, "w");
  if (!f) {
    fprintf(stderr, "Error: Cannot open file '%s' for writing\n", filename);
    return -1;
  }

  total_tests = summary.passed + summary.failed + summary.skipped;
  total_failures = summary.failed;
  total_skipped = summary.skipped;

  for (suite_count = 0; suites[suite_count] != NULL; suite_count++)
    ;

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f,
          "<testsuites tests=\"%d\" failures=\"%d\" errors=\"0\" "
          "skipped=\"%d\" time=\"%.3f\">\n",
          total_tests, total_failures, total_skipped,
          summary.total_ms / 1000.0);

  for (i = 0; i < suite_count && suites[i] != NULL; i++) {
    Suite *suite = suites[i];
    int suite_passed = 0, suite_failed = 0, suite_skipped = 0;
    double suite_time = 0;

    for (j = 0; j < tc__result_counts[i]; j++) {
      TestResult *r = &tc__results[i][j];
      suite_time += r->elapsed_ms;
      switch (r->tag) {
      case TC_PASS:
        suite_passed++;
        break;
      case TC_FAIL:
        suite_failed++;
        break;
      case TC_SKIP:
        suite_skipped++;
        break;
      }
    }

    tc__xml_escape(escaped, suite->name, sizeof(escaped));
    fprintf(f,
            "    <testsuite name=\"%s\" tests=\"%d\" failures=\"%d\" "
            "errors=\"0\" skipped=\"%d\" time=\"%.3f\">\n",
            escaped, suite_passed + suite_failed + suite_skipped, suite_failed,
            suite_skipped, suite_time / 1000.0);

    for (j = 0; j < tc__result_counts[i] && j < suite->test_count; j++) {
      TestResult *r = &tc__results[i][j];
      tc__xml_escape(escaped, suite->tests[j].name, sizeof(escaped));

      switch (r->tag) {
      case TC_PASS:
        fprintf(f, "        <testcase name=\"%s\" time=\"%.3f\"/>\n", escaped,
                r->elapsed_ms / 1000.0);
        break;

      case TC_FAIL:
        fprintf(f, "        <testcase name=\"%s\" time=\"%.3f\">\n", escaped,
                r->elapsed_ms / 1000.0);
        if (r->error_count > 0) {
          char msg_escaped[TC_MAX_MSG_LEN * 2];
          char detail[TC_MAX_MSG_LEN * 4];
          int k, pos = 0;

          tc__xml_escape(msg_escaped, r->errors[0].message,
                         sizeof(msg_escaped));

          for (k = 0; k < r->error_count && pos < (int)sizeof(detail) - 100;
               k++) {
            tc__xml_escape(escaped, r->errors[k].message, sizeof(escaped));
            pos +=
                snprintf(detail + pos, sizeof(detail) - pos, "%s\n", escaped);
            if (r->errors[k].expected[0] != '\0') {
              tc__xml_escape(escaped, r->errors[k].expected, sizeof(escaped));
              pos += snprintf(detail + pos, sizeof(detail) - pos,
                              "  Expected: %s\n", escaped);
              tc__xml_escape(escaped, r->errors[k].actual, sizeof(escaped));
              pos += snprintf(detail + pos, sizeof(detail) - pos,
                              "  Actual:   %s\n", escaped);
            }
          }

          fprintf(f,
                  "            <failure message=\"%s\" "
                  "type=\"AssertionError\">%s</failure>\n",
                  msg_escaped, detail);
        }
        fprintf(f, "        </testcase>\n");
        break;

      case TC_SKIP:
        fprintf(f, "        <testcase name=\"%s\" time=\"0\">\n", escaped);
        if (r->error_count > 0) {
          tc__xml_escape(escaped, r->errors[0].message, sizeof(escaped));
          fprintf(f, "            <skipped message=\"%s\"/>\n", escaped);
        } else {
          fprintf(f, "            <skipped/>\n");
        }
        fprintf(f, "        </testcase>\n");
        break;
      }
    }

    fprintf(f, "    </testsuite>\n");
  }

  fprintf(f, "</testsuites>\n");
  fclose(f);

  return 0;
}

/* ============================================================
   CLI
   ============================================================ */

static void tc__print_help(const char *prog) {
  printf("Usage: %s [options]\n", prog);
  printf("\nOptions:\n");
  printf("  --help                  Show this help\n");
  printf("  --list                  List all tests\n");
  printf("  --suite \"name\"          Run specific suite\n");
  printf("  --test \"suite\" \"test\"   Run specific test\n");
  printf("  --match \"pattern\"       Run tests matching pattern\n");
  printf("  --xml \"file\"            Output results as JUnit XML\n");
}

static void tc__list_tests(Suite **suites) {
  int i, j;
  for (i = 0; suites[i] != NULL; i++) {
    printf("%s:\n", suites[i]->name);
    for (j = 0; j < suites[i]->test_count; j++) {
      const char *status = suites[i]->tests[j].fn ? "" : " [skip]";
      printf("  - %s%s\n", suites[i]->tests[j].name, status);
    }
  }
}

static int tc__matches(const char *str, const char *pattern) {
  return strstr(str, pattern) != NULL;
}

int tc_main(int argc, char **argv, Suite **suites) {
  const char *suite_filter = NULL;
  const char *test_filter = NULL;
  const char *match_filter = NULL;
  const char *xml_file = NULL;
  int list_only = 0;
  int i, j, count;
  RunSummary summary;

  Suite *filtered[TC_MAX_SUITES + 1];
  static Suite filtered_suites[TC_MAX_SUITES];

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      tc__print_help(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--list") == 0) {
      list_only = 1;
    } else if (strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
      suite_filter = argv[++i];
    } else if (strcmp(argv[i], "--test") == 0 && i + 2 < argc) {
      suite_filter = argv[++i];
      test_filter = argv[++i];
    } else if (strcmp(argv[i], "--match") == 0 && i + 1 < argc) {
      match_filter = argv[++i];
    } else if (strcmp(argv[i], "--xml") == 0 && i + 1 < argc) {
      xml_file = argv[++i];
    }
  }

  if (xml_file) {
    FILE *f = fopen(xml_file, "w");
    if (!f) {
      fprintf(stderr, "Error: Cannot create XML file '%s'\n", xml_file);
      return 1;
    }
    fclose(f);
  }

  if (list_only) {
    tc__list_tests(suites);
    return 0;
  }

  count = 0;
  for (i = 0; suites[i] != NULL && count < TC_MAX_SUITES; i++) {
    if (suite_filter && !tc__matches(suites[i]->name, suite_filter)) {
      continue;
    }

    if (test_filter || match_filter) {
      Suite temp;
      memset(&temp, 0, sizeof(temp));
      temp.name = suites[i]->name;
      temp.setup = suites[i]->setup;
      temp.teardown = suites[i]->teardown;

      for (j = 0; j < suites[i]->test_count; j++) {
        int include = 0;
        if (test_filter && tc__matches(suites[i]->tests[j].name, test_filter)) {
          include = 1;
        }
        if (match_filter &&
            tc__matches(suites[i]->tests[j].name, match_filter)) {
          include = 1;
        }
        if (include) {
          temp.tests[temp.test_count++] = suites[i]->tests[j];
        }
      }

      if (temp.test_count > 0) {
        filtered_suites[count] = temp;
        filtered[count] = &filtered_suites[count];
        count++;
      }
    } else {
      filtered[count++] = suites[i];
    }
  }
  filtered[count] = NULL;

  if (count == 0) {
    printf("No tests matched filters.\n");
    return 1;
  }

  summary = tc__run_all_ex(filtered, 1);

  if (xml_file) {
    if (tc_write_junit_xml(xml_file, filtered, summary) == 0) {
      printf("\nResults written to %s\n", xml_file);
    }
  }

  return tc_print_summary(summary);
}
