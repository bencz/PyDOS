/* pbcarc.cpp - Deterministic archive of verified PBC modules. */

#include "pbcarc.h"
#include <stdlib.h>
#include <string.h>

static PBCU16 pyl_u16(const PBCU8 *p)
{
    return (PBCU16)((PBCU16)p[0] | ((PBCU16)p[1] << 8));
}

static PBCU32 pyl_u32(const PBCU8 *p)
{
    return (PBCU32)p[0] | ((PBCU32)p[1] << 8) |
           ((PBCU32)p[2] << 16) | ((PBCU32)p[3] << 24);
}

static void pyl_put_u16(PBCU8 *p, PBCU16 value)
{
    p[0] = (PBCU8)(value & 0xffU);
    p[1] = (PBCU8)((value >> 8) & 0xffU);
}

static void pyl_put_u32(PBCU8 *p, PBCU32 value)
{
    p[0] = (PBCU8)(value & 0xffU);
    p[1] = (PBCU8)((value >> 8) & 0xffU);
    p[2] = (PBCU8)((value >> 16) & 0xffU);
    p[3] = (PBCU8)((value >> 24) & 0xffU);
}

static int pyl_add(PBCU32 left, PBCU32 right, PBCU32 *sum)
{
    if (left > (PBCU32)0xffffffffU - right) return 0;
    *sum = left + right;
    return 1;
}

static int pyl_mul(PBCU32 left, PBCU32 right, PBCU32 *product)
{
    if (left != 0 && right > (PBCU32)0xffffffffU / left) return 0;
    *product = left * right;
    return 1;
}

static int pyl_align4(PBCU32 value, PBCU32 *aligned)
{
    PBCU32 sum;
    if (!pyl_add(value, 3U, &sum)) return 0;
    *aligned = sum & ~(PBCU32)3U;
    return 1;
}

static void pyl_result(PYLVerifyResult *result, PYLVerifyError error,
                       PBCU16 module_index)
{
    if (result == 0) return;
    memset(result, 0, sizeof(*result));
    result->error = error;
    result->module_index = module_index;
}

static int pyl_name_char(PBCU8 value, int first)
{
    if (value == '_') return 1;
    if (value >= 'A' && value <= 'Z') return 1;
    if (value >= 'a' && value <= 'z') return 1;
    if (!first && value >= '0' && value <= '9') return 1;
    return 0;
}

static int pyl_valid_name(const PBCU8 *name, PBCU16 size)
{
    PBCU16 i;
    int first = 1;
    if (name == 0 || size == 0) return 0;
    for (i = 0; i < size; i++) {
        if (name[i] == '.') {
            if (first || i + 1U == size) return 0;
            first = 1;
        } else {
            if (!pyl_name_char(name[i], first)) return 0;
            first = 0;
        }
    }
    return !first;
}

static int pyl_name_compare(const PBCU8 *left, PBCU16 left_size,
                            const PBCU8 *right, PBCU16 right_size)
{
    PBCU16 common = left_size < right_size ? left_size : right_size;
    int comparison = common != 0 ? memcmp(left, right, common) : 0;
    if (comparison != 0) return comparison;
    if (left_size < right_size) return -1;
    if (left_size > right_size) return 1;
    return 0;
}

const char *pyl_verify_error_name(PYLVerifyError error)
{
    switch (error) {
    case PYL_VERIFY_OK: return "ok";
    case PYL_VERIFY_NULL_INPUT: return "null archive";
    case PYL_VERIFY_TRUNCATED_HEADER: return "truncated archive header";
    case PYL_VERIFY_BAD_MAGIC: return "bad archive magic";
    case PYL_VERIFY_BAD_VERSION: return "unsupported archive version";
    case PYL_VERIFY_BAD_HEADER_SIZE: return "bad archive header size";
    case PYL_VERIFY_BAD_ENTRY_SIZE: return "bad module entry size";
    case PYL_VERIFY_BAD_FILE_SIZE: return "archive file size mismatch";
    case PYL_VERIFY_BAD_FLAGS: return "unsupported archive flags";
    case PYL_VERIFY_BAD_RESERVED: return "nonzero archive reserved field";
    case PYL_VERIFY_TOO_MANY_MODULES: return "too many archived modules";
    case PYL_VERIFY_TRUNCATED_DIRECTORY: return "truncated module directory";
    case PYL_VERIFY_BAD_MODULE_FLAGS: return "unsupported module flags";
    case PYL_VERIFY_BAD_MODULE_NAME: return "invalid module name";
    case PYL_VERIFY_NONCANONICAL_NAMES:
        return "noncanonical module name layout";
    case PYL_VERIFY_UNSORTED_MODULES: return "unsorted or duplicate modules";
    case PYL_VERIFY_MODULE_OUT_OF_RANGE: return "module data out of range";
    case PYL_VERIFY_UNALIGNED_MODULE: return "unaligned module data";
    case PYL_VERIFY_OVERLAPPING_MODULES: return "overlapping module data";
    case PYL_VERIFY_BAD_CHECKSUM: return "module checksum mismatch";
    case PYL_VERIFY_INVALID_PBC: return "invalid archived PBC module";
    case PYL_VERIFY_MODULE_NAME_MISMATCH:
        return "archive and PBC module names differ";
    }
    return "unknown archive verifier error";
}

