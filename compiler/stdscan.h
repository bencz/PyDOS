/*
 * stdlibscan.h - Stdlib registry for PyDOS compiler
 *
 * Loads a pre-compiled binary index (stdlib.idx) that maps Python
 * builtin names to C runtime symbols. Used by sema, codegen, and
 * pirbld to look up builtins, methods, and exception codes without
 * hardcoding them in the compiler source.
 *
 * v3 format adds PIR data for Python-backed builtins.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#ifndef STDSCAN_H
#define STDSCAN_H

struct PIRFunction; /* forward decl from pir.h */

/* ================================================================= */
/* Index file format constants                                        */
/* ================================================================= */

#define STDLIB_IDX_MAGIC      "PYSI"
#define STDLIB_IDX_VERSION_2  2
#define STDLIB_IDX_VERSION_3  3
#define STDLIB_IDX_VERSION    3   /* current version */

#define STDLIB_MAX_FUNCS      128
#define STDLIB_MAX_TYPES      16
#define STDLIB_MAX_METHODS    64
#define STDLIB_MAX_PIR_FUNCS  64

#define STDLIB_NAME_LEN       48
#define STDLIB_ASM_LEN        64
#define STDLIB_TYPE_NAME_LEN  32

/* ================================================================= */
/* Data structures (match binary layout in stdlib.idx)                */
/* ================================================================= */

struct BuiltinFuncEntry {
    char py_name[STDLIB_NAME_LEN];
    char asm_name[STDLIB_ASM_LEN];
    short num_params;
    short ret_type_kind;    /* TypeKind enum value */
    char is_exception;
    char is_pir;            /* 1 = Python-backed (PIR in .idx), 0 = C-backed */
    short exc_code;
};

struct BuiltinMethodEntry {
    char method_name[STDLIB_NAME_LEN];
    char asm_name[STDLIB_ASM_LEN];
    short num_params;       /* excluding self (Python param count for sema) */
    short ret_type_kind;    /* TypeKind enum value */
    short fast_argc;        /* C fast-path argc excl. self; -1 = same as num_params */
    short is_pir;           /* 1 = Python-backed method (PIR in .idx); was: reserved */
};

struct BuiltinTypeEntry {
    char type_name[STDLIB_TYPE_NAME_LEN];
    short type_kind;        /* TypeKind enum value */
    short num_methods;
    short runtime_type_tag; /* PYDT_* enum value for isinstance dispatch, -1 if unknown */
    short reserved_pad;     /* alignment padding */
    /* Operator function names (empty = use generic dispatch) */
    char op_getitem[STDLIB_ASM_LEN];    /* __getitem__ → e.g., pydos_list_get_ */
    char op_setitem[STDLIB_ASM_LEN];    /* __setitem__ → e.g., pydos_list_set_ */
    char op_getslice[STDLIB_ASM_LEN];   /* __getslice__ → e.g., pydos_list_slice_ */
    char op_contains[STDLIB_ASM_LEN];   /* __contains__ (future) */
    BuiltinMethodEntry methods[STDLIB_MAX_METHODS];
};

/* ================================================================= */
/* Binary index file header (12 bytes)                                 */
/* ================================================================= */

struct StdlibIdxHeader {
    char magic[4];          /* "PYSI" */
    short version;          /* 2 or 3 */
    short num_funcs;
    short num_types;
    short num_pir_funcs;    /* v3: count of PIR functions; v2: was 'reserved' (0) */
};

/* ================================================================= */
/* StdlibRegistry class                                               */
/* ================================================================= */

class StdlibRegistry {
public:
    StdlibRegistry();
    ~StdlibRegistry();

    /* Load pre-compiled index file. Returns 1 on success, 0 on failure.
     * Supports v2 and v3 formats. */
    int load_idx(const char *path);

    /* Lookup a builtin function by Python name. Returns NULL if not found. */
    const BuiltinFuncEntry *find_builtin(const char *name) const;

    /* Lookup a method on a type. type_kind is a TypeKind enum value.
     * Returns NULL if not found. */
    const BuiltinMethodEntry *find_method(int type_kind,
                                           const char *name) const;

    /* Find exception type code by name. Returns -1 if not found. */
    int find_exc_code(const char *name) const;

    /* Find exception constructor asm name. Returns NULL if not found. */
    const char *find_exc_asm_name(const char *name) const;

    /* Find builtin asm name by Python name.
     * Returns NULL if not found or if Python-backed (asm_name empty). */
    const char *find_builtin_asm_name(const char *name) const;

    /* Check if a builtin function is Python-backed (has PIR, not C). */
    int is_pir_backed(const char *name) const;

    /* Check if a method is Python-backed. type_kind is TypeKind enum.
     * If found, writes the PIR function name (e.g., "list_count") to
     * pir_name_out (must be at least STDLIB_NAME_LEN bytes).
     * Returns 1 if PIR-backed, 0 otherwise. */
    int is_pir_method(int type_kind, const char *method_name,
                      char *pir_name_out) const;

    /* Get PIR function by name. Returns NULL if not found or not PIR-backed. */
    PIRFunction *get_pir_func(const char *name) const;

    /* Find PYDT_* runtime type tag by type name. Returns -1 if not found. */
    int find_runtime_type_tag(const char *type_name) const;

    /* Returns 1 if a valid index has been loaded. */
    int is_loaded() const;

    /* Accessors for iteration */
    int get_num_funcs() const { return num_funcs_; }
    const BuiltinFuncEntry *get_func(int i) const;
    int get_num_types() const { return num_types_; }
    const BuiltinTypeEntry *get_type(int i) const;
    const BuiltinTypeEntry *get_type_by_kind(int type_kind) const;
    int get_num_pir_funcs() const { return num_pir_funcs_; }

private:
    BuiltinFuncEntry funcs_[STDLIB_MAX_FUNCS];
    int num_funcs_;
    BuiltinTypeEntry types_[STDLIB_MAX_TYPES];
    int num_types_;
    int loaded_;

    /* PIR functions loaded from v3 .idx */
    PIRFunction *pir_funcs_[STDLIB_MAX_PIR_FUNCS];
    int num_pir_funcs_;
};

#endif /* STDSCAN_H */
