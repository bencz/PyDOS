/*
 * pdos_tui.c - Minimal BIOS/DOS text terminal primitives for PyDOS.
 *
 * Compatible with 8086 real mode and the 386 DOS extender build.  The
 * public Python layer is implemented in stdlib/pydos/io/tui/.
 */

#include "pdos_tui.h"
#include <dos.h>

#define PYDOS_TUI_WIDTH  80
#define PYDOS_TUI_HEIGHT 25
#define PYDOS_DAY_MS     86400000L

static long obj_long(PyDosObj far *obj, long fallback)
{
    if (obj == (PyDosObj far *)0) return fallback;
    if ((PyDosType)obj->type == PYDT_INT) return obj->v.int_val;
    if ((PyDosType)obj->type == PYDT_BOOL) return (long)obj->v.bool_val;
    return fallback;
}

#ifdef PYDOS_32BIT

static void bios_set_cursor(unsigned int x, unsigned int y)
{
    union REGS inregs, outregs;
    inregs.x.eax = 0x0200;
    inregs.x.ebx = 0;
    inregs.x.edx = ((y & 0xFFU) << 8) | (x & 0xFFU);
    int386(0x10, &inregs, &outregs);
}

static void bios_write_cell(unsigned char ch, unsigned char attr)
{
    union REGS inregs, outregs;
    inregs.x.eax = 0x0900 | ch;
    inregs.x.ebx = (unsigned int)attr;
    inregs.x.ecx = 1;
    int386(0x10, &inregs, &outregs);
}

static void bios_clear(unsigned char attr)
{
    union REGS inregs, outregs;
    inregs.x.eax = 0x0600;
    inregs.x.ebx = ((unsigned int)attr) << 8;
    inregs.x.ecx = 0;
    inregs.x.edx = ((PYDOS_TUI_HEIGHT - 1) << 8) |
                   (PYDOS_TUI_WIDTH - 1);
    int386(0x10, &inregs, &outregs);
    bios_set_cursor(0, 0);
}

static void bios_cursor_visible(int visible)
{
    union REGS inregs, outregs;
    inregs.x.eax = 0x0100;
    inregs.x.ecx = visible ? 0x0607 : 0x2000;
    int386(0x10, &inregs, &outregs);
}

static int dos_key_available(void)
{
    union REGS inregs, outregs;
    inregs.x.eax = 0x0B00;
    int386(0x21, &inregs, &outregs);
    return (outregs.x.eax & 0xFFU) != 0;
}

static unsigned int dos_read_char(void)
{
    union REGS inregs, outregs;
    inregs.x.eax = 0x0800;
    int386(0x21, &inregs, &outregs);
    return outregs.x.eax & 0xFFU;
}

static long dos_ticks_ms(void)
{
    union REGS inregs, outregs;
    unsigned int hour, minute, second, hundredth;
    inregs.x.eax = 0x2C00;
    int386(0x21, &inregs, &outregs);
    hour = (outregs.x.ecx >> 8) & 0xFFU;
    minute = outregs.x.ecx & 0xFFU;
    second = (outregs.x.edx >> 8) & 0xFFU;
    hundredth = outregs.x.edx & 0xFFU;
    return (((long)hour * 60L + (long)minute) * 60L +
            (long)second) * 1000L + (long)hundredth * 10L;
}

#else

static void bios_set_cursor(unsigned int x, unsigned int y)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x02;
    inregs.h.bh = 0;
    inregs.h.dh = (unsigned char)y;
    inregs.h.dl = (unsigned char)x;
    int86(0x10, &inregs, &outregs);
}

static void bios_write_cell(unsigned char ch, unsigned char attr)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x09;
    inregs.h.al = ch;
    inregs.h.bh = 0;
    inregs.h.bl = attr;
    inregs.x.cx = 1;
    int86(0x10, &inregs, &outregs);
}

static void bios_clear(unsigned char attr)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x06;
    inregs.h.al = 0;
    inregs.h.bh = attr;
    inregs.h.ch = 0;
    inregs.h.cl = 0;
    inregs.h.dh = PYDOS_TUI_HEIGHT - 1;
    inregs.h.dl = PYDOS_TUI_WIDTH - 1;
    int86(0x10, &inregs, &outregs);
    bios_set_cursor(0, 0);
}