PBCU32 pyl_checksum(const PBCU8 *data, PBCU32 size)
{
    PBCU32 hash = (PBCU32)2166136261UL;
    PBCU32 i;
    if (data == 0) return 0;
    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= (PBCU32)16777619UL;
    }
    return hash;
}

int pyl_verify_archive(const PBCU8 *data, PBCU32 size,
                       PYLVerifyResult *result)
{
    PBCU16 count;
    PBCU32 directory_offset;
    PBCU32 directory_size;
    PBCU32 directory_end;
    PBCU32 module_data_offset;
    PBCU32 previous_name_end;
    PBCU32 previous_module_end;
    PBCU16 i;

    pyl_result(result, PYL_VERIFY_OK, 0);
    if (data == 0) {
        pyl_result(result, PYL_VERIFY_NULL_INPUT, 0);
        return 0;
    }
    if (size < PYL_HEADER_SIZE) {
        pyl_result(result, PYL_VERIFY_TRUNCATED_HEADER, 0);
        return 0;
    }
    if (data[0] != PYL_MAGIC_0 || data[1] != PYL_MAGIC_1 ||
        data[2] != PYL_MAGIC_2 || data[3] != PYL_MAGIC_3) {
        pyl_result(result, PYL_VERIFY_BAD_MAGIC, 0);
        return 0;
    }
    if (pyl_u16(data + 4) != PYL_VERSION_MAJOR ||
        pyl_u16(data + 6) > PYL_VERSION_MINOR) {
        pyl_result(result, PYL_VERIFY_BAD_VERSION, 0);
        return 0;
    }
    if (pyl_u16(data + 8) != PYL_HEADER_SIZE) {
        pyl_result(result, PYL_VERIFY_BAD_HEADER_SIZE, 0);
        return 0;
    }
    if (pyl_u16(data + 10) != PYL_MODULE_ENTRY_SIZE) {
        pyl_result(result, PYL_VERIFY_BAD_ENTRY_SIZE, 0);
        return 0;
    }
    if ((pyl_u32(data + 12) & ~(PBCU32)PYL_ARCHIVE_STDLIB) != 0) {
        pyl_result(result, PYL_VERIFY_BAD_FLAGS, 0);
        return 0;
    }
    count = pyl_u16(data + 16);
    if (count > PYL_MAX_MODULES) {
        pyl_result(result, PYL_VERIFY_TOO_MANY_MODULES, 0);
        return 0;
    }
    if (pyl_u16(data + 18) != 0 || pyl_u32(data + 28) != size) {
        pyl_result(result, pyl_u16(data + 18) != 0
                   ? PYL_VERIFY_BAD_RESERVED : PYL_VERIFY_BAD_FILE_SIZE, 0);
        return 0;
    }
    directory_offset = pyl_u32(data + 20);
    module_data_offset = pyl_u32(data + 24);
    if (directory_offset != PYL_HEADER_SIZE ||
        !pyl_mul(count, PYL_MODULE_ENTRY_SIZE, &directory_size) ||
        !pyl_add(directory_offset, directory_size, &directory_end) ||
        directory_end > size || module_data_offset < directory_end ||
        module_data_offset > size) {
        pyl_result(result, PYL_VERIFY_TRUNCATED_DIRECTORY, 0);
        return 0;
    }
    if ((module_data_offset & 3U) != 0) {
        pyl_result(result, PYL_VERIFY_UNALIGNED_MODULE, 0);
        return 0;
    }

    previous_name_end = directory_end;
    previous_module_end = module_data_offset;
    for (i = 0; i < count; i++) {
        const PBCU8 *entry = data + directory_offset +
                             (PBCU32)i * PYL_MODULE_ENTRY_SIZE;
        PBCU32 name_offset = pyl_u32(entry);
        PBCU16 name_size = pyl_u16(entry + 4);
        PBCU16 flags = pyl_u16(entry + 6);
        PBCU32 module_offset = pyl_u32(entry + 8);
        PBCU32 module_size = pyl_u32(entry + 12);
        PBCU32 name_end;
        PBCU32 module_end;
        const PBCU8 *embedded_name;
        PBCU16 embedded_name_size;
        PBCModuleVerifyResult module_result;

        if ((flags & ~(PBCU16)PYL_MODULE_PACKAGE) != 0 ||
            pyl_u32(entry + 20) != 0) {
            pyl_result(result, (flags & ~(PBCU16)PYL_MODULE_PACKAGE) != 0
                       ? PYL_VERIFY_BAD_MODULE_FLAGS
                       : PYL_VERIFY_BAD_RESERVED, i);
            return 0;
        }
        if (!pyl_add(name_offset, name_size, &name_end) ||
            name_offset < directory_end || name_end > module_data_offset ||
            !pyl_valid_name(data + name_offset, name_size)) {
            pyl_result(result, PYL_VERIFY_BAD_MODULE_NAME, i);
            return 0;
        }
        if (name_offset != previous_name_end) {
            pyl_result(result, PYL_VERIFY_NONCANONICAL_NAMES, i);
            return 0;
        }
        previous_name_end = name_end;
        if (i != 0) {
            const PBCU8 *prior = entry - PYL_MODULE_ENTRY_SIZE;
            PBCU32 prior_name_offset = pyl_u32(prior);
            PBCU16 prior_name_size = pyl_u16(prior + 4);
            if (pyl_name_compare(data + prior_name_offset, prior_name_size,
                                 data + name_offset, name_size) >= 0) {
                pyl_result(result, PYL_VERIFY_UNSORTED_MODULES, i);
                return 0;
            }
        }
        if ((module_offset & 3U) != 0) {
            pyl_result(result, PYL_VERIFY_UNALIGNED_MODULE, i);
            return 0;
        }
        if (!pyl_add(module_offset, module_size, &module_end) ||
            module_size == 0 || module_offset < module_data_offset ||
            module_end > size) {
            pyl_result(result, PYL_VERIFY_MODULE_OUT_OF_RANGE, i);
            return 0;
        }
        if (module_offset < previous_module_end) {
            pyl_result(result, PYL_VERIFY_OVERLAPPING_MODULES, i);
            return 0;
        }
        previous_module_end = module_end;
        if (pyl_checksum(data + module_offset, module_size) !=
            pyl_u32(entry + 16)) {
            pyl_result(result, PYL_VERIFY_BAD_CHECKSUM, i);
            return 0;
        }
        if (!pbc_verify_module(data + module_offset, module_size,
                               &module_result)) {
            pyl_result(result, PYL_VERIFY_INVALID_PBC, i);
            if (result != 0) result->module_error = module_result;
            return 0;
        }
        if (!pbc_module_name(data + module_offset, module_size,
                             &embedded_name, &embedded_name_size) ||
            embedded_name_size != name_size ||
            memcmp(embedded_name, data + name_offset, name_size) != 0) {
            pyl_result(result, PYL_VERIFY_MODULE_NAME_MISMATCH, i);
            return 0;
        }
    }
    if (!pyl_align4(previous_name_end, &directory_end) ||
        directory_end != module_data_offset) {
        pyl_result(result, PYL_VERIFY_NONCANONICAL_NAMES, count);
        return 0;
    }
    return 1;
}

