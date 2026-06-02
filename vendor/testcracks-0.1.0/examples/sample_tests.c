/*
 * TestCracks - Sample Tests
 * Cross-platform (Windows + POSIX)
 *
 * Build:
 *   gcc -std=c99 -o tests sample_tests.c testcracks.c -lm
 *   cl /W4 sample_tests.c testcracks.c
 *
 * Run:
 *   ./tests                              # Run all
 *   ./tests --suite "Math"               # Run suite
 *   ./tests --test "Math" "addition"     # Run single test
 *   ./tests --match "string"             # Run matching
 *   ./tests --xml results.xml            # JUnit XML output
 *   ./tests --list                       # List all tests
 */

#include "testcracks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Platform-specific includes */
#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #include <process.h>
  #define tc_getpid()    _getpid()
  #define tc_mkdir(p)    _mkdir(p)
  #define tc_rmdir(p)    _rmdir(p)
#else
  #include <sys/stat.h>
  #include <unistd.h>
  #define tc_getpid()    getpid()
  #define tc_mkdir(p)    mkdir(p, 0755)
  #define tc_rmdir(p)    rmdir(p)
#endif

/* ============================================================
   MATH TESTS - Basic assertions
   ============================================================ */

TestResult test_addition_works(void* env) {
    (void)env;
    return tc_assert_equal_int(4, 2 + 2, "should equal 4");
}

TestResult test_string_length(void* env) {
    (void)env;
    return tc_assert_equal_size(5, strlen("hello"), "should be 5 chars");
}

TestResult test_positive_numbers(void* env) {
    (void)env;
    return tc_assert_true(5 > 0, "should be positive");
}

/* ============================================================
   VALIDATION TESTS - Combining assertions (error accumulation)
   ============================================================ */

TestResult test_validate_order(void* env) {
    (void)env;
    int total = 100;
    int item_count = 3;
    int has_customer = 1;

    TestResult r = tc_assert_true(total > 0, "total positive");
    r = tc_combine(r, tc_assert_true(item_count > 0, "has items"));
    r = tc_combine(r, tc_assert_true(has_customer, "has customer"));
    return r;
}

/* Short-circuits on first failure */
TestResult test_dependent_checks(void* env) {
    (void)env;
    int x = 42;

    TestResult r = tc_assert_true(x > 0, "must be positive");
    if (tc_is_fail(r)) return r;

    r = tc_assert_true(x < 100, "must be under 100");
    if (tc_is_fail(r)) return r;

    return tc_assert_equal_int(42, x, "should be 42");
}

/* ============================================================
   FAILURE DEMO - Shows structured error output
   Uncomment to see expected/actual in output and XML
   ============================================================ */

/*
TestResult test_intentional_failure(void* env) {
    (void)env;
    return tc_assert_equal_int(100, 42, "this will fail");
}

TestResult test_multiple_failures(void* env) {
    (void)env;
    TestResult r = tc_assert_equal_int(1, 2, "first check");
    r = tc_combine(r, tc_assert_equal_str("hello", "world", "second check"));
    r = tc_combine(r, tc_assert_true(0, "third check"));
    return r;
}
*/

/* ============================================================
   SKIP TESTS - Conditional execution
   ============================================================ */

TestResult test_posix_only(void* env) {
    (void)env;
#ifdef _WIN32
    int is_posix = 0;
#else
    int is_posix = 1;
#endif
    TestResult r = tc_skip_unless(is_posix, "POSIX only test");
    if (tc_is_skip(r)) return r;

    return tc_assert_true(1, "posix-specific logic");
}

TestResult test_windows_only(void* env) {
    (void)env;
#ifdef _WIN32
    int is_windows = 1;
#else
    int is_windows = 0;
#endif
    TestResult r = tc_skip_unless(is_windows, "Windows only test");
    if (tc_is_skip(r)) return r;

    return tc_assert_true(1, "windows-specific logic");
}

TestResult test_skip_in_ci(void* env) {
    (void)env;
    int is_ci = (getenv("CI") != NULL);

    TestResult r = tc_skip_if(is_ci, "too slow for CI");
    if (tc_is_skip(r)) return r;

    return tc_assert_true(1, "slow test logic here");
}

/* ============================================================
   COLLECTION TESTS
   ============================================================ */

