/*
 * execmode.h - Execution-mode planning for the PyDOS compiler
 *
 * The frontend and optimized PIR are shared by every mode.  This module
 * selects an execution engine without depending on a particular backend.
 * C++98 compatible, no STL.
 */

#ifndef EXECMODE_H
#define EXECMODE_H

enum ExecutionMode {
    EXEC_MODE_NATIVE = 0,
    EXEC_MODE_VM,
    EXEC_MODE_HYBRID,
    EXEC_MODE_AUTO
};

enum ExecutionCapability {
    EXEC_CAP_NATIVE = 1,
    EXEC_CAP_VM     = 2,
    EXEC_CAP_HYBRID = 4
};

struct ExecutionPlan {
    ExecutionMode requested;
    ExecutionMode effective;
    int capabilities;
    int uses_native_backend;
    int uses_bytecode_backend;
    int requires_external_library;
};

const char *execution_mode_name(ExecutionMode mode);

/* Parse an exact command-line spelling. */
int execution_mode_parse(const char *text, ExecutionMode *mode);

/* Resolve AUTO and validate that the selected implementation exists.
 * auto_preference is target policy, capabilities describes this build. */
int execution_plan_resolve(ExecutionMode requested,
                           ExecutionMode auto_preference,
                           int capabilities,
                           ExecutionPlan *plan);

#endif /* EXECMODE_H */
