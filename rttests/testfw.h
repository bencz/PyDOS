/*
 * testfw.h - Minimal unit test framework for PyDOS runtime tests
 *
 * C89 compatible. Works on DOS (Open Watcom) and macOS (clang).
 * Each test file includes this header. main.c defines tf_pass/tf_fail.
 */

#ifndef TESTFW_H
#define TESTFW_H

#include <stdio.h>
#include <string.h>

/* Global counters (defined in main.c) */
extern int tf_pass;
extern int tf_fail;

/* Per-file flag used by RUN/ASSERT macros */
static int _tf_cur_fail;

/* Print suite header */
#define SUITE(name) \
    printf("\n=== %s ===\n", name)

/* Define a test function */
#define TEST(name) static void test_##name(void)

/* Run a test function and report pass/fail */
#define RUN(name) \
    do { \
        _tf_cur_fail = 0; \
        test_##name(); \
        if (_tf_cur_fail) { \
            tf_fail++; \
            printf("  FAIL: %s\n", #name); \
        } else { \
            tf_pass++; \
            printf("  PASS: %s\n", #name); \
        } \
    } while (0)

/* Assertions - on failure, print info and return from test */

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            printf("    ASSERT_TRUE failed: %s (%s:%d)\n", \
                   #expr, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) \
    do { \
        if ((expr)) { \
            printf("    ASSERT_FALSE failed: %s (%s:%d)\n", \
                   #expr, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        long _a = (long)(a); \
        long _b = (long)(b); \
        if (_a != _b) { \
            printf("    ASSERT_EQ failed: %ld != %ld (%s:%d)\n", \
                   _a, _b, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)

#define ASSERT_NEQ(a, b) \
    do { \
        long _a = (long)(a); \
        long _b = (long)(b); \
        if (_a == _b) { \
            printf("    ASSERT_NEQ failed: %ld == %ld (%s:%d)\n", \
                   _a, _b, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((void far *)(ptr) != (void far *)0) { \
            printf("    ASSERT_NULL failed: %s (%s:%d)\n", \
                   #ptr, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((void far *)(ptr) == (void far *)0) { \
            printf("    ASSERT_NOT_NULL failed: %s (%s:%d)\n", \
                   #ptr, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)

#if defined(__WATCOMC__) && !defined(PYDOS_32BIT)
/* 16-bit large model: string data lives on far heap, need _fstrcmp */
#define ASSERT_STR_EQ(a, b) \
    do { \
        const char far *_sa = (const char far *)(a); \
        const char far *_sb = (const char far *)(b); \
        if (_fstrcmp(_sa, _sb) != 0) { \
            printf("    ASSERT_STR_EQ failed: \"%Fs\" != \"%Fs\" (%s:%d)\n", \
                   _sa, _sb, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)
#else
#define ASSERT_STR_EQ(a, b) \
    do { \
        const char *_sa = (const char *)(a); \
        const char *_sb = (const char *)(b); \
        if (strcmp(_sa, _sb) != 0) { \
            printf("    ASSERT_STR_EQ failed: \"%s\" != \"%s\" (%s:%d)\n", \
                   _sa, _sb, __FILE__, __LINE__); \
            _tf_cur_fail = 1; \
            return; \
        } \
    } while (0)
#endif

#endif /* TESTFW_H */
