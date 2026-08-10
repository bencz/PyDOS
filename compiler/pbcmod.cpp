/*
 * pbcmod.cpp - Semantic PyDOS bytecode module builder and verifier
 */

#include "pbcmod.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>

static PBCU16 mod_read_u16(const PBCU8 *p)
{
    return (PBCU16)((PBCU16)p[0] | ((PBCU16)p[1] << 8));
}

static PBCU32 mod_read_u32(const PBCU8 *p)
{
    return (PBCU32)p[0] | ((PBCU32)p[1] << 8) |
           ((PBCU32)p[2] << 16) | ((PBCU32)p[3] << 24);
}

static void mod_write_u16(PBCU8 *p, PBCU16 value)
{
    p[0] = (PBCU8)(value & 0xffU);
    p[1] = (PBCU8)((value >> 8) & 0xffU);
}

static void mod_write_u32(PBCU8 *p, PBCU32 value)
{
    p[0] = (PBCU8)(value & 0xffU);
    p[1] = (PBCU8)((value >> 8) & 0xffU);
    p[2] = (PBCU8)((value >> 16) & 0xffU);
    p[3] = (PBCU8)((value >> 24) & 0xffU);
}

static void module_result(PBCModuleVerifyResult *result,
                          PBCModuleVerifyError error,
                          PBCU16 section, PBCU32 item)
{
    if (result == 0) return;
    result->error = error;
    result->section_type = section;
    result->item_index = item;
}

static int code_contains_suspension(const PBCU8 *code, PBCU32 size)
{
    PBCU32 offset = 0;
    while (offset < size) {
        const PBCOpcodeInfo *info = pbc_opcode_info(code[offset]);
        if (info == 0) return 0;
        if ((info->flags & PBC_OPF_SUSPENDS) != 0) return 1;
        offset += 1U + info->operand_width;
    }
    return 0;
}

const char *pbc_module_verify_error_name(PBCModuleVerifyError error)
{
    switch (error) {
    case PBC_MODULE_OK: return "ok";
    case PBC_MODULE_BAD_CONTAINER: return "invalid PBC container";
    case PBC_MODULE_MISSING_SECTION: return "missing required section";
    case PBC_MODULE_BAD_SECTION_SIZE: return "invalid section size";
    case PBC_MODULE_BAD_ITEM_COUNT: return "section item count mismatch";
    case PBC_MODULE_TRUNCATED_STRING: return "truncated string record";
    case PBC_MODULE_TOO_MANY_ITEMS: return "too many indexed items";
    case PBC_MODULE_BAD_STRING_INDEX: return "string index out of range";
    case PBC_MODULE_BAD_SYMBOL_FLAGS: return "unsupported symbol flags";
    case PBC_MODULE_BAD_MODULE_RECORD: return "invalid module identity record";
    case PBC_MODULE_BAD_IMPORT_RECORD: return "invalid module import record";
    case PBC_MODULE_BAD_CONSTANT_TAG: return "unknown constant tag";
    case PBC_MODULE_BAD_CONSTANT_FLAGS: return "unsupported constant flags";
    case PBC_MODULE_BAD_CONSTANT_VALUE: return "invalid constant payload";
    case PBC_MODULE_BAD_FUNCTION_FLAGS: return "unsupported function flags";
    case PBC_MODULE_BAD_FUNCTION_NAME: return "function name out of range";
    case PBC_MODULE_BAD_FUNCTION_LOCALS: return "invalid function locals";
    case PBC_MODULE_BAD_FUNCTION_SIGNATURE: return "invalid signature index";
    case PBC_MODULE_BAD_SUSPENSION: return "suspension in regular function";
    case PBC_MODULE_BAD_CODE_RANGE: return "function code out of range";
    case PBC_MODULE_OVERLAPPING_CODE: return "function code ranges overlap";
    case PBC_MODULE_BAD_EXCEPTION_RANGE: return "invalid exception range";
    case PBC_MODULE_BAD_EXCEPTION_HANDLER: return "invalid exception handler";
    case PBC_MODULE_BAD_EXCEPTION_STACK: return "invalid exception stack depth";
    case PBC_MODULE_BAD_EXCEPTION_MATCH: return "exception symbol out of range";
    case PBC_MODULE_BAD_EXCEPTION_FLAGS: return "unsupported exception flags";
    case PBC_MODULE_MULTIPLE_INITIALIZERS: return "multiple module initializers";
    case PBC_MODULE_INVALID_CODE: return "invalid function bytecode";
    }
    return "unknown module verifier error";
}

