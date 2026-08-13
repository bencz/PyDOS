/*
 * pdos_tui.c - DOS text terminal primitives for PyDOS.
 *
 * Compatible with 8086 real mode, the 386 DOS extender build and the
 * flat host build used by rttests.  The public Python layer lives in
 * stdlib/pydos/tui/.
 *
 * The engine keeps a shadow copy of text video memory and writes only
 * the cells that changed (pydos_tui_present), replacing the legacy
 * two-INT10h-per-character path.  Dimensions, monochrome detection and
 * timing come from the BIOS data area; the extended keyboard interface
 * (INT 16h AH=10h/11h/12h) reports Ctrl/Alt/Shift and F11/F12 with a
 * fallback to the XT services; the mouse uses INT 33h press/release
 * counters so clicks between polls are never lost.
 *
 * Host build: no BIOS exists, so the low-level layer runs against a
 * fake VRAM array and a synthetic clock (advances 55 ms per read).
 * That keeps every algorithm — including the present() diff —
 * deterministic and testable by rttests/t_tui.c.
 */

#include "pdos_tui.h"
#include <string.h>

#ifdef __WATCOMC__
#include <dos.h>
#include <conio.h>
#include <bios.h>
#else
#include <dos.h>   /* compat/dos.h shim */
#endif

/* ------------------------------------------------------------------ */
/* Shared state                                                        */
/* ------------------------------------------------------------------ */

/* 80x50 is the largest supported text geometry: 4000 cells.  Larger
 * modes (e.g. 132 columns set by external tools) degrade to
 * write-through without a diff. */
#define PYDOS_TUI_MAX_CELLS  4000U

/* BIOS tick counter: 18.2065 Hz, ~54.925 ms per tick, 0x1800B0 ticks
 * per day.  55 ms per tick keeps the math in 31 bits. */
#define PYDOS_TICK_MS        55L
#define PYDOS_TICK_DAY_MS    86517200L

/* The shadow mirrors interleaved VRAM (char | attr << 8).  The explicit
 * far qualifier moves the 8000 bytes into their own FAR_DATA segment on
 * the 8086 so DGROUP stays untouched; it expands to nothing on the 386
 * and host builds. */
static unsigned short far tui_shadow[PYDOS_TUI_MAX_CELLS];

static int g_probed = 0;
static unsigned int g_cols = 80;
static unsigned int g_rows = 25;
static int g_mono = 0;
static int g_diff = 1;           /* shadow diff active (cells fit) */
static long g_write_count = 0;   /* cells written since last query */
static int g_mouse_ok = 0;

static long obj_long(PyDosObj far *obj, long fallback)
{
    if (obj == (PyDosObj far *)0) return fallback;
    if ((PyDosType)obj->type == PYDT_INT) return obj->v.int_val;
    if ((PyDosType)obj->type == PYDT_BOOL) return (long)obj->v.bool_val;
    return fallback;
}

/* ------------------------------------------------------------------ */
/* Pure packing helpers (target-independent; unit-tested directly)     */
/* ------------------------------------------------------------------ */

long PYDOS_API pydos_tui_pack_key(unsigned int ax, unsigned int shift_flags)
{
    unsigned int ascii = ax & 0xFFU;
    unsigned int scan = (ax >> 8) & 0xFFU;
    long packed;

    /* The enhanced keyboard reports grey (E0-prefixed) keys with
     * AL=0xE0; normalize to 0 so Python sees ascii==0 => special. */
    if (ascii == 0xE0U && scan != 0U) ascii = 0U;

    packed = (long)ascii | ((long)scan << 8);
    if (shift_flags & 0x03U) packed |= 1L << 16;   /* either shift   */
    if (shift_flags & 0x04U) packed |= 1L << 17;   /* ctrl           */
    if (shift_flags & 0x08U) packed |= 1L << 18;   /* alt            */
    return packed;
}

long PYDOS_API pydos_tui_pack_mouse(unsigned int col, unsigned int row,
                                    unsigned int buttons,
                                    unsigned int left_press,
                                    unsigned int left_release,
                                    unsigned int right_press,
                                    unsigned int right_release)
{
    long packed = (long)(col & 0xFFU) | ((long)(row & 0xFFU) << 8) |
                  ((long)(buttons & 0x07U) << 16);
    if (left_press) packed |= 1L << 19;
    if (left_release) packed |= 1L << 20;
    if (right_press) packed |= 1L << 21;
    if (right_release) packed |= 1L << 22;
    return packed;
}

