/*
 * pirsrlz.cpp - PIR Serialization/Deserialization implementation
 *
 * Binary format for storing PIRFunction in stdlib.idx:
 *
 * Function:
 *   u16 name_str_idx, u16 num_params, u16 num_blocks
 *   u16 next_value_id, u8 is_generator, u8 is_coroutine, u16 num_locals
 *   Param[num_params]: u16 value_id, u8 type_kind
 *   Block[num_blocks]:
 *     u16 block_id, u16 label_str_idx, u16 num_insts
 *     Inst[num_insts]: (see SRLZ_HAS_* flags below)
 *
 * CFG edges are NOT serialized; they are rebuilt from instruction
 * target_block/false_block/handler_block pointers during deserialization.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirsrlz.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================= */
/* Binary I/O helpers                                                  */
/* ================================================================= */

static void write_u8(FILE *f, unsigned char val)
{
    fwrite(&val, 1, 1, f);
}

static void write_u16(FILE *f, unsigned short val)
{
    fwrite(&val, 2, 1, f);
}

static void write_i32(FILE *f, long val)
{
    int v = (int)val;
    fwrite(&v, 4, 1, f);
}

static int read_u8_val(FILE *f, unsigned char *out)
{
    return (int)fread(out, 1, 1, f);
}

static int read_u16_val(FILE *f, unsigned short *out)
{
    return (int)fread(out, 2, 1, f);
}

static int read_i32_val(FILE *f, long *out)
{
    int v = 0;
    int r = (int)fread(&v, 4, 1, f);
    *out = (long)v;
    return r;
}

/* ================================================================= */
/* Local string dup                                                    */
/* ================================================================= */

static char *srlz_str_dup(const char *s)
{
    int len;
    char *d;
    if (!s) return 0;
    len = (int)strlen(s);
    d = (char *)malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* ================================================================= */
/* String table                                                        */
/* ================================================================= */

void pir_strtab_init(PirStringTable *tab)
{
    tab->strings = 0;
    tab->count = 0;
    tab->capacity = 0;
}

void pir_strtab_destroy(PirStringTable *tab)
{
    int i;
    if (tab->strings) {
        for (i = 0; i < tab->count; i++) {
            free(tab->strings[i]);
        }
        free(tab->strings);
    }
    tab->strings = 0;
    tab->count = 0;
    tab->capacity = 0;
}

static void strtab_grow(PirStringTable *tab)
{
    int new_cap = tab->capacity ? tab->capacity * 2 : 64;
    char **new_arr = (char **)malloc(new_cap * sizeof(char *));
    if (!new_arr) return;
    if (tab->strings) {
        memcpy(new_arr, tab->strings, tab->count * sizeof(char *));
        free(tab->strings);
    }
    tab->strings = new_arr;
    tab->capacity = new_cap;
}

int pir_strtab_add(PirStringTable *tab, const char *s)
{
    int idx;
    if (!s) return -1;

    idx = pir_strtab_find(tab, s);
    if (idx >= 0) return idx;

    if (tab->count >= tab->capacity) {
        strtab_grow(tab);
    }
    idx = tab->count;
    tab->strings[idx] = srlz_str_dup(s);
    tab->count++;
    return idx;
}

int pir_strtab_find(PirStringTable *tab, const char *s)
{
    int i;
    if (!s || !tab->strings) return -1;
    for (i = 0; i < tab->count; i++) {
        if (tab->strings[i] && strcmp(tab->strings[i], s) == 0) {
            return i;
        }
    }
    return -1;
}

const char *pir_strtab_get(PirStringTable *tab, int idx)
{
    if (idx < 0 || idx >= tab->count) return 0;
    return tab->strings[idx];
}

void pir_strtab_write(PirStringTable *tab, FILE *f)
{
    int i;
    write_u16(f, (unsigned short)tab->count);
    for (i = 0; i < tab->count; i++) {
        const char *s = tab->strings[i];
        unsigned short len = s ? (unsigned short)strlen(s) : 0;
        write_u16(f, len);
        if (len > 0) {
            fwrite(s, 1, len, f);
        }
    }
}

int pir_strtab_read(PirStringTable *tab, FILE *f)
{
    unsigned short count, i;
    if (!read_u16_val(f, &count)) return 0;

    for (i = 0; i < count; i++) {
        unsigned short len;
        char *s;
        if (!read_u16_val(f, &len)) return 0;
        s = (char *)malloc(len + 1);
        if (!s) return 0;
        if (len > 0) {
            if (fread(s, 1, len, f) != len) {
                free(s);
                return 0;
            }
        }
        s[len] = '\0';
        if (tab->count >= tab->capacity) {
            strtab_grow(tab);
        }
        tab->strings[tab->count++] = s;
    }
    return 1;
}

/* ================================================================= */
/* Collect strings from a PIR function                                 */
/* ================================================================= */

void pir_collect_strings(PIRFunction *func, PirStringTable *tab)
{
    int bi;

    if (func->name) pir_strtab_add(tab, func->name);

    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;

        if (block->label) pir_strtab_add(tab, block->label);

        for (inst = block->first; inst; inst = inst->next) {
            if (inst->str_val) pir_strtab_add(tab, inst->str_val);
        }
    }
}