static int require_section(const PBCU8 *data, PBCU32 size, PBCU16 type,
                           PBCSectionView *view,
                           PBCModuleVerifyResult *result)
{
    if (pbc_find_section(data, size, type, view)) return 1;
    module_result(result, PBC_MODULE_MISSING_SECTION, type, 0);
    return 0;
}

static int verify_strings(const PBCSectionView &section,
                          PBCModuleVerifyResult *result)
{
    PBCU32 offset = 0;
    PBCU32 count = 0;
    if (section.item_count > 65535U) {
        module_result(result, PBC_MODULE_TOO_MANY_ITEMS,
                      PBC_SECTION_STRINGS, section.item_count);
        return 0;
    }
    while (offset < section.size) {
        PBCU16 length;
        if (section.size - offset < 2U) {
            module_result(result, PBC_MODULE_TRUNCATED_STRING,
                          PBC_SECTION_STRINGS, count);
            return 0;
        }
        length = mod_read_u16(section.data + offset);
        offset += 2U;
        if ((PBCU32)length > section.size - offset) {
            module_result(result, PBC_MODULE_TRUNCATED_STRING,
                          PBC_SECTION_STRINGS, count);
            return 0;
        }
        offset += length;
        count++;
    }
    if (count != section.item_count) {
        module_result(result, PBC_MODULE_BAD_ITEM_COUNT,
                      PBC_SECTION_STRINGS, count);
        return 0;
    }
    return 1;
}

static int string_at(const PBCSectionView &section, PBCU16 index,
                     const PBCU8 **text, PBCU16 *length)
{
    PBCU32 offset = 0;
    PBCU16 current = 0;
    while (offset < section.size) {
        PBCU16 item_length = mod_read_u16(section.data + offset);
        offset += 2U;
        if (current == index) {
            *text = section.data + offset;
            *length = item_length;
            return 1;
        }
        offset += item_length;
        current++;
    }
    return 0;
}

static int module_name_char(PBCU8 value, int first)
{
    if (value == '_') return 1;
    if (value >= 'A' && value <= 'Z') return 1;
    if (value >= 'a' && value <= 'z') return 1;
    return !first && value >= '0' && value <= '9';
}

static int valid_module_name(const PBCU8 *text, PBCU16 length)
{
    PBCU16 i;
    int first = 1;
    if (text == 0 || length == 0) return 0;
    for (i = 0; i < length; i++) {
        if (text[i] == '.') {
            if (first || i + 1U == length) return 0;
            first = 1;
        } else {
            if (!module_name_char(text[i], first)) return 0;
            first = 0;
        }
    }
    return !first;
}

static int verify_fixed_section(const PBCSectionView &section,
                                PBCU32 record_size,
                                PBCModuleVerifyResult *result)
{
    if (section.item_count > 65535U) {
        module_result(result, PBC_MODULE_TOO_MANY_ITEMS,
                      section.type, section.item_count);
        return 0;
    }
    if (section.item_count > (PBCU32)0xffffffffU / record_size ||
        section.size != section.item_count * record_size) {
        module_result(result, PBC_MODULE_BAD_SECTION_SIZE,
                      section.type, section.item_count);
        return 0;
    }
    return 1;
}

static int verify_symbols(const PBCSectionView &section,
                          PBCU32 string_count,
                          PBCModuleVerifyResult *result)
{
    PBCU32 i;
    if (!verify_fixed_section(section, PBC_SYMBOL_RECORD_SIZE, result))
        return 0;
    for (i = 0; i < section.item_count; i++) {
        const PBCU8 *record = section.data + i * PBC_SYMBOL_RECORD_SIZE;
        if (mod_read_u16(record) >= string_count) {
            module_result(result, PBC_MODULE_BAD_STRING_INDEX,
                          PBC_SECTION_SYMBOLS, i);
            return 0;
        }
        if (mod_read_u16(record + 2) != 0) {
            module_result(result, PBC_MODULE_BAD_SYMBOL_FLAGS,
                          PBC_SECTION_SYMBOLS, i);
            return 0;
        }
    }
    return 1;
}

static int payload_is_zero(const PBCU8 *payload, PBCU16 first)
{
    PBCU16 i;
    for (i = first; i < 8; i++) {
        if (payload[i] != 0) return 0;
    }
    return 1;
}

