/*
 * stdlibscan.cpp - Stdlib registry implementation
 *
 * Loads the pre-compiled binary index (stdlib.idx) and provides
 * fast lookup methods for the compiler pipeline.
 * Supports v2 (metadata only) and v3 (metadata + PIR) formats.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "stdscan.h"
#include "pir.h"
#include "pirsrlz.h"
#include <stdio.h>
#include <string.h>

/* ================================================================= */
/* StdlibRegistry implementation                                      */
/* ================================================================= */

StdlibRegistry::StdlibRegistry()
    : num_funcs_(0), num_types_(0), loaded_(0), num_pir_funcs_(0)
{
    memset(funcs_, 0, sizeof(funcs_));
    memset(types_, 0, sizeof(types_));
    memset(pir_funcs_, 0, sizeof(pir_funcs_));
}

StdlibRegistry::~StdlibRegistry()
{
    int i;
    for (i = 0; i < num_pir_funcs_; i++) {
        if (pir_funcs_[i]) {
            pir_func_free(pir_funcs_[i]);
            pir_funcs_[i] = 0;
        }
    }
}

int StdlibRegistry::load_idx(const char *path)
{
    FILE *f;
    StdlibIdxHeader hdr;
    int i;
    int version;

    if (!path) return 0;

    f = fopen(path, "rb");
    if (!f) return 0;

    /* Read header */
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    /* Validate magic */
    if (memcmp(hdr.magic, STDLIB_IDX_MAGIC, 4) != 0) {
        fclose(f);
        return 0;
    }

    /* Validate version (accept v2 and v3) */
    version = hdr.version;
    if (version != STDLIB_IDX_VERSION_2 && version != STDLIB_IDX_VERSION_3) {
        fclose(f);
        return 0;
    }

    /* Validate counts */
    if (hdr.num_funcs < 0 || hdr.num_funcs > STDLIB_MAX_FUNCS ||
        hdr.num_types < 0 || hdr.num_types > STDLIB_MAX_TYPES) {
        fclose(f);
        return 0;
    }

    num_funcs_ = hdr.num_funcs;
    num_types_ = hdr.num_types;

    /* Read function entries */
    for (i = 0; i < num_funcs_; i++) {
        if (fread(&funcs_[i], sizeof(BuiltinFuncEntry), 1, f) != 1) {
            fclose(f);
            num_funcs_ = 0;
            num_types_ = 0;
            return 0;
        }
        /* v2 files have padding=0 where is_pir now lives.
         * Since 0 means "not PIR", this is backward-compatible. */
    }

    /* Read type entries (header + operator fields + methods) */
    for (i = 0; i < num_types_; i++) {
        /* Read the fixed part: type_name + type_kind + num_methods + runtime_type_tag + pad */
        types_[i].runtime_type_tag = -1;
        types_[i].reserved_pad = 0;
        if (fread(types_[i].type_name, STDLIB_TYPE_NAME_LEN, 1, f) != 1 ||
            fread(&types_[i].type_kind, sizeof(short), 1, f) != 1 ||
            fread(&types_[i].num_methods, sizeof(short), 1, f) != 1 ||
            fread(&types_[i].runtime_type_tag, sizeof(short), 1, f) != 1 ||
            fread(&types_[i].reserved_pad, sizeof(short), 1, f) != 1) {
            fclose(f);
            num_funcs_ = 0;
            num_types_ = 0;
            return 0;
        }
        /* Read operator function names */
        if (fread(types_[i].op_getitem, STDLIB_ASM_LEN, 1, f) != 1 ||
            fread(types_[i].op_setitem, STDLIB_ASM_LEN, 1, f) != 1 ||
            fread(types_[i].op_getslice, STDLIB_ASM_LEN, 1, f) != 1 ||
            fread(types_[i].op_contains, STDLIB_ASM_LEN, 1, f) != 1) {
            fclose(f);
            num_funcs_ = 0;
            num_types_ = 0;
            return 0;
        }
        /* Read methods */
        if (types_[i].num_methods > STDLIB_MAX_METHODS) {
            fclose(f);
            num_funcs_ = 0;
            num_types_ = 0;
            return 0;
        }
        if (types_[i].num_methods > 0) {
            int j;
            for (j = 0; j < types_[i].num_methods; j++) {
                if (fread(&types_[i].methods[j],
                          sizeof(BuiltinMethodEntry), 1, f) != 1) {
                    fclose(f);
                    num_funcs_ = 0;
                    num_types_ = 0;
                    return 0;
                }
            }
        }
    }

    /* Read PIR section (v3 only) */
    num_pir_funcs_ = 0;
    if (version >= STDLIB_IDX_VERSION_3 && hdr.num_pir_funcs > 0) {
        PirStringTable strtab;
        int pi;

        pir_strtab_init(&strtab);
        if (!pir_strtab_read(&strtab, f)) {
            pir_strtab_destroy(&strtab);
            fclose(f);
            /* PIR section failed but metadata is OK — continue without PIR */
            loaded_ = 1;
            return 1;
        }

        for (pi = 0; pi < hdr.num_pir_funcs && pi < STDLIB_MAX_PIR_FUNCS; pi++) {
            PIRFunction *pfunc = pir_deserialize_func(f, &strtab);
            if (!pfunc) break;
            pir_funcs_[num_pir_funcs_++] = pfunc;
        }

        pir_strtab_destroy(&strtab);
    }

    fclose(f);
    loaded_ = 1;
    return 1;
}

