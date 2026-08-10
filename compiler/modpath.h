/* Resolve dotted Python module names to source files. */

#ifndef MODPATH_H
#define MODPATH_H

int module_resolve_source(const char *module_name,
                          const char **search_paths,
                          int num_search_paths,
                          char *output_path,
                          int output_capacity);

#endif