TestResult test_collection_contains(void* env) {
    (void)env;
    int arr[] = {1, 2, 3, 4, 5};
    int len = 5;

    TestResult r = tc_assert_not_empty_int(arr, len, "should have elements");
    r = tc_combine(r, tc_assert_len(5, len, "should have 5 elements"));
    r = tc_combine(r, tc_assert_contains_int(3, arr, len, "should contain 3"));
    r = tc_combine(r, tc_assert_not_contains_int(99, arr, len, "should not contain 99"));
    return r;
}

TestResult test_empty_collection(void* env) {
    (void)env;
    int* arr = NULL;
    return tc_assert_empty_int(arr, 0, "should be empty");
}

/* ============================================================
   NUMERIC COMPARISON TESTS
   ============================================================ */

TestResult test_numeric_comparisons(void* env) {
    (void)env;
    TestResult r = tc_assert_greater_int(10, 5, "10 > 5");
    r = tc_combine(r, tc_assert_less_int(3, 7, "3 < 7"));
    r = tc_combine(r, tc_assert_greater_or_equal_int(5, 5, "5 >= 5"));
    r = tc_combine(r, tc_assert_less_or_equal_int(5, 5, "5 <= 5"));
    return r;
}

TestResult test_floating_point(void* env) {
    (void)env;
    double pi = 3.14159;
    double calculated = 22.0 / 7.0;

    return tc_assert_in_delta(pi, calculated, 0.01, "close to pi");
}

/* ============================================================
   STRING TESTS
   ============================================================ */

TestResult test_string_equality(void* env) {
    (void)env;
    const char* expected = "hello";
    const char* actual = "hello";
    return tc_assert_equal_str(expected, actual, "strings match");
}

TestResult test_string_not_equal(void* env) {
    (void)env;
    return tc_assert_not_equal_str("hello", "world", "different strings");
}

/* ============================================================
   NIL/NULL TESTS
   ============================================================ */

TestResult test_nil_checking(void* env) {
    (void)env;
    const char* valid = "hello";
    const char* empty = NULL;

    TestResult r = tc_assert_not_nil(valid, "should not be null");
    r = tc_combine(r, tc_assert_nil(empty, "should be null"));
    return r;
}

/* ============================================================
   POINTER TESTS
   ============================================================ */

TestResult test_pointer_equality(void* env) {
    (void)env;
    int x = 42;
    int* p1 = &x;
    int* p2 = &x;
    int* p3 = NULL;

    TestResult r = tc_assert_equal_ptr(p1, p2, "same pointer");
    r = tc_combine(r, tc_assert_not_equal_ptr(p1, p3, "different pointers"));
    return r;
}

/* ============================================================
   DATA-DRIVEN TESTS
   ============================================================ */

TestResult test_double_2(void* env)   { (void)env; return tc_assert_equal_int(4,   2 * 2,   "2 * 2 = 4"); }
TestResult test_double_5(void* env)   { (void)env; return tc_assert_equal_int(10,  5 * 2,   "5 * 2 = 10"); }
TestResult test_double_10(void* env)  { (void)env; return tc_assert_equal_int(20,  10 * 2,  "10 * 2 = 20"); }
TestResult test_double_0(void* env)   { (void)env; return tc_assert_equal_int(0,   0 * 2,   "0 * 2 = 0"); }
TestResult test_double_neg(void* env) { (void)env; return tc_assert_equal_int(-10, -5 * 2,  "-5 * 2 = -10"); }

/* ============================================================
   FILE TESTS - Cross-platform setup/teardown
   ============================================================ */

typedef struct {
    char temp_dir[256];
    char temp_file[512];
} FileEnv;

static FileEnv file_env;

int file_tests_setup(void** env) {
#ifdef _WIN32
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = ".";
    snprintf(file_env.temp_dir, sizeof(file_env.temp_dir),
             "%s\\testcracks_%d", tmp, tc_getpid());
#else
    snprintf(file_env.temp_dir, sizeof(file_env.temp_dir),
             "/tmp/testcracks_%d", tc_getpid());
#endif

    if (tc_mkdir(file_env.temp_dir) != 0) {
        return -1;
    }

#ifdef _WIN32
    snprintf(file_env.temp_file, sizeof(file_env.temp_file),
             "%s\\test.txt", file_env.temp_dir);
#else
    snprintf(file_env.temp_file, sizeof(file_env.temp_file),
             "%s/test.txt", file_env.temp_dir);
#endif

    *env = &file_env;
    printf("  [setup] Created temp dir: %s\n", file_env.temp_dir);
    return 0;
}

