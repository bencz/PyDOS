/*
 * dos.h - macOS compatibility stub for Open Watcom dos.h
 * Provides empty definitions so runtime code compiles for validation.
 */
#ifndef _COMPAT_DOS_H
#define _COMPAT_DOS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* PYDOS_API calling convention - no-op on macOS (flat memory, no cdecl) */
#ifndef PYDOS_API
#define PYDOS_API
#endif

/* far pointer - no-op on flat memory */
#ifndef far
#define far
#endif
#ifndef __far
#define __far
#endif
#ifndef _far
#define _far
#endif

/* far memory functions -> standard equivalents */
#define _fmalloc(s)         malloc(s)
#define _ffree(p)           free(p)
#define _frealloc(p,s)      realloc(p,s)
#define _fmemset(p,v,n)     memset(p,v,n)
#define _fmemcpy(d,s,n)     memcpy(d,s,n)
#define _fmemcmp(a,b,n)     memcmp(a,b,n)
#define _fstrlen(s)         strlen((const char*)(s))
#define _fstrcpy(d,s)       strcpy((char*)(d),(const char*)(s))
#define _fstrcmp(a,b)       strcmp((const char*)(a),(const char*)(b))
#define _fstrcat(d,s)       strcat((char*)(d),(const char*)(s))
#define _fmemmove(d,s,n)    memmove(d,s,n)

/* heap functions - stubs */
#define _fheapgrow()        ((void)0)
#define _fheapshrink()      ((void)0)
#define _fmsize(p)          ((size_t)0)
#define _memavl()           ((size_t)0x7FFFFFFF)
#define _freect(s)          ((unsigned)(0x7FFFFFFF/(s)))

/* DOS register structs - stubs */
struct WORDREGS {
    unsigned short ax, bx, cx, dx, si, di, cflag;
};
struct BYTEREGS {
    unsigned char al, ah, bl, bh, cl, ch, dl, dh;
};
struct DWORDREGS {
    unsigned int eax, ebx, ecx, edx, esi, edi, cflag;
};
union REGS {
    struct WORDREGS w;
    struct BYTEREGS h;
    struct WORDREGS x;
    struct DWORDREGS e;  /* for 32-bit field access via outregs.e.eax */
};
struct SREGS {
    unsigned short es, cs, ss, ds;
};

/* DOS interrupt stubs - return 0, do nothing */
static int int86(int intno, union REGS *in, union REGS *out)
{
    (void)intno; (void)in;
    memset(out, 0, sizeof(*out));
    return 0;
}
static int int86x(int intno, union REGS *in, union REGS *out, struct SREGS *sr)
{
    (void)intno; (void)in; (void)sr;
    memset(out, 0, sizeof(*out));
    return 0;
}
static void segread(struct SREGS *sr)
{
    memset(sr, 0, sizeof(*sr));
}

/* Far pointer segment/offset extraction - stubs */
#define FP_SEG(p)   ((unsigned short)0)
#define FP_OFF(p)   ((unsigned short)(uintptr_t)(p))
#define MK_FP(s,o)  ((void*)(uintptr_t)(o))

/* Non-standard string conversion functions (Watcom/MSVC) */
static char *ltoa(long value, char *buf, int radix)
{
    (void)radix;
    sprintf(buf, "%ld", value);
    return buf;
}
static char *ultoa(unsigned long value, char *buf, int radix)
{
    (void)radix;
    sprintf(buf, "%lu", value);
    return buf;
}
static char *itoa(int value, char *buf, int radix)
{
    (void)radix;
    sprintf(buf, "%d", value);
    return buf;
}

#endif /* _COMPAT_DOS_H */
