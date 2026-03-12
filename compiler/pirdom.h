/*
 * pirdom.h - Dominance tree and dominance frontiers for PIR
 *
 * Computes immediate dominators, dominance depth, and dominance
 * frontiers using the Cooper-Harvey-Kennedy iterative algorithm.
 * Also detects natural loops for LICM.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRDOM_H
#define PIRDOM_H

#include "pir.h"

#define PIRDOM_MAX_BLOCKS 1024
#define PIRDOM_MAX_LOOPS  64

/* --------------------------------------------------------------- */
/* Dominance info                                                    */
/* --------------------------------------------------------------- */
struct DomInfo {
    int idom[PIRDOM_MAX_BLOCKS];      /* immediate dominator block id */
    int dom_depth[PIRDOM_MAX_BLOCKS]; /* depth in dominator tree */
    int rpo[PIRDOM_MAX_BLOCKS];       /* reverse postorder numbering */
    int rpo_order[PIRDOM_MAX_BLOCKS]; /* blocks in RPO order (rpo_order[0] = entry) */
    int num_blocks;

    /* Does block a dominate block b? */
    int dominates(int a, int b);

    /* Compute dominators for a function */
    void compute(PIRFunction *func);

private:
    void compute_rpo(PIRFunction *func);
    int intersect(int b1, int b2);
};

/* --------------------------------------------------------------- */
/* Dominance frontier                                                */
/* --------------------------------------------------------------- */
struct DomFrontier {
    PdVector<int> df[PIRDOM_MAX_BLOCKS]; /* dominance frontier sets */

    void compute(PIRFunction *func, DomInfo *dom);
};

/* --------------------------------------------------------------- */
/* Natural loop info                                                 */
/* --------------------------------------------------------------- */
struct NaturalLoop {
    int header_id;                    /* loop header block id */
    int preheader_id;                 /* preheader block id (-1 if none) */
    PdVector<int> body;               /* block ids in loop body (includes header) */
    PdVector<int> exits;              /* block ids of loop exits */
};

struct LoopInfo {
    NaturalLoop loops[PIRDOM_MAX_LOOPS];
    int num_loops;

    void compute(PIRFunction *func, DomInfo *dom);

    /* Is block_id inside the given loop? */
    int is_in_loop(int loop_idx, int block_id);

private:
    void find_loop_body(PIRFunction *func, int header, int back_edge_src);
};

#endif /* PIRDOM_H */
