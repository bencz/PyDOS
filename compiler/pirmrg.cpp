/*
 * pirmrg.cpp - PIR Stdlib Merge implementation
 *
 * Worklist algorithm:
 * 1. Scan all PIR_CALL in user functions and init_func
 * 2. For each callee, if Python-backed in StdlibRegistry, add to worklist
 * 3. Deep-copy the PIRFunction from the registry → add to PIRModule
 * 4. Scan the newly added function for more calls (transitive closure)
 * 5. Repeat until fixpoint
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirmrg.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================= */
/* String comparison helper                                            */
/* ================================================================= */

static int str_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

/* ================================================================= */
/* Deep copy a PIR function (makes fully independent clone)            */
/* ================================================================= */

static char *merge_str_dup(const char *s)
{
    int len;
    char *d;
    if (!s) return 0;
    len = (int)strlen(s);
    d = (char *)malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

static PIRFunction *deep_copy_func(PIRFunction *src)
{
    PIRFunction *dst;
    int bi, pi;
    PIRBlock **block_map = 0;
    int block_map_cap = 0;

    dst = pir_func_new(src->name);
    dst->next_value_id = src->next_value_id;
    dst->next_block_id = 0;
    dst->num_params = src->num_params;
    dst->num_locals = src->num_locals;
    dst->is_generator = src->is_generator;
    dst->is_coroutine = src->is_coroutine;

    /* Copy params */
    for (pi = 0; pi < src->params.size(); pi++) {
        dst->params.push_back(src->params[pi]);
    }

    /* Allocate block map for pointer resolution */
    block_map_cap = src->blocks.size() < 256 ? 256 : src->blocks.size() + 1;
    block_map = (PIRBlock **)malloc(block_map_cap * sizeof(PIRBlock *));
    if (!block_map) { pir_func_free(dst); return 0; }
    memset(block_map, 0, block_map_cap * sizeof(PIRBlock *));

    /* Create blocks and copy instructions */
    for (bi = 0; bi < src->blocks.size(); bi++) {
        PIRBlock *src_blk = src->blocks[bi];
        PIRBlock *dst_blk = pir_block_new(dst, src_blk->label);
        PIRInst *src_inst;

        dst_blk->id = src_blk->id;
        dst_blk->sealed = 1;
        dst_blk->filled = 1;

        if (src_blk->id < block_map_cap) {
            block_map[src_blk->id] = dst_blk;
        }

        for (src_inst = src_blk->first; src_inst; src_inst = src_inst->next) {
            PIRInst *di = pir_inst_new(src_inst->op);
            int oi;

            di->result = src_inst->result;
            di->num_operands = src_inst->num_operands;
            for (oi = 0; oi < src_inst->num_operands; oi++) {
                di->operands[oi] = src_inst->operands[oi];
            }
            di->int_val = src_inst->int_val;
            di->str_val = merge_str_dup(src_inst->str_val);
            di->line = src_inst->line;
            /* type_hint not copied (sema-specific, not available for merged funcs) */

            /* Store block IDs for later resolution */
            if (src_inst->target_block) {
                di->target_block = (PIRBlock *)(long)src_inst->target_block->id;
            }
            if (src_inst->false_block) {
                di->false_block = (PIRBlock *)(long)src_inst->false_block->id;
            }
            if (src_inst->handler_block) {
                di->handler_block = (PIRBlock *)(long)src_inst->handler_block->id;
            }

            /* Copy phi entries */
            if (src_inst->op == PIR_PHI && src_inst->extra.phi.count > 0) {
                int ei;
                di->extra.phi.count = src_inst->extra.phi.count;
                di->extra.phi.entries = (PIRPhiEntry *)malloc(
                    di->extra.phi.count * sizeof(PIRPhiEntry));
                if (di->extra.phi.entries) {
                    for (ei = 0; ei < di->extra.phi.count; ei++) {
                        di->extra.phi.entries[ei].value = src_inst->extra.phi.entries[ei].value;
                        di->extra.phi.entries[ei].block = src_inst->extra.phi.entries[ei].block
                            ? (PIRBlock *)(long)src_inst->extra.phi.entries[ei].block->id
                            : 0;
                    }
                }
            }

            pir_block_append(dst_blk, di);
        }
    }

    /* Set entry block */
    if (dst->blocks.size() > 0) {
        dst->entry_block = dst->blocks[0];
    }
    dst->next_block_id = src->next_block_id;

    /* Resolve block pointers */
    for (bi = 0; bi < dst->blocks.size(); bi++) {
        PIRBlock *blk = dst->blocks[bi];
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (inst->target_block) {
                int tid = (int)(long)inst->target_block;
                inst->target_block = (tid >= 0 && tid < block_map_cap)
                                     ? block_map[tid] : 0;
            }
            if (inst->false_block) {
                int fid = (int)(long)inst->false_block;
                inst->false_block = (fid >= 0 && fid < block_map_cap)
                                    ? block_map[fid] : 0;
            }
            if (inst->handler_block) {
                int hid = (int)(long)inst->handler_block;
                inst->handler_block = (hid >= 0 && hid < block_map_cap)
                                      ? block_map[hid] : 0;
            }
            if (inst->op == PIR_PHI && inst->extra.phi.entries) {
                int ei;
                for (ei = 0; ei < inst->extra.phi.count; ei++) {
                    int bid = (int)(long)inst->extra.phi.entries[ei].block;
                    inst->extra.phi.entries[ei].block =
                        (bid >= 0 && bid < block_map_cap) ? block_map[bid] : 0;
                }
            }
        }
    }

    /* Rebuild CFG edges */
    for (bi = 0; bi < dst->blocks.size(); bi++) {
        PIRBlock *blk = dst->blocks[bi];
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (inst->target_block)  pir_block_add_edge(blk, inst->target_block);
            if (inst->false_block)   pir_block_add_edge(blk, inst->false_block);
            if (inst->handler_block) pir_block_add_edge(blk, inst->handler_block);
        }
    }

    free(block_map);
    return dst;
}

