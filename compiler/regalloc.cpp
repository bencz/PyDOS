/*
 * regalloc.cpp - Linear-scan register allocator for PyDOS compiler
 *
 * Targets the 8086 general-purpose registers (AX, BX, CX, DX, SI, DI).
 * All Python values are far pointers (4 bytes), so each temp/local
 * occupies a 4-byte stack slot when spilled.
 *
 * Strategy:
 *  1. Number each IR instruction sequentially.
 *  2. For each virtual temp, find first def and last use to form a
 *     live range.
 *  3. Sort live ranges by start point.
 *  4. Walk ranges in order. Maintain an "active" set of ranges
 *     currently occupying registers. When a new range starts, expire
 *     any active ranges that ended before this point. If a free
 *     register exists, assign it; otherwise spill the range ending
 *     latest (either the new one or one in the active set).
 *
 * Heuristics:
 *  - Prefer SI/DI for long-lived temps (callee-saved on Watcom).
 *  - Try to keep AX/DX free near CALL sites (return value registers).
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL.
 */

#include "regalloc.h"
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* RegAllocation query methods                                      */
/* --------------------------------------------------------------- */

PhysReg RegAllocation::get_reg(int temp) const
{
    if (temp >= 0 && temp < num_ranges) {
        return ranges[temp].reg;
    }
    return REG_NONE;
}

int RegAllocation::get_spill_offset(int temp) const
{
    if (temp >= 0 && temp < num_ranges) {
        return ranges[temp].spill_offset;
    }
    return 0;
}

int RegAllocation::is_spilled(int temp) const
{
    if (temp >= 0 && temp < num_ranges) {
        return ranges[temp].reg == REG_NONE ? 1 : 0;
    }
    return 1;
}

/* --------------------------------------------------------------- */
/* RegisterAllocator constructor / destructor                       */
/* --------------------------------------------------------------- */

RegisterAllocator::RegisterAllocator()
{
    ranges = 0;
    num_ranges = 0;
    max_ranges = 0;
    num_active = 0;
    next_spill_offset = 0;
    num_locals = 0;

    int i;
    for (i = 0; i < REG_COUNT; i++) {
        active[i] = -1;
        reg_free[i] = 1;
    }
}

RegisterAllocator::~RegisterAllocator()
{
    if (ranges) {
        free(ranges);
        ranges = 0;
    }
}

/* --------------------------------------------------------------- */
/* allocate() - main entry point                                    */
/* --------------------------------------------------------------- */

RegAllocation *RegisterAllocator::allocate(IRFunc *func)
{
    int i;

    /* Reset state */
    if (ranges) {
        free(ranges);
        ranges = 0;
    }
    num_ranges = 0;
    max_ranges = 0;
    num_active = 0;
    next_spill_offset = 0;
    num_locals = func->num_locals;

    for (i = 0; i < REG_COUNT; i++) {
        active[i] = -1;
        reg_free[i] = 1;
    }

    /* Phase 1: build live ranges */
    compute_live_ranges(func);

    /* Phase 2: linear scan */
    linear_scan();

    /* Build result */
    RegAllocation *alloc = (RegAllocation *)malloc(sizeof(RegAllocation));
    if (!alloc) return 0;

    alloc->num_ranges = func->num_temps;

    /* Create a ranges array indexed by temp number for fast lookup */
    alloc->ranges = (LiveRange *)malloc(
        sizeof(LiveRange) * (func->num_temps > 0 ? func->num_temps : 1));
    if (!alloc->ranges) {
        free(alloc);
        return 0;
    }

    /* Initialize all to spilled with default offsets */
    for (i = 0; i < func->num_temps; i++) {
        alloc->ranges[i].temp = i;
        alloc->ranges[i].start = 0;
        alloc->ranges[i].end = 0;
        alloc->ranges[i].reg = REG_NONE;
        alloc->ranges[i].spill_offset = 0;
        alloc->ranges[i].is_param = 0;
        alloc->ranges[i].prefers_callee = 0;
    }

    /* Copy computed allocations */
    for (i = 0; i < num_ranges; i++) {
        int t = ranges[i].temp;
        if (t >= 0 && t < func->num_temps) {
            alloc->ranges[t] = ranges[i];
        }
    }

    /* Any temp not found in our ranges gets a spill slot.
     * Assign sequential spill offsets for unallocated temps. */
    int spill_slot = next_spill_offset;
    for (i = 0; i < func->num_temps; i++) {
        if (alloc->ranges[i].reg == REG_NONE &&
            alloc->ranges[i].spill_offset == 0) {
            /* Locals occupy slots [BP-4] through [BP - num_locals*4].
             * Spills go after that. */
            spill_slot += 4;
            alloc->ranges[i].spill_offset =
                -(num_locals * 4 + spill_slot);
        }
    }

    /* Calculate total frame size: locals + spills.
     * Each local is 4 bytes, plus all spill bytes. */
    int locals_bytes = num_locals * 4;
    int spills_bytes = spill_slot;
    alloc->stack_frame_size = locals_bytes + spills_bytes;

    /* Ensure frame size is even (8086 word alignment) */
    if (alloc->stack_frame_size & 1) {
        alloc->stack_frame_size++;
    }

    return alloc;
}

