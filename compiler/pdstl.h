/* pdstl.h -- Mini-STL for PyDOS compiler (C++98/Open Watcom compatible)
 *
 * Provides three containers for the PIR system:
 *   PdVector<T>    -- Dynamic array (POD types only)
 *   PdString        -- Owned string with deep copy
 *   PdHashMap<K,V> -- Open addressing hash map
 *
 * Constraints:
 *   - C++98 only (no auto, range-for, nullptr, lambdas)
 *   - No destructor calls on elements (POD/pointer types only)
 *   - malloc/free based (no new/delete)
 *   - Fatal exit(1) on OOM (matches existing codebase)
 *   - Compatible with Open Watcom wpp
 */

#ifndef PDSTL_H
#define PDSTL_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * PdVector<T> -- Dynamic array for POD/pointer types
 * ======================================================================== */

template <typename T>
class PdVector {
public:
    PdVector() : data_(0), size_(0), cap_(0) {}

    ~PdVector() {
        if (data_) free(data_);
    }

    /* Copy constructor (deep copy) */
    PdVector(const PdVector &other) : data_(0), size_(0), cap_(0) {
        if (other.size_ > 0) {
            reserve(other.size_);
            memcpy(data_, other.data_, sizeof(T) * other.size_);
            size_ = other.size_;
        }
    }

    /* Assignment operator (deep copy) */
    PdVector &operator=(const PdVector &other) {
        if (this != &other) {
            clear();
            if (other.size_ > 0) {
                reserve(other.size_);
                memcpy(data_, other.data_, sizeof(T) * other.size_);
                size_ = other.size_;
            }
        }
        return *this;
    }

    void push_back(T val) {
        if (size_ >= cap_) {
            int new_cap = cap_ < 8 ? 8 : cap_ * 2;
            grow(new_cap);
        }
        data_[size_] = val;
        size_++;
    }

    void pop_back() {
        if (size_ > 0) size_--;
    }

    T &operator[](int index) { return data_[index]; }
    const T &operator[](int index) const { return data_[index]; }

    int size() const { return size_; }
    int empty() const { return size_ == 0; }
    int capacity() const { return cap_; }

    T *begin() { return data_; }
    T *end() { return data_ + size_; }
    const T *begin() const { return data_; }
    const T *end() const { return data_ + size_; }

    T &back() { return data_[size_ - 1]; }
    const T &back() const { return data_[size_ - 1]; }

    void clear() { size_ = 0; }

    /* Explicit teardown — use instead of explicit dtor call
       (Open Watcom wpp does not support obj.~PdVector() syntax) */
    void destroy() {
        if (data_) { free(data_); data_ = 0; }
        size_ = 0;
        cap_ = 0;
    }

    void reserve(int new_cap) {
        if (new_cap > cap_) {
            grow(new_cap);
        }
    }

    void resize(int new_size) {
        if (new_size > cap_) {
            grow(new_size);
        }
        if (new_size > size_) {
            memset(data_ + size_, 0, sizeof(T) * (new_size - size_));
        }
        size_ = new_size;
    }

    /* Remove element at index, shifting remaining elements */
    void remove_at(int index) {
        int i;
        for (i = index; i < size_ - 1; i++) {
            data_[i] = data_[i + 1];
        }
        size_--;
    }

private:
    void grow(int new_cap) {
        T *nd = (T *)malloc(sizeof(T) * new_cap);
        if (!nd) {
            fprintf(stderr, "pdstl: PdVector out of memory\n");
            exit(1);
        }
        if (data_) {
            memcpy(nd, data_, sizeof(T) * size_);
            free(data_);
        }
        data_ = nd;
        cap_ = new_cap;
    }

    T *data_;
    int size_;
    int cap_;
};

/* ========================================================================
 * PdString -- Owned string with deep copy semantics
 * ======================================================================== */

class PdString {
public:
    PdString() : data_(0), len_(0) {}

    PdString(const char *s) : data_(0), len_(0) {
        if (s) {
            len_ = (int)strlen(s);
            data_ = (char *)malloc(len_ + 1);
            if (!data_) {
                fprintf(stderr, "pdstl: PdString out of memory\n");
                exit(1);
            }
            memcpy(data_, s, len_ + 1);
        }
    }

    ~PdString() {
        if (data_) free(data_);
    }

    /* Copy constructor */
    PdString(const PdString &other) : data_(0), len_(0) {
        if (other.data_) {
            len_ = other.len_;
            data_ = (char *)malloc(len_ + 1);
            if (!data_) {
                fprintf(stderr, "pdstl: PdString out of memory\n");
                exit(1);
            }
            memcpy(data_, other.data_, len_ + 1);
        }
    }

