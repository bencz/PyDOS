/*
 * regalloc.h - Register allocator for PyDOS Python-to-8086 compiler
 *
 * Linear-scan register allocation targeting the 8086 general-purpose
 * register set (AX, BX, CX, DX, SI, DI). Virtual temporaries from
 * the IR are mapped to physical registers where possible, and spilled
 * to the stack frame otherwise.
 *
 * All Python values are far pointers (4 bytes: segment:offset), so
 * each local and spill slot occupies 4 bytes on the stack.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL - arrays, manual memory only.
 */

#ifndef REGALLOC_H
#define REGALLOC_H

#include "ir.h"

/* --------------------------------------------------------------- */
/* Physical 8086 registers                                          */
/* --------------------------------------------------------------- */
enum PhysReg {
    REG_AX = 0,
    REG_BX = 1,
    REG_CX = 2,
    REG_DX = 3,
    REG_SI = 4,
    REG_DI = 5,
    REG_NONE = -1,      /* spilled to stack frame */
    REG_COUNT = 6
};

/* --------------------------------------------------------------- */
/* Live range for a single virtual temp                             */
/* --------------------------------------------------------------- */
struct LiveRange {
    int temp;            /* virtual temp number */
    int start;           /* first instruction index where temp is live */
    int end;             /* last instruction index where temp is live */
    PhysReg reg;         /* allocated physical register, or REG_NONE */
    int spill_offset;    /* stack offset relative to BP if spilled */
    int is_param;        /* nonzero if this represents a function parameter */
    int prefers_callee;  /* hint: prefer SI/DI for long-lived temps */
};

/* --------------------------------------------------------------- */
/* Result of register allocation for one function                   */
/* --------------------------------------------------------------- */
struct RegAllocation {
    LiveRange *ranges;           /* array, indexed by temp number */
    int num_ranges;              /* number of entries (== num_temps) */
    int stack_frame_size;        /* total bytes for locals + spills */

    /* Query: which register holds virtual temp N?
     * Returns REG_NONE if temp is spilled to stack. */
    PhysReg get_reg(int temp) const;

    /* Query: stack offset for a spilled temp (relative to BP) */
    int get_spill_offset(int temp) const;

    /* Query: is this temp spilled to the stack? */
    int is_spilled(int temp) const;
};

/* --------------------------------------------------------------- */
/* Register allocator class                                         */
/* --------------------------------------------------------------- */
class RegisterAllocator {
public:
    RegisterAllocator();
    ~RegisterAllocator();

    /* Run register allocation for a function.
     * Returns a heap-allocated RegAllocation (caller owns it). */
    RegAllocation *allocate(IRFunc *func);

private:
    /* Phase 1: compute live ranges by walking instructions */
    void compute_live_ranges(IRFunc *func);

    /* Phase 2: classic linear-scan allocation */
    void linear_scan();

    /* Spill the range ending latest among active + current */
    void spill(int range_idx);

    /* Remove from active set any ranges that have expired by pos */
    void expire_old(int current_start);

    /* Insert into active set, maintaining sorted-by-end order */
    void add_to_active(int range_idx);

    /* Remove from active set by index into the active array */
    void remove_from_active(int active_slot);

    /* Internal storage */
    LiveRange *ranges;
    int num_ranges;
    int max_ranges;

    /* Active set: indices into ranges[], sorted by end point */
    int active[REG_COUNT];
    int num_active;

    /* Register availability: 1 = free, 0 = in use */
    int reg_free[REG_COUNT];

    /* Running spill offset counter */
    int next_spill_offset;

    /* Number of local variable slots in the current function */
    int num_locals;
};

/* --------------------------------------------------------------- */
/* Utility: register name strings                                   */
/* --------------------------------------------------------------- */

/* 16-bit register name: "ax", "bx", "cx", "dx", "si", "di" */
const char *reg_name_16(PhysReg r);

/* 32-bit register name: "eax", "ebx", "ecx", "edx", "esi", "edi" */
const char *reg_name_32(PhysReg r);

/* Low 8-bit register name: "al", "bl", "cl", "dl" (SI/DI have none) */
const char *reg_name_8_lo(PhysReg r);

/* High 8-bit register name: "ah", "bh", "ch", "dh" (SI/DI have none) */
const char *reg_name_8_hi(PhysReg r);

#endif /* REGALLOC_H */