int pyl_find_module(const PBCU8 *data, PBCU32 size,
                    const char *name, PYLModuleView *view)
{
    PYLVerifyResult verification;
    PBCU16 low;
    PBCU16 high;
    PBCU16 name_size;
    if (name == 0 || view == 0 || !pyl_verify_archive(data, size,
                                                       &verification))
        return 0;
    if (strlen(name) > 65535U) return 0;
    name_size = (PBCU16)strlen(name);
    low = 0;
    high = pyl_u16(data + 16);
    while (low < high) {
        PBCU16 middle = (PBCU16)(low + (high - low) / 2U);
        const PBCU8 *entry = data + pyl_u32(data + 20) +
                             (PBCU32)middle * PYL_MODULE_ENTRY_SIZE;
        PBCU32 entry_name_offset = pyl_u32(entry);
        PBCU16 entry_name_size = pyl_u16(entry + 4);
        int comparison = pyl_name_compare(
            (const PBCU8 *)name, name_size,
            data + entry_name_offset, entry_name_size);
        if (comparison > 0) low = (PBCU16)(middle + 1U);
        else if (comparison < 0) high = middle;
        else {
            view->name = data + entry_name_offset;
            view->name_size = entry_name_size;
            view->flags = pyl_u16(entry + 6);
            view->data = data + pyl_u32(entry + 8);
            view->size = pyl_u32(entry + 12);
            view->checksum = pyl_u32(entry + 16);
            return 1;
        }
    }
    return 0;
}

