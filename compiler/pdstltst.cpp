/* pdstltst.cpp -- Smoke tests for mini-STL containers
 *
 * Build: clang++ -std=c++98 -Wall -Wextra -o pdstltst compiler/pdstltst.cpp
 * Run:   ./pdstltst
 */

#include "pdstl.h"
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name()
#define RUN(name) do { \
    printf("  %-40s", #name); \
    name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ======================================================================
 * PdVector tests
 * ====================================================================== */

TEST(test_vector_empty) {
    PdVector<int> v;
    ASSERT(v.size() == 0);
    ASSERT(v.empty());
    ASSERT(v.capacity() == 0);
}

TEST(test_vector_push_back) {
    PdVector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    ASSERT(v.size() == 3);
    ASSERT(!v.empty());
    ASSERT(v[0] == 10);
    ASSERT(v[1] == 20);
    ASSERT(v[2] == 30);
}

TEST(test_vector_pop_back) {
    PdVector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.pop_back();
    ASSERT(v.size() == 2);
    ASSERT(v.back() == 2);
    v.pop_back();
    v.pop_back();
    ASSERT(v.empty());
}

TEST(test_vector_back) {
    PdVector<int> v;
    v.push_back(42);
    ASSERT(v.back() == 42);
    v.push_back(99);
    ASSERT(v.back() == 99);
}

TEST(test_vector_reserve) {
    PdVector<int> v;
    v.reserve(100);
    ASSERT(v.capacity() >= 100);
    ASSERT(v.size() == 0);
    v.push_back(1);
    ASSERT(v.size() == 1);
}

TEST(test_vector_resize) {
    PdVector<int> v;
    v.resize(5);
    ASSERT(v.size() == 5);
    ASSERT(v[0] == 0);
    ASSERT(v[4] == 0);
    v[3] = 42;
    ASSERT(v[3] == 42);
}

TEST(test_vector_clear) {
    PdVector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.clear();
    ASSERT(v.size() == 0);
    ASSERT(v.empty());
    /* Capacity preserved */
    ASSERT(v.capacity() > 0);
}

TEST(test_vector_iteration) {
    PdVector<int> v;
    int sum = 0;
    int *p;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    for (p = v.begin(); p != v.end(); p++) {
        sum += *p;
    }
    ASSERT(sum == 6);
}

TEST(test_vector_growth) {
    PdVector<int> v;
    int i;
    for (i = 0; i < 1000; i++) {
        v.push_back(i);
    }
    ASSERT(v.size() == 1000);
    for (i = 0; i < 1000; i++) {
        ASSERT(v[i] == i);
    }
}

TEST(test_vector_pointers) {
    PdVector<const char *> v;
    v.push_back("hello");
    v.push_back("world");
    ASSERT(v.size() == 2);
    ASSERT(strcmp(v[0], "hello") == 0);
    ASSERT(strcmp(v[1], "world") == 0);
}

TEST(test_vector_copy) {
    PdVector<int> a;
    a.push_back(1);
    a.push_back(2);
    a.push_back(3);
    PdVector<int> b(a);
    ASSERT(b.size() == 3);
    ASSERT(b[0] == 1);
    ASSERT(b[2] == 3);
    /* Modify original, copy unaffected */
    a[0] = 99;
    ASSERT(b[0] == 1);
}

TEST(test_vector_assign) {
    PdVector<int> a;
    PdVector<int> b;
    a.push_back(10);
    a.push_back(20);
    b = a;
    ASSERT(b.size() == 2);
    ASSERT(b[0] == 10);
    a[0] = 99;
    ASSERT(b[0] == 10);
}

TEST(test_vector_remove_at) {
    PdVector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);
    v.remove_at(1); /* Remove 2 */
    ASSERT(v.size() == 3);
    ASSERT(v[0] == 1);
    ASSERT(v[1] == 3);
    ASSERT(v[2] == 4);
}

/* ======================================================================
 * PdString tests
 * ====================================================================== */

TEST(test_string_empty) {
    PdString s;
    ASSERT(s.empty());
    ASSERT(s.length() == 0);
    ASSERT(strcmp(s.c_str(), "") == 0);
}

TEST(test_string_from_cstr) {
    PdString s("hello");
    ASSERT(!s.empty());
    ASSERT(s.length() == 5);
    ASSERT(strcmp(s.c_str(), "hello") == 0);
}

TEST(test_string_copy) {
    PdString a("original");
    PdString b(a);
    ASSERT(strcmp(b.c_str(), "original") == 0);
    ASSERT(b.length() == 8);
}

TEST(test_string_assign) {
    PdString a("first");
    PdString b;
    b = a;
    ASSERT(strcmp(b.c_str(), "first") == 0);
    /* Self-assignment */
    a = a;
    ASSERT(strcmp(a.c_str(), "first") == 0);
}

TEST(test_string_assign_cstr) {
    PdString s;
    s = "hello world";
    ASSERT(strcmp(s.c_str(), "hello world") == 0);
    ASSERT(s.length() == 11);
}

TEST(test_string_append) {
    PdString s("hello");
    s.append(" world");
    ASSERT(strcmp(s.c_str(), "hello world") == 0);
    ASSERT(s.length() == 11);
}

TEST(test_string_append_empty) {
    PdString s;
    s.append("abc");
    ASSERT(strcmp(s.c_str(), "abc") == 0);
}

TEST(test_string_append_char) {
    PdString s("ab");
    s.append_char('c');
    ASSERT(strcmp(s.c_str(), "abc") == 0);
    ASSERT(s.length() == 3);
}