/* ================================================================= */
/* Instruction flags                                                   */
/* ================================================================= */

#define SRLZ_HAS_INT_VAL    0x01
#define SRLZ_HAS_STR_VAL    0x02
#define SRLZ_HAS_TARGET     0x04
#define SRLZ_HAS_FALSE      0x08
#define SRLZ_HAS_HANDLER    0x10
#define SRLZ_HAS_PHI        0x20

#define SRLZ_NO_ID 0xFFFF

/* ================================================================= */
/* Serialize PIR function                                              */
/* ================================================================= */

void pir_serialize_func(PIRFunction *func, FILE *f, PirStringTable *tab)
{
    int bi;
    int name_idx;

    /* Function header */
    name_idx = func->name ? pir_strtab_find(tab, func->name) : -1;
    write_u16(f, (unsigned short)(name_idx >= 0 ? name_idx : SRLZ_NO_ID));
    write_u16(f, (unsigned short)func->num_params);
    write_u16(f, (unsigned short)func->blocks.size());
    write_u16(f, (unsigned short)func->next_value_id);
    write_u8(f, (unsigned char)func->is_generator);
    write_u8(f, (unsigned char)func->is_coroutine);
    write_u16(f, (unsigned short)func->num_locals);

    /* Parameters */
    {
        int pi;
        for (pi = 0; pi < func->params.size(); pi++) {
            write_u16(f, (unsigned short)func->params[pi].id);
            write_u8(f, (unsigned char)func->params[pi].type);
        }
    }

    /* Blocks */
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        int label_idx;

        label_idx = block->label ? pir_strtab_find(tab, block->label) : -1;
        write_u16(f, (unsigned short)block->id);
        write_u16(f, (unsigned short)(label_idx >= 0 ? label_idx : SRLZ_NO_ID));
        write_u16(f, (unsigned short)block->inst_count);

        /* Instructions */
        for (inst = block->first; inst; inst = inst->next) {
            unsigned char flags = 0;
            int oi;

            write_u16(f, (unsigned short)inst->op);
            write_u16(f, pir_value_valid(inst->result)
                         ? (unsigned short)inst->result.id
                         : (unsigned short)SRLZ_NO_ID);
            write_u8(f, (unsigned char)inst->result.type);
            write_u8(f, (unsigned char)inst->num_operands);

            for (oi = 0; oi < inst->num_operands; oi++) {
                write_u16(f, pir_value_valid(inst->operands[oi])
                             ? (unsigned short)inst->operands[oi].id
                             : (unsigned short)SRLZ_NO_ID);
                write_u8(f, (unsigned char)inst->operands[oi].type);
            }

            /* Build flags */
            if (inst->int_val != 0) flags |= SRLZ_HAS_INT_VAL;
            if (inst->str_val)      flags |= SRLZ_HAS_STR_VAL;
            if (inst->target_block) flags |= SRLZ_HAS_TARGET;
            if (inst->false_block)  flags |= SRLZ_HAS_FALSE;
            if (inst->handler_block) flags |= SRLZ_HAS_HANDLER;
            if (inst->op == PIR_PHI && inst->extra.phi.count > 0)
                flags |= SRLZ_HAS_PHI;

            write_u8(f, flags);

            if (flags & SRLZ_HAS_INT_VAL) {
                write_i32(f, inst->int_val);
            }
            if (flags & SRLZ_HAS_STR_VAL) {
                int sidx = pir_strtab_find(tab, inst->str_val);
                write_u16(f, (unsigned short)(sidx >= 0 ? sidx : SRLZ_NO_ID));
            }
            if (flags & SRLZ_HAS_TARGET) {
                write_u16(f, (unsigned short)inst->target_block->id);
            }
            if (flags & SRLZ_HAS_FALSE) {
                write_u16(f, (unsigned short)inst->false_block->id);
            }
            if (flags & SRLZ_HAS_HANDLER) {
                write_u16(f, (unsigned short)inst->handler_block->id);
            }
            if (flags & SRLZ_HAS_PHI) {
                int ei;
                write_u16(f, (unsigned short)inst->extra.phi.count);
                for (ei = 0; ei < inst->extra.phi.count; ei++) {
                    PIRPhiEntry *pe = &inst->extra.phi.entries[ei];
                    write_u16(f, (unsigned short)pe->value.id);
                    write_u8(f, (unsigned char)pe->value.type);
                    write_u16(f, (unsigned short)(pe->block ? pe->block->id : SRLZ_NO_ID));
                }
            }
        }
    }
}