const BuiltinFuncEntry *StdlibRegistry::find_builtin(const char *name) const
{
    int i;
    if (!loaded_ || !name) return 0;
    for (i = 0; i < num_funcs_; i++) {
        if (strcmp(funcs_[i].py_name, name) == 0) {
            return &funcs_[i];
        }
    }
    return 0;
}

const BuiltinMethodEntry *StdlibRegistry::find_method(int type_kind,
                                                        const char *name) const
{
    int i, j;
    if (!loaded_ || !name) return 0;
    for (i = 0; i < num_types_; i++) {
        if (types_[i].type_kind == type_kind) {
            for (j = 0; j < types_[i].num_methods; j++) {
                if (strcmp(types_[i].methods[j].method_name, name) == 0) {
                    return &types_[i].methods[j];
                }
            }
            return 0;
        }
    }
    return 0;
}

int StdlibRegistry::find_exc_code(const char *name) const
{
    int i;
    if (!loaded_ || !name) return -1;
    for (i = 0; i < num_funcs_; i++) {
        if (funcs_[i].is_exception &&
            strcmp(funcs_[i].py_name, name) == 0) {
            return funcs_[i].exc_code;
        }
    }
    return -1;
}

const char *StdlibRegistry::find_exc_asm_name(const char *name) const
{
    int i;
    if (!loaded_ || !name) return 0;
    for (i = 0; i < num_funcs_; i++) {
        if (funcs_[i].is_exception &&
            strcmp(funcs_[i].py_name, name) == 0) {
            return funcs_[i].asm_name;
        }
    }
    return 0;
}

const char *StdlibRegistry::find_builtin_asm_name(const char *name) const
{
    const BuiltinFuncEntry *e = find_builtin(name);
    if (e && e->asm_name[0] != '\0') return e->asm_name;
    return 0;
}

int StdlibRegistry::is_pir_backed(const char *name) const
{
    const BuiltinFuncEntry *e = find_builtin(name);
    if (e && e->is_pir) return 1;
    return 0;
}

int StdlibRegistry::is_pir_method(int type_kind, const char *method_name,
                                   char *pir_name_out) const
{
    int i, j;
    if (!loaded_ || !method_name || !pir_name_out) return 0;
    for (i = 0; i < num_types_; i++) {
        if (types_[i].type_kind == type_kind) {
            for (j = 0; j < types_[i].num_methods; j++) {
                if (strcmp(types_[i].methods[j].method_name, method_name) == 0 &&
                    types_[i].methods[j].is_pir) {
                    /* Derive PIR function name: TYPE_METHOD */
                    snprintf(pir_name_out, STDLIB_NAME_LEN, "%s_%s",
                             types_[i].type_name, method_name);
                    return 1;
                }
            }
            return 0;
        }
    }
    return 0;
}

PIRFunction *StdlibRegistry::get_pir_func(const char *name) const
{
    int i;
    if (!loaded_ || !name) return 0;
    for (i = 0; i < num_pir_funcs_; i++) {
        if (pir_funcs_[i] && pir_funcs_[i]->name &&
            strcmp(pir_funcs_[i]->name, name) == 0) {
            return pir_funcs_[i];
        }
    }
    return 0;
}

int StdlibRegistry::find_runtime_type_tag(const char *type_name) const
{
    int i;
    /* Static fallback for types without stub files (float, bool, range, bytes) */
    static struct { const char *name; int tag; } fallback[] = {
        {"float", 3}, {"bool", 1}, {"range", 15}, {"bytes", 9}, {0, 0}
    };

    if (!loaded_ || !type_name) return -1;

    /* First check type entries from .idx */
    for (i = 0; i < num_types_; i++) {
        if (strcmp(types_[i].type_name, type_name) == 0) {
            if (types_[i].runtime_type_tag >= 0)
                return (int)types_[i].runtime_type_tag;
        }
    }

    /* Fallback for types without stub files */
    for (i = 0; fallback[i].name; i++) {
        if (strcmp(type_name, fallback[i].name) == 0)
            return fallback[i].tag;
    }

    return -1;
}

int StdlibRegistry::is_loaded() const
{
    return loaded_;
}

const BuiltinFuncEntry *StdlibRegistry::get_func(int i) const
{
    if (i < 0 || i >= num_funcs_) return 0;
    return &funcs_[i];
}

const BuiltinTypeEntry *StdlibRegistry::get_type(int i) const
{
    if (i < 0 || i >= num_types_) return 0;
    return &types_[i];
}

const BuiltinTypeEntry *StdlibRegistry::get_type_by_kind(int type_kind) const
{
    int i;
    if (!loaded_) return 0;
    for (i = 0; i < num_types_; i++) {
        if (types_[i].type_kind == type_kind) return &types_[i];
    }
    return 0;
}