/* ------------------------------------------------------------------ */
/* Low-level layer                                                     */
/* ------------------------------------------------------------------ */

#if !defined(__WATCOMC__)

/* ---- host build: fake VRAM, synthetic clock, no input ------------- */

static unsigned short host_vram[PYDOS_TUI_MAX_CELLS];
static long host_ticks = 0;
static unsigned int host_rows = 25;
static unsigned int host_cursor_shape = 0x0607U;
static unsigned int host_mode = 3;

static void tlow_probe_dims(unsigned int *cols, unsigned int *rows,
                            int *mono)
{
    *cols = 80;
    *rows = host_rows;
    *mono = 0;
}

static void tlow_map_vram(int mono)
{
    (void)mono;
}

static unsigned short tlow_vid_read(unsigned int idx)
{
    return idx < PYDOS_TUI_MAX_CELLS ? host_vram[idx] : 0;
}

static void tlow_vid_write(unsigned int idx, unsigned short cell)
{
    if (idx < PYDOS_TUI_MAX_CELLS) host_vram[idx] = cell;
}

static void tlow_set_cursor_pos(unsigned int x, unsigned int y)
{
    (void)x;
    (void)y;
}

static void tlow_set_cursor_shape_cx(unsigned int cx)
{
    host_cursor_shape = cx;
}

static unsigned int tlow_font_height(void)
{
    return host_rows == 25 ? 16 : 8;
}

static unsigned int tlow_video_mode(void)
{
    return host_mode;
}

static unsigned int tlow_cursor_shape_word(void)
{
    return host_cursor_shape;
}

static void tlow_apply_rows(unsigned int rows)
{
    host_rows = rows;
    host_mode = 3;
}

static void tlow_set_video_mode(unsigned int mode)
{
    host_mode = mode;
}

static void tlow_blink(int enabled)
{
    (void)enabled;
}

static void tlow_vsync_wait(void)
{
}

static long tlow_ticks_ms(void)
{
    host_ticks += PYDOS_TICK_MS;
    return host_ticks;
}

static void tlow_sleep_ms(long duration)
{
    host_ticks += duration;
}

static long tlow_key_event(void)
{
    return -1L;
}

static long tlow_shift_state(void)
{
    return 0L;
}

static long tlow_mouse_init(void)
{
    return 0L;
}

static long tlow_mouse_poll(void)
{
    return -1L;
}

static void tlow_mouse_show(int visible)
{
    (void)visible;
}

static unsigned short far *tlow_debug_vram(void)
{
    return host_vram;
}

#else

/* ---- DOS builds (8086 real mode and 386 extender) ----------------- */

#ifdef PYDOS_32BIT
#define TUI_RW(r)        ((r).w)
#define TUI_BDA_BYTE(o)  (*(volatile unsigned char *)(0x400UL + (o)))
#define TUI_BDA_WORD(o)  (*(volatile unsigned short *)(0x400UL + (o)))
#define TUI_BDA_DWORD(o) (*(volatile unsigned long *)(0x400UL + (o)))
#else
#define TUI_RW(r)        ((r).x)
#define TUI_BDA_BYTE(o)  (*(volatile unsigned char far *)MK_FP(0x0040, (o)))
#define TUI_BDA_WORD(o)  (*(volatile unsigned short far *)MK_FP(0x0040, (o)))
#define TUI_BDA_DWORD(o) (*(volatile unsigned long far *)MK_FP(0x0040, (o)))
#endif

static void tui_int(int vector, union REGS *regs)
{
#ifdef PYDOS_32BIT
    int386(vector, regs, regs);
#else
    int86(vector, regs, regs);
#endif
}

#ifdef PYDOS_32BIT
/* Video memory under the extender.  CauseWay and DOS/4GW both provide
 * DPMI services, so a selector for the real-mode segment (INT 31h
 * AX=0002) is the portable primary path; the zero-based linear mapping
 * of the first megabyte is the fallback. */
static unsigned short __far *vram_fp = 0;
static volatile unsigned short *vram_lp = 0;
#else
static unsigned int vram_seg = 0xB800U;
#endif

