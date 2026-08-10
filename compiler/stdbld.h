/*
 * stdbld.h - Stdlib Builder (--build-stdlib orchestrator)
 *
 * Implements the --build-stdlib compiler mode:
 *   1. Scan stdlib stubs for metadata (like stdgen)
 *   2. Compile Python-backed functions to PIR
 *   3. Write v3 stdlib.idx (metadata + serialized PIR)
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef STDBLD_H
#define STDBLD_H

/* Run the stdlib build process.
 * stdlib_dir: path to stdlib/ directory (e.g., "stdlib")
 * pyfncs_path: path to pyfncs.py (Python-backed implementations),
 *              or NULL to use stdlib_dir/builtins/pyfncs.py
 * output_path: path to write stdlib.idx
 * Returns 0 on success, non-zero on failure. */
int stdlib_build(const char *stdlib_dir, const char *pyfncs_path,
                 const char *output_path);

#endif /* STDBLD_H */