/* --------------------------------------------------------------- */
/* compute_live_ranges - walk instructions and find first/last use  */
/* --------------------------------------------------------------- */

void RegisterAllocator::compute_live_ranges(IRFunc *func)
{
    /* First pass: count instructions */
    int instr_count = 0;
    IRInstr *ip;
    for (ip = func->first; ip; ip = ip->next) {
        instr_count++;
    }
    if (instr_count == 0) return;

    /* Allocate temp tracking: for each virtual temp, track first and
     * last instruction indices. Initialize start to a very large
     * number and end to -1. */
    int nt = func->num_temps;
    if (nt <= 0) return;

    int *first_def = (int *)malloc(sizeof(int) * nt);
    int *last_use  = (int *)malloc(sizeof(int) * nt);
    if (!first_def || !last_use) {
        if (first_def) free(first_def);
        if (last_use)  free(last_use);
        return;
    }

    int i;
    for (i = 0; i < nt; i++) {
        first_def[i] = instr_count + 1; /* sentinel: not yet seen */
        last_use[i] = -1;
    }

    /* Walk instructions, record defs and uses */
    int idx = 0;
    for (ip = func->first; ip; ip = ip->next, idx++) {
        /* dest is a def */
        if (ip->dest >= 0 && ip->dest < nt) {
            if (idx < first_def[ip->dest]) {
                first_def[ip->dest] = idx;
            }
            if (idx > last_use[ip->dest]) {
                last_use[ip->dest] = idx;
            }
        }
        /* src1 is a use */
        if (ip->src1 >= 0 && ip->src1 < nt) {
            if (idx < first_def[ip->src1]) {
                first_def[ip->src1] = idx;
            }
            if (idx > last_use[ip->src1]) {
                last_use[ip->src1] = idx;
            }
        }
        /* src2 is a use */
        if (ip->src2 >= 0 && ip->src2 < nt) {
            if (idx < first_def[ip->src2]) {
                first_def[ip->src2] = idx;
            }
            if (idx > last_use[ip->src2]) {
                last_use[ip->src2] = idx;
            }
        }
        /* extra can reference temps for some ops (e.g. IR_STORE_SUBSCRIPT) */
        if (ip->op == IR_STORE_SUBSCRIPT) {
            int et = ip->extra;
            if (et >= 0 && et < nt) {
                if (idx < first_def[et]) first_def[et] = idx;
                if (idx > last_use[et])  last_use[et] = idx;
            }
        }
    }

    /* Build LiveRange array from temps that are actually used */
    max_ranges = nt;
    ranges = (LiveRange *)malloc(sizeof(LiveRange) * max_ranges);
    if (!ranges) {
        free(first_def);
        free(last_use);
        return;
    }
    num_ranges = 0;

    for (i = 0; i < nt; i++) {
        if (last_use[i] < 0) {
            /* Temp never used -- skip */
            continue;
        }
        LiveRange *lr = &ranges[num_ranges];
        lr->temp = i;
        lr->start = first_def[i];
        lr->end = last_use[i];
        lr->reg = REG_NONE;
        lr->spill_offset = 0;
        lr->is_param = (i < func->num_params) ? 1 : 0;

        /* Heuristic: if range spans more than 10 instructions,
         * prefer callee-saved registers (SI, DI) */
        lr->prefers_callee = ((lr->end - lr->start) > 10) ? 1 : 0;

        num_ranges++;
    }

    free(first_def);
    free(last_use);

    /* Sort ranges by start point (simple insertion sort -- fine for
     * the number of temps in typical functions) */
    int j;
    for (i = 1; i < num_ranges; i++) {
        LiveRange tmp = ranges[i];
        j = i - 1;
        while (j >= 0 && ranges[j].start > tmp.start) {
            ranges[j + 1] = ranges[j];
            j--;
        }
        ranges[j + 1] = tmp;
    }
}

/* --------------------------------------------------------------- */
/* linear_scan - classic linear-scan register allocation            */
/* --------------------------------------------------------------- */

void RegisterAllocator::linear_scan()
{
    int i;

    /* Initialize register availability */
    for (i = 0; i < REG_COUNT; i++) {
        reg_free[i] = 1;
    }
    num_active = 0;

    next_spill_offset = 0;

    /* Process ranges in order of start point */
    for (i = 0; i < num_ranges; i++) {
        expire_old(ranges[i].start);

        if (num_active == REG_COUNT) {
            /* All registers in use -- must spill something */
            spill(i);
        } else {
            /* Assign a free register */
            PhysReg chosen = REG_NONE;
            int r;

            if (ranges[i].prefers_callee) {
                /* Try SI, DI first for long-lived temps */
                if (reg_free[REG_SI]) {
                    chosen = REG_SI;
                } else if (reg_free[REG_DI]) {
                    chosen = REG_DI;
                }
            }

            if (chosen == REG_NONE) {
                /* Try BX, CX first (avoid AX/DX which are used for
                 * return values and special purposes) */
                if (reg_free[REG_BX]) chosen = REG_BX;
                else if (reg_free[REG_CX]) chosen = REG_CX;
                else if (reg_free[REG_SI]) chosen = REG_SI;
                else if (reg_free[REG_DI]) chosen = REG_DI;
                else if (reg_free[REG_AX]) chosen = REG_AX;
                else if (reg_free[REG_DX]) chosen = REG_DX;
            }

            if (chosen == REG_NONE) {
                /* Should not happen since num_active < REG_COUNT */
                spill(i);
            } else {
                ranges[i].reg = chosen;
                reg_free[chosen] = 0;
                add_to_active(i);
            }
        }
    }
}

