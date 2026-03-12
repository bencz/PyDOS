/*
 * pirdom.cpp - Dominance tree, dominance frontiers, and loop detection
 *
 * Uses Cooper-Harvey-Kennedy iterative dominance algorithm.
 * Simple, C++98-friendly, converges in 2-3 passes for typical
 * DOS-sized functions (10-100 blocks).
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirdom.h"
#include "pirutil.h"

#include <string.h>

/* ================================================================ */
/* DomInfo — Immediate Dominators                                    */
/* ================================================================ */

/* Compute reverse postorder numbering via DFS */
void DomInfo::compute_rpo(PIRFunction *func)
{
    int visited[PIRDOM_MAX_BLOCKS];
    int i;

    memset(visited, 0, sizeof(visited));
    num_blocks = func->blocks.size();

    if (num_blocks == 0) return;

    /* Iterative DFS for postorder */
    /* We use a two-pass approach: first pass pushes, second pops */
    int post_order[PIRDOM_MAX_BLOCKS];
    int post_idx = 0;

    /* Simple iterative postorder DFS */
    int dfs_stack[PIRDOM_MAX_BLOCKS * 2]; /* block_id, child_index pairs */
    int dfs_top = 0;

    int entry_id = func->entry_block->id;
    memset(visited, 0, sizeof(visited));

    dfs_stack[dfs_top++] = entry_id;
    dfs_stack[dfs_top++] = 0; /* child index */
    visited[entry_id] = 1;

    while (dfs_top > 0) {
        int child_idx = dfs_stack[dfs_top - 1];
        int blk_id = dfs_stack[dfs_top - 2];
        PIRBlock *blk = 0;

        /* Find the block */
        for (i = 0; i < func->blocks.size(); i++) {
            if (func->blocks[i]->id == blk_id) {
                blk = func->blocks[i];
                break;
            }
        }

        if (!blk || child_idx >= blk->succs.size()) {
            /* All children visited, emit postorder */
            post_order[post_idx++] = blk_id;
            dfs_top -= 2;
        } else {
            /* Advance to next child */
            dfs_stack[dfs_top - 1] = child_idx + 1;
            int succ_id = blk->succs[child_idx]->id;
            if (!visited[succ_id]) {
                visited[succ_id] = 1;
                dfs_stack[dfs_top++] = succ_id;
                dfs_stack[dfs_top++] = 0;
            }
        }
    }

    /* Reverse postorder = reverse of postorder */
    for (i = 0; i < post_idx; i++) {
        int blk_id = post_order[post_idx - 1 - i];
        rpo[blk_id] = i;
        rpo_order[i] = blk_id;
    }
}

int DomInfo::intersect(int b1, int b2)
{
    while (b1 != b2) {
        while (rpo[b1] > rpo[b2]) b1 = idom[b1];
        while (rpo[b2] > rpo[b1]) b2 = idom[b2];
    }
    return b1;
}

void DomInfo::compute(PIRFunction *func)
{
    int i, changed;

    num_blocks = func->blocks.size();
    if (num_blocks == 0) return;

    /* Initialize */
    for (i = 0; i < PIRDOM_MAX_BLOCKS; i++) {
        idom[i] = -1;
        dom_depth[i] = 0;
    }

    compute_rpo(func);

    int entry_id = func->entry_block->id;
    idom[entry_id] = entry_id;

    /* Iterate until convergence */
    changed = 1;
    while (changed) {
        changed = 0;
        for (i = 0; i < num_blocks; i++) {
            int b = rpo_order[i];
            if (b == entry_id) continue;

            PIRBlock *blk = 0;
            int j;
            for (j = 0; j < func->blocks.size(); j++) {
                if (func->blocks[j]->id == b) {
                    blk = func->blocks[j];
                    break;
                }
            }
            if (!blk) continue;

            int new_idom = -1;
            for (j = 0; j < blk->preds.size(); j++) {
                int p = blk->preds[j]->id;
                if (idom[p] == -1) continue; /* not yet processed */
                if (new_idom == -1) {
                    new_idom = p;
                } else {
                    new_idom = intersect(new_idom, p);
                }
            }

            if (new_idom != -1 && idom[b] != new_idom) {
                idom[b] = new_idom;
                changed = 1;
            }
        }
    }

    /* Compute dominance depth */
    for (i = 0; i < num_blocks; i++) {
        int b = rpo_order[i];
        if (b == entry_id) {
            dom_depth[b] = 0;
        } else if (idom[b] >= 0) {
            dom_depth[b] = dom_depth[idom[b]] + 1;
        }
    }
}

