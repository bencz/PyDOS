/*
 * pydos_io.c - DOS I/O operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 * With PYDOS_32BIT: flat model (-mf), 386 protected-mode DOS/4GW.
 *
 * All I/O is performed via INT 21h DOS system calls.
 * 16-bit: int86()/int86x() with segment registers.
 * 32-bit: int386()/int386x() with flat pointers (DOS/4GW extender).
 */

#include "pdos_io.h"
#include <dos.h>
#include <string.h>

/* ================================================================== */
/* 32-bit protected mode (DOS/4GW) I/O                                */
/* ================================================================== */
#ifdef PYDOS_32BIT

/*
 * dos_write_handle - Write data to a DOS file handle.
 * Uses INT 21h AH=40h. EBX=handle, ECX=count, EDX=buffer (linear).
 * DOS/4GW transparently handles the protected->real mode transition.
 */
static int dos_write_handle(int handle, const char *data, unsigned int len)
{
    union REGS inregs, outregs;

    if (len == 0) {
        return 0;
    }

    inregs.x.eax = 0x4000;              /* AH=40h */
    inregs.x.ebx = (unsigned int)handle;
    inregs.x.ecx = len;
    inregs.x.edx = (unsigned int)data;  /* linear pointer */

    int386(0x21, &inregs, &outregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)(outregs.x.eax & 0xFFFF);
}

int PYDOS_API pydos_dos_write_far(const char far *data, unsigned int len)
{
    /* In flat model, far == near */
    return dos_write_handle(1, (const char *)data, len);
}

int PYDOS_API pydos_dos_write(const char *data, unsigned int len)
{
    return dos_write_handle(1, data, len);
}

void PYDOS_API pydos_dos_putchar(char c)
{
    union REGS inregs, outregs;

    inregs.x.eax = 0x0200;              /* AH=02h */
    inregs.x.edx = (unsigned char)c;    /* DL=char */
    int386(0x21, &inregs, &outregs);
}

unsigned int PYDOS_API pydos_dos_readline(char far *buf, unsigned int maxlen)
{
    union REGS inregs, outregs;
    unsigned int pos;

    pos = 0;
    if (maxlen == 0) {
        return 0;
    }

    for (;;) {
        /* AH=01h - Read character with echo */
        inregs.x.eax = 0x0100;
        int386(0x21, &inregs, &outregs);

        if ((outregs.x.eax & 0xFF) == 0x0D) {
            pydos_dos_putchar('\r');
            pydos_dos_putchar('\n');
            break;
        }

        if ((outregs.x.eax & 0xFF) == 0x08) {
            if (pos > 0) {
                pos--;
                pydos_dos_putchar(' ');
                pydos_dos_putchar(0x08);
            }
            continue;
        }

        if ((outregs.x.eax & 0xFF) == 0x1B) {
            while (pos > 0) {
                pos--;
                pydos_dos_putchar(0x08);
                pydos_dos_putchar(' ');
                pydos_dos_putchar(0x08);
            }
            continue;
        }

        if (pos < maxlen - 1) {
            buf[pos] = (char)(outregs.x.eax & 0xFF);
            pos++;
        }
    }

    buf[pos] = '\0';
    return pos;
}

void PYDOS_API pydos_dos_print_str(const char far *str)
{
    unsigned int len;

    if (str == (const char far *)0) {
        return;
    }

    len = _fstrlen(str);
    if (len > 0) {
        pydos_dos_write_far(str, len);
    }
}

int PYDOS_API pydos_dos_file_open(const char far *path, unsigned char mode)
{
    union REGS inregs, outregs;

    inregs.x.edx = (unsigned int)path;  /* linear pointer */

    if (mode == 1) {
        inregs.x.eax = 0x3C00;          /* AH=3Ch create file */
        inregs.x.ecx = 0;               /* normal attributes */
    } else {
        inregs.x.eax = 0x3D00;          /* AH=3Dh open file */
        if (mode == 0) {
            inregs.x.eax = 0x3D00;      /* AL=00 read only */
        } else {
            inregs.x.eax = 0x3D02;      /* AL=02 read/write */
        }
    }

    int386(0x21, &inregs, &outregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)(outregs.x.eax & 0xFFFF);
}

void PYDOS_API pydos_dos_file_close(int handle)
{
    union REGS inregs, outregs;

    if (handle < 0) {
        return;
    }

    inregs.x.eax = 0x3E00;              /* AH=3Eh close file */
    inregs.x.ebx = (unsigned int)handle;
    int386(0x21, &inregs, &outregs);
}

int PYDOS_API pydos_dos_file_read(int handle, char far *buf, unsigned int count)
{
    union REGS inregs, outregs;

    if (handle < 0 || count == 0) {
        return 0;
    }

    inregs.x.eax = 0x3F00;              /* AH=3Fh read file */
    inregs.x.ebx = (unsigned int)handle;
    inregs.x.ecx = count;
    inregs.x.edx = (unsigned int)buf;   /* linear pointer */

    int386(0x21, &inregs, &outregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)(outregs.x.eax & 0xFFFF);
}

int PYDOS_API pydos_dos_file_write(int handle, const char far *data, unsigned int count)
{
    return dos_write_handle(handle, (const char *)data, count);
}

/* ================================================================== */
/* 16-bit real-mode I/O (existing code)                               */
/* ================================================================== */
#else /* !PYDOS_32BIT */

/*
 * pydos_dos_write_far - Write far data to a DOS file handle.
 * Uses INT 21h AH=40h. BX=handle, CX=count, DS:DX=buffer.
 * Returns bytes written, or -1 on error.
 */