static void tlow_map_vram(int mono)
{
    unsigned int seg = mono ? 0xB000U : 0xB800U;
#ifdef PYDOS_32BIT
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.x.eax = 0x0002UL;
    regs.x.ebx = (unsigned long)seg;
    tui_int(0x31, &regs);
    if (!TUI_RW(regs).cflag) {
        vram_fp = (unsigned short __far *)MK_FP(TUI_RW(regs).ax, 0);
        vram_lp = 0;
    } else {
        vram_fp = 0;
        vram_lp = (volatile unsigned short *)((unsigned long)seg << 4);
    }
#else
    vram_seg = seg;
#endif
}

static unsigned short tlow_vid_read(unsigned int idx)
{
#ifdef PYDOS_32BIT
    if (vram_fp) return vram_fp[idx];
    return vram_lp[idx];
#else
    return *(volatile unsigned short far *)MK_FP(vram_seg, idx * 2U);
#endif
}

static void tlow_vid_write(unsigned int idx, unsigned short cell)
{
#ifdef PYDOS_32BIT
    if (vram_fp) vram_fp[idx] = cell;
    else vram_lp[idx] = cell;
#else
    *(volatile unsigned short far *)MK_FP(vram_seg, idx * 2U) = cell;
#endif
}

static void tlow_probe_dims(unsigned int *cols, unsigned int *rows,
                            int *mono)
{
    unsigned int columns = TUI_BDA_WORD(0x4A);
    unsigned int lines = (unsigned int)TUI_BDA_BYTE(0x84) + 1U;
    if (columns < 40U || columns > 132U) columns = 80U;
    if (lines < 2U || lines > 60U) lines = 25U;   /* pre-EGA BDA: 0 */
    *cols = columns;
    *rows = lines;
    *mono = TUI_BDA_WORD(0x63) == 0x3B4U;
}

static void tlow_set_cursor_pos(unsigned int x, unsigned int y)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x02;
    regs.h.bh = 0;
    regs.h.dh = (unsigned char)y;
    regs.h.dl = (unsigned char)x;
    tui_int(0x10, &regs);
}

static void tlow_set_cursor_shape_cx(unsigned int cx)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x01;
    TUI_RW(regs).cx = (unsigned short)cx;
    tui_int(0x10, &regs);
}

static unsigned int tlow_font_height(void)
{
    unsigned int height = TUI_BDA_BYTE(0x85);
    if (height < 2U || height > 32U) height = 16U;
    return height;
}

static unsigned int tlow_video_mode(void)
{
    return TUI_BDA_BYTE(0x49);
}

static unsigned int tlow_cursor_shape_word(void)
{
    return TUI_BDA_WORD(0x60);
}

static void tlow_set_video_mode(unsigned int mode)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x00;
    regs.h.al = (unsigned char)mode;
    tui_int(0x10, &regs);
}

/* AX=1201h BL=30h selects 350 scanlines, AX=1202h 400 scanlines; both
 * take effect on the next mode set.  Ignored by pre-VGA BIOSes, which
 * makes set_rows degrade to staying at 25 lines. */
static void tlow_set_scanlines(unsigned char al)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x12;
    regs.h.al = al;
    regs.h.bl = 0x30;
    tui_int(0x10, &regs);
}

/* AX=1112h loads the 8x8 font (43/50 lines), AX=1114h the 8x16. */
static void tlow_load_font(unsigned char al)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x11;
    regs.h.al = al;
    regs.h.bl = 0;
    tui_int(0x10, &regs);
}

static void tlow_apply_rows(unsigned int rows)
{
    if (rows == 43U) {
        tlow_set_scanlines(0x01);
        tlow_set_video_mode(0x03);
        tlow_load_font(0x12);
    } else if (rows == 50U) {
        tlow_set_scanlines(0x02);
        tlow_set_video_mode(0x03);
        tlow_load_font(0x12);
    } else {
        tlow_set_scanlines(0x02);
        tlow_set_video_mode(0x03);
        tlow_load_font(0x14);
    }
}

static void tlow_blink(int enabled)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x1003U;
    regs.h.bl = (unsigned char)(enabled ? 1 : 0);
    tui_int(0x10, &regs);
}

/* Wait for the start of a vertical retrace on the CRTC status port.
 * Bounded: emulators that do not toggle bit 3 must not hang us. */