static int verify_constants(const PBCSectionView &section,
                            PBCU32 string_count,
                            PBCModuleVerifyResult *result)
{
    PBCU32 i;
    if (!verify_fixed_section(section, PBC_CONSTANT_RECORD_SIZE, result))
        return 0;
    for (i = 0; i < section.item_count; i++) {
        const PBCU8 *record = section.data + i * PBC_CONSTANT_RECORD_SIZE;
        PBCU8 tag = record[0];
        const PBCU8 *payload = record + 4;
        if (record[1] != 0 || mod_read_u16(record + 2) != 0) {
            module_result(result, PBC_MODULE_BAD_CONSTANT_FLAGS,
                          PBC_SECTION_CONSTANTS, i);
            return 0;
        }
        if (tag > PBC_CONST_STRING) {
            module_result(result, PBC_MODULE_BAD_CONSTANT_TAG,
                          PBC_SECTION_CONSTANTS, i);
            return 0;
        }
        if (tag == PBC_CONST_NONE && !payload_is_zero(payload, 0)) {
            module_result(result, PBC_MODULE_BAD_CONSTANT_VALUE,
                          PBC_SECTION_CONSTANTS, i);
            return 0;
        }
        if (tag == PBC_CONST_BOOL &&
            ((payload[0] > 1) || !payload_is_zero(payload, 1))) {
            module_result(result, PBC_MODULE_BAD_CONSTANT_VALUE,
                          PBC_SECTION_CONSTANTS, i);
            return 0;
        }
        if (tag == PBC_CONST_INT32 && !payload_is_zero(payload, 4)) {
            module_result(result, PBC_MODULE_BAD_CONSTANT_VALUE,
                          PBC_SECTION_CONSTANTS, i);
            return 0;
        }
        if (tag == PBC_CONST_STRING &&
            (mod_read_u32(payload) >= string_count ||
             !payload_is_zero(payload, 4))) {
            module_result(result, PBC_MODULE_BAD_STRING_INDEX,
                          PBC_SECTION_CONSTANTS, i);
            return 0;
        }
    }
    return 1;
}

static int verify_exception(const PBCU8 *record, PBCU32 item,
                            PBCU16 code_size, PBCU16 max_stack,
                            PBCU32 symbol_count,
                            PBCModuleVerifyResult *result)
{
    PBCU16 start = mod_read_u16(record);
    PBCU16 end = mod_read_u16(record + 2);
    PBCU16 handler = mod_read_u16(record + 4);
    PBCU16 stack = mod_read_u16(record + 6);
    PBCU16 match = mod_read_u16(record + 8);
    if (start >= end || end > code_size) {
        module_result(result, PBC_MODULE_BAD_EXCEPTION_RANGE,
                      PBC_SECTION_EXCEPTIONS, item);
        return 0;
    }
    if (handler >= code_size) {
        module_result(result, PBC_MODULE_BAD_EXCEPTION_HANDLER,
                      PBC_SECTION_EXCEPTIONS, item);
        return 0;
    }
    /* The handler entry pushes the active exception above stack_depth. */
    if (stack >= max_stack) {
        module_result(result, PBC_MODULE_BAD_EXCEPTION_STACK,
                      PBC_SECTION_EXCEPTIONS, item);
        return 0;
    }
    if (match != 0xffffU && match >= symbol_count) {
        module_result(result, PBC_MODULE_BAD_EXCEPTION_MATCH,
                      PBC_SECTION_EXCEPTIONS, item);
        return 0;
    }
    if (mod_read_u16(record + 10) != 0) {
        module_result(result, PBC_MODULE_BAD_EXCEPTION_FLAGS,
                      PBC_SECTION_EXCEPTIONS, item);
        return 0;
    }
    return 1;
}