static int dos_write_handle(int handle, const char far *data, unsigned int len)
{
    union REGS inregs, outregs;
    struct SREGS sregs;

    if (len == 0) {
        return 0;
    }

    inregs.h.ah = 0x40;
    inregs.x.bx = (unsigned int)handle;
    inregs.x.cx = len;
    inregs.x.dx = FP_OFF(data);
    segread(&sregs);
    sregs.ds = FP_SEG(data);

    int86x(0x21, &inregs, &outregs, &sregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)outregs.x.ax;
}

int PYDOS_API pydos_dos_write_far(const char far *data, unsigned int len)
{
    return dos_write_handle(1, data, len);
}

int PYDOS_API pydos_dos_write(const char *data, unsigned int len)
{
    union REGS inregs, outregs;
    struct SREGS sregs;

    if (len == 0) {
        return 0;
    }

    inregs.h.ah = 0x40;
    inregs.x.bx = 1;  /* stdout */
    inregs.x.cx = len;
    inregs.x.dx = FP_OFF(data);
    segread(&sregs);
    /* For near pointers in large model, DS is already correct from segread */

    int86x(0x21, &inregs, &outregs, &sregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)outregs.x.ax;
}

void PYDOS_API pydos_dos_putchar(char c)
{
    union REGS inregs, outregs;

    inregs.h.ah = 0x02;
    inregs.h.dl = (unsigned char)c;
    int86(0x21, &inregs, &outregs);
}

unsigned int PYDOS_API pydos_dos_readline(char far *buf, unsigned int maxlen)
{
    union REGS inregs, outregs;
    unsigned int pos;

    pos = 0;
    if (maxlen == 0) {
        return 0;
    }

    for (;;) {
        /* AH=01h - Read character with echo */
        inregs.h.ah = 0x01;
        int86(0x21, &inregs, &outregs);

        if (outregs.h.al == 0x0D) {
            /* CR received - end of line */
            /* Echo a newline */
            pydos_dos_putchar('\r');
            pydos_dos_putchar('\n');
            break;
        }

        if (outregs.h.al == 0x08) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                /* Erase character on screen: backspace, space, backspace */
                pydos_dos_putchar(' ');
                pydos_dos_putchar(0x08);
            }
            continue;
        }

        if (outregs.h.al == 0x1B) {
            /* Escape - clear line */
            while (pos > 0) {
                pos--;
                pydos_dos_putchar(0x08);
                pydos_dos_putchar(' ');
                pydos_dos_putchar(0x08);
            }
            continue;
        }

        /* Store printable character */
        if (pos < maxlen - 1) {
            buf[pos] = (char)outregs.h.al;
            pos++;
        }
    }

    buf[pos] = '\0';
    return pos;
}

void PYDOS_API pydos_dos_print_str(const char far *str)
{
    unsigned int len;

    if (str == (const char far *)0) {
        return;
    }

    len = _fstrlen(str);
    if (len > 0) {
        pydos_dos_write_far(str, len);
    }
}

int PYDOS_API pydos_dos_file_open(const char far *path, unsigned char mode)
{
    union REGS inregs, outregs;
    struct SREGS sregs;

    segread(&sregs);
    sregs.ds = FP_SEG(path);
    inregs.x.dx = FP_OFF(path);

    if (mode == 1) {
        /* Write mode: create new file (or truncate existing) */
        /* AH=3Ch - Create file, CX=0 (normal attributes) */
        inregs.h.ah = 0x3C;
        inregs.x.cx = 0;
    } else {
        /* Read or read/write mode: open existing */
        /* AH=3Dh - Open existing file, AL=access mode */
        inregs.h.ah = 0x3D;
        if (mode == 0) {
            inregs.h.al = 0x00;  /* read only */
        } else {
            inregs.h.al = 0x02;  /* read/write */
        }
    }

    int86x(0x21, &inregs, &outregs, &sregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)outregs.x.ax;
}

void PYDOS_API pydos_dos_file_close(int handle)
{
    union REGS inregs, outregs;

    if (handle < 0) {
        return;
    }

    /* AH=3Eh - Close file, BX=handle */
    inregs.h.ah = 0x3E;
    inregs.x.bx = (unsigned int)handle;
    int86(0x21, &inregs, &outregs);
}

int PYDOS_API pydos_dos_file_read(int handle, char far *buf, unsigned int count)
{
    union REGS inregs, outregs;
    struct SREGS sregs;

    if (handle < 0 || count == 0) {
        return 0;
    }

    /* AH=3Fh - Read from file, BX=handle, CX=count, DS:DX=buffer */
    inregs.h.ah = 0x3F;
    inregs.x.bx = (unsigned int)handle;
    inregs.x.cx = count;
    inregs.x.dx = FP_OFF(buf);
    segread(&sregs);
    sregs.ds = FP_SEG(buf);

    int86x(0x21, &inregs, &outregs, &sregs);

    if (outregs.x.cflag & 1) {
        return -1;
    }
    return (int)outregs.x.ax;
}

int PYDOS_API pydos_dos_file_write(int handle, const char far *data, unsigned int count)
{
    return dos_write_handle(handle, data, count);
}

#endif /* PYDOS_32BIT */

void PYDOS_API pydos_io_init(void)
{
    /* No initialization needed for basic DOS I/O */
}

void PYDOS_API pydos_io_shutdown(void)
{
    /* No cleanup needed for basic DOS I/O */
}