TEST(test_string_compare) {
    PdString a("abc");
    PdString b("abc");
    PdString c("abd");
    ASSERT(a.compare(b) == 0);
    ASSERT(a.compare(c) < 0);
    ASSERT(c.compare(a) > 0);
}

TEST(test_string_operators) {
    PdString a("hello");
    PdString b("hello");
    PdString c("world");
    ASSERT(a == b);
    ASSERT(a != c);
    ASSERT(a == "hello");
    ASSERT(a != "world");
}

TEST(test_string_null) {
    PdString s((const char *)0);
    ASSERT(s.empty());
    ASSERT(s.length() == 0);
}

/* ======================================================================
 * PdHashMap tests
 * ====================================================================== */

TEST(test_map_empty) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    ASSERT(m.size() == 0);
    ASSERT(m.empty());
    ASSERT(m.get("key") == 0);
}

TEST(test_map_put_get) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    int *val;
    m.put("hello", 42);
    m.put("world", 99);
    ASSERT(m.size() == 2);
    val = m.get("hello");
    ASSERT(val != 0);
    ASSERT(*val == 42);
    val = m.get("world");
    ASSERT(val != 0);
    ASSERT(*val == 99);
    ASSERT(m.get("missing") == 0);
}

TEST(test_map_update) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    int *val;
    m.put("key", 1);
    m.put("key", 2);
    ASSERT(m.size() == 1);
    val = m.get("key");
    ASSERT(val != 0);
    ASSERT(*val == 2);
}

TEST(test_map_has) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    m.put("present", 1);
    ASSERT(m.has("present"));
    ASSERT(!m.has("absent"));
}

TEST(test_map_remove) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    m.put("a", 1);
    m.put("b", 2);
    m.put("c", 3);
    m.remove("b");
    ASSERT(m.size() == 2);
    ASSERT(m.has("a"));
    ASSERT(!m.has("b"));
    ASSERT(m.has("c"));
}

TEST(test_map_clear) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    m.put("a", 1);
    m.put("b", 2);
    m.clear();
    ASSERT(m.size() == 0);
    ASSERT(!m.has("a"));
}

TEST(test_map_iteration) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    int i, sum = 0, count = 0;
    m.put("a", 10);
    m.put("b", 20);
    m.put("c", 30);
    for (i = 0; i < m.capacity(); i++) {
        if (m.slot_occupied(i)) {
            sum += m.slot_value(i);
            count++;
        }
    }
    ASSERT(count == 3);
    ASSERT(sum == 60);
}

TEST(test_map_int_keys) {
    PdHashMap<int, int> m(
        (PdHashMap<int, int>::HashFn)pd_hash_int,
        (PdHashMap<int, int>::EqFn)pd_eq_int);
    int *val;
    m.put(1, 100);
    m.put(2, 200);
    m.put(3, 300);
    val = m.get(2);
    ASSERT(val != 0);
    ASSERT(*val == 200);
    ASSERT(!m.has(4));
}

TEST(test_map_many_entries) {
    PdHashMap<int, int> m(
        (PdHashMap<int, int>::HashFn)pd_hash_int,
        (PdHashMap<int, int>::EqFn)pd_eq_int);
    int i;
    int *val;
    for (i = 0; i < 500; i++) {
        m.put(i, i * 10);
    }
    ASSERT(m.size() == 500);
    for (i = 0; i < 500; i++) {
        val = m.get(i);
        ASSERT(val != 0);
        ASSERT(*val == i * 10);
    }
}

TEST(test_map_remove_then_insert) {
    PdHashMap<const char *, int> m(
        (PdHashMap<const char *, int>::HashFn)pd_hash_str,
        (PdHashMap<const char *, int>::EqFn)pd_eq_str);
    int *val;
    m.put("a", 1);
    m.put("b", 2);
    m.remove("a");
    m.put("c", 3);
    /* Can still insert after tombstone */
    m.put("a", 10);
    ASSERT(m.size() == 3);
    val = m.get("a");
    ASSERT(val != 0);
    ASSERT(*val == 10);
}

/* ======================================================================
 * Main
 * ====================================================================== */

int main()
{
    printf("PdVector tests:\n");
    RUN(test_vector_empty);
    RUN(test_vector_push_back);
    RUN(test_vector_pop_back);
    RUN(test_vector_back);
    RUN(test_vector_reserve);
    RUN(test_vector_resize);
    RUN(test_vector_clear);
    RUN(test_vector_iteration);
    RUN(test_vector_growth);
    RUN(test_vector_pointers);
    RUN(test_vector_copy);
    RUN(test_vector_assign);
    RUN(test_vector_remove_at);

    printf("\nPdString tests:\n");
    RUN(test_string_empty);
    RUN(test_string_from_cstr);
    RUN(test_string_copy);
    RUN(test_string_assign);
    RUN(test_string_assign_cstr);
    RUN(test_string_append);
    RUN(test_string_append_empty);
    RUN(test_string_append_char);
    RUN(test_string_compare);
    RUN(test_string_operators);
    RUN(test_string_null);

    printf("\nPdHashMap tests:\n");
    RUN(test_map_empty);
    RUN(test_map_put_get);
    RUN(test_map_update);
    RUN(test_map_has);
    RUN(test_map_remove);
    RUN(test_map_clear);
    RUN(test_map_iteration);
    RUN(test_map_int_keys);
    RUN(test_map_many_entries);
    RUN(test_map_remove_then_insert);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
