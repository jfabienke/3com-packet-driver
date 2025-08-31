#ifndef TEST_MACROS_H
#define TEST_MACROS_H

#include "test_common.h"

/* Basic assertion macros */

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            test_error("ASSERT_TRUE failed: %s at %s:%d", #condition, __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            test_error("ASSERT_FALSE failed: %s at %s:%d", #condition, __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            test_error("ASSERT_EQ failed: expected %ld, got %ld at %s:%d", \
                      (long)(expected), (long)(actual), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_NE(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            test_error("ASSERT_NE failed: both values are %ld at %s:%d", \
                      (long)(expected), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_LT(a, b) \
    do { \
        if ((a) >= (b)) { \
            test_error("ASSERT_LT failed: %ld >= %ld at %s:%d", \
                      (long)(a), (long)(b), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_LE(a, b) \
    do { \
        if ((a) > (b)) { \
            test_error("ASSERT_LE failed: %ld > %ld at %s:%d", \
                      (long)(a), (long)(b), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_GT(a, b) \
    do { \
        if ((a) <= (b)) { \
            test_error("ASSERT_GT failed: %ld <= %ld at %s:%d", \
                      (long)(a), (long)(b), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_GE(a, b) \
    do { \
        if ((a) < (b)) { \
            test_error("ASSERT_GE failed: %ld < %ld at %s:%d", \
                      (long)(a), (long)(b), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

/* String assertion macros */

#define ASSERT_STR_EQ(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            test_error("ASSERT_STR_EQ failed: expected '%s', got '%s' at %s:%d", \
                      (expected), (actual), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_STR_NE(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) == 0) { \
            test_error("ASSERT_STR_NE failed: both strings are '%s' at %s:%d", \
                      (expected), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

/* Pointer assertion macros */

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            test_error("ASSERT_NULL failed: pointer is not NULL at %s:%d", __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            test_error("ASSERT_NOT_NULL failed: pointer is NULL at %s:%d", __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

/* Memory assertion macros */

#define ASSERT_MEM_EQ(expected, actual, size) \
    do { \
        if (memcmp((expected), (actual), (size)) != 0) { \
            test_error("ASSERT_MEM_EQ failed: memory contents differ at %s:%d", __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

/* Floating point assertion macros */

#define ASSERT_FLOAT_EQ(expected, actual, epsilon) \
    do { \
        double diff = (double)(expected) - (double)(actual); \
        if (diff < 0) diff = -diff; \
        if (diff > (epsilon)) { \
            test_error("ASSERT_FLOAT_EQ failed: expected %f, got %f (diff %f > %f) at %s:%d", \
                      (double)(expected), (double)(actual), diff, (double)(epsilon), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

/* Test control macros */

#define TEST_SKIP(reason) \
    do { \
        test_info("Test skipped: %s at %s:%d", (reason), __FILE__, __LINE__); \
        return TEST_SKIP; \
    } while(0)

#define TEST_FAIL_MSG(message) \
    do { \
        test_error("Test failed: %s at %s:%d", (message), __FILE__, __LINE__); \
        return TEST_FAIL; \
    } while(0)

/* Test declaration macros */

#define DECLARE_TEST(name) \
    test_result_t test_##name(void)

#define TEST_CASE(name, description) \
    { #name, test_##name, description }

#define TEST_SUITE_END { NULL, NULL, NULL }

/* Expect macros (non-fatal) */

#define EXPECT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            test_error("EXPECT_TRUE failed: %s at %s:%d", #condition, __FILE__, __LINE__); \
        } \
    } while(0)

#define EXPECT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            test_error("EXPECT_EQ failed: expected %ld, got %ld at %s:%d", \
                      (long)(expected), (long)(actual), __FILE__, __LINE__); \
        } \
    } while(0)

/* Performance testing macros */

#define BENCHMARK_START(timer) test_timer_start(&(timer))
#define BENCHMARK_END(timer) test_timer_stop(&(timer))
#define BENCHMARK_ELAPSED_MS(timer) test_timer_elapsed_ms(&(timer))

#define ASSERT_PERFORMANCE_LT(timer, max_ms) \
    do { \
        uint64_t elapsed = test_timer_elapsed_ms(&(timer)); \
        if (elapsed >= (max_ms)) { \
            test_error("Performance assertion failed: %lu ms >= %lu ms at %s:%d", \
                      (unsigned long)elapsed, (unsigned long)(max_ms), __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while(0)

#endif /* TEST_MACROS_H */