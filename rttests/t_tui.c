/*
 * t_tui.c - Unit tests for the pdos_tui engine.
 *
 * Group 1 exercises the pure packing helpers (no BIOS involved).
 * Group 2 drives the video engine through the debug hooks: on the host
 * build the low-level layer writes into a fake VRAM, so the present()
 * diff, clipping, fill and scroll are fully checked; on DOS the same
 * assertions run against real video memory (the suite scribbles on the
 * screen while running, which is expected).
 * Group 3 pins the deterministic host semantics that keep this suite
 * (and the golden tests) reproducible: no input, synthetic clock.
 */

#include "testfw.h"
#include "../runtime/pdos_tui.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_lst.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static PyDosObj far *tui_str(const char *data, unsigned int len)
{
    return pydos_obj_new_str((const char far *)data, len);
}

static PyDosObj far *tui_int(long value)
{
    return pydos_obj_new_int(value);
}

/* Builds the (glyphs, attrs, x, y) argv for pydos_tui_present with a
 * single row and invokes it. */
static void tui_present_row(const char *glyphs, const char *attrs,
                            unsigned int len, long x, long y)
{
    PyDosObj far *glyph_list = pydos_list_new(1);
    PyDosObj far *attr_list = pydos_list_new(1);
    PyDosObj far *gs = tui_str(glyphs, len);
    PyDosObj far *as = tui_str(attrs, len);
    PyDosObj far *px = tui_int(x);
    PyDosObj far *py = tui_int(y);
    PyDosObj far *argv[4];
    PyDosObj far *ret;

    pydos_list_append(glyph_list, gs);
    pydos_list_append(attr_list, as);
    argv[0] = glyph_list;
    argv[1] = attr_list;
    argv[2] = px;
    argv[3] = py;
    ret = pydos_tui_present(4, argv);
    PYDOS_DECREF(ret);
    PYDOS_DECREF(py);
    PYDOS_DECREF(px);
    PYDOS_DECREF(as);
    PYDOS_DECREF(gs);
    PYDOS_DECREF(attr_list);
    PYDOS_DECREF(glyph_list);
}

static void tui_call6(PyDosObj far * (PYDOS_API *fn)(int,
                                                     PyDosObj far * far *),
                      long a, long b, long c, long d, long e, long f)
{
    PyDosObj far *argv[6];
    PyDosObj far *ret;
    int i;
    argv[0] = tui_int(a);
    argv[1] = tui_int(b);
    argv[2] = tui_int(c);
    argv[3] = tui_int(d);
    argv[4] = tui_int(e);
    argv[5] = tui_int(f);
    ret = fn(6, argv);
    PYDOS_DECREF(ret);
    for (i = 0; i < 6; i++) PYDOS_DECREF(argv[i]);
}

static long tui_call1_long(PyDosObj far * (PYDOS_API *fn)(
                               int, PyDosObj far * far *), long a)
{
    PyDosObj far *argv[1];
    PyDosObj far *ret;
    long value;
    argv[0] = tui_int(a);
    ret = fn(1, argv);
    value = ret->v.int_val;
    PYDOS_DECREF(ret);
    PYDOS_DECREF(argv[0]);
    return value;
}

static long tui_call0_long(PyDosObj far * (PYDOS_API *fn)(
                               int, PyDosObj far * far *))
{
    PyDosObj far *ret = fn(0, (PyDosObj far * far *)0);
    long value = ret->v.int_val;
    PYDOS_DECREF(ret);
    return value;
}

/* ------------------------------------------------------------------ */
/* Group 1: packing helpers                                            */
/* ------------------------------------------------------------------ */

TEST(tui_pack_key_plain)
{
    /* scan 0x1E, ascii 'a', no modifiers */
    ASSERT_EQ(pydos_tui_pack_key(0x1E61U, 0x00U), 0x1E61L);
}

TEST(tui_pack_key_modifiers)
{
    ASSERT_EQ(pydos_tui_pack_key(0x1E61U, 0x04U),
              0x1E61L | (1L << 17));                     /* ctrl      */
    ASSERT_EQ(pydos_tui_pack_key(0x8500U, 0x08U),
              0x8500L | (1L << 18));                     /* alt + F11 */
    /* left shift and right shift set the same bit */
    ASSERT_EQ(pydos_tui_pack_key(0x1041U, 0x01U),
              pydos_tui_pack_key(0x1041U, 0x02U));
    ASSERT_TRUE(pydos_tui_pack_key(0x1041U, 0x01U) & (1L << 16));
}

TEST(tui_pack_key_e0_normalized)
{
    /* Grey cursor key from the enhanced BIOS: AL=0xE0 becomes 0 */
    ASSERT_EQ(pydos_tui_pack_key(0x48E0U, 0x00U), 0x4800L);
    /* A real 0xE0 character (scan 0) is preserved */
    ASSERT_EQ(pydos_tui_pack_key(0x00E0U, 0x00U), 0x00E0L);
}

TEST(tui_pack_key_no_sign_bit)
{
    ASSERT_TRUE(pydos_tui_pack_key(0xFFFFU, 0x0FU) >= 0L);
}

