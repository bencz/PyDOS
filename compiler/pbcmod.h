/*
 * pbcmod.h - Semantic PyDOS bytecode module builder and verifier
 *
 * Records are encoded field by field.  The declarations below describe the
 * logical values only and are never used as on-disk structures.
 */

#ifndef PBCMOD_H
#define PBCMOD_H

#include "pbcop.h"

struct PBCExceptionSpec {
    PBCU16 start_offset;
    PBCU16 end_offset;
    PBCU16 handler_offset;
    PBCU16 stack_depth;
    PBCU16 match_symbol;       /* 0xffff means catch all */
    PBCU16 flags;
};

struct PBCFunctionSpec {
    PBCU16 name_symbol;
    PBCU16 flags;
    PBCU16 arg_count;
    PBCU16 local_count;
    PBCU16 max_stack;
    PBCU16 closure_count;
    PBCU16 signature_index;    /* 0xffff until signatures are materialized */
};

enum PBCModuleVerifyError {
    PBC_MODULE_OK = 0,
    PBC_MODULE_BAD_CONTAINER,
    PBC_MODULE_MISSING_SECTION,
    PBC_MODULE_BAD_SECTION_SIZE,
    PBC_MODULE_BAD_ITEM_COUNT,
    PBC_MODULE_TRUNCATED_STRING,
    PBC_MODULE_TOO_MANY_ITEMS,
    PBC_MODULE_BAD_STRING_INDEX,
    PBC_MODULE_BAD_SYMBOL_FLAGS,
    PBC_MODULE_BAD_MODULE_RECORD,
    PBC_MODULE_BAD_IMPORT_RECORD,
    PBC_MODULE_BAD_CONSTANT_TAG,
    PBC_MODULE_BAD_CONSTANT_FLAGS,
    PBC_MODULE_BAD_CONSTANT_VALUE,
    PBC_MODULE_BAD_FUNCTION_FLAGS,
    PBC_MODULE_BAD_FUNCTION_NAME,
    PBC_MODULE_BAD_FUNCTION_LOCALS,
    PBC_MODULE_BAD_FUNCTION_SIGNATURE,
    PBC_MODULE_BAD_SUSPENSION,
    PBC_MODULE_BAD_CODE_RANGE,
    PBC_MODULE_OVERLAPPING_CODE,
    PBC_MODULE_BAD_EXCEPTION_RANGE,
    PBC_MODULE_BAD_EXCEPTION_HANDLER,
    PBC_MODULE_BAD_EXCEPTION_STACK,
    PBC_MODULE_BAD_EXCEPTION_MATCH,
    PBC_MODULE_BAD_EXCEPTION_FLAGS,
    PBC_MODULE_MULTIPLE_INITIALIZERS,
    PBC_MODULE_INVALID_CODE
};

struct PBCModuleVerifyResult {
    PBCModuleVerifyError error;
    PBCU16 section_type;
    PBCU32 item_index;
    PBCCodeVerifyResult code_error;
    PBCVerifyResult container_error;
};

const char *pbc_module_verify_error_name(PBCModuleVerifyError error);

int pbc_verify_module(const PBCU8 *data, PBCU32 size,
                      PBCModuleVerifyResult *result);
int pbc_module_name(const PBCU8 *data, PBCU32 size,
                    const PBCU8 **name, PBCU16 *name_size);

class PBCModuleBuilder {
public:
    PBCModuleBuilder();
    ~PBCModuleBuilder();

    long add_string(const char *text);
    long add_string(const char *text, PBCU16 length);
    long add_symbol(PBCU16 string_index, PBCU16 flags);
    int set_module(PBCU16 name_symbol, PBCU16 flags);
    int add_import(PBCU16 module_symbol, PBCU16 flags);
    long add_constant_none();
    long add_constant_bool(int value);
    long add_constant_int32(long value);
    long add_constant_float64(double value);
    long add_constant_string(PBCU16 string_index);
    long add_function(const PBCFunctionSpec &spec,
                      const PBCU8 *code, PBCU16 code_size,
                      const PBCExceptionSpec *exceptions,
                      PBCU16 exception_count);

    int write(PBCWriter &writer);
    const char *error() const;

private:
    struct Buffer {
        PBCU8 *data;
        PBCU32 size;
        PBCU32 capacity;
    };

    Buffer strings;
    Buffer constants;
    Buffer symbols;
    Buffer functions;
    Buffer code;
    Buffer exceptions;
    Buffer imports;
    PBCU8 module_record[PBC_MODULE_RECORD_SIZE];
    PBCU32 string_count;
    PBCU32 constant_count;
    PBCU32 symbol_count;
    PBCU32 function_count;
    PBCU32 exception_count;
    PBCU32 import_count;
    int has_module;
    const char *last_error;

    int append(Buffer &buffer, const void *bytes, PBCU32 byte_count);
    int append_u16(Buffer &buffer, PBCU16 value);
    int append_u32(Buffer &buffer, PBCU32 value);
    long add_constant(PBCU8 tag, const PBCU8 payload[8]);

    PBCModuleBuilder(const PBCModuleBuilder &);
    PBCModuleBuilder &operator=(const PBCModuleBuilder &);
};

#endif /* PBCMOD_H */