int DomInfo::dominates(int a, int b)
{
    if (a == b) return 1;
    while (b != idom[b] && b != -1) {
        b = idom[b];
        if (b == a) return 1;
    }
    return 0;
}

/* ================================================================ */
/* DomFrontier                                                       */
/* ================================================================ */

void DomFrontier::compute(PIRFunction *func, DomInfo *dom)
{
    int i, j;

    /* Clear frontier sets */
    for (i = 0; i < PIRDOM_MAX_BLOCKS; i++) {
        df[i].clear();
    }

    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        if (blk->preds.size() < 2) continue;

        for (j = 0; j < blk->preds.size(); j++) {
            int runner = blk->preds[j]->id;
            while (runner != dom->idom[blk->id] && runner != -1) {
                /* Add blk->id to DF(runner) if not already present */
                int k, found = 0;
                for (k = 0; k < df[runner].size(); k++) {
                    if (df[runner][k] == blk->id) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    df[runner].push_back(blk->id);
                }
                if (runner == dom->idom[runner]) break;
                runner = dom->idom[runner];
            }
        }
    }
}

/* ================================================================ */
/* LoopInfo — Natural loop detection                                 */
/* ================================================================ */

int LoopInfo::is_in_loop(int loop_idx, int block_id)
{
    int i;
    if (loop_idx < 0 || loop_idx >= num_loops) return 0;
    for (i = 0; i < loops[loop_idx].body.size(); i++) {
        if (loops[loop_idx].body[i] == block_id) return 1;
    }
    return 0;
}

void LoopInfo::find_loop_body(PIRFunction *func, int header, int back_edge_src)
{
    if (num_loops >= PIRDOM_MAX_LOOPS) return;

    NaturalLoop *loop = &loops[num_loops];
    loop->header_id = header;
    loop->preheader_id = -1;
    loop->body.clear();
    loop->exits.clear();

    /* BFS backwards from back_edge_src to header to find loop body */
    loop->body.push_back(header);
    if (back_edge_src != header) {
        loop->body.push_back(back_edge_src);

        /* Work list */
        int worklist[PIRDOM_MAX_BLOCKS];
        int wl_size = 0;
        worklist[wl_size++] = back_edge_src;

        while (wl_size > 0) {
            int b = worklist[--wl_size];
            PIRBlock *blk = pir_func_get_block(func, b);
            if (!blk) continue;

            int i;
            for (i = 0; i < blk->preds.size(); i++) {
                int pred_id = blk->preds[i]->id;
                /* Check if already in body */
                int j, found = 0;
                for (j = 0; j < loop->body.size(); j++) {
                    if (loop->body[j] == pred_id) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    loop->body.push_back(pred_id);
                    worklist[wl_size++] = pred_id;
                }
            }
        }
    }

    /* Find exit blocks: blocks in the loop with successors outside */
    int i;
    for (i = 0; i < loop->body.size(); i++) {
        PIRBlock *blk = pir_func_get_block(func, loop->body[i]);
        if (!blk) continue;

        int j;
        for (j = 0; j < blk->succs.size(); j++) {
            int succ_id = blk->succs[j]->id;
            int k, in_loop = 0;
            for (k = 0; k < loop->body.size(); k++) {
                if (loop->body[k] == succ_id) {
                    in_loop = 1;
                    break;
                }
            }
            if (!in_loop) {
                /* Add to exits if not already there */
                int already = 0;
                for (k = 0; k < loop->exits.size(); k++) {
                    if (loop->exits[k] == loop->body[i]) {
                        already = 1;
                        break;
                    }
                }
                if (!already) {
                    loop->exits.push_back(loop->body[i]);
                }
            }
        }
    }

    num_loops++;
}

void LoopInfo::compute(PIRFunction *func, DomInfo *dom)
{
    int i, j;
    num_loops = 0;

    /* Find back edges: edge B->H where H dominates B */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        for (j = 0; j < blk->succs.size(); j++) {
            int succ_id = blk->succs[j]->id;
            if (dom->dominates(succ_id, blk->id)) {
                /* Back edge found: blk->id -> succ_id */
                find_loop_body(func, succ_id, blk->id);
            }
        }
    }
}
