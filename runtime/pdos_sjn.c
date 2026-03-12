/*
 * pdos_sjn.c - String join for PyDOS runtime
 *
 * pydos_str_join_n() concatenates N string parts in a single
 * allocation+copy pass, eliminating the N-1 intermediate string
 * objects that pairwise pydos_str_concat() would create.
 *
 * Used by the compiler for f-strings with 3+ parts:
 *   f"Hello {name}, age {age}"
 *   -> pydos_str_join_n(4, "Hello ", str(name), ", age ", str(age))
 *
 * Impact on 8086: 5-part f-string goes from 10 allocations + 4 far
 * calls to 2 allocations + 1 far call.
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_sjn.h"
#include "pdos_mem.h"
#include <string.h>
#include <stdarg.h>

#define STR_JOIN_MAX 16

PyDosObj far * PYDOS_API pydos_str_join_n(unsigned int count, ...)
{
    va_list ap;
    unsigned int i;
    unsigned long total_len;
    char far *buf;
    char far *pos;
    PyDosObj far *result;
    PyDosObj far *parts[STR_JOIN_MAX];
    unsigned int lens[STR_JOIN_MAX];

    if (count == 0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }
    if (count > STR_JOIN_MAX) {
        count = STR_JOIN_MAX;
    }

    /* Collect parts and compute total length */
    va_start(ap, count);
    total_len = 0;
    for (i = 0; i < count; i++) {
        parts[i] = va_arg(ap, PyDosObj far *);
        if (parts[i] != (PyDosObj far *)0 &&
            parts[i]->type == PYDT_STR) {
            lens[i] = parts[i]->v.str.len;
            total_len += lens[i];
        } else {
            lens[i] = 0;
        }
    }
    va_end(ap);

    if (total_len == 0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    /* Single allocation for the concatenated result */
    buf = (char far *)pydos_far_alloc(total_len + 1);
    if (buf == (char far *)0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    /* Single copy pass */
    pos = buf;
    for (i = 0; i < count; i++) {
        if (lens[i] > 0) {
            _fmemcpy(pos, parts[i]->v.str.data, lens[i]);
            pos += lens[i];
        }
    }
    *pos = '\0';

    result = pydos_obj_new_str(buf, (unsigned int)total_len);
    pydos_far_free(buf);
    return result;
}
