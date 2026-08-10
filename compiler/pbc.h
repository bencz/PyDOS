/*
 * pbc.h - Portable PyDOS bytecode container
 *
 * This is a binary-format API, not the bytecode instruction set.  All fields
 * are encoded explicitly in little-endian order; no C/C++ structure is ever
 * written directly to disk.
 *
 * C++98 compatible, no STL.
 */

#ifndef PBC_H
#define PBC_H

#include "../common/pdospbc.h"

enum PBCVerifyError {
    PBC_VERIFY_OK = 0,
    PBC_VERIFY_NULL_INPUT,
    PBC_VERIFY_TRUNCATED_HEADER,
    PBC_VERIFY_BAD_MAGIC,
    PBC_VERIFY_BAD_VERSION,
    PBC_VERIFY_BAD_HEADER_SIZE,
    PBC_VERIFY_BAD_ENTRY_SIZE,
    PBC_VERIFY_BAD_FILE_SIZE,
    PBC_VERIFY_TOO_MANY_SECTIONS,
    PBC_VERIFY_TRUNCATED_DIRECTORY,
    PBC_VERIFY_BAD_HEADER_FLAGS,
    PBC_VERIFY_BAD_RESERVED_FIELD,
    PBC_VERIFY_BAD_SECTION_FLAGS,
    PBC_VERIFY_UNKNOWN_REQUIRED_SECTION,
    PBC_VERIFY_DUPLICATE_SECTION,
    PBC_VERIFY_SECTION_BEFORE_DATA,
    PBC_VERIFY_UNALIGNED_SECTION,
    PBC_VERIFY_SECTION_OUT_OF_RANGE,
    PBC_VERIFY_OVERLAPPING_SECTIONS
};

struct PBCVerifyResult {
    PBCVerifyError error;
    PBCU16 section_index;
};

struct PBCSectionView {
    PBCU16 type;
    PBCU16 flags;
    const PBCU8 *data;
    PBCU32 size;
    PBCU32 item_count;
};

const char *pbc_verify_error_name(PBCVerifyError error);

/* Verify the structural container.  Semantic section verification is added
 * by the bytecode module reader after the instruction format is defined. */
int pbc_verify_container(const PBCU8 *data, PBCU32 size,
                         PBCVerifyResult *result);

/* Looks up a section in an already verified container.  The function still
 * performs all bounds checks needed to remain safe when called independently.
 * It returns zero when the container is malformed or the section is absent. */
int pbc_find_section(const PBCU8 *data, PBCU32 size, PBCU16 type,
                     PBCSectionView *view);

class PBCWriter {
public:
    PBCWriter();
    ~PBCWriter();

    int add_section(PBCU16 type, PBCU16 flags,
                    const void *data, PBCU32 size, PBCU32 item_count);
    int finalize();

    const PBCU8 *data() const;
    PBCU32 size() const;
    const char *error() const;

private:
    struct PendingSection {
        PBCU16 type;
        PBCU16 flags;
        PBCU8 *data;
        PBCU32 size;
        PBCU32 item_count;
    };

    PendingSection sections[PBC_MAX_SECTIONS];
    PBCU16 section_count;
    PBCU8 *output;
    PBCU32 output_size;
    const char *last_error;

    PBCWriter(const PBCWriter &);
    PBCWriter &operator=(const PBCWriter &);
};

#endif /* PBC_H */