TEST(tui_pack_mouse_bits)
{
    long packed = pydos_tui_pack_mouse(79U, 24U, 1U, 1U, 0U, 0U, 1U);
    ASSERT_EQ(packed & 0xFFL, 79L);
    ASSERT_EQ((packed >> 8) & 0xFFL, 24L);
    ASSERT_EQ((packed >> 16) & 0x07L, 1L);      /* left held      */
    ASSERT_TRUE(packed & (1L << 19));           /* left pressed   */
    ASSERT_TRUE(!(packed & (1L << 20)));        /* not released   */
    ASSERT_TRUE(!(packed & (1L << 21)));
    ASSERT_TRUE(packed & (1L << 22));           /* right released */
}

TEST(tui_pack_mouse_no_bleed)
{
    /* Extreme coordinates must not leak into the button bits */
    long packed = pydos_tui_pack_mouse(255U, 255U, 0U, 0U, 0U, 0U, 0U);
    ASSERT_EQ((packed >> 16) & 0x7FL, 0L);
    ASSERT_EQ(packed & 0xFFL, 255L);
    ASSERT_EQ((packed >> 8) & 0xFFL, 255L);
}

/* ------------------------------------------------------------------ */
/* Group 2: video engine                                               */
/* ------------------------------------------------------------------ */

TEST(tui_probe_reports_sane_dims)
{
    long packed = tui_call0_long(pydos_tui_probe);
    long cols = packed & 0xFFL;
    long rows = (packed >> 8) & 0xFFL;
    ASSERT_TRUE(cols >= 40L && cols <= 132L);
    ASSERT_TRUE(rows >= 2L && rows <= 60L);
}

TEST(tui_present_writes_cells)
{
    unsigned short far *vram;
    tui_call0_long(pydos_tui_probe);
    pydos_tui_debug_writes();

    tui_present_row("AB", "\x07\x1F", 2, 0, 0);
    ASSERT_EQ(pydos_tui_debug_writes(), 2L);

    vram = pydos_tui_debug_vram();
    if (vram) {
        ASSERT_EQ(vram[0], 0x0741L);
        ASSERT_EQ(vram[1], 0x1F42L);
    }
}

TEST(tui_present_diff_skips_unchanged)
{
    tui_call0_long(pydos_tui_probe);
    tui_present_row("XY", "\x70\x70", 2, 4, 1);
    pydos_tui_debug_writes();

    /* Identical frame: nothing may be written */
    tui_present_row("XY", "\x70\x70", 2, 4, 1);
    ASSERT_EQ(pydos_tui_debug_writes(), 0L);

    /* One changed cell: exactly one write */
    tui_present_row("XZ", "\x70\x70", 2, 4, 1);
    ASSERT_EQ(pydos_tui_debug_writes(), 1L);
}

TEST(tui_present_clips)
{
    tui_call0_long(pydos_tui_probe);
    pydos_tui_debug_writes();

    /* Fully above / left of the screen */
    tui_present_row("AB", "\x07\x07", 2, 0, -1);
    tui_present_row("AB", "\x07\x07", 2, -2, 0);
    ASSERT_EQ(pydos_tui_debug_writes(), 0L);

    /* Partially left-clipped: only the visible half is written */
    tui_present_row("CD", "\x07\x07", 2, -1, 2);
    ASSERT_EQ(pydos_tui_debug_writes(), 1L);

    /* Far beyond the right edge: nothing */
    tui_present_row("EF", "\x07\x07", 2, 500, 2);
    ASSERT_EQ(pydos_tui_debug_writes(), 0L);
}

TEST(tui_present_rejects_bad_args)
{
    PyDosObj far *not_list = tui_int(5);
    PyDosObj far *argv[2];
    PyDosObj far *ret;

    argv[0] = not_list;
    argv[1] = not_list;
    ret = pydos_tui_present(2, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);
    PYDOS_DECREF(not_list);

    ret = pydos_tui_present(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);
}

TEST(tui_fill_rect)
{
    unsigned short far *vram;
    long cols;

    cols = tui_call0_long(pydos_tui_probe) & 0xFFL;
    pydos_tui_debug_writes();
    tui_call6(pydos_tui_fill, 2, 3, 4, 2, (long)'#', 0x1E);
    ASSERT_EQ(pydos_tui_debug_writes(), 8L);

    vram = pydos_tui_debug_vram();
    if (vram) {
        ASSERT_EQ(vram[3 * (unsigned int)cols + 2], 0x1E23L);
        ASSERT_EQ(vram[4 * (unsigned int)cols + 5], 0x1E23L);
    }
}

TEST(tui_scroll_moves_content)
{
    unsigned short far *vram;
    long cols;

    cols = tui_call0_long(pydos_tui_probe) & 0xFFL;

    /* Two stacked rows inside a 1-column region, then scroll up 1 */
    tui_call6(pydos_tui_fill, 10, 5, 1, 1, (long)'T', 0x07);
    tui_call6(pydos_tui_fill, 10, 6, 1, 1, (long)'U', 0x07);
    tui_call6(pydos_tui_scroll, 10, 5, 1, 2, 1, 0x07);

    vram = pydos_tui_debug_vram();
    if (vram) {
        ASSERT_EQ(vram[5 * (unsigned int)cols + 10], 0x0755L); /* 'U' */
        ASSERT_EQ(vram[6 * (unsigned int)cols + 10], 0x0720L); /* ' ' */
    }
}

