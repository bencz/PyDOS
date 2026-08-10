#include "../pbcmod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures++; \
        } \
    } while (0)

static void put_u16(PBCU8 *p, PBCU16 value)
{
    p[0] = (PBCU8)(value & 0xffU);
    p[1] = (PBCU8)((value >> 8) & 0xffU);
}

static void put_u32(PBCU8 *p, PBCU32 value)
{
    p[0] = (PBCU8)(value & 0xffU);
    p[1] = (PBCU8)((value >> 8) & 0xffU);
    p[2] = (PBCU8)((value >> 16) & 0xffU);
    p[3] = (PBCU8)((value >> 24) & 0xffU);
}

static PBCU8 *copy_writer(const PBCWriter &writer)
{
    PBCU8 *copy = (PBCU8 *)malloc((size_t)writer.size());
    CHECK(copy != 0);
    if (copy != 0) memcpy(copy, writer.data(), (size_t)writer.size());
    return copy;
}

static int build_fixture(PBCModuleBuilder &module, PBCWriter &writer,
                         int second_function)
{
    const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    const PBCU8 helper_code[] = {
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_RETURN_VALUE
    };
    PBCExceptionSpec handler;
    PBCFunctionSpec function;
    long main_string = module.add_string("main");
    long duplicate = module.add_string("main");
    long helper_string = module.add_string("helper");
    long text_string = module.add_string("portable");
    long main_symbol;
    long helper_symbol;

    CHECK(main_string == duplicate);
    CHECK(main_string >= 0 && helper_string >= 0 && text_string >= 0);
    main_symbol = module.add_symbol((PBCU16)main_string, 0);
    helper_symbol = module.add_symbol((PBCU16)helper_string, 0);
    CHECK(main_symbol >= 0 && helper_symbol >= 0);
    CHECK(module.add_constant_none() == 0);
    CHECK(module.add_constant_string((PBCU16)text_string) == 1);
    CHECK(module.add_constant_bool(1) == 2);
    CHECK(module.add_constant_int32(-1234567L) == 3);
    CHECK(module.add_constant_float64(1.5) == 4);

    handler.start_offset = 0;
    handler.end_offset = 3;
    handler.handler_offset = 4;
    handler.stack_depth = 0;
    handler.match_symbol = 0xffffU;
    handler.flags = 0;
    function.name_symbol = (PBCU16)main_symbol;
    function.flags = PBC_FUNC_MODULE_INIT;
    function.arg_count = 0;
    function.local_count = 0;
    function.max_stack = 1;
    function.closure_count = 0;
    function.signature_index = 0xffffU;
    CHECK(module.add_function(function, code, sizeof(code), &handler, 1) == 0);

    if (second_function) {
        function.name_symbol = (PBCU16)helper_symbol;
        function.flags = 0;
        function.max_stack = 1;
        CHECK(module.add_function(function, helper_code,
                                  sizeof(helper_code), 0, 0) == 1);
    }
    return module.write(writer);
}

static void expect_module_error(PBCU8 *copy, PBCU32 size,
                                PBCModuleVerifyError expected)
{
    PBCModuleVerifyResult result;
    CHECK(!pbc_verify_module(copy, size, &result));
    CHECK(result.error == expected);
}

static PBCU8 *mutable_section(PBCU8 *copy, PBCU32 size, PBCU16 type)
{
    PBCSectionView view;
    CHECK(pbc_find_section(copy, size, type, &view));
    if (view.data == 0) return 0;
    return copy + (view.data - copy);
}

static void test_round_trip_and_determinism()
{
    PBCModuleBuilder first_module;
    PBCModuleBuilder second_module;
    PBCWriter first;
    PBCWriter second;
    PBCModuleVerifyResult result;
    PBCSectionView functions;
    PBCSectionView constants;

    CHECK(build_fixture(first_module, first, 1));
    CHECK(build_fixture(second_module, second, 1));
    CHECK(pbc_verify_module(first.data(), first.size(), &result));
    CHECK(result.error == PBC_MODULE_OK);
    CHECK(first.size() == second.size());
    CHECK(memcmp(first.data(), second.data(), (size_t)first.size()) == 0);
    CHECK(pbc_find_section(first.data(), first.size(),
                           PBC_SECTION_FUNCTIONS, &functions));
    CHECK(functions.item_count == 2);
    CHECK(functions.size == 2 * PBC_FUNCTION_RECORD_SIZE);
    CHECK(pbc_find_section(first.data(), first.size(),
                           PBC_SECTION_CONSTANTS, &constants));
    CHECK(constants.data[4 * PBC_CONSTANT_RECORD_SIZE + 4] == 0x00);
    CHECK(constants.data[4 * PBC_CONSTANT_RECORD_SIZE + 10] == 0xf8);
    CHECK(constants.data[4 * PBC_CONSTANT_RECORD_SIZE + 11] == 0x3f);
}

static void test_semantic_corruption()
{
    PBCModuleBuilder module;
    PBCWriter writer;
    PBCU8 *copy;
    PBCU8 *section;

    CHECK(build_fixture(module, writer, 1));

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_SYMBOLS);
    if (section != 0) put_u16(section, 0xffffU);
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_STRING_INDEX);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_CONSTANTS);
    if (section != 0) section[0] = 0xffU;
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_CONSTANT_TAG);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_FUNCTIONS);
    if (section != 0) put_u16(section, 0xffffU);
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_FUNCTION_NAME);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_CODE);
    if (section != 0) section[0] = 0xffU;
    expect_module_error(copy, writer.size(), PBC_MODULE_INVALID_CODE);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_EXCEPTIONS);
    if (section != 0) put_u16(section + 4, 1); /* instruction operand */
    expect_module_error(copy, writer.size(), PBC_MODULE_INVALID_CODE);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_FUNCTIONS);
    if (section != 0) {
        put_u32(section + PBC_FUNCTION_RECORD_SIZE + 4, 0);
        put_u32(section + PBC_FUNCTION_RECORD_SIZE + 8, 2);
    }
    expect_module_error(copy, writer.size(), PBC_MODULE_OVERLAPPING_CODE);
    free(copy);
}

