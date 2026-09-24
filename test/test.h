#ifndef SAGE_TEST_H
#define SAGE_TEST_H

#include <stdio.h>

static int test_failures = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #expr); \
        ++test_failures; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    long long test_expected = (long long)(expected); \
    long long test_actual = (long long)(actual); \
    if (test_expected != test_actual) { \
        fprintf(stderr, "%s:%d: expected %lld, got %lld\n", \
                __FILE__, __LINE__, test_expected, test_actual); \
        ++test_failures; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    fn(); \
    if (test_failures != 0) return 1; \
} while (0)

#endif