TEST(tui_scroll_full_is_fill)
{
    unsigned short far *vram;
    long cols;

    cols = tui_call0_long(pydos_tui_probe) & 0xFFL;
    tui_call6(pydos_tui_fill, 20, 8, 2, 2, (long)'Q', 0x07);
    /* |lines| >= h degenerates into a blank fill */
    tui_call6(pydos_tui_scroll, 20, 8, 2, 2, 5, 0x07);

    vram = pydos_tui_debug_vram();
    if (vram) {
        ASSERT_EQ(vram[8 * (unsigned int)cols + 20], 0x0720L);
        ASSERT_EQ(vram[9 * (unsigned int)cols + 21], 0x0720L);
    }
}

TEST(tui_set_rows_roundtrip)
{
    long rows50 = tui_call1_long(pydos_tui_set_rows, 50L);
    long packed;

    /* On the host this always works; a real pre-VGA BIOS may stay at
     * 25, which set_rows reports honestly. */
    ASSERT_TRUE(rows50 == 50L || rows50 == 25L);
    packed = tui_call0_long(pydos_tui_probe);
    ASSERT_EQ((packed >> 8) & 0xFFL, rows50);

    ASSERT_EQ(tui_call1_long(pydos_tui_set_rows, 25L), 25L);
}

TEST(tui_set_rows_rejects_invalid)
{
    long before = (tui_call0_long(pydos_tui_probe) >> 8) & 0xFFL;
    ASSERT_EQ(tui_call1_long(pydos_tui_set_rows, 37L), before);
}

TEST(tui_save_restore_video)
{
    long state = tui_call0_long(pydos_tui_save_video);
    PyDosObj far *argv[1];
    PyDosObj far *ret;

    ASSERT_TRUE(state >= 0L);
    ASSERT_EQ((state >> 8) & 0xFFL,
              (tui_call0_long(pydos_tui_probe) >> 8) & 0xFFL);

    argv[0] = tui_int(state);
    ret = pydos_tui_restore_video(1, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);
    PYDOS_DECREF(argv[0]);

    ASSERT_EQ((tui_call0_long(pydos_tui_probe) >> 8) & 0xFFL,
              (state >> 8) & 0xFFL);
}

/* ------------------------------------------------------------------ */
/* Group 3: deterministic host semantics / timing                      */
/* ------------------------------------------------------------------ */

#if !defined(__WATCOMC__)

TEST(tui_host_has_no_input)
{
    ASSERT_EQ(tui_call0_long(pydos_tui_key_event), -1L);
    ASSERT_EQ(tui_call0_long(pydos_tui_shift_state), 0L);
    ASSERT_EQ(tui_call0_long(pydos_tui_mouse_init), 0L);
    ASSERT_EQ(tui_call0_long(pydos_tui_mouse_poll), -1L);
}

#endif

TEST(tui_ticks_monotonic)
{
    long first = tui_call0_long(pydos_tui_ticks_ms);
    long second = tui_call0_long(pydos_tui_ticks_ms);
    ASSERT_TRUE(first >= 0L);
    ASSERT_TRUE(second >= first);
}

TEST(tui_sleep_advances_clock)
{
    long start = tui_call0_long(pydos_tui_ticks_ms);
    PyDosObj far *argv[1];
    PyDosObj far *ret;
    long finish;

    argv[0] = tui_int(120L);
    ret = pydos_tui_sleep_ms(1, argv);
    PYDOS_DECREF(ret);
    PYDOS_DECREF(argv[0]);

    finish = tui_call0_long(pydos_tui_ticks_ms);
    ASSERT_TRUE(finish - start >= 120L);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_tui_tests(void)
{
    SUITE("pdos_tui");

    RUN(tui_pack_key_plain);
    RUN(tui_pack_key_modifiers);
    RUN(tui_pack_key_e0_normalized);
    RUN(tui_pack_key_no_sign_bit);
    RUN(tui_pack_mouse_bits);
    RUN(tui_pack_mouse_no_bleed);

    RUN(tui_probe_reports_sane_dims);
    RUN(tui_present_writes_cells);
    RUN(tui_present_diff_skips_unchanged);
    RUN(tui_present_clips);
    RUN(tui_present_rejects_bad_args);
    RUN(tui_fill_rect);
    RUN(tui_scroll_moves_content);
    RUN(tui_scroll_full_is_fill);
    RUN(tui_set_rows_roundtrip);
    RUN(tui_set_rows_rejects_invalid);
    RUN(tui_save_restore_video);

#if !defined(__WATCOMC__)
    RUN(tui_host_has_no_input);
#endif
    RUN(tui_ticks_monotonic);
    RUN(tui_sleep_advances_clock);
}
