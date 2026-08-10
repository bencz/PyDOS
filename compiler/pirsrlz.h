/*
 * pirsrlz.h - PIR Serialization/Deserialization
 *
 * Serializes PIRFunction to a compact binary format for storage in
 * stdlib.idx. Deserializes back to full PIRFunction with blocks,
 * instructions, and CFG edges.
 *
 * Used by --build-stdlib to write pre-compiled PIR, and by
 * stdscan.cpp to load it at compile time.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRSRLZ_H
#define PIRSRLZ_H

#include "pir.h"
#include <stdio.h>

/* ================================================================= */
/* String table for PIR serialization                                  */
/* ================================================================= */

struct PirStringTable {
    char **strings;
    int count;
    int capacity;
};

void pir_strtab_init(PirStringTable *tab);
void pir_strtab_destroy(PirStringTable *tab);

/* Add string to table. Returns index. Deduplicates. */
int  pir_strtab_add(PirStringTable *tab, const char *s);

/* Find string in table. Returns index or -1 if not found. */
int  pir_strtab_find(PirStringTable *tab, const char *s);

/* Get string by index. Returns NULL if out of range. */
const char *pir_strtab_get(PirStringTable *tab, int idx);

/* ================================================================= */
/* Serialization                                                       */
/* ================================================================= */

/* Write string table to file. */
void pir_strtab_write(PirStringTable *tab, FILE *f);

/* Read string table from file. Returns 1 on success, 0 on failure. */
int  pir_strtab_read(PirStringTable *tab, FILE *f);

/* Collect all strings from a PIR function into the string table. */
void pir_collect_strings(PIRFunction *func, PirStringTable *tab);

/* Serialize one PIR function to file.
 * String table must have been populated via pir_collect_strings. */
void pir_serialize_func(PIRFunction *func, FILE *f, PirStringTable *tab);

/* Deserialize one PIR function from file.
 * String table must have been read via pir_strtab_read.
 * Returns NULL on failure. Caller owns the returned PIRFunction. */
PIRFunction *pir_deserialize_func(FILE *f, PirStringTable *tab);

#endif /* PIRSRLZ_H */
