/* pbclwr.h - Lower optimized PIR to portable PyDOS bytecode. */

#ifndef PBCLWR_H
#define PBCLWR_H

#include "pir.h"
#include "pbcmod.h"

class PBCPIRLowerer {
public:
    PBCPIRLowerer();

    int lower(PIRModule *module, PBCWriter &writer);
    int get_error_count() const;
    const char *get_error() const;
    const char *get_error_function() const;
    PIROp get_error_op() const;
    int get_error_line() const;

private:
    struct NamedIndex {
        const char *name;
        PBCU16 index;
    };

    PBCModuleBuilder *builder;
    PIRModule *pir_module;
    PdVector<NamedIndex> symbols;
    PdVector<PBCU16> function_symbols;
    int error_count;
    const char *last_error;
    const char *last_function;
    PIROp last_op;
    int last_line;

    long symbol(const char *name);
    long function_index(const char *name) const;
    void fail(const char *message, const PIRFunction *function,
              const PIRInst *instruction);
    int lower_function(PIRFunction *function, PBCU16 function_index);

    PBCPIRLowerer(const PBCPIRLowerer &);
    PBCPIRLowerer &operator=(const PBCPIRLowerer &);
};

#endif /* PBCLWR_H */
