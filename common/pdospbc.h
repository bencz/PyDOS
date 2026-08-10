/*
 * pdospbc.h - Shared wire constants for PyDOS portable bytecode
 *
 * This header is C89 and C++98 compatible.  It contains only stable wire
 * values shared by the compiler, verifier, loader and runtime VM.
 */

#ifndef PYDOS_PBC_FORMAT_H
#define PYDOS_PBC_FORMAT_H

#include <limits.h>

#if UINT_MAX >= 0xFFFFFFFFU
typedef unsigned int PBCU32;
#else
typedef unsigned long PBCU32;
#endif
typedef unsigned short PBCU16;
typedef unsigned char PBCU8;
typedef signed short PBCI16;

typedef char PBCU32_must_have_4_bytes[(sizeof(PBCU32) == 4) ? 1 : -1];
typedef char PBCU16_must_have_2_bytes[(sizeof(PBCU16) == 2) ? 1 : -1];
typedef char PBCI16_must_have_2_bytes[(sizeof(PBCI16) == 2) ? 1 : -1];

#define PBC_VERSION_MAJOR       1
#define PBC_VERSION_MINOR       0
#define PBC_HEADER_SIZE         24
#define PBC_SECTION_ENTRY_SIZE  16
#define PBC_MAX_SECTIONS        32
#define PBC_FUNCTION_RECORD_SIZE 28
#define PBC_SYMBOL_RECORD_SIZE    4
#define PBC_CONSTANT_RECORD_SIZE 12
#define PBC_EXCEPTION_RECORD_SIZE 12
#define PBC_MODULE_RECORD_SIZE      8
#define PBC_IMPORT_RECORD_SIZE      4

enum PBCSectionType {
    PBC_SECTION_STRINGS    = 1,
    PBC_SECTION_CONSTANTS  = 2,
    PBC_SECTION_SYMBOLS    = 3,
    PBC_SECTION_FUNCTIONS  = 4,
    PBC_SECTION_CODE       = 5,
    PBC_SECTION_EXCEPTIONS = 6,
    PBC_SECTION_CLASSES    = 7,
    PBC_SECTION_SIGNATURES = 8,
    PBC_SECTION_DEBUG      = 9,
    PBC_SECTION_CHECKSUM   = 10,
    PBC_SECTION_MODULE     = 11,
    PBC_SECTION_IMPORTS    = 12
};

enum PBCSectionFlags {
    PBC_SECTION_OPTIONAL = 1
};

enum PBCConstantTag {
    PBC_CONST_NONE = 0,
    PBC_CONST_BOOL = 1,
    PBC_CONST_INT32 = 2,
    PBC_CONST_FLOAT64 = 3,
    PBC_CONST_STRING = 4
};

enum PBCFunctionFlags {
    PBC_FUNC_GENERATOR = 1,
    PBC_FUNC_COROUTINE = 2,
    PBC_FUNC_MODULE_INIT = 4
};

enum PBCOpcode {
    PBC_OP_NOP = 0,
    PBC_OP_LOAD_NONE,
    PBC_OP_LOAD_TRUE,
    PBC_OP_LOAD_FALSE,
    PBC_OP_LOAD_CONST8,
    PBC_OP_LOAD_CONST16,
    PBC_OP_LOAD_LOCAL8,
    PBC_OP_LOAD_LOCAL16,
    PBC_OP_STORE_LOCAL8,
    PBC_OP_STORE_LOCAL16,
    PBC_OP_LOAD_GLOBAL16,
    PBC_OP_STORE_GLOBAL16,
    PBC_OP_POP_TOP,
    PBC_OP_DUP_TOP,
    PBC_OP_PY_ADD,
    PBC_OP_PY_SUB,
    PBC_OP_PY_MUL,
    PBC_OP_PY_TRUE_DIV,
    PBC_OP_PY_FLOOR_DIV,
    PBC_OP_PY_MOD,
    PBC_OP_PY_POW,
    PBC_OP_PY_NEG,
    PBC_OP_PY_POS,
    PBC_OP_PY_NOT,
    PBC_OP_PY_BIT_NOT,
    PBC_OP_CMP_EQ,
    PBC_OP_CMP_NE,
    PBC_OP_CMP_LT,
    PBC_OP_CMP_LE,
    PBC_OP_CMP_GT,
    PBC_OP_CMP_GE,
    PBC_OP_IS,
    PBC_OP_IS_NOT,
    PBC_OP_CONTAINS,
    PBC_OP_NOT_CONTAINS,
    PBC_OP_JUMP16,
    PBC_OP_JUMP_IF_TRUE16,
    PBC_OP_JUMP_IF_FALSE16,
    PBC_OP_CALL8,
    PBC_OP_RETURN_VALUE,
    PBC_OP_RETURN_NONE,
    PBC_OP_RAISE,
    PBC_OP_BUILD_LIST8,
    PBC_OP_BUILD_TUPLE8,
    PBC_OP_BUILD_SET8,
    PBC_OP_BUILD_DICT8,
    PBC_OP_GET_ITEM,
    PBC_OP_SET_ITEM,
    PBC_OP_GET_ATTR16,
    PBC_OP_SET_ATTR16,
    PBC_OP_GET_ITER,
    PBC_OP_FOR_ITER16,
    PBC_OP_YIELD_VALUE,
    PBC_OP_MAKE_FUNCTION16,
    PBC_OP_MAKE_CELL16,
    PBC_OP_LOAD_CELL16,
    PBC_OP_STORE_CELL16,
    PBC_OP_LOAD_DEREF16,
    PBC_OP_STORE_DEREF16,
    PBC_OP_CHECK_EXCEPTION16,
    PBC_OP_CLEAR_EXCEPTION,
    PBC_OP_RERAISE,
    PBC_OP_EXC_MATCH16,
    PBC_OP_COUNT
};

#endif /* PYDOS_PBC_FORMAT_H */