/* ================================================================= */
/* Check if a function name already exists in the module               */
/* ================================================================= */

static int func_exists(PIRModule *mod, const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < mod->functions.size(); i++) {
        if (mod->functions[i]->name && str_eq(mod->functions[i]->name, name)) {
            return 1;
        }
    }
    return 0;
}

/* ================================================================= */
/* Scan a function for PIR_CALL targets                                */
/* ================================================================= */

static int should_merge(PIRInst *inst, StdlibRegistry *reg)
{
    if (!inst->str_val) return 0;
    if (inst->op == PIR_CALL) {
        /* Check registered PIR-backed builtins first, then check
         * if the function exists in the PIR section (for method PIR
         * functions like list_count that aren't in BuiltinFuncEntry) */
        if (reg->is_pir_backed(inst->str_val)) return 1;
        if (reg->get_pir_func(inst->str_val)) return 1;
        return 0;
    }
    /* Generator/coroutine/function references: check if the referenced
     * function exists in the PIR section (e.g., _genresume_enumerate) */
    if (inst->op == PIR_MAKE_GENERATOR || inst->op == PIR_MAKE_COROUTINE ||
        inst->op == PIR_MAKE_FUNCTION) {
        return reg->get_pir_func(inst->str_val) != 0;
    }
    return 0;
}

static void scan_calls(PIRFunction *func, const char **worklist,
                        int *wl_count, int wl_cap,
                        PIRModule *mod, StdlibRegistry *reg)
{
    int bi;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            if (should_merge(inst, reg)) {
                const char *callee = inst->str_val;
                /* Check if already in module */
                if (func_exists(mod, callee)) continue;
                /* Check if already in worklist */
                {
                    int wi, found = 0;
                    for (wi = 0; wi < *wl_count; wi++) {
                        if (str_eq(worklist[wi], callee)) { found = 1; break; }
                    }
                    if (found) continue;
                }
                if (*wl_count < wl_cap) {
                    worklist[(*wl_count)++] = callee;
                }
            }
        }
    }
}

/* ================================================================= */
/* Public: merge stdlib PIR into module                                 */
/* ================================================================= */

int pir_merge_stdlib(PIRModule *pir_mod, StdlibRegistry *reg)
{
    const char *worklist[64];
    int wl_count = 0;
    int merged = 0;
    int fi;

    if (!pir_mod || !reg || !reg->is_loaded()) return 0;
    if (reg->get_num_pir_funcs() == 0) return 0;

    /* Initial scan: all existing functions */
    for (fi = 0; fi < pir_mod->functions.size(); fi++) {
        scan_calls(pir_mod->functions[fi], worklist, &wl_count, 64, pir_mod, reg);
    }

    /* Process worklist until fixpoint */
    while (wl_count > 0) {
        const char *name = worklist[--wl_count];
        PIRFunction *src, *copy;

        if (func_exists(pir_mod, name)) continue;

        src = reg->get_pir_func(name);
        if (!src) continue;

        copy = deep_copy_func(src);
        if (!copy) continue;

        pir_mod->functions.push_back(copy);
        merged++;

        /* Scan the newly added function for more calls */
        scan_calls(copy, worklist, &wl_count, 64, pir_mod, reg);
    }

    return merged;
}