static void tlow_vsync_wait(void)
{
    unsigned int port = TUI_BDA_WORD(0x63) + 6U;
    unsigned int guard;
    for (guard = 0xFFFFU; guard != 0U; guard--) {
        if (!(inp(port) & 0x08)) break;
    }
    for (guard = 0xFFFFU; guard != 0U; guard--) {
        if (inp(port) & 0x08) break;
    }
}

/* BIOS tick counter at 0040:006C.  On the 8086 the timer ISR can fire
 * between the two 16-bit halves of the read, so read until stable. */
static long tlow_ticks_ms(void)
{
    unsigned long first, second;
    do {
        first = TUI_BDA_DWORD(0x6C);
        second = TUI_BDA_DWORD(0x6C);
    } while (first != second);
    return (long)(first * (unsigned long)PYDOS_TICK_MS);
}

/* Release the time slice while waiting (INT 2Fh AX=1680h: harmless
 * no-op under plain DOS, yields under DOSEMU/Windows/DPMI hosts). */
static void tlow_idle(void)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x1680U;
    tui_int(0x2F, &regs);
}

static void tlow_sleep_ms(long duration)
{
    long start = tlow_ticks_ms();
    long now, elapsed;
    do {
        tlow_idle();
        now = tlow_ticks_ms();
        elapsed = now >= start ? now - start
                               : PYDOS_TICK_DAY_MS - start + now;
    } while (elapsed < duration);
}

/* Enhanced keyboard when the BIOS advertises it (BDA 0040:0096 bit 4),
 * XT services otherwise.  The packing is shared with the unit tests. */
static long tlow_key_event(void)
{
    int enhanced = (TUI_BDA_BYTE(0x96) & 0x10) != 0;
    unsigned int ax, shift;
    if (_bios_keybrd(enhanced ? _NKEYBRD_READY : _KEYBRD_READY) == 0U)
        return -1L;
    ax = _bios_keybrd(enhanced ? _NKEYBRD_READ : _KEYBRD_READ);
    shift = _bios_keybrd(enhanced ? _NKEYBRD_SHIFTSTATUS
                                  : _KEYBRD_SHIFTSTATUS);
    return pydos_tui_pack_key(ax, shift & 0x0FU);
}

static long tlow_shift_state(void)
{
    int enhanced = (TUI_BDA_BYTE(0x96) & 0x10) != 0;
    return (long)(_bios_keybrd(enhanced ? _NKEYBRD_SHIFTSTATUS
                                        : _KEYBRD_SHIFTSTATUS) & 0xFFU);
}

static long tlow_mouse_init(void)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x0000U;
    tui_int(0x33, &regs);
    if (TUI_RW(regs).ax == 0U) return 0L;
    return TUI_RW(regs).bx == 0U ? 2L : (long)TUI_RW(regs).bx;
}

static long tlow_mouse_poll(void)
{
    union REGS regs;
    unsigned int buttons, col, row;
    unsigned int lp, lr, rp, rr;

    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x0003U;
    tui_int(0x33, &regs);
    buttons = TUI_RW(regs).bx & 0x07U;
    col = TUI_RW(regs).cx >> 3;
    row = TUI_RW(regs).dx >> 3;

    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x0005U;
    TUI_RW(regs).bx = 0U;
    tui_int(0x33, &regs);
    lp = TUI_RW(regs).bx;

    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x0006U;
    TUI_RW(regs).bx = 0U;
    tui_int(0x33, &regs);
    lr = TUI_RW(regs).bx;

    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x0005U;
    TUI_RW(regs).bx = 1U;
    tui_int(0x33, &regs);
    rp = TUI_RW(regs).bx;

    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = 0x0006U;
    TUI_RW(regs).bx = 1U;
    tui_int(0x33, &regs);
    rr = TUI_RW(regs).bx;

    return pydos_tui_pack_mouse(col, row, buttons, lp, lr, rp, rr);
}

static void tlow_mouse_show(int visible)
{
    union REGS regs;
    memset(&regs, 0, sizeof(regs));
    TUI_RW(regs).ax = visible ? 0x0001U : 0x0002U;
    tui_int(0x33, &regs);
}

static unsigned short far *tlow_debug_vram(void)
{
#ifdef PYDOS_32BIT
    /* A near pointer cannot carry the DPMI selector; NULL tells the
     * tests to skip direct read-back in that configuration. */
    return (unsigned short far *)vram_lp;
#else
    return (unsigned short far *)MK_FP(vram_seg, 0);
#endif
}

