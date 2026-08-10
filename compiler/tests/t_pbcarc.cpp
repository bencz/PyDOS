#include "../pbcarc.h"
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

static PBCU32 get_u32(const PBCU8 *p)
{
    return (PBCU32)p[0] | ((PBCU32)p[1] << 8) |
           ((PBCU32)p[2] << 16) | ((PBCU32)p[3] << 24);
}

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

static int build_module(const char *module_name, long constant,
                        PBCWriter &writer)
{
    const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_RETURN_VALUE
    };
    PBCModuleBuilder module;
    PBCFunctionSpec function;
    long string_index = module.add_string(module_name);
    long symbol_index = module.add_symbol((PBCU16)string_index, 0);
    if (string_index < 0 || symbol_index < 0 ||
        module.add_constant_int32(constant) < 0)
        return 0;
    memset(&function, 0, sizeof(function));
    function.name_symbol = (PBCU16)symbol_index;
    function.max_stack = 1;
    function.signature_index = 0xffffU;
    return module.set_module((PBCU16)symbol_index, 0) &&
           module.add_function(function, code, sizeof(code), 0, 0) == 0 &&
           module.write(writer);
}

static PBCU8 *copy_archive(const PYLArchiveBuilder &archive)
{
    PBCU8 *copy = (PBCU8 *)malloc((size_t)archive.size());
    CHECK(copy != 0);
    if (copy != 0)
        memcpy(copy, archive.data(), (size_t)archive.size());
    return copy;
}

static void expect_error(PBCU8 *data, PBCU32 size,
                         PYLVerifyError expected)
{
    PYLVerifyResult result;
    CHECK(!pyl_verify_archive(data, size, &result));
    if (result.error != expected) {
        fprintf(stderr, "expected %s, got %s for module %u\n",
                pyl_verify_error_name(expected),
                pyl_verify_error_name(result.error),
                (unsigned)result.module_index);
        failures++;
    }
}

static void test_round_trip_lookup_and_determinism()
{
    PBCWriter alpha;
    PBCWriter beta;
    PYLArchiveBuilder first;
    PYLArchiveBuilder second;
    PYLVerifyResult result;
    PYLModuleView view;
    CHECK(build_module("pkg.alpha", 1, alpha));
    CHECK(build_module("pkg.beta", 2, beta));
    first.set_flags(PYL_ARCHIVE_STDLIB);
    second.set_flags(PYL_ARCHIVE_STDLIB);
    CHECK(first.add_module("pkg.beta", beta.data(), beta.size(), 0));
    CHECK(first.add_module("pkg.alpha", alpha.data(), alpha.size(),
                           PYL_MODULE_PACKAGE));
    CHECK(second.add_module("pkg.alpha", alpha.data(), alpha.size(),
                            PYL_MODULE_PACKAGE));
    CHECK(second.add_module("pkg.beta", beta.data(), beta.size(), 0));
    CHECK(first.finalize());
    CHECK(second.finalize());
    CHECK(first.size() == second.size());
    CHECK(memcmp(first.data(), second.data(), (size_t)first.size()) == 0);
    CHECK(pyl_verify_archive(first.data(), first.size(), &result));
    CHECK(result.error == PYL_VERIFY_OK);
    CHECK(pyl_find_module(first.data(), first.size(), "pkg.alpha", &view));
    CHECK(view.flags == PYL_MODULE_PACKAGE);
    CHECK(view.size == alpha.size());
    CHECK(memcmp(view.data, alpha.data(), (size_t)alpha.size()) == 0);
    CHECK(!pyl_find_module(first.data(), first.size(), "pkg.missing", &view));
}