/* --------------------------------------------------------------- */
/* expire_old - remove expired ranges from active set               */
/* --------------------------------------------------------------- */

void RegisterAllocator::expire_old(int current_start)
{
    /* Active set is sorted by end point. Remove any whose end
     * is before current_start. Since the set is small (max 6),
     * a linear scan is fine. */
    int i = 0;
    while (i < num_active) {
        int ri = active[i];
        if (ranges[ri].end < current_start) {
            /* This range has expired -- free its register */
            PhysReg r = ranges[ri].reg;
            if (r != REG_NONE && r >= 0 && r < REG_COUNT) {
                reg_free[r] = 1;
            }
            remove_from_active(i);
            /* Do not increment i -- the next element shifted down */
        } else {
            i++;
        }
    }
}

/* --------------------------------------------------------------- */
/* spill - evict the range that ends latest                         */
/* --------------------------------------------------------------- */

void RegisterAllocator::spill(int range_idx)
{
    /* Find the active range ending latest */
    int spill_target = range_idx;
    int latest_end = ranges[range_idx].end;
    int active_slot = -1;

    int i;
    for (i = 0; i < num_active; i++) {
        int ri = active[i];
        if (ranges[ri].end > latest_end) {
            latest_end = ranges[ri].end;
            spill_target = ri;
            active_slot = i;
        }
    }

    if (spill_target == range_idx) {
        /* The current range itself ends latest -- spill it */
        next_spill_offset += 4;
        ranges[range_idx].reg = REG_NONE;
        ranges[range_idx].spill_offset =
            -(num_locals * 4 + next_spill_offset);
    } else {
        /* Evict the active range that ends latest, give its register
         * to the current range */
        PhysReg stolen = ranges[spill_target].reg;

        /* Assign spill slot to the evicted range */
        next_spill_offset += 4;
        ranges[spill_target].reg = REG_NONE;
        ranges[spill_target].spill_offset =
            -(num_locals * 4 + next_spill_offset);

        /* Remove evicted range from active set */
        if (active_slot >= 0) {
            remove_from_active(active_slot);
        }

        /* Give the freed register to the current range */
        ranges[range_idx].reg = stolen;
        add_to_active(range_idx);
    }
}

/* --------------------------------------------------------------- */
/* add_to_active - insert into active set sorted by end point       */
/* --------------------------------------------------------------- */

void RegisterAllocator::add_to_active(int range_idx)
{
    if (num_active >= REG_COUNT) return;

    /* Find insertion point: maintain sorted by end point ascending */
    int i;
    int end = ranges[range_idx].end;
    for (i = num_active; i > 0; i--) {
        if (ranges[active[i - 1]].end <= end) {
            break;
        }
        active[i] = active[i - 1];
    }
    active[i] = range_idx;
    num_active++;
}

/* --------------------------------------------------------------- */
/* remove_from_active - remove entry at given position              */
/* --------------------------------------------------------------- */

void RegisterAllocator::remove_from_active(int active_slot)
{
    if (active_slot < 0 || active_slot >= num_active) return;
    int i;
    for (i = active_slot; i < num_active - 1; i++) {
        active[i] = active[i + 1];
    }
    num_active--;
}

/* --------------------------------------------------------------- */
/* Register name utilities                                          */
/* --------------------------------------------------------------- */

static const char *reg_names_16[REG_COUNT] = {
    "ax", "bx", "cx", "dx", "si", "di"
};

static const char *reg_names_32[REG_COUNT] = {
    "eax", "ebx", "ecx", "edx", "esi", "edi"
};

static const char *reg_names_8_lo[REG_COUNT] = {
    "al", "bl", "cl", "dl", "si", "di"   /* SI/DI have no 8-bit halves */
};

static const char *reg_names_8_hi[REG_COUNT] = {
    "ah", "bh", "ch", "dh", "si", "di"
};

const char *reg_name_16(PhysReg r)
{
    if (r >= 0 && r < REG_COUNT) {
        return reg_names_16[r];
    }
    return "??";
}

const char *reg_name_32(PhysReg r)
{
    if (r >= 0 && r < REG_COUNT) {
        return reg_names_32[r];
    }
    return "???";
}

const char *reg_name_8_lo(PhysReg r)
{
    if (r >= 0 && r < REG_COUNT) {
        return reg_names_8_lo[r];
    }
    return "??";
}

const char *reg_name_8_hi(PhysReg r)
{
    if (r >= 0 && r < REG_COUNT) {
        return reg_names_8_hi[r];
    }
    return "??";
}
