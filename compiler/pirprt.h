/*
 * pirprt.h - PIR textual printer for debugging
 *
 * Outputs human-readable PIR for --dump-pir flag.
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRPRT_H
#define PIRPRT_H

#include "pir.h"
#include <stdio.h>

void pir_print_module(PIRModule *mod, FILE *out);
void pir_print_function(PIRFunction *func, FILE *out);

#endif /* PIRPRT_H */
