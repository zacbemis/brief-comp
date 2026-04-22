#ifndef BRIEF_TEST_UTILS_H
#define BRIEF_TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimal test harness. Each test file defines cases using TEST() inside
// main() and calls RUN_TESTS() at the end. Failures print a message and
// set a nonzero exit code so CTest reports the failure.

static int brief_tests_run = 0;
static int brief_tests_failed = 0;
static const char *brief_current_test = NULL;

#define TEST(name)                                                             \
  for (int _once = (brief_current_test = name, brief_tests_run++, 1); _once;   \
       _once = 0)

#define FAIL(msg)                                                              \
  do {                                                                         \
    fprintf(stderr, "  [FAIL] %s: %s (at %s:%d)\n", brief_current_test, (msg), \
            __FILE__, __LINE__);                                               \
    brief_tests_failed++;                                                      \
    continue;                                                                  \
  } while (0)

#define ASSERT_TRUE(cond)                                                      \
  do {                                                                         \
    if (!(cond)) FAIL("expected true: " #cond);                                \
  } while (0)

#define ASSERT_EQ_INT(expected, actual)                                        \
  do {                                                                         \
    long long _e = (long long)(expected);                                      \
    long long _a = (long long)(actual);                                        \
    if (_e != _a) {                                                            \
      char _buf[256];                                                          \
      snprintf(_buf, sizeof(_buf),                                             \
               "expected %lld, got %lld (" #actual ")", _e, _a);               \
      FAIL(_buf);                                                              \
    }                                                                          \
  } while (0)

#define ASSERT_EQ_STRN(expected, actual, len)                                  \
  do {                                                                         \
    size_t _len = (size_t)(len);                                               \
    if (strlen(expected) != _len ||                                            \
        memcmp((expected), (actual), _len) != 0) {                             \
      char _buf[256];                                                          \
      snprintf(_buf, sizeof(_buf), "expected \"%s\", got \"%.*s\"",            \
               (expected), (int)_len, (actual));                               \
      FAIL(_buf);                                                              \
    }                                                                          \
  } while (0)

#define RUN_TESTS()                                                            \
  do {                                                                         \
    printf("%s: %d run, %d failed\n", __FILE__,                                \
           brief_tests_run, brief_tests_failed);                               \
    return brief_tests_failed == 0 ? 0 : 1;                                    \
  } while (0)

#endif