void file_tests_teardown(void* env) {
    FileEnv* e = (FileEnv*)env;
    remove(e->temp_file);
    tc_rmdir(e->temp_dir);
    printf("  [teardown] Cleaned up temp dir\n");
}

TestResult test_can_create_file(void* env) {
    FileEnv* e = (FileEnv*)env;

    FILE* f = fopen(e->temp_file, "w");
    if (!f) return tc_fail("could not create file");

    fprintf(f, "hello");
    fclose(f);

    FILE* check = fopen(e->temp_file, "r");
    if (!check) return tc_fail("file should exist");
    fclose(check);

    return tc_pass();
}

TestResult test_can_read_file(void* env) {
    FileEnv* e = (FileEnv*)env;

    FILE* f = fopen(e->temp_file, "w");
    if (!f) return tc_fail("could not create file");
    fprintf(f, "hello");
    fclose(f);

    char buf[64] = {0};
    f = fopen(e->temp_file, "r");
    if (!f) return tc_fail("could not open file");
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    return tc_assert_equal_str("hello", buf, "should read content");
}

TestResult test_can_append_file(void* env) {
    FileEnv* e = (FileEnv*)env;

    FILE* f = fopen(e->temp_file, "w");
    if (!f) return tc_fail("could not create file");
    fprintf(f, "hello");
    fclose(f);

    f = fopen(e->temp_file, "a");
    if (!f) return tc_fail("could not open for append");
    fprintf(f, " world");
    fclose(f);

    char buf[64] = {0};
    f = fopen(e->temp_file, "r");
    if (!f) return tc_fail("could not open file");
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    return tc_assert_equal_str("hello world", buf, "should read appended content");
}

/* ============================================================
   MAIN
   ============================================================ */

int main(int argc, char** argv) {

    Suite math_suite = tc_suite("Math Tests", (Test[]){
        {"addition works", test_addition_works},
        {"string length", test_string_length},
        {"positive numbers", test_positive_numbers},
        {0}
    });

    Suite validation_suite = tc_suite("Validation Tests", (Test[]){
        {"validate order (accumulate)", test_validate_order},
        {"dependent checks (short-circuit)", test_dependent_checks},
        /* Uncomment to see failure output:
        {"intentional failure", test_intentional_failure},
        {"multiple failures", test_multiple_failures},
        */
        {0}
    });

    Suite skip_suite = tc_suite("Skip Tests", (Test[]){
        {"posix only", test_posix_only},
        {"windows only", test_windows_only},
        {"skip in CI", test_skip_in_ci},
        tc_skip_test("not implemented", "waiting for feature X"),
        {0}
    });

    Suite collection_suite = tc_suite("Collection Tests", (Test[]){
        {"contains and length", test_collection_contains},
        {"empty collection", test_empty_collection},
        {0}
    });

    Suite numeric_suite = tc_suite("Numeric Tests", (Test[]){
        {"comparisons", test_numeric_comparisons},
        {"floating point delta", test_floating_point},
        {0}
    });

    Suite string_suite = tc_suite("String Tests", (Test[]){
        {"equality", test_string_equality},
        {"not equal", test_string_not_equal},
        {0}
    });

    Suite nil_suite = tc_suite("Nil/Pointer Tests", (Test[]){
        {"nil checking", test_nil_checking},
        {"pointer equality", test_pointer_equality},
        {0}
    });

    Suite data_suite = tc_suite("Data-Driven Tests", (Test[]){
        {"2 * 2 = 4", test_double_2},
        {"5 * 2 = 10", test_double_5},
        {"10 * 2 = 20", test_double_10},
        {"0 * 2 = 0", test_double_0},
        {"-5 * 2 = -10", test_double_neg},
        {0}
    });

    Suite file_suite = tc_suite_with("File Operations",
        file_tests_setup,
        file_tests_teardown,
        (Test[]){
            {"can create file", test_can_create_file},
            {"can read file", test_can_read_file},
            {"can append file", test_can_append_file},
            tc_skip_test("performance test", "too slow for regular runs"),
            {0}
        }
    );

    Suite* all_suites[] = {
        &math_suite,
        &validation_suite,
        &skip_suite,
        &collection_suite,
        &numeric_suite,
        &string_suite,
        &nil_suite,
        &data_suite,
        &file_suite,
        NULL
    };

    return tc_main(argc, argv, all_suites);
}