static void test_corruption_is_rejected()
{
    PBCWriter alpha;
    PBCWriter beta;
    PYLArchiveBuilder archive;
    PBCU8 *copy;
    PBCU8 *first;
    PBCU8 *second;
    PBCU32 module_offset;
    PBCU32 module_size;

    CHECK(build_module("alpha", 1, alpha));
    CHECK(build_module("beta", 2, beta));
    CHECK(archive.add_module("alpha", alpha.data(), alpha.size(), 0));
    CHECK(archive.add_module("beta", beta.data(), beta.size(), 0));
    CHECK(archive.finalize());
    first = (PBCU8 *)archive.data() + PYL_HEADER_SIZE;
    second = first + PYL_MODULE_ENTRY_SIZE;

    copy = copy_archive(archive);
    copy[0] = 'X';
    expect_error(copy, archive.size(), PYL_VERIFY_BAD_MAGIC);
    free(copy);

    copy = copy_archive(archive);
    put_u32(copy + 12, 0x80000000UL);
    expect_error(copy, archive.size(), PYL_VERIFY_BAD_FLAGS);
    free(copy);

    copy = copy_archive(archive);
    put_u16(copy + 18, 1);
    expect_error(copy, archive.size(), PYL_VERIFY_BAD_RESERVED);
    free(copy);

    copy = copy_archive(archive);
    put_u16(copy + PYL_HEADER_SIZE + 6, 0x8000U);
    expect_error(copy, archive.size(), PYL_VERIFY_BAD_MODULE_FLAGS);
    free(copy);

    copy = copy_archive(archive);
    copy[get_u32(copy + PYL_HEADER_SIZE)] = '.';
    expect_error(copy, archive.size(), PYL_VERIFY_BAD_MODULE_NAME);
    free(copy);

    copy = copy_archive(archive);
    put_u32(copy + PYL_HEADER_SIZE,
            get_u32(copy + PYL_HEADER_SIZE) + 1U);
    expect_error(copy, archive.size(), PYL_VERIFY_NONCANONICAL_NAMES);
    free(copy);

    copy = copy_archive(archive);
    memcpy(copy + get_u32(copy + PYL_HEADER_SIZE), "abbba", 5);
    expect_error(copy, archive.size(), PYL_VERIFY_MODULE_NAME_MISMATCH);
    free(copy);

    copy = copy_archive(archive);
    memcpy(copy + get_u32(copy + PYL_HEADER_SIZE +
                          PYL_MODULE_ENTRY_SIZE), "alph", 4);
    expect_error(copy, archive.size(), PYL_VERIFY_UNSORTED_MODULES);
    free(copy);

    copy = copy_archive(archive);
    module_offset = get_u32(first + 8);
    copy[module_offset] ^= 1U;
    expect_error(copy, archive.size(), PYL_VERIFY_BAD_CHECKSUM);
    free(copy);

    copy = copy_archive(archive);
    module_offset = get_u32(copy + PYL_HEADER_SIZE + 8);
    module_size = get_u32(copy + PYL_HEADER_SIZE + 12);
    copy[module_offset] = 'X';
    put_u32(copy + PYL_HEADER_SIZE + 16,
            pyl_checksum(copy + module_offset, module_size));
    expect_error(copy, archive.size(), PYL_VERIFY_INVALID_PBC);
    free(copy);

    copy = copy_archive(archive);
    put_u32(copy + PYL_HEADER_SIZE + PYL_MODULE_ENTRY_SIZE + 8,
            get_u32(copy + PYL_HEADER_SIZE + 8));
    expect_error(copy, archive.size(), PYL_VERIFY_OVERLAPPING_MODULES);
    free(copy);

    (void)second;
}

static void test_builder_rejects_bad_inputs()
{
    PBCWriter module;
    PYLArchiveBuilder archive;
    const PBCU8 invalid[] = { 0, 1, 2, 3 };
    CHECK(build_module("valid", 1, module));
    CHECK(!archive.add_module("bad..name", module.data(), module.size(), 0));
    CHECK(!archive.add_module("valid", invalid, sizeof(invalid), 0));
    CHECK(archive.add_module("valid", module.data(), module.size(), 0));
    CHECK(!archive.add_module("valid", module.data(), module.size(), 0));
}

int main()
{
    test_round_trip_lookup_and_determinism();
    test_corruption_is_rejected();
    test_builder_rejects_bad_inputs();
    if (failures != 0) {
        fprintf(stderr, "%d PYL archive test failure(s)\n", failures);
        return 1;
    }
    printf("PYL archive tests passed\n");
    return 0;
}
