/*
 * pbc.cpp - Portable PyDOS bytecode container
 */

#include "pbc.h"
#include <stdlib.h>
#include <string.h>

static PBCU16 read_u16(const PBCU8 *p)
{
    return (PBCU16)((PBCU16)p[0] | ((PBCU16)p[1] << 8));
}

static PBCU32 read_u32(const PBCU8 *p)
{
    return (PBCU32)p[0] |
           ((PBCU32)p[1] << 8) |
           ((PBCU32)p[2] << 16) |
           ((PBCU32)p[3] << 24);
}

static void write_u16(PBCU8 *p, PBCU16 value)
{
    p[0] = (PBCU8)(value & 0xFFU);
    p[1] = (PBCU8)((value >> 8) & 0xFFU);
}

static void write_u32(PBCU8 *p, PBCU32 value)
{
    p[0] = (PBCU8)(value & 0xFFU);
    p[1] = (PBCU8)((value >> 8) & 0xFFU);
    p[2] = (PBCU8)((value >> 16) & 0xFFU);
    p[3] = (PBCU8)((value >> 24) & 0xFFU);
}

static int known_section(PBCU16 type)
{
    return type >= PBC_SECTION_STRINGS && type <= PBC_SECTION_IMPORTS;
}

static PBCU32 align4(PBCU32 value)
{
    return (value + 3U) & ~(PBCU32)3U;
}

static void set_result(PBCVerifyResult *result, PBCVerifyError error,
                       PBCU16 section_index)
{
    if (result != 0) {
        result->error = error;
        result->section_index = section_index;
    }
}

const char *pbc_verify_error_name(PBCVerifyError error)
{
    switch (error) {
    case PBC_VERIFY_OK: return "ok";
    case PBC_VERIFY_NULL_INPUT: return "null input";
    case PBC_VERIFY_TRUNCATED_HEADER: return "truncated header";
    case PBC_VERIFY_BAD_MAGIC: return "bad magic";
    case PBC_VERIFY_BAD_VERSION: return "unsupported version";
    case PBC_VERIFY_BAD_HEADER_SIZE: return "bad header size";
    case PBC_VERIFY_BAD_ENTRY_SIZE: return "bad section entry size";
    case PBC_VERIFY_BAD_FILE_SIZE: return "file size mismatch";
    case PBC_VERIFY_TOO_MANY_SECTIONS: return "too many sections";
    case PBC_VERIFY_TRUNCATED_DIRECTORY: return "truncated section directory";
    case PBC_VERIFY_BAD_HEADER_FLAGS: return "unsupported header flags";
    case PBC_VERIFY_BAD_RESERVED_FIELD: return "nonzero reserved field";
    case PBC_VERIFY_BAD_SECTION_FLAGS: return "unsupported section flags";
    case PBC_VERIFY_UNKNOWN_REQUIRED_SECTION:
        return "unknown required section";
    case PBC_VERIFY_DUPLICATE_SECTION: return "duplicate section";
    case PBC_VERIFY_SECTION_BEFORE_DATA: return "section overlaps directory";
    case PBC_VERIFY_UNALIGNED_SECTION: return "unaligned section";
    case PBC_VERIFY_SECTION_OUT_OF_RANGE: return "section out of range";
    case PBC_VERIFY_OVERLAPPING_SECTIONS: return "overlapping sections";
    }
    return "unknown verifier error";
}