PYLArchiveBuilder::PYLArchiveBuilder()
{
    modules = 0;
    module_count = 0;
    module_capacity = 0;
    archive_flags = 0;
    output = 0;
    output_size = 0;
    last_error = 0;
}

PYLArchiveBuilder::~PYLArchiveBuilder()
{
    PBCU16 i;
    for (i = 0; i < module_count; i++) {
        free(modules[i].name);
        free(modules[i].data);
    }
    free(modules);
    free(output);
}

void PYLArchiveBuilder::set_flags(PBCU32 flags)
{
    archive_flags = flags;
}

int PYLArchiveBuilder::reserve(PBCU16 required)
{
    PBCU16 capacity;
    Module *replacement;
    if (required <= module_capacity) return 1;
    capacity = module_capacity == 0 ? 8 : (PBCU16)(module_capacity * 2U);
    if (capacity < required) capacity = required;
    if (capacity > PYL_MAX_MODULES) capacity = PYL_MAX_MODULES;
    replacement = (Module *)realloc(
        modules, (size_t)capacity * sizeof(Module));
    if (replacement == 0) return 0;
    modules = replacement;
    module_capacity = capacity;
    return 1;
}

int PYLArchiveBuilder::add_module(const char *name, const PBCU8 *bytes,
                                  PBCU32 byte_count, PBCU16 flags)
{
    PBCModuleVerifyResult verification;
    const PBCU8 *embedded_name;
    PBCU16 embedded_name_size;
    size_t name_length;
    PBCU16 i;
    char *name_copy;
    PBCU8 *data_copy;
    if (output != 0) {
        last_error = "archive is already finalized";
        return 0;
    }
    name_length = name != 0 ? strlen(name) : 0;
    if (name_length == 0 || name_length > 65535U ||
        !pyl_valid_name((const PBCU8 *)name, (PBCU16)name_length)) {
        last_error = "invalid module name";
        return 0;
    }
    if ((flags & ~(PBCU16)PYL_MODULE_PACKAGE) != 0) {
        last_error = "unsupported module flags";
        return 0;
    }
    if (!pbc_verify_module(bytes, byte_count, &verification)) {
        last_error = "invalid PBC module";
        return 0;
    }
    if (!pbc_module_name(bytes, byte_count,
                         &embedded_name, &embedded_name_size) ||
        embedded_name_size != (PBCU16)name_length ||
        memcmp(embedded_name, name, name_length) != 0) {
        last_error = "archive and PBC module names differ";
        return 0;
    }
    for (i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, name) == 0) {
            last_error = "duplicate module name";
            return 0;
        }
    }
    if (module_count >= PYL_MAX_MODULES ||
        !reserve((PBCU16)(module_count + 1U))) {
        last_error = "cannot grow module directory";
        return 0;
    }
    name_copy = (char *)malloc(name_length + 1U);
    data_copy = (PBCU8 *)malloc((size_t)byte_count);
    if (name_copy == 0 || data_copy == 0) {
        free(name_copy);
        free(data_copy);
        last_error = "cannot copy archived module";
        return 0;
    }
    memcpy(name_copy, name, name_length + 1U);
    memcpy(data_copy, bytes, (size_t)byte_count);
    modules[module_count].name = name_copy;
    modules[module_count].data = data_copy;
    modules[module_count].size = byte_count;
    modules[module_count].flags = flags;
    module_count++;
    return 1;
}

