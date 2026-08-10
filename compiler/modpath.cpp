#include "modpath.h"

#include <stdio.h>
#include <string.h>

static int module_relative_base(const char *module_name, int dos83,
                                char *output, int capacity)
{
    int source = 0;
    int target = 0;
    int component = 0;
    if (!module_name || !output || capacity <= 0) return 0;
    while (module_name[source] != '\0') {
        char value = module_name[source++];
        if (value == '.') {
            if (component == 0 || target + 1 >= capacity) return 0;
            output[target++] = '/';
            component = 0;
        } else {
            if ((!dos83 || component < 8) && target + 1 < capacity)
                output[target++] = value;
            else if (!dos83 || target + 1 >= capacity)
                return 0;
            component++;
        }
    }
    if (component == 0) return 0;
    output[target] = '\0';
    return 1;
}

int module_resolve_source(const char *module_name,
                          const char **search_paths,
                          int num_search_paths,
                          char *output_path,
                          int output_capacity)
{
    char relative_base[384];
    char relative[416];
    int path_index;
    int package_form;
    int alias_form;

    if (!module_name || !output_path || output_capacity <= 0) return 0;

    for (path_index = 0; path_index <= num_search_paths; path_index++) {
        for (alias_form = 0; alias_form < 2; alias_form++) {
            if (!module_relative_base(module_name, alias_form,
                                      relative_base,
                                      (int)sizeof(relative_base)))
                continue;
            for (package_form = 0; package_form < 2; package_form++) {
                FILE *probe;
                if (package_form)
                    snprintf(relative, sizeof(relative), "%s/__init__.py",
                             relative_base);
                else
                    snprintf(relative, sizeof(relative), "%s.py",
                             relative_base);

                if (path_index == num_search_paths) {
                    if ((int)strlen(relative) >= output_capacity) continue;
                    strcpy(output_path, relative);
                } else {
                    int prefix_length =
                        (int)strlen(search_paths[path_index]);
                    int separator = prefix_length > 0 &&
                        search_paths[path_index][prefix_length - 1] != '/' &&
                        search_paths[path_index][prefix_length - 1] != '\\';
                    if (prefix_length + separator +
                            (int)strlen(relative) + 1 > output_capacity)
                        continue;
                    strcpy(output_path, search_paths[path_index]);
                    if (separator) strcat(output_path, "/");
                    strcat(output_path, relative);
                }

                probe = fopen(output_path, "rb");
                if (probe) {
                    fclose(probe);
                    return 1;
                }
            }
        }
    }
    output_path[0] = '\0';
    return 0;
}