int pbc_verify_module(const PBCU8 *data, PBCU32 size,
                      PBCModuleVerifyResult *result)
{
    PBCSectionView strings;
    PBCSectionView constants;
    PBCSectionView symbols;
    PBCSectionView functions;
    PBCSectionView code;
    PBCSectionView exceptions;
    PBCSectionView module;
    PBCSectionView imports;
    PBCVerifyResult container;
    PBCU32 i;
    PBCU32 initializer_count = 0;

    if (result != 0) {
        memset(result, 0, sizeof(*result));
        result->error = PBC_MODULE_OK;
    }
    if (!pbc_verify_container(data, size, &container)) {
        module_result(result, PBC_MODULE_BAD_CONTAINER, 0, 0);
        if (result != 0) result->container_error = container;
        return 0;
    }
    if (!require_section(data, size, PBC_SECTION_STRINGS, &strings, result) ||
        !require_section(data, size, PBC_SECTION_CONSTANTS, &constants, result) ||
        !require_section(data, size, PBC_SECTION_SYMBOLS, &symbols, result) ||
        !require_section(data, size, PBC_SECTION_FUNCTIONS, &functions, result) ||
        !require_section(data, size, PBC_SECTION_CODE, &code, result))
        return 0;

    if (!verify_strings(strings, result) ||
        !verify_constants(constants, strings.item_count, result) ||
        !verify_symbols(symbols, strings.item_count, result) ||
        !verify_fixed_section(functions, PBC_FUNCTION_RECORD_SIZE, result))
        return 0;

    if (pbc_find_section(data, size, PBC_SECTION_EXCEPTIONS, &exceptions)) {
        if (!verify_fixed_section(exceptions, PBC_EXCEPTION_RECORD_SIZE, result))
            return 0;
    } else {
        exceptions.type = PBC_SECTION_EXCEPTIONS;
        exceptions.data = 0;
        exceptions.size = 0;
        exceptions.item_count = 0;
    }

    if (pbc_find_section(data, size, PBC_SECTION_MODULE, &module)) {
        PBCU16 declared_imports;
        PBCU16 module_symbol;
        PBCU16 module_string;
        const PBCU8 *module_name;
        PBCU16 module_name_size;
        if (!verify_fixed_section(module, PBC_MODULE_RECORD_SIZE, result) ||
            module.item_count != 1 ||
            mod_read_u16(module.data) >= symbols.item_count ||
            mod_read_u16(module.data + 2) != 0 ||
            mod_read_u16(module.data + 6) != 0) {
            module_result(result, PBC_MODULE_BAD_MODULE_RECORD,
                          PBC_SECTION_MODULE, 0);
            return 0;
        }
        module_symbol = mod_read_u16(module.data);
        module_string = mod_read_u16(
            symbols.data + (PBCU32)module_symbol * PBC_SYMBOL_RECORD_SIZE);
        if (!string_at(strings, module_string, &module_name,
                       &module_name_size) ||
            !valid_module_name(module_name, module_name_size)) {
            module_result(result, PBC_MODULE_BAD_MODULE_RECORD,
                          PBC_SECTION_MODULE, 0);
            return 0;
        }
        declared_imports = mod_read_u16(module.data + 4);
        if (pbc_find_section(data, size, PBC_SECTION_IMPORTS, &imports)) {
            PBCU32 import_index;
            if (!verify_fixed_section(imports, PBC_IMPORT_RECORD_SIZE,
                                      result) ||
                imports.item_count != declared_imports) {
                module_result(result, PBC_MODULE_BAD_IMPORT_RECORD,
                              PBC_SECTION_IMPORTS, 0);
                return 0;
            }
            for (import_index = 0; import_index < imports.item_count;
                 import_index++) {
                const PBCU8 *record = imports.data +
                    import_index * PBC_IMPORT_RECORD_SIZE;
                PBCU16 import_symbol = mod_read_u16(record);
                PBCU16 import_string;
                const PBCU8 *import_name;
                PBCU16 import_name_size;
                PBCU32 prior;
                if (import_symbol >= symbols.item_count ||
                    mod_read_u16(record + 2) != 0) {
                    module_result(result, PBC_MODULE_BAD_IMPORT_RECORD,
                                  PBC_SECTION_IMPORTS, import_index);
                    return 0;
                }
                import_string = mod_read_u16(
                    symbols.data + (PBCU32)import_symbol *
                    PBC_SYMBOL_RECORD_SIZE);
                if (!string_at(strings, import_string, &import_name,
                               &import_name_size) ||
                    !valid_module_name(import_name, import_name_size)) {
                    module_result(result, PBC_MODULE_BAD_IMPORT_RECORD,
                                  PBC_SECTION_IMPORTS, import_index);
                    return 0;
                }
                for (prior = 0; prior < import_index; prior++) {
                    if (mod_read_u16(imports.data +
                                     prior * PBC_IMPORT_RECORD_SIZE) ==
                        mod_read_u16(record)) {
                        module_result(result, PBC_MODULE_BAD_IMPORT_RECORD,
                                      PBC_SECTION_IMPORTS, import_index);
                        return 0;
                    }
                }
            }
        } else if (declared_imports != 0) {
            module_result(result, PBC_MODULE_BAD_IMPORT_RECORD,
                          PBC_SECTION_IMPORTS, 0);
            return 0;
        }
    } else if (pbc_find_section(data, size, PBC_SECTION_IMPORTS, &imports)) {
        module_result(result, PBC_MODULE_BAD_MODULE_RECORD,
                      PBC_SECTION_MODULE, 0);
        return 0;
    }

    for (i = 0; i < functions.item_count; i++) {
        const PBCU8 *record = functions.data + i * PBC_FUNCTION_RECORD_SIZE;
        PBCU16 flags = mod_read_u16(record + 2);
        PBCU32 code_offset = mod_read_u32(record + 4);
        PBCU32 code_size = mod_read_u32(record + 8);
        PBCU16 args = mod_read_u16(record + 12);
        PBCU16 locals = mod_read_u16(record + 14);
        PBCU16 max_stack = mod_read_u16(record + 16);
        PBCU16 first_exception = mod_read_u16(record + 18);
        PBCU16 exception_count = mod_read_u16(record + 20);
        PBCU16 closure_count = mod_read_u16(record + 24);
        PBCCodeLimits limits;
        PBCCodeVerifyResult code_result;
        PBCCodeHandler *code_handlers = 0;
        PBCU32 j;

        if (mod_read_u16(record) >= symbols.item_count) {
            module_result(result, PBC_MODULE_BAD_FUNCTION_NAME,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        if ((flags & ~(PBCU16)(PBC_FUNC_GENERATOR | PBC_FUNC_COROUTINE |
                               PBC_FUNC_MODULE_INIT)) != 0 ||
            ((flags & PBC_FUNC_GENERATOR) != 0 &&
             (flags & PBC_FUNC_COROUTINE) != 0)) {
            module_result(result, PBC_MODULE_BAD_FUNCTION_FLAGS,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        if ((flags & PBC_FUNC_MODULE_INIT) != 0 && ++initializer_count > 1) {
            module_result(result, PBC_MODULE_MULTIPLE_INITIALIZERS,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        if (args > locals) {
            module_result(result, PBC_MODULE_BAD_FUNCTION_LOCALS,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        if (mod_read_u16(record + 22) != 0xffffU ||
            mod_read_u16(record + 26) != 0) {
            module_result(result, PBC_MODULE_BAD_FUNCTION_SIGNATURE,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        if (code_size == 0 || code_size > 65535U ||
            code_offset > code.size || code_size > code.size - code_offset) {
            module_result(result, PBC_MODULE_BAD_CODE_RANGE,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        for (j = 0; j < i; j++) {
            const PBCU8 *prior = functions.data +
                                 j * PBC_FUNCTION_RECORD_SIZE;
            PBCU32 prior_start = mod_read_u32(prior + 4);
            PBCU32 prior_size = mod_read_u32(prior + 8);
            if (code_offset < prior_start + prior_size &&
                prior_start < code_offset + code_size) {
                module_result(result, PBC_MODULE_OVERLAPPING_CODE,
                              PBC_SECTION_FUNCTIONS, i);
                return 0;
            }
        }
        if ((PBCU32)first_exception > exceptions.item_count ||
            (PBCU32)exception_count >
                exceptions.item_count - (PBCU32)first_exception) {
            module_result(result, PBC_MODULE_BAD_EXCEPTION_RANGE,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        for (j = 0; j < exception_count; j++) {
            PBCU32 index = (PBCU32)first_exception + j;
            if (!verify_exception(exceptions.data +
                                  index * PBC_EXCEPTION_RECORD_SIZE,
                                  index, (PBCU16)code_size, max_stack,
                                  symbols.item_count, result))
                return 0;
        }

        if (exception_count != 0) {
            code_handlers = (PBCCodeHandler *)malloc(
                (size_t)exception_count * sizeof(PBCCodeHandler));
            if (code_handlers == 0) {
                module_result(result, PBC_MODULE_INVALID_CODE,
                              PBC_SECTION_FUNCTIONS, i);
                if (result != 0)
                    result->code_error.error = PBC_CODE_OUT_OF_MEMORY;
                return 0;
            }
            for (j = 0; j < exception_count; j++) {
                const PBCU8 *handler = exceptions.data +
                    ((PBCU32)first_exception + j) *
                    PBC_EXCEPTION_RECORD_SIZE;
                code_handlers[j].start_offset = mod_read_u16(handler);
                code_handlers[j].end_offset = mod_read_u16(handler + 2);
                code_handlers[j].handler_offset = mod_read_u16(handler + 4);
                code_handlers[j].stack_depth = mod_read_u16(handler + 6);
            }
        }

        memset(&limits, 0, sizeof(limits));
        limits.constant_count = (PBCU16)constants.item_count;
        limits.local_count = locals;
        limits.global_count = (PBCU16)symbols.item_count;
        limits.symbol_count = (PBCU16)symbols.item_count;
        limits.function_count = (PBCU16)functions.item_count;
        limits.closure_count = closure_count;
        limits.max_stack = max_stack;
        limits.max_call_args = 255;
        limits.max_collection_items = 255;
        if (!pbc_verify_code_with_handlers(code.data + code_offset, code_size,
                                           &limits, code_handlers,
                                           exception_count, &code_result)) {
            free(code_handlers);
            module_result(result, PBC_MODULE_INVALID_CODE,
                          PBC_SECTION_FUNCTIONS, i);
            if (result != 0) result->code_error = code_result;
            return 0;
        }
        if (code_contains_suspension(code.data + code_offset, code_size) &&
            (flags & (PBC_FUNC_GENERATOR | PBC_FUNC_COROUTINE)) == 0) {
            free(code_handlers);
            module_result(result, PBC_MODULE_BAD_SUSPENSION,
                          PBC_SECTION_FUNCTIONS, i);
            return 0;
        }
        free(code_handlers);
    }
    return 1;
}

int pbc_module_name(const PBCU8 *data, PBCU32 size,
                    const PBCU8 **name, PBCU16 *name_size)
{
    PBCModuleVerifyResult verification;
    PBCSectionView module;
    PBCSectionView symbols;
    PBCSectionView strings;
    PBCU16 symbol_index;
    PBCU16 string_index;
    if (name == 0 || name_size == 0 ||
        !pbc_verify_module(data, size, &verification) ||
        !pbc_find_section(data, size, PBC_SECTION_MODULE, &module) ||
        !pbc_find_section(data, size, PBC_SECTION_SYMBOLS, &symbols) ||
        !pbc_find_section(data, size, PBC_SECTION_STRINGS, &strings))
        return 0;
    symbol_index = mod_read_u16(module.data);
    if (symbol_index >= symbols.item_count) return 0;
    string_index = mod_read_u16(
        symbols.data + (PBCU32)symbol_index * PBC_SYMBOL_RECORD_SIZE);
    return string_at(strings, string_index, name, name_size);
}

PBCModuleBuilder::PBCModuleBuilder()
{
    memset(&strings, 0, sizeof(strings));
    memset(&constants, 0, sizeof(constants));
    memset(&symbols, 0, sizeof(symbols));
    memset(&functions, 0, sizeof(functions));
    memset(&code, 0, sizeof(code));
    memset(&exceptions, 0, sizeof(exceptions));
    memset(&imports, 0, sizeof(imports));
    memset(module_record, 0, sizeof(module_record));
    string_count = 0;
    constant_count = 0;
    symbol_count = 0;
    function_count = 0;
    exception_count = 0;
    import_count = 0;
    has_module = 0;
    last_error = 0;
}

PBCModuleBuilder::~PBCModuleBuilder()
{
    free(strings.data);
    free(constants.data);
    free(symbols.data);
    free(functions.data);
    free(code.data);
    free(exceptions.data);
    free(imports.data);
}

int PBCModuleBuilder::append(Buffer &buffer, const void *bytes,
                             PBCU32 byte_count)
{
    PBCU32 required;
    PBCU32 capacity;
    PBCU8 *replacement;
    if (byte_count == 0) return 1;
    if (bytes == 0 || buffer.size > (PBCU32)0xffffffffU - byte_count) {
        last_error = "module buffer size overflow";
        return 0;
    }
    required = buffer.size + byte_count;
    if (required > buffer.capacity) {
        capacity = buffer.capacity == 0 ? 64U : buffer.capacity;
        while (capacity < required) {
            if (capacity > (PBCU32)0x7fffffffU) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        replacement = (PBCU8 *)realloc(buffer.data, (size_t)capacity);
        if (replacement == 0) {
            last_error = "cannot grow module buffer";
            return 0;
        }
        buffer.data = replacement;
        buffer.capacity = capacity;
    }
    memcpy(buffer.data + buffer.size, bytes, (size_t)byte_count);
    buffer.size = required;
    return 1;
}

int PBCModuleBuilder::append_u16(Buffer &buffer, PBCU16 value)
{
    PBCU8 bytes[2];
    mod_write_u16(bytes, value);
    return append(buffer, bytes, 2);
}

int PBCModuleBuilder::append_u32(Buffer &buffer, PBCU32 value)
{
    PBCU8 bytes[4];
    mod_write_u32(bytes, value);
    return append(buffer, bytes, 4);
}

long PBCModuleBuilder::add_string(const char *text)
{
    size_t length;
    if (text == 0) {
        last_error = "string is null";
        return -1;
    }
    length = strlen(text);
    if (length > 65535U) {
        last_error = "string exceeds 65535 bytes";
        return -1;
    }
    return add_string(text, (PBCU16)length);
}

long PBCModuleBuilder::add_string(const char *text, PBCU16 length)
{
    PBCU32 offset = 0;
    PBCU32 index = 0;
    if (text == 0 && length != 0) {
        last_error = "string is null";
        return -1;
    }
    while (offset < strings.size) {
        PBCU16 existing = mod_read_u16(strings.data + offset);
        offset += 2U;
        if (existing == length &&
            (length == 0 || memcmp(strings.data + offset, text, length) == 0))
            return (long)index;
        offset += existing;
        index++;
    }
    if (string_count >= 65535U) {
        last_error = "too many strings";
        return -1;
    }
    if (!append_u16(strings, length) ||
        !append(strings, text, (PBCU32)length))
        return -1;
    return (long)string_count++;
}

long PBCModuleBuilder::add_symbol(PBCU16 string_index, PBCU16 flags)
{
    PBCU8 record[PBC_SYMBOL_RECORD_SIZE];
    if (string_index >= string_count || flags != 0) {
        last_error = "invalid symbol";
        return -1;
    }
    if (symbol_count >= 65535U) {
        last_error = "too many symbols";
        return -1;
    }
    mod_write_u16(record, string_index);
    mod_write_u16(record + 2, flags);
    if (!append(symbols, record, sizeof(record))) return -1;
    return (long)symbol_count++;
}

int PBCModuleBuilder::set_module(PBCU16 name_symbol, PBCU16 flags)
{
    if (has_module || name_symbol >= symbol_count || flags != 0) {
        last_error = "invalid module identity";
        return 0;
    }
    memset(module_record, 0, sizeof(module_record));
    mod_write_u16(module_record, name_symbol);
    has_module = 1;
    return 1;
}

int PBCModuleBuilder::add_import(PBCU16 module_symbol, PBCU16 flags)
{
    PBCU8 record[PBC_IMPORT_RECORD_SIZE];
    PBCU32 i;
    if (!has_module || module_symbol >= symbol_count || flags != 0 ||
        import_count >= 65535U) {
        last_error = "invalid module import";
        return 0;
    }
    for (i = 0; i < import_count; i++) {
        if (mod_read_u16(imports.data + i * PBC_IMPORT_RECORD_SIZE) ==
            module_symbol) {
            last_error = "duplicate module import";
            return 0;
        }
    }
    mod_write_u16(record, module_symbol);
    mod_write_u16(record + 2, flags);
    if (!append(imports, record, sizeof(record))) return 0;
    import_count++;
    return 1;
}

long PBCModuleBuilder::add_constant(PBCU8 tag, const PBCU8 payload[8])
{
    PBCU8 record[PBC_CONSTANT_RECORD_SIZE];
    if (constant_count >= 65535U) {
        last_error = "too many constants";
        return -1;
    }
    memset(record, 0, sizeof(record));
    record[0] = tag;
    memcpy(record + 4, payload, 8);
    if (!append(constants, record, sizeof(record))) return -1;
    return (long)constant_count++;
}

long PBCModuleBuilder::add_constant_none()
{
    PBCU8 payload[8];
    memset(payload, 0, sizeof(payload));
    return add_constant(PBC_CONST_NONE, payload);
}

long PBCModuleBuilder::add_constant_bool(int value)
{
    PBCU8 payload[8];
    memset(payload, 0, sizeof(payload));
    payload[0] = value ? 1 : 0;
    return add_constant(PBC_CONST_BOOL, payload);
}

long PBCModuleBuilder::add_constant_int32(long value)
{
    PBCU8 payload[8];
    if (value < (-2147483647L - 1L) || value > 2147483647L) {
        last_error = "integer constant does not fit int32";
        return -1;
    }
    memset(payload, 0, sizeof(payload));
    mod_write_u32(payload, (PBCU32)value);
    return add_constant(PBC_CONST_INT32, payload);
}

long PBCModuleBuilder::add_constant_float64(double value)
{
    PBCU8 payload[8];
    PBCU8 native[8];
    PBCU8 one[8];
    static const PBCU8 ieee_one_le[8] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f
    };
    static const PBCU8 ieee_one_be[8] = {
        0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    double one_value = 1.0;
    PBCU16 i;
    if (sizeof(double) != 8 || FLT_RADIX != 2 ||
        DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024) {
        last_error = "host double is not IEEE-754 binary64";
        return -1;
    }
    memcpy(native, &value, 8);
    memcpy(one, &one_value, 8);
    if (memcmp(one, ieee_one_le, 8) == 0) {
        memcpy(payload, native, 8);
    } else if (memcmp(one, ieee_one_be, 8) == 0) {
        for (i = 0; i < 8; i++) payload[i] = native[7 - i];
    } else {
        last_error = "unsupported mixed-endian binary64 representation";
        return -1;
    }
    return add_constant(PBC_CONST_FLOAT64, payload);
}

long PBCModuleBuilder::add_constant_string(PBCU16 string_index)
{
    PBCU8 payload[8];
    if (string_index >= string_count) {
        last_error = "constant string index out of range";
        return -1;
    }
    memset(payload, 0, sizeof(payload));
    mod_write_u32(payload, string_index);
    return add_constant(PBC_CONST_STRING, payload);
}

long PBCModuleBuilder::add_function(const PBCFunctionSpec &spec,
                                    const PBCU8 *bytes,
                                    PBCU16 byte_count,
                                    const PBCExceptionSpec *handlers,
                                    PBCU16 handler_count)
{
    PBCU8 record[PBC_FUNCTION_RECORD_SIZE];
    PBCU16 i;
    PBCU32 code_offset = code.size;
    PBCU32 first_exception = exception_count;
    if (function_count >= 65535U || spec.name_symbol >= symbol_count ||
        byte_count == 0 || bytes == 0 ||
        (handler_count != 0 && handlers == 0) ||
        spec.arg_count > spec.local_count ||
        first_exception + handler_count > 65535U) {
        last_error = "invalid function definition";
        return -1;
    }
    if (spec.signature_index != 0xffffU) {
        last_error = "signatures are not materialized in PBC v1.0";
        return -1;
    }
    if (!append(code, bytes, byte_count)) return -1;
    for (i = 0; i < handler_count; i++) {
        PBCU8 exception[PBC_EXCEPTION_RECORD_SIZE];
        mod_write_u16(exception, handlers[i].start_offset);
        mod_write_u16(exception + 2, handlers[i].end_offset);
        mod_write_u16(exception + 4, handlers[i].handler_offset);
        mod_write_u16(exception + 6, handlers[i].stack_depth);
        mod_write_u16(exception + 8, handlers[i].match_symbol);
        mod_write_u16(exception + 10, handlers[i].flags);
        if (!append(exceptions, exception, sizeof(exception))) return -1;
        exception_count++;
    }
    memset(record, 0, sizeof(record));
    mod_write_u16(record, spec.name_symbol);
    mod_write_u16(record + 2, spec.flags);
    mod_write_u32(record + 4, code_offset);
    mod_write_u32(record + 8, byte_count);
    mod_write_u16(record + 12, spec.arg_count);
    mod_write_u16(record + 14, spec.local_count);
    mod_write_u16(record + 16, spec.max_stack);
    mod_write_u16(record + 18, (PBCU16)first_exception);
    mod_write_u16(record + 20, handler_count);
    mod_write_u16(record + 22, spec.signature_index);
    mod_write_u16(record + 24, spec.closure_count);
    mod_write_u16(record + 26, 0);
    if (!append(functions, record, sizeof(record))) return -1;
    return (long)function_count++;
}

int PBCModuleBuilder::write(PBCWriter &writer)
{
    PBCModuleVerifyResult verification;
    last_error = 0;
    if (!writer.add_section(PBC_SECTION_STRINGS, 0, strings.data,
                            strings.size, string_count) ||
        !writer.add_section(PBC_SECTION_CONSTANTS, 0, constants.data,
                            constants.size, constant_count) ||
        !writer.add_section(PBC_SECTION_SYMBOLS, 0, symbols.data,
                            symbols.size, symbol_count) ||
        !writer.add_section(PBC_SECTION_FUNCTIONS, 0, functions.data,
                            functions.size, function_count) ||
        !writer.add_section(PBC_SECTION_CODE, 0, code.data,
                            code.size, code.size)) {
        last_error = writer.error();
        return 0;
    }
    if (exception_count != 0 &&
        !writer.add_section(PBC_SECTION_EXCEPTIONS, 0, exceptions.data,
                            exceptions.size, exception_count)) {
        last_error = writer.error();
        return 0;
    }
    if (has_module) {
        mod_write_u16(module_record + 4, (PBCU16)import_count);
        if (!writer.add_section(PBC_SECTION_MODULE, 0, module_record,
                                sizeof(module_record), 1) ||
            (import_count != 0 &&
             !writer.add_section(PBC_SECTION_IMPORTS, 0, imports.data,
                                 imports.size, import_count))) {
            last_error = writer.error();
            return 0;
        }
    }
    if (!writer.finalize()) {
        last_error = writer.error();
        return 0;
    }
    if (!pbc_verify_module(writer.data(), writer.size(), &verification)) {
        last_error = pbc_module_verify_error_name(verification.error);
        return 0;
    }
    return 1;
}

const char *PBCModuleBuilder::error() const
{
    return last_error;
}