void PYLArchiveBuilder::sort_modules()
{
    PBCU16 i;
    for (i = 1; i < module_count; i++) {
        Module value = modules[i];
        PBCU16 position = i;
        while (position != 0 &&
               strcmp(modules[position - 1U].name, value.name) > 0) {
            modules[position] = modules[position - 1U];
            position--;
        }
        modules[position] = value;
    }
}

int PYLArchiveBuilder::finalize()
{
    PBCU32 directory_size;
    PBCU32 names_offset;
    PBCU32 names_end;
    PBCU32 module_offset;
    PBCU32 total_size;
    PBCU16 i;
    PYLVerifyResult verification;

    last_error = 0;
    if (output != 0) return 1;
    if ((archive_flags & ~(PBCU32)PYL_ARCHIVE_STDLIB) != 0) {
        last_error = "unsupported archive flags";
        return 0;
    }
    if (!pyl_mul(module_count, PYL_MODULE_ENTRY_SIZE, &directory_size) ||
        !pyl_add(PYL_HEADER_SIZE, directory_size, &names_offset)) {
        last_error = "archive directory overflow";
        return 0;
    }
    sort_modules();
    names_end = names_offset;
    for (i = 0; i < module_count; i++) {
        if (!pyl_add(names_end, (PBCU32)strlen(modules[i].name),
                     &names_end)) {
            last_error = "archive names overflow";
            return 0;
        }
    }
    if (!pyl_align4(names_end, &module_offset)) {
        last_error = "archive alignment overflow";
        return 0;
    }
    total_size = module_offset;
    for (i = 0; i < module_count; i++) {
        if (!pyl_add(total_size, modules[i].size, &total_size) ||
            (i + 1U < module_count &&
             !pyl_align4(total_size, &total_size))) {
            last_error = "archive module data overflow";
            return 0;
        }
    }
    output = (PBCU8 *)malloc((size_t)total_size);
    if (output == 0) {
        last_error = "cannot allocate archive output";
        return 0;
    }
    memset(output, 0, (size_t)total_size);
    output_size = total_size;
    output[0] = PYL_MAGIC_0;
    output[1] = PYL_MAGIC_1;
    output[2] = PYL_MAGIC_2;
    output[3] = PYL_MAGIC_3;
    pyl_put_u16(output + 4, PYL_VERSION_MAJOR);
    pyl_put_u16(output + 6, PYL_VERSION_MINOR);
    pyl_put_u16(output + 8, PYL_HEADER_SIZE);
    pyl_put_u16(output + 10, PYL_MODULE_ENTRY_SIZE);
    pyl_put_u32(output + 12, archive_flags);
    pyl_put_u16(output + 16, module_count);
    pyl_put_u32(output + 20, PYL_HEADER_SIZE);
    pyl_put_u32(output + 24, module_offset);
    pyl_put_u32(output + 28, total_size);

    names_end = names_offset;
    for (i = 0; i < module_count; i++) {
        PBCU8 *entry = output + PYL_HEADER_SIZE +
                       (PBCU32)i * PYL_MODULE_ENTRY_SIZE;
        PBCU16 name_size = (PBCU16)strlen(modules[i].name);
        pyl_put_u32(entry, names_end);
        pyl_put_u16(entry + 4, name_size);
        pyl_put_u16(entry + 6, modules[i].flags);
        pyl_put_u32(entry + 8, module_offset);
        pyl_put_u32(entry + 12, modules[i].size);
        pyl_put_u32(entry + 16,
                    pyl_checksum(modules[i].data, modules[i].size));
        memcpy(output + names_end, modules[i].name, name_size);
        memcpy(output + module_offset, modules[i].data,
               (size_t)modules[i].size);
        names_end += name_size;
        module_offset += modules[i].size;
        if (i + 1U < module_count &&
            !pyl_align4(module_offset, &module_offset)) {
            last_error = "archive alignment overflow";
            free(output);
            output = 0;
            output_size = 0;
            return 0;
        }
    }
    if (!pyl_verify_archive(output, output_size, &verification)) {
        last_error = pyl_verify_error_name(verification.error);
        free(output);
        output = 0;
        output_size = 0;
        return 0;
    }
    return 1;
}

const PBCU8 *PYLArchiveBuilder::data() const
{
    return output;
}

PBCU32 PYLArchiveBuilder::size() const
{
    return output_size;
}

const char *PYLArchiveBuilder::error() const
{
    return last_error;
}
