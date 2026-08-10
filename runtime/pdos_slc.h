/*
 * pdos_slc.h - Shared primitive slice-index normalization.
 *
 * The compiler uses PYDOS_SLICE_MISSING for an omitted bound.  Keeping the
 * normalization here avoids subtly different Python slicing rules in every
 * primitive sequence implementation.
 */

#ifndef PDOS_SLC_H
#define PDOS_SLC_H

#define PYDOS_SLICE_MISSING 0x7FFFFFFFL

static int pydos_slice_normalize(long length, long *start, long *stop,
                                 long step)
{
    long value;

    if (step == 0L) return 0;
    if (length < 0L) length = 0L;

    if (step > 0L) {
        value = *start;
        if (value == PYDOS_SLICE_MISSING) {
            value = 0L;
        } else {
            if (value < 0L) value += length;
            if (value < 0L) value = 0L;
            if (value > length) value = length;
        }
        *start = value;

        value = *stop;
        if (value == PYDOS_SLICE_MISSING) {
            value = length;
        } else {
            if (value < 0L) value += length;
            if (value < 0L) value = 0L;
            if (value > length) value = length;
        }
        *stop = value;
    } else {
        value = *start;
        if (value == PYDOS_SLICE_MISSING) {
            value = length - 1L;
        } else {
            if (value < 0L) value += length;
            if (value < 0L) value = -1L;
            if (value >= length) value = length - 1L;
        }
        *start = value;

        value = *stop;
        if (value == PYDOS_SLICE_MISSING) {
            value = -1L;
        } else {
            if (value < 0L) value += length;
            if (value < 0L) value = -1L;
            if (value >= length) value = length - 1L;
        }
        *stop = value;
    }

    return 1;
}

#endif /* PDOS_SLC_H */
