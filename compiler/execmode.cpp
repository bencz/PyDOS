/*
 * execmode.cpp - Execution-mode planning for the PyDOS compiler
 */

#include "execmode.h"
#include <string.h>

const char *execution_mode_name(ExecutionMode mode)
{
    switch (mode) {
    case EXEC_MODE_NATIVE: return "native";
    case EXEC_MODE_VM:     return "vm";
    case EXEC_MODE_HYBRID: return "hybrid";
    case EXEC_MODE_AUTO:   return "auto";
    }
    return "unknown";
}

int execution_mode_parse(const char *text, ExecutionMode *mode)
{
    if (text == 0 || mode == 0) return 0;
    if (strcmp(text, "native") == 0) {
        *mode = EXEC_MODE_NATIVE;
        return 1;
    }
    if (strcmp(text, "vm") == 0) {
        *mode = EXEC_MODE_VM;
        return 1;
    }
    if (strcmp(text, "hybrid") == 0) {
        *mode = EXEC_MODE_HYBRID;
        return 1;
    }
    if (strcmp(text, "auto") == 0) {
        *mode = EXEC_MODE_AUTO;
        return 1;
    }
    return 0;
}

static int mode_capability(ExecutionMode mode)
{
    switch (mode) {
    case EXEC_MODE_NATIVE: return EXEC_CAP_NATIVE;
    case EXEC_MODE_VM:     return EXEC_CAP_VM;
    case EXEC_MODE_HYBRID: return EXEC_CAP_HYBRID;
    case EXEC_MODE_AUTO:   break;
    }
    return 0;
}

static ExecutionMode first_available_mode(int capabilities)
{
    if ((capabilities & EXEC_CAP_HYBRID) != 0) return EXEC_MODE_HYBRID;
    if ((capabilities & EXEC_CAP_NATIVE) != 0) return EXEC_MODE_NATIVE;
    if ((capabilities & EXEC_CAP_VM) != 0) return EXEC_MODE_VM;
    return EXEC_MODE_AUTO;
}

int execution_plan_resolve(ExecutionMode requested,
                           ExecutionMode auto_preference,
                           int capabilities,
                           ExecutionPlan *plan)
{
    ExecutionMode effective;
    int required;

    if (plan == 0) return 0;
    effective = requested;
    if (effective == EXEC_MODE_AUTO) {
        required = mode_capability(auto_preference);
        if (required != 0 && (capabilities & required) != 0)
            effective = auto_preference;
        else
            effective = first_available_mode(capabilities);
    }

    required = mode_capability(effective);
    if (required == 0 || (capabilities & required) == 0) return 0;

    plan->requested = requested;
    plan->effective = effective;
    plan->capabilities = capabilities;
    plan->uses_native_backend =
        effective == EXEC_MODE_NATIVE || effective == EXEC_MODE_HYBRID;
    plan->uses_bytecode_backend =
        effective == EXEC_MODE_VM || effective == EXEC_MODE_HYBRID;
    plan->requires_external_library = plan->uses_bytecode_backend;
    return 1;
}
