/* pdospyl.h - Portable PyDOS library archive wire constants. */

#ifndef PYDOS_PYL_H
#define PYDOS_PYL_H

#include "pdospbc.h"

#define PYL_MAGIC_0 'P'
#define PYL_MAGIC_1 'Y'
#define PYL_MAGIC_2 'L'
#define PYL_MAGIC_3 'A'

#define PYL_VERSION_MAJOR 1
#define PYL_VERSION_MINOR 0
#define PYL_HEADER_SIZE 32
#define PYL_MODULE_ENTRY_SIZE 24
#define PYL_MAX_MODULES 4096

enum PYLArchiveFlags {
    PYL_ARCHIVE_STDLIB = 1
};

enum PYLModuleFlags {
    PYL_MODULE_PACKAGE = 1
};

#endif /* PYDOS_PYL_H */