    /* Assignment operator */
    PdString &operator=(const PdString &other) {
        if (this != &other) {
            if (data_) free(data_);
            data_ = 0;
            len_ = 0;
            if (other.data_) {
                len_ = other.len_;
                data_ = (char *)malloc(len_ + 1);
                if (!data_) {
                    fprintf(stderr, "pdstl: PdString out of memory\n");
                    exit(1);
                }
                memcpy(data_, other.data_, len_ + 1);
            }
        }
        return *this;
    }

    /* Assignment from C string */
    PdString &operator=(const char *s) {
        if (data_) free(data_);
        data_ = 0;
        len_ = 0;
        if (s) {
            len_ = (int)strlen(s);
            data_ = (char *)malloc(len_ + 1);
            if (!data_) {
                fprintf(stderr, "pdstl: PdString out of memory\n");
                exit(1);
            }
            memcpy(data_, s, len_ + 1);
        }
        return *this;
    }

    const char *c_str() const { return data_ ? data_ : ""; }
    int length() const { return len_; }
    int empty() const { return len_ == 0; }

    void append(const char *s) {
        int slen;
        int new_len;
        char *nd;
        if (!s) return;
        slen = (int)strlen(s);
        if (slen == 0) return;
        new_len = len_ + slen;
        nd = (char *)malloc(new_len + 1);
        if (!nd) {
            fprintf(stderr, "pdstl: PdString out of memory\n");
            exit(1);
        }
        if (data_) {
            memcpy(nd, data_, len_);
            free(data_);
        }
        memcpy(nd + len_, s, slen + 1);
        data_ = nd;
        len_ = new_len;
    }

    void append_char(char c) {
        char buf[2];
        buf[0] = c;
        buf[1] = '\0';
        append(buf);
    }

    int compare(const PdString &other) const {
        if (!data_ && !other.data_) return 0;
        if (!data_) return -1;
        if (!other.data_) return 1;
        return strcmp(data_, other.data_);
    }

    int compare(const char *s) const {
        if (!data_ && !s) return 0;
        if (!data_) return -1;
        if (!s) return 1;
        return strcmp(data_, s);
    }

private:
    char *data_;
    int len_;
};

/* PdString comparison operators (free functions) */
inline int operator==(const PdString &a, const PdString &b) { return a.compare(b) == 0; }
inline int operator!=(const PdString &a, const PdString &b) { return a.compare(b) != 0; }
inline int operator==(const PdString &a, const char *b) { return a.compare(b) == 0; }
inline int operator!=(const PdString &a, const char *b) { return a.compare(b) != 0; }

/* ========================================================================
 * PdHashMap<K,V> -- Open addressing hash map
 *
 * Hash and equality are provided via function pointers to avoid
 * template specialization (unreliable in Watcom).
 * ======================================================================== */

/* Built-in hash functions */
inline unsigned int pd_hash_str(const char *s)
{
    /* djb2 */
    unsigned int h = 5381;
    if (s) {
        while (*s) {
            h = ((h << 5) + h) + (unsigned char)*s;
            s++;
        }
    }
    return h;
}

inline unsigned int pd_hash_int(int v)
{
    /* murmurhash-style mixing */
    unsigned int h = (unsigned int)v;
    h ^= h >> 16;
    h *= 0x45d9f3bU;
    h ^= h >> 16;
    return h;
}

