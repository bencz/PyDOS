/* pbcarc.h - Deterministic archive of verified PBC modules. */

#ifndef PBCARCHIVE_H
#define PBCARCHIVE_H

#include "pbcmod.h"
#include "../common/pdospyl.h"

enum PYLVerifyError {
    PYL_VERIFY_OK = 0,
    PYL_VERIFY_NULL_INPUT,
    PYL_VERIFY_TRUNCATED_HEADER,
    PYL_VERIFY_BAD_MAGIC,
    PYL_VERIFY_BAD_VERSION,
    PYL_VERIFY_BAD_HEADER_SIZE,
    PYL_VERIFY_BAD_ENTRY_SIZE,
    PYL_VERIFY_BAD_FILE_SIZE,
    PYL_VERIFY_BAD_FLAGS,
    PYL_VERIFY_BAD_RESERVED,
    PYL_VERIFY_TOO_MANY_MODULES,
    PYL_VERIFY_TRUNCATED_DIRECTORY,
    PYL_VERIFY_BAD_MODULE_FLAGS,
    PYL_VERIFY_BAD_MODULE_NAME,
    PYL_VERIFY_NONCANONICAL_NAMES,
    PYL_VERIFY_UNSORTED_MODULES,
    PYL_VERIFY_MODULE_OUT_OF_RANGE,
    PYL_VERIFY_UNALIGNED_MODULE,
    PYL_VERIFY_OVERLAPPING_MODULES,
    PYL_VERIFY_BAD_CHECKSUM,
    PYL_VERIFY_INVALID_PBC,
    PYL_VERIFY_MODULE_NAME_MISMATCH
};

struct PYLVerifyResult {
    PYLVerifyError error;
    PBCU16 module_index;
    PBCModuleVerifyResult module_error;
};

struct PYLModuleView {
    const PBCU8 *name;
    PBCU16 name_size;
    PBCU16 flags;
    const PBCU8 *data;
    PBCU32 size;
    PBCU32 checksum;
};

const char *pyl_verify_error_name(PYLVerifyError error);
PBCU32 pyl_checksum(const PBCU8 *data, PBCU32 size);
int pyl_verify_archive(const PBCU8 *data, PBCU32 size,
                       PYLVerifyResult *result);
int pyl_find_module(const PBCU8 *data, PBCU32 size,
                    const char *name, PYLModuleView *view);

class PYLArchiveBuilder {
public:
    PYLArchiveBuilder();
    ~PYLArchiveBuilder();

    void set_flags(PBCU32 flags);
    int add_module(const char *name, const PBCU8 *data, PBCU32 size,
                   PBCU16 flags);
    int finalize();

    const PBCU8 *data() const;
    PBCU32 size() const;
    const char *error() const;

private:
    struct Module {
        char *name;
        PBCU8 *data;
        PBCU32 size;
        PBCU16 flags;
    };

    Module *modules;
    PBCU16 module_count;
    PBCU16 module_capacity;
    PBCU32 archive_flags;
    PBCU8 *output;
    PBCU32 output_size;
    const char *last_error;

    int reserve(PBCU16 required);
    void sort_modules();

    PYLArchiveBuilder(const PYLArchiveBuilder &);
    PYLArchiveBuilder &operator=(const PYLArchiveBuilder &);
};

#endif /* PBCARCHIVE_H */