int pbc_verify_container(const PBCU8 *data, PBCU32 size,
                         PBCVerifyResult *result)
{
    PBCU16 section_count;
    PBCU32 directory_end;
    PBCU16 i;
    PBCU16 j;

    set_result(result, PBC_VERIFY_OK, 0);
    if (data == 0) {
        set_result(result, PBC_VERIFY_NULL_INPUT, 0);
        return 0;
    }
    if (size < PBC_HEADER_SIZE) {
        set_result(result, PBC_VERIFY_TRUNCATED_HEADER, 0);
        return 0;
    }
    if (data[0] != 'P' || data[1] != 'Y' ||
        data[2] != 'B' || data[3] != 'C') {
        set_result(result, PBC_VERIFY_BAD_MAGIC, 0);
        return 0;
    }
    if (read_u16(data + 4) != PBC_VERSION_MAJOR ||
        read_u16(data + 6) > PBC_VERSION_MINOR) {
        set_result(result, PBC_VERIFY_BAD_VERSION, 0);
        return 0;
    }
    if (read_u16(data + 8) != PBC_HEADER_SIZE) {
        set_result(result, PBC_VERIFY_BAD_HEADER_SIZE, 0);
        return 0;
    }
    if (read_u16(data + 10) != PBC_SECTION_ENTRY_SIZE) {
        set_result(result, PBC_VERIFY_BAD_ENTRY_SIZE, 0);
        return 0;
    }
    if (read_u32(data + 12) != 0) {
        set_result(result, PBC_VERIFY_BAD_HEADER_FLAGS, 0);
        return 0;
    }
    section_count = read_u16(data + 16);
    if (section_count > PBC_MAX_SECTIONS) {
        set_result(result, PBC_VERIFY_TOO_MANY_SECTIONS, 0);
        return 0;
    }
    if (read_u16(data + 18) != 0) {
        set_result(result, PBC_VERIFY_BAD_RESERVED_FIELD, 0);
        return 0;
    }
    if (read_u32(data + 20) != size) {
        set_result(result, PBC_VERIFY_BAD_FILE_SIZE, 0);
        return 0;
    }

    if ((PBCU32)section_count >
        (size - PBC_HEADER_SIZE) / PBC_SECTION_ENTRY_SIZE) {
        set_result(result, PBC_VERIFY_TRUNCATED_DIRECTORY, 0);
        return 0;
    }
    directory_end = PBC_HEADER_SIZE +
                    (PBCU32)section_count * PBC_SECTION_ENTRY_SIZE;

    for (i = 0; i < section_count; i++) {
        const PBCU8 *entry = data + PBC_HEADER_SIZE +
                             (PBCU32)i * PBC_SECTION_ENTRY_SIZE;
        PBCU16 type = read_u16(entry);
        PBCU16 flags = read_u16(entry + 2);
        PBCU32 offset = read_u32(entry + 4);
        PBCU32 section_size = read_u32(entry + 8);

        if ((flags & ~(PBCU16)PBC_SECTION_OPTIONAL) != 0) {
            set_result(result, PBC_VERIFY_BAD_SECTION_FLAGS, i);
            return 0;
        }
        if (!known_section(type) &&
            (flags & PBC_SECTION_OPTIONAL) == 0) {
            set_result(result, PBC_VERIFY_UNKNOWN_REQUIRED_SECTION, i);
            return 0;
        }
        for (j = 0; j < i; j++) {
            const PBCU8 *prior = data + PBC_HEADER_SIZE +
                                 (PBCU32)j * PBC_SECTION_ENTRY_SIZE;
            if (read_u16(prior) == type) {
                set_result(result, PBC_VERIFY_DUPLICATE_SECTION, i);
                return 0;
            }
        }
        if (offset < directory_end) {
            set_result(result, PBC_VERIFY_SECTION_BEFORE_DATA, i);
            return 0;
        }
        if ((offset & 3U) != 0) {
            set_result(result, PBC_VERIFY_UNALIGNED_SECTION, i);
            return 0;
        }
        if (offset > size || section_size > size - offset) {
            set_result(result, PBC_VERIFY_SECTION_OUT_OF_RANGE, i);
            return 0;
        }
    }

    for (i = 0; i < section_count; i++) {
        const PBCU8 *a = data + PBC_HEADER_SIZE +
                         (PBCU32)i * PBC_SECTION_ENTRY_SIZE;
        PBCU32 a_start = read_u32(a + 4);
        PBCU32 a_size = read_u32(a + 8);
        PBCU32 a_end = a_start + a_size;
        if (a_size == 0) continue;
        for (j = (PBCU16)(i + 1); j < section_count; j++) {
            const PBCU8 *b = data + PBC_HEADER_SIZE +
                             (PBCU32)j * PBC_SECTION_ENTRY_SIZE;
            PBCU32 b_start = read_u32(b + 4);
            PBCU32 b_size = read_u32(b + 8);
            PBCU32 b_end = b_start + b_size;
            if (b_size != 0 && a_start < b_end && b_start < a_end) {
                set_result(result, PBC_VERIFY_OVERLAPPING_SECTIONS, j);
                return 0;
            }
        }
    }

    return 1;
}

int pbc_find_section(const PBCU8 *data, PBCU32 size, PBCU16 type,
                     PBCSectionView *view)
{
    PBCU16 section_count;
    PBCU16 i;

    if (view != 0) {
        view->type = 0;
        view->flags = 0;
        view->data = 0;
        view->size = 0;
        view->item_count = 0;
    }
    if (data == 0 || size < PBC_HEADER_SIZE || view == 0) return 0;
    section_count = read_u16(data + 16);
    if (section_count > PBC_MAX_SECTIONS ||
        (PBCU32)section_count >
            (size - PBC_HEADER_SIZE) / PBC_SECTION_ENTRY_SIZE)
        return 0;

    for (i = 0; i < section_count; i++) {
        const PBCU8 *entry = data + PBC_HEADER_SIZE +
                             (PBCU32)i * PBC_SECTION_ENTRY_SIZE;
        PBCU32 offset;
        PBCU32 section_size;
        if (read_u16(entry) != type) continue;
        offset = read_u32(entry + 4);
        section_size = read_u32(entry + 8);
        if (offset > size || section_size > size - offset) return 0;
        view->type = type;
        view->flags = read_u16(entry + 2);
        view->data = data + offset;
        view->size = section_size;
        view->item_count = read_u32(entry + 12);
        return 1;
    }
    return 0;
}

