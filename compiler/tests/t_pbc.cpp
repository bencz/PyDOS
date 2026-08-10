#include "../pbc.h"
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
    p[0] = (PBCU8)(value & 0xFFU);
    p[1] = (PBCU8)((value >> 8) & 0xFFU);
}

static void put_u32(PBCU8 *p, PBCU32 value)
{
    p[0] = (PBCU8)(value & 0xFFU);
    p[1] = (PBCU8)((value >> 8) & 0xFFU);
    p[2] = (PBCU8)((value >> 16) & 0xFFU);
    p[3] = (PBCU8)((value >> 24) & 0xFFU);
}

static PBCU32 get_u32(const PBCU8 *p)
{
    return (PBCU32)p[0] | ((PBCU32)p[1] << 8) |
           ((PBCU32)p[2] << 16) | ((PBCU32)p[3] << 24);
}

static PBCU8 *copy_output(const PBCWriter &writer)
{
    PBCU8 *copy = (PBCU8 *)malloc((size_t)writer.size());
    CHECK(copy != 0);
    if (copy != 0)
        memcpy(copy, writer.data(), (size_t)writer.size());
    return copy;
}

static void test_writer_and_determinism()
{
    static const PBCU8 strings[] = { 0, 'm', 'a', 'i', 'n', 0 };
    static const PBCU8 code[] = { 1, 0, 2, 0, 3 };
    PBCWriter first;
    PBCWriter second;
    PBCVerifyResult result;

    CHECK(first.add_section(PBC_SECTION_STRINGS, 0,
                            strings, sizeof(strings), 2));
    CHECK(first.add_section(PBC_SECTION_CODE, 0,
                            code, sizeof(code), 3));
    CHECK(first.finalize());
    CHECK(first.data() != 0);
    CHECK(pbc_verify_container(first.data(), first.size(), &result));
    CHECK(result.error == PBC_VERIFY_OK);

    CHECK(second.add_section(PBC_SECTION_STRINGS, 0,
                             strings, sizeof(strings), 2));
    CHECK(second.add_section(PBC_SECTION_CODE, 0,
                             code, sizeof(code), 3));
    CHECK(second.finalize());
    CHECK(first.size() == second.size());
    CHECK(memcmp(first.data(), second.data(), (size_t)first.size()) == 0);

    CHECK(!first.add_section(PBC_SECTION_CODE, 0, code, sizeof(code), 3));
    CHECK(strcmp(first.error(), "duplicate section") == 0);
}

static void test_header_failures(const PBCWriter &writer)
{
    PBCVerifyResult result;
    PBCU8 *copy;

    CHECK(!pbc_verify_container(0, 0, &result));
    CHECK(result.error == PBC_VERIFY_NULL_INPUT);
    CHECK(!pbc_verify_container(writer.data(), 4, &result));
    CHECK(result.error == PBC_VERIFY_TRUNCATED_HEADER);

    copy = copy_output(writer);
    if (copy == 0) return;
    copy[0] = 'X';
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_BAD_MAGIC);
    free(copy);

    copy = copy_output(writer);
    if (copy == 0) return;
    put_u16(copy + 4, PBC_VERSION_MAJOR + 1);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_BAD_VERSION);
    free(copy);

    copy = copy_output(writer);
    if (copy == 0) return;
    put_u32(copy + 20, writer.size() - 1);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_BAD_FILE_SIZE);
    free(copy);
}

static void test_section_failures(const PBCWriter &writer)
{
    PBCVerifyResult result;
    PBCU8 *copy;
    PBCU8 *first_entry;
    PBCU8 *second_entry;
    PBCU32 first_offset;

    copy = copy_output(writer);
    if (copy == 0) return;
    first_entry = copy + PBC_HEADER_SIZE;
    second_entry = first_entry + PBC_SECTION_ENTRY_SIZE;
    first_offset = get_u32(first_entry + 4);
    put_u16(second_entry, PBC_SECTION_STRINGS);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_DUPLICATE_SECTION);
    free(copy);

    copy = copy_output(writer);
    if (copy == 0) return;
    first_entry = copy + PBC_HEADER_SIZE;
    second_entry = first_entry + PBC_SECTION_ENTRY_SIZE;
    first_offset = get_u32(first_entry + 4);
    put_u32(second_entry + 4, first_offset);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_OVERLAPPING_SECTIONS);
    free(copy);

    copy = copy_output(writer);
    if (copy == 0) return;
    first_entry = copy + PBC_HEADER_SIZE;
    put_u32(first_entry + 4, writer.size() + 4);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_SECTION_OUT_OF_RANGE);
    free(copy);

    copy = copy_output(writer);
    if (copy == 0) return;
    first_entry = copy + PBC_HEADER_SIZE;
    put_u32(first_entry + 4, get_u32(first_entry + 4) + 1);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_UNALIGNED_SECTION);
    free(copy);

    copy = copy_output(writer);
    if (copy == 0) return;
    first_entry = copy + PBC_HEADER_SIZE;
    put_u16(first_entry, 0x7777);
    put_u16(first_entry + 2, 0);
    CHECK(!pbc_verify_container(copy, writer.size(), &result));
    CHECK(result.error == PBC_VERIFY_UNKNOWN_REQUIRED_SECTION);
    put_u16(first_entry + 2, PBC_SECTION_OPTIONAL);
    CHECK(pbc_verify_container(copy, writer.size(), &result));
    free(copy);
}

int main()
{
    static const PBCU8 strings[] = { 0, 'x', 0 };
    static const PBCU8 code[] = { 1, 2, 3, 4 };
    PBCWriter writer;

    test_writer_and_determinism();
    CHECK(writer.add_section(PBC_SECTION_STRINGS, 0,
                             strings, sizeof(strings), 2));
    CHECK(writer.add_section(PBC_SECTION_CODE, 0,
                             code, sizeof(code), 1));
    CHECK(writer.finalize());
    test_header_failures(writer);
    test_section_failures(writer);

    if (failures != 0) {
        fprintf(stderr, "%d PBC test failure(s)\n", failures);
        return 1;
    }
    printf("PBC container tests passed\n");
    return 0;
}
