/*
 * modscan.h - Module scanner for PyDOS compiler
 *
 * Scans imported .py files to extract top-level symbol information
 * (functions, classes, globals) without performing full compilation.
 * Used by sema.cpp to resolve from...import names to proper symbol kinds.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef MODSCAN_H
#define MODSCAN_H

struct TypeInfo; /* forward decl from types.h */

/* --------------------------------------------------------------- */
/* Scanned symbol info                                               */
/* --------------------------------------------------------------- */

struct ModFuncInfo {
    const char *name;
    int num_params;
};

struct ModClassInfo {
    const char *name;
    const char *base_name;    /* NULL if no base */
};

struct ModuleInfo {
    const char *module_name;
    ModFuncInfo functions[128];
    int num_functions;
    ModClassInfo classes[32];
    int num_classes;
    const char *globals[128];
    int num_globals;
    int valid;                /* 1 if scan succeeded */
};

/* --------------------------------------------------------------- */
/* Module scanner API                                                */
/* --------------------------------------------------------------- */

/*
 * Scan a Python module file for top-level symbols.
 *
 * Searches for <module_name>.py in each search_paths[] directory.
 * Lexes and parses the file, then walks the top-level AST to extract
 * function, class, and global variable declarations.
 *
 * search_paths is a NULL-terminated array of directory paths.
 * Returns 1 on success (out->valid set), 0 if module not found.
 */
int module_scan(const char *module_name,
                const char **search_paths, int num_search_paths,
                ModuleInfo *out);

#endif /* MODSCAN_H */