#endif /* target selection */

/* ------------------------------------------------------------------ */
/* Shared engine                                                       */
/* ------------------------------------------------------------------ */

/* Establishes the invariant shadow[i] == VRAM[i] for every cell. */
static void tui_resync(void)
{
    unsigned long cells = (unsigned long)g_cols * (unsigned long)g_rows;
    unsigned int idx;
    g_diff = cells <= (unsigned long)PYDOS_TUI_MAX_CELLS;
    if (!g_diff) return;
    for (idx = 0; idx < (unsigned int)cells; idx++) {
        tui_shadow[idx] = tlow_vid_read(idx);
    }
}

static void tui_probe(void)
{
    tlow_probe_dims(&g_cols, &g_rows, &g_mono);
    tlow_map_vram(g_mono);
    tui_resync();
    g_probed = 1;
}

static void tui_ensure_probed(void)
{
    if (!g_probed) tui_probe();
}

/* Every engine write goes through here: VRAM is only touched when the
 * cell actually changes, and the shadow follows each write. */
static void tui_store(unsigned int idx, unsigned short cell)
{
    if (g_diff) {
        if (tui_shadow[idx] == cell) return;
        tui_shadow[idx] = cell;
    }
    tlow_vid_write(idx, cell);
    g_write_count++;
}

static unsigned short tui_load(unsigned int idx)
{
    if (g_diff) return tui_shadow[idx];
    return tlow_vid_read(idx);
}

/* ---- video entry points ------------------------------------------- */

PyDosObj far * PYDOS_API pydos_tui_probe(int argc,
                                         PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    tui_probe();
    return pydos_obj_new_int((long)g_cols | ((long)g_rows << 8) |
                             ((long)(g_mono ? 1 : 0) << 16));
}