PBCWriter::PBCWriter()
{
    PBCU16 i;
    section_count = 0;
    output = 0;
    output_size = 0;
    last_error = 0;
    for (i = 0; i < PBC_MAX_SECTIONS; i++) {
        sections[i].data = 0;
        sections[i].size = 0;
    }
}

PBCWriter::~PBCWriter()
{
    PBCU16 i;
    for (i = 0; i < section_count; i++) free(sections[i].data);
    free(output);
}

int PBCWriter::add_section(PBCU16 type, PBCU16 flags,
                           const void *bytes, PBCU32 byte_count,
                           PBCU32 item_count)
{
    PBCU16 i;
    PBCU8 *copy = 0;

    last_error = 0;
    if (!known_section(type)) {
        last_error = "writer requires a known section type";
        return 0;
    }
    if ((flags & ~(PBCU16)PBC_SECTION_OPTIONAL) != 0) {
        last_error = "unsupported section flags";
        return 0;
    }
    if (section_count >= PBC_MAX_SECTIONS) {
        last_error = "too many sections";
        return 0;
    }
    for (i = 0; i < section_count; i++) {
        if (sections[i].type == type) {
            last_error = "duplicate section";
            return 0;
        }
    }
    if (byte_count != 0 && bytes == 0) {
        last_error = "section data is null";
        return 0;
    }
    if (byte_count != 0) {
        copy = (PBCU8 *)malloc((size_t)byte_count);
        if (copy == 0) {
            last_error = "cannot allocate section copy";
            return 0;
        }
        memcpy(copy, bytes, (size_t)byte_count);
    }

    sections[section_count].type = type;
    sections[section_count].flags = flags;
    sections[section_count].data = copy;
    sections[section_count].size = byte_count;
    sections[section_count].item_count = item_count;
    section_count++;
    return 1;
}

int PBCWriter::finalize()
{
    PBCU16 i;
    PBCU32 cursor;
    PBCVerifyResult verification;

    free(output);
    output = 0;
    output_size = 0;
    last_error = 0;

    cursor = PBC_HEADER_SIZE +
             (PBCU32)section_count * PBC_SECTION_ENTRY_SIZE;
    cursor = align4(cursor);
    for (i = 0; i < section_count; i++) {
        if (sections[i].size > (PBCU32)0xFFFFFFFFU - cursor) {
            last_error = "container size overflow";
            return 0;
        }
        cursor += sections[i].size;
        if (i + 1 < section_count) {
            if (cursor > (PBCU32)0xFFFFFFFCU) {
                last_error = "container alignment overflow";
                return 0;
            }
            cursor = align4(cursor);
        }
    }

    output = (PBCU8 *)malloc((size_t)cursor);
    if (output == 0) {
        last_error = "cannot allocate container";
        return 0;
    }
    memset(output, 0, (size_t)cursor);
    output_size = cursor;

    output[0] = 'P';
    output[1] = 'Y';
    output[2] = 'B';
    output[3] = 'C';
    write_u16(output + 4, PBC_VERSION_MAJOR);
    write_u16(output + 6, PBC_VERSION_MINOR);
    write_u16(output + 8, PBC_HEADER_SIZE);
    write_u16(output + 10, PBC_SECTION_ENTRY_SIZE);
    write_u32(output + 12, 0);
    write_u16(output + 16, section_count);
    write_u16(output + 18, 0);
    write_u32(output + 20, output_size);

    cursor = align4(PBC_HEADER_SIZE +
                    (PBCU32)section_count * PBC_SECTION_ENTRY_SIZE);
    for (i = 0; i < section_count; i++) {
        PBCU8 *entry = output + PBC_HEADER_SIZE +
                       (PBCU32)i * PBC_SECTION_ENTRY_SIZE;
        write_u16(entry, sections[i].type);
        write_u16(entry + 2, sections[i].flags);
        write_u32(entry + 4, cursor);
        write_u32(entry + 8, sections[i].size);
        write_u32(entry + 12, sections[i].item_count);
        if (sections[i].size != 0)
            memcpy(output + cursor, sections[i].data,
                   (size_t)sections[i].size);
        cursor += sections[i].size;
        if (i + 1 < section_count) cursor = align4(cursor);
    }

    if (!pbc_verify_container(output, output_size, &verification)) {
        last_error = pbc_verify_error_name(verification.error);
        free(output);
        output = 0;
        output_size = 0;
        return 0;
    }
    return 1;
}

const PBCU8 *PBCWriter::data() const
{
    return output;
}

PBCU32 PBCWriter::size() const
{
    return output_size;
}

const char *PBCWriter::error() const
{
    return last_error;
}