static void bios_cursor_visible(int visible)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x01;
    inregs.h.ch = visible ? 0x06 : 0x20;
    inregs.h.cl = visible ? 0x07 : 0x00;
    int86(0x10, &inregs, &outregs);
}

static int dos_key_available(void)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x0B;
    int86(0x21, &inregs, &outregs);
    return outregs.h.al != 0;
}

static unsigned int dos_read_char(void)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x08;
    int86(0x21, &inregs, &outregs);
    return (unsigned int)outregs.h.al;
}

static long dos_ticks_ms(void)
{
    union REGS inregs, outregs;
    inregs.h.ah = 0x2C;
    int86(0x21, &inregs, &outregs);
    return (((long)outregs.h.ch * 60L + (long)outregs.h.cl) * 60L +
            (long)outregs.h.dh) * 1000L + (long)outregs.h.dl * 10L;
}

#endif

PyDosObj far * PYDOS_API pydos_tui_clear(int argc,
                                          PyDosObj far * far *argv)
{
    unsigned char fg = (unsigned char)(argc > 0 ? obj_long(argv[0], 7L) : 7L);
    unsigned char bg = (unsigned char)(argc > 1 ? obj_long(argv[1], 0L) : 0L);
    bios_clear((unsigned char)((bg & 0x0FU) << 4) | (fg & 0x0FU));
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_write_at(int argc,
                                             PyDosObj far * far *argv)
{
    long x, y;
    unsigned char fg, bg, attr;
    PyDosObj far *text;
    unsigned int i;

    if (argc < 3 || argv[2] == (PyDosObj far *)0 ||
        (PyDosType)argv[2]->type != PYDT_STR)
        return pydos_obj_new_none();
    x = obj_long(argv[0], 0L);
    y = obj_long(argv[1], 0L);
    text = argv[2];
    fg = (unsigned char)(argc > 3 ? obj_long(argv[3], 7L) : 7L);
    bg = (unsigned char)(argc > 4 ? obj_long(argv[4], 0L) : 0L);
    attr = (unsigned char)(((bg & 0x0FU) << 4) | (fg & 0x0FU));

    if (y < 0 || y >= PYDOS_TUI_HEIGHT) return pydos_obj_new_none();
    for (i = 0; i < text->v.str.len; i++, x++) {
        if (x >= 0 && x < PYDOS_TUI_WIDTH) {
            bios_set_cursor((unsigned int)x, (unsigned int)y);
            bios_write_cell((unsigned char)text->v.str.data[i], attr);
        }
        if (x >= PYDOS_TUI_WIDTH) break;
    }
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_cursor(int argc,
                                          PyDosObj far * far *argv)
{
    long x = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    long y = argc > 1 ? obj_long(argv[1], 0L) : 0L;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= PYDOS_TUI_WIDTH) x = PYDOS_TUI_WIDTH - 1;
    if (y >= PYDOS_TUI_HEIGHT) y = PYDOS_TUI_HEIGHT - 1;
    bios_set_cursor((unsigned int)x, (unsigned int)y);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_cursor_visible(
    int argc, PyDosObj far * far *argv)
{
    bios_cursor_visible(argc > 0 && obj_long(argv[0], 0L) != 0L);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_key_available(
    int argc, PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_bool(dos_key_available());
}

PyDosObj far * PYDOS_API pydos_tui_read_key(int argc,
                                             PyDosObj far * far *argv)
{
    unsigned int value;
    (void)argc;
    (void)argv;
    if (!dos_key_available()) return pydos_obj_new_none();
    value = dos_read_char();
    if (value == 0 || value == 0xE0U)
        value = 0x100U + dos_read_char();
    return pydos_obj_new_int((long)value);
}

PyDosObj far * PYDOS_API pydos_tui_ticks_ms(int argc,
                                             PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_int(dos_ticks_ms());
}

PyDosObj far * PYDOS_API pydos_tui_delay_ms(int argc,
                                             PyDosObj far * far *argv)
{
    long duration = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    long start;
    long now;
    long elapsed;
    if (duration <= 0) return pydos_obj_new_none();
    start = dos_ticks_ms();
    do {
        now = dos_ticks_ms();
        elapsed = now >= start ? now - start : PYDOS_DAY_MS - start + now;
    } while (elapsed < duration);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_width(int argc,
                                         PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_int(PYDOS_TUI_WIDTH);
}

PyDosObj far * PYDOS_API pydos_tui_height(int argc,
                                          PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_int(PYDOS_TUI_HEIGHT);
}