/* ================================================================= */
/* Deserialize PIR function                                            */
/* ================================================================= */

/* Macro for error cleanup during deserialization */
#define DESER_FAIL() do { if (block_map) free(block_map); \
    pir_func_free(func); return 0; } while (0)

PIRFunction *pir_deserialize_func(FILE *f, PirStringTable *tab)
{
    PIRFunction *func;
    unsigned short name_idx, num_params, num_blocks, next_val_id, num_locals;
    unsigned char is_gen, is_cor;
    int bi;

    PIRBlock **block_map = 0;
    int block_map_cap = 0;

    /* Read function header */
    if (!read_u16_val(f, &name_idx)) return 0;
    if (!read_u16_val(f, &num_params)) return 0;
    if (!read_u16_val(f, &num_blocks)) return 0;
    if (!read_u16_val(f, &next_val_id)) return 0;
    if (!read_u8_val(f, &is_gen)) return 0;
    if (!read_u8_val(f, &is_cor)) return 0;
    if (!read_u16_val(f, &num_locals)) return 0;

    {
        const char *name_str = 0;
        if (name_idx != SRLZ_NO_ID) {
            name_str = pir_strtab_get(tab, name_idx);
        }
        func = pir_func_new(name_str ? name_str : "?deser");
    }
    func->next_value_id = next_val_id;
    func->num_params = num_params;
    func->num_locals = num_locals;
    func->is_generator = is_gen;
    func->is_coroutine = is_cor;
    /* Reset next_block_id — pir_block_new will auto-assign,
     * but we override each block's id after creation. */
    func->next_block_id = 0;

    /* Read parameters */
    {
        int pi;
        for (pi = 0; pi < (int)num_params; pi++) {
            unsigned short val_id;
            unsigned char type_kind;
            PIRValue pv;
            if (!read_u16_val(f, &val_id)) DESER_FAIL();
            if (!read_u8_val(f, &type_kind)) DESER_FAIL();
            pv.id = val_id;
            pv.type = (PIRTypeKind)type_kind;
            func->params.push_back(pv);
        }
    }

    /* Allocate block map for resolving block IDs to pointers */
    block_map_cap = (int)num_blocks < 256 ? 256 : (int)num_blocks + 1;
    block_map = (PIRBlock **)malloc(block_map_cap * sizeof(PIRBlock *));
    if (!block_map) { pir_func_free(func); return 0; }
    memset(block_map, 0, block_map_cap * sizeof(PIRBlock *));

    /* Create all blocks and read their instructions */
    for (bi = 0; bi < (int)num_blocks; bi++) {
        unsigned short blk_id, label_idx, num_insts;
        PIRBlock *block;
        const char *label_str = 0;
        int ii;

        if (!read_u16_val(f, &blk_id)) DESER_FAIL();
        if (!read_u16_val(f, &label_idx)) DESER_FAIL();
        if (!read_u16_val(f, &num_insts)) DESER_FAIL();

        if (label_idx != SRLZ_NO_ID) {
            label_str = pir_strtab_get(tab, label_idx);
        }

        block = pir_block_new(func, label_str);
        block->id = blk_id;
        block->sealed = 1;
        block->filled = 1;

        if ((int)blk_id < block_map_cap) {
            block_map[blk_id] = block;
        }

        /* Read instructions */
        for (ii = 0; ii < (int)num_insts; ii++) {
            unsigned short opcode_val, result_id;
            unsigned char result_type, num_ops, flags;
            PIRInst *inst;
            int oi;

            if (!read_u16_val(f, &opcode_val)) DESER_FAIL();
            if (!read_u16_val(f, &result_id)) DESER_FAIL();
            if (!read_u8_val(f, &result_type)) DESER_FAIL();
            if (!read_u8_val(f, &num_ops)) DESER_FAIL();

            inst = pir_inst_new((PIROp)opcode_val);
            if (result_id != SRLZ_NO_ID) {
                inst->result.id = result_id;
                inst->result.type = (PIRTypeKind)result_type;
            }
            inst->num_operands = num_ops;

            for (oi = 0; oi < (int)num_ops && oi < 3; oi++) {
                unsigned short op_id;
                unsigned char op_type;
                if (!read_u16_val(f, &op_id)) DESER_FAIL();
                if (!read_u8_val(f, &op_type)) DESER_FAIL();
                if (op_id != SRLZ_NO_ID) {
                    inst->operands[oi].id = op_id;
                    inst->operands[oi].type = (PIRTypeKind)op_type;
                }
            }

            if (!read_u8_val(f, &flags)) DESER_FAIL();

            if (flags & SRLZ_HAS_INT_VAL) {
                if (!read_i32_val(f, &inst->int_val)) DESER_FAIL();
            }
            if (flags & SRLZ_HAS_STR_VAL) {
                unsigned short sidx;
                if (!read_u16_val(f, &sidx)) DESER_FAIL();
                if (sidx != SRLZ_NO_ID) {
                    const char *s = pir_strtab_get(tab, sidx);
                    inst->str_val = srlz_str_dup(s);
                }
            }
            if (flags & SRLZ_HAS_TARGET) {
                unsigned short tid;
                if (!read_u16_val(f, &tid)) DESER_FAIL();
                /* Store block ID temporarily as pointer; resolve later */
                inst->target_block = (PIRBlock *)(long)(int)tid;
            }
            if (flags & SRLZ_HAS_FALSE) {
                unsigned short fid;
                if (!read_u16_val(f, &fid)) DESER_FAIL();
                inst->false_block = (PIRBlock *)(long)(int)fid;
            }
            if (flags & SRLZ_HAS_HANDLER) {
                unsigned short hid;
                if (!read_u16_val(f, &hid)) DESER_FAIL();
                inst->handler_block = (PIRBlock *)(long)(int)hid;
            }
            if (flags & SRLZ_HAS_PHI) {
                unsigned short phi_count;
                int ei;
                if (!read_u16_val(f, &phi_count)) DESER_FAIL();
                inst->extra.phi.count = phi_count;
                if (phi_count > 0) {
                    inst->extra.phi.entries = (PIRPhiEntry *)malloc(
                        phi_count * sizeof(PIRPhiEntry));
                    if (!inst->extra.phi.entries) DESER_FAIL();
                    for (ei = 0; ei < (int)phi_count; ei++) {
                        unsigned short vid, blkid;
                        unsigned char vtype;
                        if (!read_u16_val(f, &vid)) DESER_FAIL();
                        if (!read_u8_val(f, &vtype)) DESER_FAIL();
                        if (!read_u16_val(f, &blkid)) DESER_FAIL();
                        inst->extra.phi.entries[ei].value.id = vid;
                        inst->extra.phi.entries[ei].value.type = (PIRTypeKind)vtype;
                        inst->extra.phi.entries[ei].block = (PIRBlock *)(long)(int)blkid;
                    }
                }
            }

            pir_block_append(block, inst);
        }
    }

    /* Set entry block */
    if (func->blocks.size() > 0) {
        func->entry_block = func->blocks[0];
    }
    /* Fix next_block_id */
    func->next_block_id = (int)num_blocks;

    /* Resolve block pointers in instructions */
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
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

    /* Rebuild CFG edges from instruction targets */
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            if (inst->target_block) {
                pir_block_add_edge(block, inst->target_block);
            }
            if (inst->false_block) {
                pir_block_add_edge(block, inst->false_block);
            }
            if (inst->handler_block) {
                pir_block_add_edge(block, inst->handler_block);
            }
        }
    }

    free(block_map);
    return func;
}

#undef DESER_FAIL