PyDosObj far * PYDOS_API pydos_tui_present(int argc,
                                           PyDosObj far * far *argv)
{
    PyDosObj far *glyphs;
    PyDosObj far *attrs;
    long origin_x, origin_y;
    unsigned int nrows, i;

    if (argc < 2 ||
        argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[0]->type != PYDT_LIST ||
        (PyDosType)argv[1]->type != PYDT_LIST)
        return pydos_obj_new_none();

    tui_ensure_probed();

    glyphs = argv[0];
    attrs = argv[1];
    origin_x = argc > 2 ? obj_long(argv[2], 0L) : 0L;
    origin_y = argc > 3 ? obj_long(argv[3], 0L) : 0L;

    nrows = glyphs->v.list.len < attrs->v.list.len
            ? glyphs->v.list.len : attrs->v.list.len;
    for (i = 0; i < nrows; i++) {
        PyDosObj far *glyph_row = glyphs->v.list.items[i];
        PyDosObj far *attr_row = attrs->v.list.items[i];
        long row = origin_y + (long)i;
        unsigned int len, j0, j;
        long left;
        unsigned int base;

        if (row < 0 || row >= (long)g_rows) continue;
        if (glyph_row == (PyDosObj far *)0 ||
            attr_row == (PyDosObj far *)0 ||
            (PyDosType)glyph_row->type != PYDT_STR ||
            (PyDosType)attr_row->type != PYDT_STR)
            continue;

        len = glyph_row->v.str.len < attr_row->v.str.len
              ? glyph_row->v.str.len : attr_row->v.str.len;
        j0 = 0;
        left = origin_x;
        if (left < 0) {
            if ((long)len <= -left) continue;
            j0 = (unsigned int)(-left);
            left = 0;
        }
        if (left >= (long)g_cols) continue;
        if ((long)len - (long)j0 > (long)g_cols - left) {
            len = (unsigned int)((long)g_cols - left) + j0;
        }
        base = (unsigned int)row * g_cols + (unsigned int)left;
        for (j = j0; j < len; j++) {
            unsigned short cell = (unsigned short)
                ((unsigned char)glyph_row->v.str.data[j] |
                 ((unsigned short)(unsigned char)attr_row->v.str.data[j]
                  << 8));
            tui_store(base + (j - j0), cell);
        }
    }
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_fill(int argc,
                                        PyDosObj far * far *argv)
{
    long x = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    long y = argc > 1 ? obj_long(argv[1], 0L) : 0L;
    long w = argc > 2 ? obj_long(argv[2], 0L) : 0L;
    long h = argc > 3 ? obj_long(argv[3], 0L) : 0L;
    long ch = argc > 4 ? obj_long(argv[4], 32L) : 32L;
    long attr = argc > 5 ? obj_long(argv[5], 7L) : 7L;
    unsigned short cell;
    long row, col;

    tui_ensure_probed();

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (long)g_cols) w = (long)g_cols - x;
    if (y + h > (long)g_rows) h = (long)g_rows - y;
    if (w <= 0 || h <= 0) return pydos_obj_new_none();

    cell = (unsigned short)((unsigned char)ch |
                            ((unsigned short)(unsigned char)attr << 8));
    for (row = y; row < y + h; row++) {
        unsigned int base = (unsigned int)row * g_cols;
        for (col = x; col < x + w; col++) {
            tui_store(base + (unsigned int)col, cell);
        }
    }
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_scroll(int argc,
                                          PyDosObj far * far *argv)
{
    long x = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    long y = argc > 1 ? obj_long(argv[1], 0L) : 0L;
    long w = argc > 2 ? obj_long(argv[2], 0L) : 0L;
    long h = argc > 3 ? obj_long(argv[3], 0L) : 0L;
    long lines = argc > 4 ? obj_long(argv[4], 0L) : 0L;
    long attr = argc > 5 ? obj_long(argv[5], 7L) : 7L;
    unsigned short blank;
    long magnitude, row, col;

    tui_ensure_probed();

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (long)g_cols) w = (long)g_cols - x;
    if (y + h > (long)g_rows) h = (long)g_rows - y;
    if (w <= 0 || h <= 0 || lines == 0) return pydos_obj_new_none();

    blank = (unsigned short)(32U |
                             ((unsigned short)(unsigned char)attr << 8));
    magnitude = lines < 0 ? -lines : lines;
    if (magnitude >= h) {
        for (row = y; row < y + h; row++) {
            unsigned int base = (unsigned int)row * g_cols;
            for (col = x; col < x + w; col++) {
                tui_store(base + (unsigned int)col, blank);
            }
        }
        return pydos_obj_new_none();
    }

    if (lines > 0) {
        /* Scroll up: rows move towards y. */
        for (row = y; row < y + h - lines; row++) {
            unsigned int dst = (unsigned int)row * g_cols;
            unsigned int src = (unsigned int)(row + lines) * g_cols;
            for (col = x; col < x + w; col++) {
                tui_store(dst + (unsigned int)col,
                          tui_load(src + (unsigned int)col));
            }
        }
        for (row = y + h - lines; row < y + h; row++) {
            unsigned int base = (unsigned int)row * g_cols;
            for (col = x; col < x + w; col++) {
                tui_store(base + (unsigned int)col, blank);
            }
        }
    } else {
        /* Scroll down: rows move away from y; iterate bottom-up. */
        for (row = y + h - 1; row >= y + magnitude; row--) {
            unsigned int dst = (unsigned int)row * g_cols;
            unsigned int src = (unsigned int)(row - magnitude) * g_cols;
            for (col = x; col < x + w; col++) {
                tui_store(dst + (unsigned int)col,
                          tui_load(src + (unsigned int)col));
            }
        }
        for (row = y; row < y + magnitude; row++) {
            unsigned int base = (unsigned int)row * g_cols;
            for (col = x; col < x + w; col++) {
                tui_store(base + (unsigned int)col, blank);
            }
        }
    }
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_cursor(int argc,
                                          PyDosObj far * far *argv)
{
    long x = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    long y = argc > 1 ? obj_long(argv[1], 0L) : 0L;
    tui_ensure_probed();
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (long)g_cols) x = (long)g_cols - 1;
    if (y >= (long)g_rows) y = (long)g_rows - 1;
    tlow_set_cursor_pos((unsigned int)x, (unsigned int)y);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_cursor_shape(int argc,
                                                PyDosObj far * far *argv)
{
    long kind = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    unsigned int height, cx;

    tui_ensure_probed();
    if (kind <= 0) {
        cx = 0x2000U;                       /* hidden */
    } else {
        height = tlow_font_height();
        if (kind == 1) {
            cx = ((height - 2U) << 8) | (height - 1U);   /* underline */
        } else {
            cx = (0U << 8) | (height - 1U);              /* block */
        }
    }
    tlow_set_cursor_shape_cx(cx);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_set_rows(int argc,
                                            PyDosObj far * far *argv)
{
    long rows = argc > 0 ? obj_long(argv[0], 25L) : 25L;
    if (rows != 25L && rows != 43L && rows != 50L) {
        tui_ensure_probed();
        return pydos_obj_new_int((long)g_rows);
    }
    tlow_apply_rows((unsigned int)rows);
    tui_probe();
    return pydos_obj_new_int((long)g_rows);
}

PyDosObj far * PYDOS_API pydos_tui_blink(int argc,
                                         PyDosObj far * far *argv)
{
    tlow_blink(argc > 0 && obj_long(argv[0], 0L) != 0L);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_save_video(int argc,
                                              PyDosObj far * far *argv)
{
    unsigned int shape;
    long state;
    (void)argc;
    (void)argv;
    tui_ensure_probed();
    shape = tlow_cursor_shape_word();
    state = (long)(tlow_video_mode() & 0xFFU) |
            ((long)(g_rows & 0xFFU) << 8) |
            ((long)((shape >> 8) & 0x7FU) << 16) |
            ((long)(shape & 0x7FU) << 23);
    return pydos_obj_new_int(state);
}

PyDosObj far * PYDOS_API pydos_tui_restore_video(int argc,
                                                 PyDosObj far * far *argv)
{
    long state = argc > 0 ? obj_long(argv[0], -1L) : -1L;
    unsigned int mode, rows, start, end;

    if (state < 0) return pydos_obj_new_none();
    mode = (unsigned int)(state & 0xFFL);
    rows = (unsigned int)((state >> 8) & 0xFFL);
    start = (unsigned int)((state >> 16) & 0x7FL);
    end = (unsigned int)((state >> 23) & 0x7FL);

    tui_ensure_probed();
    if (rows != g_rows &&
        (rows == 25U || rows == 43U || rows == 50U)) {
        tlow_apply_rows(rows);
    } else if (mode != tlow_video_mode()) {
        tlow_set_video_mode(mode);
    }
    tlow_set_cursor_shape_cx((start << 8) | end);
    tui_probe();
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_tui_vsync(int argc,
                                         PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    tui_ensure_probed();
    tlow_vsync_wait();
    return pydos_obj_new_none();
}

/* ---- keyboard entry points ---------------------------------------- */

PyDosObj far * PYDOS_API pydos_tui_key_event(int argc,
                                             PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_int(tlow_key_event());
}

PyDosObj far * PYDOS_API pydos_tui_shift_state(int argc,
                                               PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_int(tlow_shift_state());
}

/* ---- mouse entry points ------------------------------------------- */

PyDosObj far * PYDOS_API pydos_tui_mouse_init(int argc,
                                              PyDosObj far * far *argv)
{
    long buttons;
    (void)argc;
    (void)argv;
    buttons = tlow_mouse_init();
    g_mouse_ok = buttons > 0;
    return pydos_obj_new_int(buttons);
}

PyDosObj far * PYDOS_API pydos_tui_mouse_poll(int argc,
                                              PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    if (!g_mouse_ok) return pydos_obj_new_int(-1L);
    return pydos_obj_new_int(tlow_mouse_poll());
}

PyDosObj far * PYDOS_API pydos_tui_mouse_show(int argc,
                                              PyDosObj far * far *argv)
{
    if (g_mouse_ok) {
        tlow_mouse_show(argc > 0 && obj_long(argv[0], 0L) != 0L);
    }
    return pydos_obj_new_none();
}

/* ---- time entry points -------------------------------------------- */

PyDosObj far * PYDOS_API pydos_tui_ticks_ms(int argc,
                                            PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_obj_new_int(tlow_ticks_ms());
}

PyDosObj far * PYDOS_API pydos_tui_sleep_ms(int argc,
                                            PyDosObj far * far *argv)
{
    long duration = argc > 0 ? obj_long(argv[0], 0L) : 0L;
    if (duration > 0) tlow_sleep_ms(duration);
    return pydos_obj_new_none();
}

/* ---- test hooks ---------------------------------------------------- */

unsigned short far * PYDOS_API pydos_tui_debug_vram(void)
{
    return tlow_debug_vram();
}

long PYDOS_API pydos_tui_debug_writes(void)
{
    long count = g_write_count;
    g_write_count = 0;
    return count;
}