inline int pd_eq_str(const char *a, const char *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

inline int pd_eq_int(int a, int b)
{
    return a == b;
}

/* Hash/equality function pointer types */
typedef unsigned int (*PdHashFn_cstr)(const char *);
typedef int (*PdEqFn_cstr)(const char *, const char *);
typedef unsigned int (*PdHashFn_int)(int);
typedef int (*PdEqFn_int)(int, int);

template <typename K, typename V>
class PdHashMap {
public:
    /* Slot states */
    enum SlotState { SLOT_EMPTY = 0, SLOT_OCCUPIED = 1, SLOT_TOMBSTONE = 2 };

    struct Slot {
        K key;
        V value;
        SlotState state;
    };

    /* Constructor with function pointers for hash/equality */
    typedef unsigned int (*HashFn)(K);
    typedef int (*EqFn)(K, K);

    PdHashMap(HashFn hash_fn, EqFn eq_fn)
        : slots_(0), cap_(0), size_(0), hash_fn_(hash_fn), eq_fn_(eq_fn) {}

    ~PdHashMap() {
        if (slots_) free(slots_);
    }

    void put(K key, V value) {
        int idx;
        if (size_ * 4 >= cap_ * 3) { /* 75% load factor */
            rehash(cap_ < 16 ? 16 : cap_ * 2);
        }
        idx = find_slot(key);
        if (slots_[idx].state == SLOT_OCCUPIED) {
            /* Update existing */
            slots_[idx].value = value;
        } else {
            /* Insert new */
            slots_[idx].key = key;
            slots_[idx].value = value;
            slots_[idx].state = SLOT_OCCUPIED;
            size_++;
        }
    }

    /* Returns pointer to value if found, 0 if not found */
    V *get(K key) {
        int idx;
        if (cap_ == 0) return 0;
        idx = find_slot(key);
        if (slots_[idx].state == SLOT_OCCUPIED) {
            return &slots_[idx].value;
        }
        return 0;
    }

    const V *get(K key) const {
        int idx;
        if (cap_ == 0) return 0;
        idx = find_slot_const(key);
        if (slots_[idx].state == SLOT_OCCUPIED) {
            return &slots_[idx].value;
        }
        return 0;
    }

    int has(K key) const {
        if (cap_ == 0) return 0;
        return slots_[find_slot_const(key)].state == SLOT_OCCUPIED;
    }

    void remove(K key) {
        int idx;
        if (cap_ == 0) return;
        idx = find_slot(key);
        if (slots_[idx].state == SLOT_OCCUPIED) {
            slots_[idx].state = SLOT_TOMBSTONE;
            size_--;
        }
    }

    void clear() {
        if (slots_) {
            memset(slots_, 0, sizeof(Slot) * cap_);
        }
        size_ = 0;
    }

    int size() const { return size_; }
    int empty() const { return size_ == 0; }
    int capacity() const { return cap_; }

    /* Index-based iteration:
     * for (i = 0; i < map.capacity(); i++)
     *     if (map.slot_occupied(i)) { map.slot_key(i); map.slot_value(i); }
     */
    int slot_occupied(int i) const {
        return slots_ && i < cap_ && slots_[i].state == SLOT_OCCUPIED;
    }
    K slot_key(int i) const { return slots_[i].key; }
    V &slot_value(int i) { return slots_[i].value; }
    const V &slot_value(int i) const { return slots_[i].value; }

private:
    /* Disallow copy (too expensive for hash maps) */
    PdHashMap(const PdHashMap &);
    PdHashMap &operator=(const PdHashMap &);

    int find_slot(K key) const {
        unsigned int h = hash_fn_(key);
        int idx = (int)(h % (unsigned int)cap_);
        int first_tombstone = -1;
        while (1) {
            if (slots_[idx].state == SLOT_EMPTY) {
                return first_tombstone >= 0 ? first_tombstone : idx;
            }
            if (slots_[idx].state == SLOT_TOMBSTONE) {
                if (first_tombstone < 0) first_tombstone = idx;
            } else if (eq_fn_(slots_[idx].key, key)) {
                return idx;
            }
            idx = (idx + 1) % cap_;
        }
    }

    int find_slot_const(K key) const {
        unsigned int h = hash_fn_(key);
        int idx = (int)(h % (unsigned int)cap_);
        while (1) {
            if (slots_[idx].state == SLOT_EMPTY) {
                return idx;
            }
            if (slots_[idx].state == SLOT_OCCUPIED && eq_fn_(slots_[idx].key, key)) {
                return idx;
            }
            idx = (idx + 1) % cap_;
        }
    }

    void rehash(int new_cap) {
        Slot *old_slots = slots_;
        int old_cap = cap_;
        int i;

        slots_ = (Slot *)malloc(sizeof(Slot) * new_cap);
        if (!slots_) {
            fprintf(stderr, "pdstl: PdHashMap out of memory\n");
            exit(1);
        }
        memset(slots_, 0, sizeof(Slot) * new_cap);
        cap_ = new_cap;
        size_ = 0;

        if (old_slots) {
            for (i = 0; i < old_cap; i++) {
                if (old_slots[i].state == SLOT_OCCUPIED) {
                    put(old_slots[i].key, old_slots[i].value);
                }
            }
            free(old_slots);
        }
    }

    Slot *slots_;
    int cap_;
    int size_;
    HashFn hash_fn_;
    EqFn eq_fn_;
};

/* ========================================================================
 * Convenience typedefs for common map types
 * ======================================================================== */

/* Usage:
 *   PdHashMap<const char*, int> map(
 *       (PdHashMap<const char*, int>::HashFn)pd_hash_str,
 *       (PdHashMap<const char*, int>::EqFn)pd_eq_str);
 *   map.put("hello", 42);
 *   int *val = map.get("hello");
 */

#endif /* PDSTL_H */
