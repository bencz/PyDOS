/*
 * pirhyg.cpp - whole-module PIR hygiene
 *
 * Python metadata is emitted while the AST is still available.  Keeping all
 * of that construction through lowering is particularly expensive on 8086.
 * This pass records which reflection surfaces are observable and removes
 * code-metadata bundles that the runtime can reconstruct lazily.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirhyg.h"
#include "pirutil.h"

#include <string.h>

static unsigned feature_for_attribute(const char *name)
{
    if (!name) return 0;
    if (strcmp(name, "__code__") == 0 ||
        strncmp(name, "co_", 3) == 0) {
        return PIR_HYG_CODE;
    }
    if (strcmp(name, "__annotations__") == 0) {
        return PIR_HYG_ANNOTATIONS;
    }
    if (strcmp(name, "__defaults__") == 0 ||
        strcmp(name, "__kwdefaults__") == 0 ||
        strcmp(name, "__signature__") == 0) {
        return PIR_HYG_PARAMETERS;
    }
    if (strcmp(name, "__type_params__") == 0 ||
        strcmp(name, "__parameters__") == 0 ||
        strcmp(name, "__orig_bases__") == 0) {
        return PIR_HYG_TYPE_PARAMS;
    }
    if (strcmp(name, "__dict__") == 0) {
        return PIR_HYG_DICT;
    }
    return 0;
}

static PIRInst *find_definition(PIRFunction *func, int value_id,
                                PIRBlock **owner)
{
    int bi;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            if (pir_value_valid(inst->result) &&
                inst->result.id == value_id) {
                if (owner) *owner = block;
                return inst;
            }
        }
    }
    return 0;
}

static const char *constant_string(PIRFunction *func, PIRValue value)
{
    PIRInst *definition;
    if (!pir_value_valid(value)) return 0;
    definition = find_definition(func, value.id, 0);
    if (!definition || definition->op != PIR_CONST_STR) return 0;
    return definition->str_val;
}

static int is_dynamic_reflection_call(const char *name)
{
    if (!name) return 0;
    return strcmp(name, "pydos_builtin_getattr_") == 0 ||
           strcmp(name, "pydos_builtin_hasattr_") == 0 ||
           strcmp(name, "getattr") == 0 ||
           strcmp(name, "hasattr") == 0;
}

/* Recover the argument pushes belonging to a call.  Values may be computed
 * between pushes, so this walks backwards until the requested count has been
 * found.  A previous argument consumer is a hard boundary. */
static int collect_pushes(PIRInst *consumer, int count, PIRInst **pushes)
{
    PIRInst *cursor = consumer ? consumer->prev : 0;
    int found = 0;
    while (cursor && found < count) {
        if (cursor->op == PIR_PUSH_ARG) {
            pushes[count - found - 1] = cursor;
            found++;
        } else if (cursor->op == PIR_CALL ||
                   cursor->op == PIR_CALL_METHOD ||
                   cursor->op == PIR_GUARDED_CALL_METHOD ||
                   cursor->op == PIR_BUILD_LIST ||
                   cursor->op == PIR_BUILD_DICT ||
                   cursor->op == PIR_BUILD_TUPLE ||
                   cursor->op == PIR_BUILD_SET ||
                   cursor->op == PIR_STR_JOIN) {
            break;
        }
        cursor = cursor->prev;
    }
    return found == count;
}

static unsigned analyze_function(PIRFunction *func)
{
    unsigned features = 0;
    int bi;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRInst *inst;
        for (inst = func->blocks[bi]->first; inst; inst = inst->next) {
            if (inst->op == PIR_GET_ATTR || inst->op == PIR_SET_ATTR ||
                inst->op == PIR_DEL_ATTR) {
                features |= feature_for_attribute(inst->str_val);
            } else if (inst->op == PIR_CALL &&
                       is_dynamic_reflection_call(inst->str_val)) {
                PIRInst *pushes[3];
                int argc = (int)inst->int_val;
                if (argc < 2 || argc > 3 ||
                    !collect_pushes(inst, argc, pushes)) {
                    features |= PIR_HYG_DYNAMIC;
                } else {
                    const char *attribute = constant_string(
                        func, pushes[1]->operands[0]);
                    if (attribute) {
                        features |= feature_for_attribute(attribute);
                    } else {
                        features |= PIR_HYG_DYNAMIC;
                    }
                }
            }
        }
    }
    return features;
}

static void remove_inst(PIRBlock *block, PIRInst *inst,
                        PIRHygieneReport *report)
{
    pir_inst_remove(block, inst);
    report->instructions_removed++;
}