static void test_directory_semantics()
{
    PBCModuleBuilder module;
    PBCWriter writer;
    PBCU8 *copy;
    PBCU16 section_count;
    PBCU16 i;

    CHECK(build_fixture(module, writer, 0));
    copy = copy_writer(writer);
    if (copy == 0) return;
    section_count = (PBCU16)(copy[16] | ((PBCU16)copy[17] << 8));
    for (i = 0; i < section_count; i++) {
        PBCU8 *entry = copy + PBC_HEADER_SIZE +
                       (PBCU32)i * PBC_SECTION_ENTRY_SIZE;
        PBCU16 type = (PBCU16)(entry[0] | ((PBCU16)entry[1] << 8));
        if (type == PBC_SECTION_CONSTANTS) {
            put_u16(entry, PBC_SECTION_CLASSES);
            break;
        }
    }
    expect_module_error(copy, writer.size(), PBC_MODULE_MISSING_SECTION);
    free(copy);

    copy = copy_writer(writer);
    if (copy == 0) return;
    for (i = 0; i < section_count; i++) {
        PBCU8 *entry = copy + PBC_HEADER_SIZE +
                       (PBCU32)i * PBC_SECTION_ENTRY_SIZE;
        PBCU16 type = (PBCU16)(entry[0] | ((PBCU16)entry[1] << 8));
        if (type == PBC_SECTION_CONSTANTS) {
            put_u32(entry + 12, 99);
            break;
        }
    }
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_SECTION_SIZE);
    free(copy);
}

static void test_suspension_requires_function_flag()
{
    const PBCU8 code[] = {
        PBC_OP_LOAD_NONE,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    PBCModuleBuilder module;
    PBCWriter writer;
    PBCFunctionSpec function;
    PBCU8 *copy;
    PBCU8 *records;
    long string_index;
    long symbol_index;

    string_index = module.add_string("generator");
    symbol_index = module.add_symbol((PBCU16)string_index, 0);
    memset(&function, 0, sizeof(function));
    function.name_symbol = (PBCU16)symbol_index;
    function.flags = PBC_FUNC_GENERATOR;
    function.max_stack = 1;
    function.signature_index = 0xffffU;
    CHECK(module.add_function(function, code, sizeof(code), 0, 0) == 0);
    CHECK(module.write(writer));

    copy = copy_writer(writer);
    records = mutable_section(copy, writer.size(), PBC_SECTION_FUNCTIONS);
    if (records != 0) put_u16(records + 2, 0);
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_SUSPENSION);
    free(copy);
}

static void test_module_identity_and_imports()
{
    const PBCU8 code[] = { PBC_OP_RETURN_NONE };
    PBCModuleBuilder module;
    PBCWriter writer;
    PBCFunctionSpec function;
    PBCModuleVerifyResult result;
    const PBCU8 *name;
    PBCU16 name_size;
    PBCU8 *copy;
    PBCU8 *section;
    long main_string = module.add_string("app.main");
    long dep_string = module.add_string("stdlib.abc");
    long main_symbol = module.add_symbol((PBCU16)main_string, 0);
    long dep_symbol = module.add_symbol((PBCU16)dep_string, 0);

    CHECK(module.set_module((PBCU16)main_symbol, 0));
    CHECK(module.add_import((PBCU16)dep_symbol, 0));
    CHECK(!module.add_import((PBCU16)dep_symbol, 0));
    memset(&function, 0, sizeof(function));
    function.name_symbol = (PBCU16)main_symbol;
    function.max_stack = 0;
    function.signature_index = 0xffffU;
    CHECK(module.add_function(function, code, sizeof(code), 0, 0) == 0);
    if (!module.write(writer)) {
        fprintf(stderr, "module identity fixture: %s\n", module.error());
        CHECK(0);
        return;
    }
    CHECK(pbc_verify_module(writer.data(), writer.size(), &result));
    CHECK(pbc_module_name(writer.data(), writer.size(), &name, &name_size));
    CHECK(name_size == strlen("app.main"));
    CHECK(memcmp(name, "app.main", name_size) == 0);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_MODULE);
    if (section != 0) put_u16(section + 4, 2);
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_IMPORT_RECORD);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_IMPORTS);
    if (section != 0) put_u16(section, 0xffffU);
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_IMPORT_RECORD);
    free(copy);

    copy = copy_writer(writer);
    section = mutable_section(copy, writer.size(), PBC_SECTION_STRINGS);
    if (section != 0) section[2 + 4] = '.';
    expect_module_error(copy, writer.size(), PBC_MODULE_BAD_MODULE_RECORD);
    free(copy);
}

int main()
{
    test_round_trip_and_determinism();
    test_semantic_corruption();
    test_directory_semantics();
    test_suspension_requires_function_flag();
    test_module_identity_and_imports();
    if (failures != 0) {
        fprintf(stderr, "%d PBC module test failure(s)\n", failures);
        return 1;
    }
    printf("PBC semantic module tests passed\n");
    return 0;
}
