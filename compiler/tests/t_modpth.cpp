#include "../modpath.h"
#include <stdio.h>
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

static void check_module(const char *logical, const char *physical)
{
    const char *paths[] = { "stdlib" };
    char resolved[512];
    CHECK(module_resolve_source(logical, paths, 1,
                                resolved, sizeof(resolved)));
    CHECK(strcmp(resolved, physical) == 0);
}

int main()
{
    check_module("dataclasses", "stdlib/dataclas.py");
    check_module("_internal", "stdlib/_interna.py");
    check_module("builtins.descriptors",
                 "stdlib/builtins/descript.py");
    check_module("pydos.io.tui.constants",
                 "stdlib/pydos/io/tui/constant.py");
    check_module("pydos.io.tui.widgets.application",
                 "stdlib/pydos/io/tui/widgets/applicat.py");
    check_module("pydos.io.tui.widgets.text_input",
                 "stdlib/pydos/io/tui/widgets/text_inp.py");
    check_module("pydos.io.files", "stdlib/pydos/io/files.py");

    if (failures != 0) {
        fprintf(stderr, "%d module path test failure(s)\n", failures);
        return 1;
    }
    printf("module source path tests passed\n");
    return 0;
}