/* Remove a metadata-only producer after its explicit SSA uses disappeared.
 * BUILD_TUPLE consumes its elements through PUSH_ARG, so those implicit uses
 * are removed as one indivisible bundle. */
static void remove_dead_metadata_value(PIRFunction *func, PIRValue value,
                                       PIRHygieneReport *report)
{
    PIRBlock *block = 0;
    PIRInst *definition;
    if (!pir_value_valid(value) || pir_value_has_uses(func, value)) return;
    definition = find_definition(func, value.id, &block);
    if (!definition || !block) return;

    if (definition->op == PIR_CONST_STR) {
        remove_inst(block, definition, report);
    } else if (definition->op == PIR_BUILD_TUPLE) {
        int count = (int)definition->int_val;
        PIRInst *pushes[64];
        PIRValue values[64];
        int i;
        if (count < 0 || count > 64) return;
        if (count > 0 && !collect_pushes(definition, count, pushes)) return;
        for (i = 0; i < count; i++) values[i] = pushes[i]->operands[0];
        for (i = 0; i < count; i++) {
            remove_inst(block, pushes[i], report);
        }
        remove_inst(block, definition, report);
        for (i = 0; i < count; i++) {
            remove_dead_metadata_value(func, values[i], report);
        }
    }
}

static void clean_function(PIRFunction *func, unsigned features,
                           PIRHygieneReport *report)
{
    int bi;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst = block->first;
        while (inst) {
            PIRInst *next = inst->next;
            if (inst->op == PIR_CALL && inst->str_val &&
                strcmp(inst->str_val,
                       "pydos_func_set_code_metadata") == 0 &&
                inst->int_val == 4) {
                PIRInst *pushes[4];
                if (collect_pushes(inst, 4, pushes)) {
                    PIRValue function_value = pushes[0]->operands[0];
                    PIRValue name_value = pushes[1]->operands[0];
                    PIRValue freevars_value = pushes[2]->operands[0];
                    PIRValue consts_value = pushes[3]->operands[0];
                    PIRInst *freevars = find_definition(
                        func, freevars_value.id, 0);
                    int empty_freevars = freevars &&
                        freevars->op == PIR_BUILD_TUPLE &&
                        freevars->int_val == 0;
                    int code_unobservable =
                        (features & (PIR_HYG_CODE | PIR_HYG_DYNAMIC)) == 0;
                    int i;

                    /* Empty metadata is reconstructed exactly by the lazy
                     * runtime.  Non-empty co_freevars may also be discarded
                     * when no code reflection operation is observable. */
                    if (empty_freevars || code_unobservable) {
                        for (i = 0; i < 4; i++) {
                            remove_inst(block, pushes[i], report);
                        }
                        remove_inst(block, inst, report);
                        report->metadata_bundles_removed++;
                        remove_dead_metadata_value(func, name_value, report);
                        remove_dead_metadata_value(func, freevars_value, report);
                        remove_dead_metadata_value(func, consts_value, report);
                        /* The function object itself is semantic, not metadata. */
                        (void)function_value;
                    }
                }
            }
            inst = next;
        }
    }
}

/* Every object-producing operation follows the runtime ABI contract:
 * success returns a non-null owned object with no pending exception, while
 * failure returns null with an exception pending.  Attach that SSA result to
 * the following exceptional edge so target lowering can use a cheap null
 * guard and call traceback machinery only on the cold path. */
static void link_exception_results(PIRFunction *func,
                                   PIRHygieneReport *report)
{
    int bi;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRInst *inst;
        for (inst = func->blocks[bi]->first; inst; inst = inst->next) {
            PIRInst *producer;
            if (inst->op != PIR_CHECK_EXCEPTION ||
                inst->num_operands != 0)
                continue;
            producer = inst->prev;
            if (producer && pir_value_valid(producer->result) &&
                producer->result.type == PIR_TYPE_PYOBJ) {
                inst->operands[0] = producer->result;
                inst->num_operands = 1;
                report->exception_fast_paths++;
            }
        }
    }
}

void pir_hygiene_run(PIRModule *mod, PIRHygieneReport *report)
{
    int i;
    if (!report) return;
    report->observed_features = 0;
    report->metadata_bundles_removed = 0;
    report->instructions_removed = 0;
    report->exception_fast_paths = 0;
    if (!mod) return;

    for (i = 0; i < mod->functions.size(); i++) {
        report->observed_features |= analyze_function(mod->functions[i]);
    }
    for (i = 0; i < mod->functions.size(); i++) {
        clean_function(mod->functions[i], report->observed_features, report);
        link_exception_results(mod->functions[i], report);
    }
}
