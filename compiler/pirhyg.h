/*
 * pirhyg.h - whole-module PIR hygiene
 *
 * Removes compiler-generated semantic scaffolding after its observability has
 * been established, but before arena scopes and target lowering are created.
 * The bit mask deliberately keeps the analysis compact on 16-bit hosts.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRHYG_H
#define PIRHYG_H

#include "pir.h"

enum PIRHygieneFeature {
    PIR_HYG_CODE        = 0x0001,
    PIR_HYG_ANNOTATIONS = 0x0002,
    PIR_HYG_PARAMETERS  = 0x0004,
    PIR_HYG_TYPE_PARAMS = 0x0008,
    PIR_HYG_DICT        = 0x0010,
    PIR_HYG_DYNAMIC     = 0x8000
};

struct PIRHygieneReport {
    unsigned observed_features;
    unsigned long metadata_bundles_removed;
    unsigned long instructions_removed;
    unsigned long exception_fast_paths;
};

void pir_hygiene_run(PIRModule *mod, PIRHygieneReport *report);

#endif /* PIRHYG_H */
