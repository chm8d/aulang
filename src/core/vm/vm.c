// This source file is part of the aulang project
// Copyright (c) 2021 the aulang contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AU_USE_ALLOCA
#ifdef _WIN32
#include <malloc.h>
#ifndef alloca
#define alloca _alloca
#endif
#else
#include <alloca.h>
#endif
#define ALLOCA_MAX_VALUES 256
#endif

#include "platform/mmap.h"
#include "platform/path.h"
#include "platform/platform.h"

#include "core/fn.h"
#include "core/parser/parser.h"
#include "exception.h"
#include "stdlib/au_stdlib.h"
#include "vm.h"

#include "core/int_error/error_printer.h"
#include "core/rt/au_array.h"
#include "core/rt/au_class.h"
#include "core/rt/au_string.h"
#include "core/rt/au_struct.h"
#include "core/rt/au_tuple.h"
#include "core/rt/exception.h"

#ifdef DEBUG_VM
static void debug_value(au_value_t v) {
    switch (au_value_get_type(v)) {
    case VALUE_NONE: {
        printf("(none)");
        break;
    }
    case VALUE_INT: {
        printf("%d", au_value_get_int(v));
        break;
    }
    case VALUE_BOOL: {
        printf("%s\n", au_value_get_bool(v) ? "(true)" : "(false)");
        break;
    }
    case VALUE_STR: {
        printf("(string %p)\n", au_value_get_string(v));
        break;
    }
    default:
        break;
    }
}

static void debug_frame(struct au_vm_frame *frame) {
    printf("registers:\n");
    for (int i = 0; i < AU_REGS; i++) {
        if (au_value_get_type(frame->regs[i]) == VALUE_NONE)
            continue;
        printf("  %d: ", i);
        debug_value(frame->regs[i]);
        printf("\n");
    }
    char c;
    scanf("%c", &c);
}
#endif

static void link_to_imported(const struct au_program_data *p_data,
                             const uint32_t relative_module_idx,
                             const struct au_program_data *loaded_module) {
    struct au_imported_module *relative_module =
        &p_data->imported_modules.data[relative_module_idx];
    AU_HM_VARS_FOREACH_PAIR(&relative_module->fn_map, key, entry, {
        assert(p_data->fns.data[entry->idx].type == AU_FN_IMPORTER);
        const struct au_imported_func *import_func =
            &p_data->fns.data[entry->idx].as.import_func;
        const struct au_hm_var_value *fn_idx =
            au_hm_vars_get(&loaded_module->fn_map, key, key_len);
        if (fn_idx == 0)
            au_fatal("unknown function %.*s", key_len, key);
        struct au_fn *fn = &loaded_module->fns.data[fn_idx->idx];
        if ((fn->flags & AU_FN_FLAG_EXPORTED) == 0)
            au_fatal("this function is not exported");
        if (au_fn_num_args(fn) != import_func->num_args)
            au_fatal("unexpected number of arguments");
        au_fn_fill_import_cache_unsafe(&p_data->fns.data[entry->idx], fn,
                                       loaded_module);
    })
    AU_HM_VARS_FOREACH_PAIR(&relative_module->class_map, key, entry, {
        assert(p_data->classes.data[entry->idx] == 0);
        const struct au_hm_var_value *class_idx =
            au_hm_vars_get(&loaded_module->class_map, key, key_len);
        if (class_idx == 0)
            au_fatal("unknown class %.*s", key_len, key);
        struct au_class_interface *class_interface =
            loaded_module->classes.data[class_idx->idx];
        if ((class_interface->flags & AU_CLASS_FLAG_EXPORTED) == 0)
            au_fatal("this class is not exported");
        p_data->classes.data[entry->idx] = class_interface;
        au_class_interface_ref(class_interface);
    })
    if (relative_module->class_map.entries_occ > 0) {
        for (size_t i = 0; i < p_data->fns.len; i++) {
            au_fn_fill_class_cache_unsafe(&p_data->fns.data[i], p_data);
        }
    }
}

static void bin_op_error(au_value_t left, au_value_t right,
                         const struct au_program_data *p_data,
                         struct au_vm_frame *frame) {
    au_vm_error(
        (struct au_interpreter_result){
            .type = AU_INT_ERR_INCOMPAT_BIN_OP,
            .data.incompat_bin_op.left = left,
            .data.incompat_bin_op.right = right,
            .pos = 0,
        },
        p_data, frame);
}

static void call_error(const struct au_program_data *p_data,
                       struct au_vm_frame *frame) {
    au_vm_error(
        (struct au_interpreter_result){
            .type = AU_INT_ERR_INCOMPAT_CALL,
            .pos = 0,
        },
        p_data, frame);
}

static void
indexing_non_collection_error(au_value_t value,
                              const struct au_program_data *p_data,
                              struct au_vm_frame *frame) {
    au_vm_error(
        (struct au_interpreter_result){
            .type = AU_INT_ERR_INDEXING_NON_COLLECTION,
            .data.invalid_collection.value = value,
            .pos = 0,
        },
        p_data, frame);
}

static void invalid_index_error(au_value_t collection, au_value_t idx,
                                const struct au_program_data *p_data,
                                struct au_vm_frame *frame) {
    au_vm_error(
        (struct au_interpreter_result){
            .type = AU_INT_ERR_INVALID_INDEX,
            .data.invalid_index.collection = collection,
            .data.invalid_index.idx = idx,
            .pos = 0,
        },
        p_data, frame);
}

au_value_t au_vm_exec_unverified(struct au_vm_thread_local *tl,
                                 const struct au_bc_storage *bcs,
                                 const struct au_program_data *p_data,
                                 const au_value_t *args) {
    struct au_vm_frame frame;

    // We add the frame to the linked list first,
    // because tl and frame are fresh in the stack/registers
    frame.link = tl->current_frame;
    tl->current_frame.bcs = bcs;
    tl->current_frame.data = p_data;
    tl->current_frame.frame = &frame;

#ifdef AU_USE_ALLOCA
    au_value_t *alloca_values = 0;
    if (_Likely((bcs->num_registers + bcs->locals_len) <
                ALLOCA_MAX_VALUES)) {
        const size_t n_values = bcs->num_registers + bcs->locals_len;
        alloca_values = alloca(n_values * sizeof(au_value_t));
        au_value_clear(alloca_values, n_values);
        frame.regs = alloca_values;
        frame.locals = &alloca_values[bcs->num_registers];
    } else {
        frame.regs = au_value_calloc(bcs->num_registers);
        frame.locals = au_value_calloc(bcs->locals_len);
    }
#else
    au_value_clear(frame.regs, bcs->num_registers);
    frame.locals = au_value_calloc(bcs->locals_len);
#endif

    for (int i = 0; i < bcs->num_args; i++) {
        frame.locals[i] = args[i];
    }

    frame.retval = au_value_none();
    frame.bc = (uint8_t *)bcs->bc.data;
    frame.bc_start = frame.bc;

    frame.arg_stack = (struct au_value_array){0};
    frame.self = 0;

    while (1) {
#ifdef DEBUG_VM
#define DISPATCH_DEBUG debug_frame(&frame);
#else
#define DISPATCH_DEBUG
#endif

#ifndef AU_USE_DISPATCH_JMP
#define CASE(x) case x
#define DISPATCH                                                          \
    DISPATCH_DEBUG;                                                       \
    frame.bc += 4;                                                        \
    continue
#define DISPATCH_JMP                                                      \
    DISPATCH_DEBUG;                                                       \
    continue
        switch (frame.bc[0]) {
#else
#define CASE(x) CB_##x
#define DISPATCH                                                          \
    do {                                                                  \
        DISPATCH_DEBUG;                                                   \
        frame.bc += 4;                                                    \
        uint8_t op = frame.bc[0];                                         \
        goto *cb[op];                                                     \
    } while (0)
#define DISPATCH_JMP                                                      \
    do {                                                                  \
        DISPATCH_DEBUG;                                                   \
        uint8_t op = frame.bc[0];                                         \
        goto *cb[op];                                                     \
    } while (0)
        static void *cb[] = {
            &&CASE(AU_OP_LOAD_SELF),
            &&CASE(AU_OP_MOV_U16),
            &&CASE(AU_OP_MUL),
            &&CASE(AU_OP_DIV),
            &&CASE(AU_OP_ADD),
            &&CASE(AU_OP_SUB),
            &&CASE(AU_OP_MOD),
            &&CASE(AU_OP_MOV_REG_LOCAL),
            &&CASE(AU_OP_MOV_LOCAL_REG),
            &&CASE(AU_OP_PRINT),
            &&CASE(AU_OP_EQ),
            &&CASE(AU_OP_NEQ),
            &&CASE(AU_OP_LT),
            &&CASE(AU_OP_GT),
            &&CASE(AU_OP_LEQ),
            &&CASE(AU_OP_GEQ),
            &&CASE(AU_OP_JIF),
            &&CASE(AU_OP_JNIF),
            &&CASE(AU_OP_JREL),
            &&CASE(AU_OP_JRELB),
            &&CASE(AU_OP_LOAD_CONST),
            &&CASE(AU_OP_MOV_BOOL),
            &&CASE(AU_OP_NOP),
            &&CASE(AU_OP_MUL_ASG),
            &&CASE(AU_OP_DIV_ASG),
            &&CASE(AU_OP_ADD_ASG),
            &&CASE(AU_OP_SUB_ASG),
            &&CASE(AU_OP_MOD_ASG),
            &&CASE(AU_OP_PUSH_ARG),
            &&CASE(AU_OP_CALL),
            &&CASE(AU_OP_RET_LOCAL),
            &&CASE(AU_OP_RET),
            &&CASE(AU_OP_RET_NULL),
            &&CASE(AU_OP_IMPORT),
            &&CASE(AU_OP_ARRAY_NEW),
            &&CASE(AU_OP_ARRAY_PUSH),
            &&CASE(AU_OP_IDX_GET),
            &&CASE(AU_OP_IDX_SET),
            &&CASE(AU_OP_NOT),
            &&CASE(AU_OP_TUPLE_NEW),
            &&CASE(AU_OP_IDX_SET_STATIC),
            &&CASE(AU_OP_CLASS_GET_INNER),
            &&CASE(AU_OP_CLASS_SET_INNER),
            &&CASE(AU_OP_CLASS_NEW),
            &&CASE(AU_OP_CALL1),
            &&CASE(AU_OP_MUL_INT),
            &&CASE(AU_OP_DIV_INT),
            &&CASE(AU_OP_ADD_INT),
            &&CASE(AU_OP_SUB_INT),
            &&CASE(AU_OP_MOD_INT),
            &&CASE(AU_OP_EQ_INT),
            &&CASE(AU_OP_NEQ_INT),
            &&CASE(AU_OP_LT_INT),
            &&CASE(AU_OP_GT_INT),
            &&CASE(AU_OP_LEQ_INT),
            &&CASE(AU_OP_GEQ_INT),
            &&CASE(AU_OP_JIF_BOOL),
            &&CASE(AU_OP_JNIF_BOOL),
            &&CASE(AU_OP_MUL_DOUBLE),
            &&CASE(AU_OP_DIV_DOUBLE),
            &&CASE(AU_OP_ADD_DOUBLE),
            &&CASE(AU_OP_SUB_DOUBLE),
            &&CASE(AU_OP_EQ_DOUBLE),
            &&CASE(AU_OP_NEQ_DOUBLE),
            &&CASE(AU_OP_LT_DOUBLE),
            &&CASE(AU_OP_GT_DOUBLE),
            &&CASE(AU_OP_LEQ_DOUBLE),
            &&CASE(AU_OP_GEQ_DOUBLE),
        };
        goto *cb[frame.bc[0]];
#endif

#ifdef AU_FEAT_DELAYED_RC
#define COPY_VALUE(dest, src)                                             \
    do {                                                                  \
        dest = src;                                                       \
    } while (0)
#else
/// Copies an au_value from src to dest. For memory safety, please use this
/// function instead of copying directly
#define COPY_VALUE(dest, src)                                             \
    do {                                                                  \
        const au_value_t old = dest;                                      \
        dest = src;                                                       \
        au_value_ref(dest);                                               \
        au_value_deref(old);                                              \
    } while (0)

#define MOVE_VALUE(dest, src)                                             \
    do {                                                                  \
        const au_value_t old = dest;                                      \
        dest = src;                                                       \
        au_value_deref(old);                                              \
    } while (0)
#endif

            CASE(AU_OP_LOAD_SELF) : {
                frame.self = (struct au_obj_class *)au_value_get_struct(
                    frame.locals[0]);
#ifndef AU_FEAT_DELAYED_RC
                frame.self->header.rc++;
#endif
                DISPATCH;
            }
            // Register/local move operations
            CASE(AU_OP_MOV_U16) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t n = *(uint16_t *)(&frame.bc[2]);
#ifdef AU_FEAT_DELAYED_RC
                frame.regs[reg] = au_value_int(n);
#else
            MOVE_VALUE(frame.regs[reg], au_value_int(n));
#endif
                DISPATCH;
            }
            CASE(AU_OP_MOV_REG_LOCAL) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t local = *(uint16_t *)(&frame.bc[2]);
                COPY_VALUE(frame.locals[local], frame.regs[reg]);
                DISPATCH;
            }
            CASE(AU_OP_MOV_LOCAL_REG) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t local = *(uint16_t *)(&frame.bc[2]);
                COPY_VALUE(frame.regs[reg], frame.locals[local]);
                DISPATCH;
            }
            CASE(AU_OP_MOV_BOOL) : {
                const uint8_t n = frame.bc[1];
                const uint8_t reg = frame.bc[2];
                COPY_VALUE(frame.regs[reg], au_value_bool(n));
                DISPATCH;
            }
            CASE(AU_OP_LOAD_CONST) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t rel_c = *(uint16_t *)(&frame.bc[2]);
                const size_t abs_c = rel_c + p_data->tl_constant_start;
                au_value_t v;
                if (au_value_get_type(tl->const_cache[abs_c]) !=
                    VALUE_NONE) {
                    v = tl->const_cache[abs_c];
                } else {
                    const struct au_program_data_val *data_val =
                        &p_data->data_val.data[rel_c];
                    v = data_val->real_value;
                    switch (au_value_get_type(v)) {
                    case VALUE_STR: {
                        v = au_value_string(au_string_from_const(
                            (const char
                                 *)(&p_data->data_buf[data_val->buf_idx]),
                            data_val->buf_len));
                        tl->const_cache[abs_c] = v;
                        break;
                    }
                    default:
                        break;
                    }
                }
                COPY_VALUE(frame.regs[reg], v);
                DISPATCH;
            }
            // Unary operations
            CASE(AU_OP_NOT) : {
                const uint8_t reg = frame.bc[1];
                if (_Likely(au_value_get_type(frame.regs[reg]) ==
                            VALUE_BOOL)) {
                    frame.regs[reg] =
                        au_value_bool(!au_value_get_bool(frame.regs[reg]));
                } else {
#ifdef AU_FEAT_DELAYED_RC
                    frame.regs[reg] = au_value_bool(
                        !au_value_is_truthy(frame.regs[reg]));
#else
                MOVE_VALUE(
                    frame.regs[reg],
                    au_value_bool(!au_value_is_truthy(frame.regs[reg])));
#endif
                }
                DISPATCH;
            }
            // Binary operations
#ifdef AU_FEAT_DELAYED_RC
#define SPECIALIZED_INT_ONLY(NAME)                                        \
    if ((au_value_get_type(lhs) == VALUE_INT) &&                          \
        (au_value_get_type(rhs) == VALUE_INT)) {                          \
        frame.bc[0] = NAME##_INT;                                         \
        goto _##NAME##_INT;                                               \
    }

#define SPECIALIZED_INT_AND_DOUBLE(NAME)                                  \
    SPECIALIZED_INT_ONLY(NAME)                                            \
    else if ((au_value_get_type(lhs) == VALUE_DOUBLE) &&                  \
             (au_value_get_type(rhs) == VALUE_DOUBLE)) {                  \
        frame.bc[0] = NAME##_DOUBLE;                                      \
        goto _##NAME##_DOUBLE;                                            \
    }

#define BIN_OP(NAME, FUN, SPECIALIZER)                                    \
    CASE(NAME) : {                                                        \
        _##NAME:;                                                         \
        const au_value_t lhs = frame.regs[frame.bc[1]];                   \
        const au_value_t rhs = frame.regs[frame.bc[2]];                   \
        const uint8_t res = frame.bc[3];                                  \
        SPECIALIZER                                                       \
        const au_value_t result = au_value_##FUN(lhs, rhs);               \
        if (_Unlikely(au_value_is_op_error(result))) {                    \
            bin_op_error(lhs, rhs, p_data, &frame);                       \
        }                                                                 \
        frame.regs[res] = result;                                         \
        au_value_deref(result);                                           \
        DISPATCH;                                                         \
    }
#else
#define BIN_OP(NAME, FUN, SPECIALIZER)                                    \
    CASE(NAME) : {                                                        \
        _##NAME:;                                                         \
        const au_value_t lhs = frame.regs[frame.bc[1]];                   \
        const au_value_t rhs = frame.regs[frame.bc[2]];                   \
        const uint8_t res = frame.bc[3];                                  \
        SPECIALIZER                                                       \
        const au_value_t result = au_value_##FUN(lhs, rhs);               \
        if (_Unlikely(au_value_is_op_error(result))) {                    \
            bin_op_error(lhs, rhs, p_data, &frame);                       \
        }                                                                 \
        MOVE_VALUE(frame.regs[res], result);                              \
        DISPATCH;                                                         \
    }
#endif
            BIN_OP(AU_OP_MUL, mul, SPECIALIZED_INT_AND_DOUBLE(AU_OP_MUL))
            BIN_OP(AU_OP_DIV, div, SPECIALIZED_INT_AND_DOUBLE(AU_OP_DIV))
            BIN_OP(AU_OP_ADD, add, SPECIALIZED_INT_AND_DOUBLE(AU_OP_ADD))
            BIN_OP(AU_OP_SUB, sub, SPECIALIZED_INT_AND_DOUBLE(AU_OP_SUB))
            BIN_OP(AU_OP_MOD, mod, SPECIALIZED_INT_ONLY(AU_OP_MOD))
            BIN_OP(AU_OP_EQ, eq, SPECIALIZED_INT_AND_DOUBLE(AU_OP_EQ))
            BIN_OP(AU_OP_NEQ, neq, SPECIALIZED_INT_AND_DOUBLE(AU_OP_NEQ))
            BIN_OP(AU_OP_LT, lt, SPECIALIZED_INT_AND_DOUBLE(AU_OP_LT))
            BIN_OP(AU_OP_GT, gt, SPECIALIZED_INT_AND_DOUBLE(AU_OP_GT))
            BIN_OP(AU_OP_LEQ, leq, SPECIALIZED_INT_AND_DOUBLE(AU_OP_LEQ))
            BIN_OP(AU_OP_GEQ, geq, SPECIALIZED_INT_AND_DOUBLE(AU_OP_GEQ))
#undef SPECIALIZED_INT_ONLY
#undef SPECIALIZED_INT_AND_DOUBLE
#undef BIN_OP
            // Binary operations (specialized on int)
#ifdef AU_FEAT_DELAYED_RC
#define FAST_MOVE_VALUE(dest, src) dest = src;
#else
#define FAST_MOVE_VALUE(dest, src) MOVE_VALUE(dest, src)
#endif
#define BIN_OP(NAME, OP, TYPE)                                            \
    CASE(NAME##_INT) : {                                                  \
        _##NAME##_INT:;                                                   \
        const au_value_t lhs = frame.regs[frame.bc[1]];                   \
        const au_value_t rhs = frame.regs[frame.bc[2]];                   \
        const uint8_t res = frame.bc[3];                                  \
        if (_Unlikely((au_value_get_type(lhs) != VALUE_INT) ||            \
                      (au_value_get_type(rhs) != VALUE_INT))) {           \
            frame.bc[0] = NAME;                                           \
            goto _##NAME;                                                 \
        }                                                                 \
        const au_value_t result =                                         \
            TYPE(au_value_get_int(lhs) OP au_value_get_int(rhs));         \
        FAST_MOVE_VALUE(frame.regs[res], result);                         \
        DISPATCH;                                                         \
    }
            BIN_OP(AU_OP_MUL, *, au_value_int)
            BIN_OP(AU_OP_DIV, /, au_value_int)
            BIN_OP(AU_OP_ADD, +, au_value_int)
            BIN_OP(AU_OP_SUB, -, au_value_int)
            BIN_OP(AU_OP_MOD, %, au_value_int)
            BIN_OP(AU_OP_EQ, ==, au_value_bool)
            BIN_OP(AU_OP_NEQ, !=, au_value_bool)
            BIN_OP(AU_OP_LT, <, au_value_bool)
            BIN_OP(AU_OP_GT, >, au_value_bool)
            BIN_OP(AU_OP_LEQ, <=, au_value_bool)
            BIN_OP(AU_OP_GEQ, >=, au_value_bool)
#undef BIN_OP
#undef FAST_MOVE_VALUE
            // Binary operations (specialized on float)
#ifdef AU_FEAT_DELAYED_RC
#define FAST_MOVE_VALUE(dest, src) dest = src;
#else
#define FAST_MOVE_VALUE(dest, src) MOVE_VALUE(dest, src)
#endif
#define BIN_OP(NAME, OP, TYPE)                                            \
    CASE(NAME##_DOUBLE) : {                                               \
        _##NAME##_DOUBLE:;                                                \
        const au_value_t lhs = frame.regs[frame.bc[1]];                   \
        const au_value_t rhs = frame.regs[frame.bc[2]];                   \
        const uint8_t res = frame.bc[3];                                  \
        if (_Unlikely((au_value_get_type(lhs) != VALUE_DOUBLE) ||         \
                      (au_value_get_type(rhs) != VALUE_DOUBLE))) {        \
            frame.bc[0] = NAME;                                           \
            goto _##NAME;                                                 \
        }                                                                 \
        const au_value_t result =                                         \
            TYPE(au_value_get_double(lhs) OP au_value_get_double(rhs));   \
        FAST_MOVE_VALUE(frame.regs[res], result);                         \
        DISPATCH;                                                         \
    }
            BIN_OP(AU_OP_MUL, *, au_value_double)
            BIN_OP(AU_OP_DIV, /, au_value_double)
            BIN_OP(AU_OP_ADD, +, au_value_double)
            BIN_OP(AU_OP_SUB, -, au_value_double)
            BIN_OP(AU_OP_EQ, ==, au_value_bool)
            BIN_OP(AU_OP_NEQ, !=, au_value_bool)
            BIN_OP(AU_OP_LT, <, au_value_bool)
            BIN_OP(AU_OP_GT, >, au_value_bool)
            BIN_OP(AU_OP_LEQ, <=, au_value_bool)
            BIN_OP(AU_OP_GEQ, >=, au_value_bool)
#undef BIN_OP
#undef FAST_MOVE_VALUE
            // Jump instructions
            CASE(AU_OP_JIF) : {
_AU_OP_JIF:;
                const au_value_t cmp = frame.regs[frame.bc[1]];
                const uint16_t n = *(uint16_t *)(&frame.bc[2]);
                const size_t offset = ((size_t)n) * 4;
                if (au_value_get_type(cmp) == VALUE_BOOL) {
                    frame.bc[0] = AU_OP_JIF_BOOL;
                }
                if (au_value_is_truthy(cmp)) {
                    frame.bc += offset;
                    DISPATCH_JMP;
                } else {
                    DISPATCH;
                }
            }
            CASE(AU_OP_JNIF) : {
_AU_OP_JNIF:;
                const au_value_t cmp = frame.regs[frame.bc[1]];
                const uint16_t n = *(uint16_t *)(&frame.bc[2]);
                const size_t offset = ((size_t)n) * 4;
                if (au_value_get_type(cmp) == VALUE_BOOL) {
                    frame.bc[0] = AU_OP_JNIF_BOOL;
                }
                if (!au_value_is_truthy(cmp)) {
                    frame.bc += offset;
                    DISPATCH_JMP;
                } else {
                    DISPATCH;
                }
            }
            CASE(AU_OP_JREL) : {
                const uint16_t *ptr = (uint16_t *)(&frame.bc[2]);
                const size_t offset = ((size_t)ptr[0]) * 4;
                frame.bc += offset;
                DISPATCH_JMP;
            }
            CASE(AU_OP_JRELB) : {
                const uint16_t n = *(uint16_t *)(&frame.bc[2]);
                const size_t offset = ((size_t)n) * 4;
                frame.bc -= offset;
                DISPATCH_JMP;
            }
            // Jump instructions optimized on bools
            CASE(AU_OP_JIF_BOOL) : {
                const au_value_t cmp = frame.regs[frame.bc[1]];
                const uint16_t n = *(uint16_t *)(&frame.bc[2]);
                const size_t offset = ((size_t)n) * 4;
                if (_Unlikely(au_value_get_type(cmp) != VALUE_BOOL)) {
                    frame.bc[0] = AU_OP_JIF;
                    goto _AU_OP_JIF;
                }
                if (au_value_get_bool(cmp)) {
                    frame.bc += offset;
                    DISPATCH_JMP;
                } else {
                    DISPATCH;
                }
            }
            CASE(AU_OP_JNIF_BOOL) : {
                const au_value_t cmp = frame.regs[frame.bc[1]];
                const uint16_t n = *(uint16_t *)(&frame.bc[2]);
                const size_t offset = ((size_t)n) * 4;
                if (_Unlikely(au_value_get_type(cmp) != VALUE_BOOL)) {
                    frame.bc[0] = AU_OP_JNIF;
                    goto _AU_OP_JNIF;
                }
                if (!au_value_get_bool(cmp)) {
                    frame.bc += offset;
                    DISPATCH_JMP;
                } else {
                    DISPATCH;
                }
            }
            // Binary operation into local instructions
#ifdef AU_FEAT_DELAYED_RC
#define BIN_AU_OP_ASG(NAME, FUN)                                          \
    CASE(NAME) : {                                                        \
        const uint8_t reg = frame.bc[1];                                  \
        const uint8_t local = frame.bc[2];                                \
        const au_value_t lhs = frame.locals[local];                       \
        const au_value_t rhs = frame.regs[reg];                           \
        const au_value_t result = au_value_##FUN(lhs, rhs);               \
        au_value_deref(result);                                           \
        if (_Unlikely(au_value_is_op_error(result))) {                    \
            bin_op_error(lhs, rhs, p_data, &frame);                       \
        }                                                                 \
        frame.locals[local] = result;                                     \
        DISPATCH;                                                         \
    }
#else
#define BIN_AU_OP_ASG(NAME, FUN)                                          \
    CASE(NAME) : {                                                        \
        const uint8_t reg = frame.bc[1];                                  \
        const uint8_t local = frame.bc[2];                                \
        const au_value_t lhs = frame.locals[local];                       \
        const au_value_t rhs = frame.regs[reg];                           \
        const au_value_t result = au_value_##FUN(lhs, rhs);               \
        if (_Unlikely(au_value_is_op_error(result))) {                    \
            bin_op_error(lhs, rhs, p_data, &frame);                       \
        }                                                                 \
        MOVE_VALUE(frame.locals[local], result);                          \
        DISPATCH;                                                         \
    }
#endif
            BIN_AU_OP_ASG(AU_OP_MUL_ASG, mul)
            BIN_AU_OP_ASG(AU_OP_DIV_ASG, div)
            BIN_AU_OP_ASG(AU_OP_ADD_ASG, add)
            BIN_AU_OP_ASG(AU_OP_SUB_ASG, sub)
            BIN_AU_OP_ASG(AU_OP_MOD_ASG, mod)
#undef BIN_AU_OP_ASG
            // Call instructions
            CASE(AU_OP_PUSH_ARG) : {
                const uint8_t reg = frame.bc[1];
                au_value_array_add(&frame.arg_stack, frame.regs[reg]);
                au_value_ref(frame.regs[reg]);
                DISPATCH;
            }
            CASE(AU_OP_CALL) : {
                const uint8_t ret_reg = frame.bc[1];
                const uint16_t func_id = *((uint16_t *)(&frame.bc[2]));
                const struct au_fn *call_fn = &p_data->fns.data[func_id];
                int n_regs = au_fn_num_args(call_fn);
                const au_value_t *args =
                    &frame.arg_stack.data[frame.arg_stack.len - n_regs];
#ifdef AU_FEAT_DELAYED_RC
                int is_native = 0;
                const au_value_t callee_retval = au_fn_call_internal(
                    call_fn, tl, p_data, args, &is_native);
                if (_Unlikely(au_value_is_op_error(callee_retval)))
                    call_error(p_data, &frame);
                frame.regs[ret_reg] = callee_retval;
                if (is_native)
                    au_value_deref(callee_retval);
#else
            const au_value_t callee_retval =
                au_fn_call(call_fn, tl, p_data, args);
            if (_Unlikely(au_value_is_op_error(callee_retval)))
                call_error(p_data, &frame);
            MOVE_VALUE(frame.regs[ret_reg], callee_retval);
#endif
                frame.arg_stack.len -= n_regs;
                DISPATCH;
            }
            CASE(AU_OP_CALL1) : {
                const uint8_t ret_reg = frame.bc[1];
                const uint16_t func_id = *((uint16_t *)(&frame.bc[2]));
                const struct au_fn *call_fn = &p_data->fns.data[func_id];
                au_value_t arg_reg = frame.regs[ret_reg];
                // arg_reg is moved to locals in au_fn_call
#ifdef AU_FEAT_DELAYED_RC
                int is_native = 0;
                const au_value_t callee_retval = au_fn_call_internal(
                    call_fn, tl, p_data, &arg_reg, &is_native);
                if (_Unlikely(au_value_is_op_error(callee_retval)))
                    call_error(p_data, &frame);
                frame.regs[ret_reg] = callee_retval;
                if (_Unlikely(is_native))
                    au_value_deref(callee_retval);
#else
            const au_value_t callee_retval =
                au_fn_call(call_fn, tl, p_data, &arg_reg);
            if (_Unlikely(au_value_is_op_error(callee_retval)))
                call_error(p_data, &frame);
            MOVE_VALUE(frame.regs[ret_reg], callee_retval);
#endif
                DISPATCH;
            }
            // Return instructions
            CASE(AU_OP_RET_LOCAL) : {
                const uint8_t ret_local = frame.bc[2];
                // Move ownership of value in ret_local -> return reg in
                // prev. frame
                frame.retval = frame.locals[ret_local];
                frame.locals[ret_local] = au_value_none();
                goto end;
            }
            CASE(AU_OP_RET) : {
                const uint8_t ret_reg = frame.bc[1];
                // Move ownership of value in ret_reg -> return reg in
                // prev. frame
                frame.retval = frame.regs[ret_reg];
                frame.regs[ret_reg] = au_value_none();
                goto end;
            }
            CASE(AU_OP_RET_NULL) : { goto end; }
            // Array instructions
            CASE(AU_OP_ARRAY_NEW) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t capacity = *((uint16_t *)(&frame.bc[2]));
#ifdef AU_FEAT_DELAYED_RC
                struct au_struct *s =
                    (struct au_struct *)au_obj_array_new(capacity);
                frame.regs[reg] = au_value_struct(s);
                s->rc = 0;
#else
            MOVE_VALUE(frame.regs[reg],
                       au_value_struct((
                           struct au_struct *)au_obj_array_new(capacity)));
#endif
                DISPATCH;
            }
            CASE(AU_OP_ARRAY_PUSH) : {
                const au_value_t array_val = frame.regs[frame.bc[1]];
                const au_value_t value_val = frame.regs[frame.bc[2]];
                struct au_obj_array *obj_array =
                    au_obj_array_coerce(array_val);
                if (_Likely(obj_array != 0)) {
                    au_obj_array_push(obj_array, value_val);
                }
                DISPATCH;
            }
            CASE(AU_OP_IDX_GET) : {
                const au_value_t col_val = frame.regs[frame.bc[1]];
                const au_value_t idx_val = frame.regs[frame.bc[2]];
                const uint8_t ret_reg = frame.bc[3];
                struct au_struct *collection = au_struct_coerce(col_val);
                if (_Likely(collection != 0)) {
                    au_value_t value;
                    if (!collection->vdata->idx_get_fn(collection, idx_val,
                                                       &value)) {
                        invalid_index_error(col_val, idx_val, p_data,
                                            &frame);
                    }
                    COPY_VALUE(frame.regs[ret_reg], value);
                } else {
                    indexing_non_collection_error(col_val, p_data, &frame);
                }
                DISPATCH;
            }
            CASE(AU_OP_IDX_SET) : {
                const au_value_t col_val = frame.regs[frame.bc[1]];
                const au_value_t idx_val = frame.regs[frame.bc[2]];
                const au_value_t value_val = frame.regs[frame.bc[3]];
                struct au_struct *collection = au_struct_coerce(col_val);
                if (_Likely(collection != 0)) {
                    if (_Unlikely(collection->vdata->idx_set_fn(
                                      collection, idx_val, value_val) ==
                                  0)) {
                        invalid_index_error(col_val, idx_val, p_data,
                                            &frame);
                    }
                } else {
                    indexing_non_collection_error(col_val, p_data, &frame);
                }
                DISPATCH;
            }
            // Tuple instructions
            CASE(AU_OP_TUPLE_NEW) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t length = *((uint16_t *)(&frame.bc[2]));
#ifdef AU_FEAT_DELAYED_RC
                struct au_struct *s =
                    (struct au_struct *)au_obj_tuple_new(length);
                frame.regs[reg] = au_value_struct(s);
                s->rc = 0;
#else
            MOVE_VALUE(frame.regs[reg],
                       au_value_struct(
                           (struct au_struct *)au_obj_tuple_new(length)));
#endif
                DISPATCH;
            }
            CASE(AU_OP_IDX_SET_STATIC) : {
                const au_value_t col_val = frame.regs[frame.bc[1]];
                const au_value_t idx_val = au_value_int(frame.bc[2]);
                const au_value_t value_val = frame.regs[frame.bc[3]];
                struct au_struct *collection = au_struct_coerce(col_val);
                if (_Likely(collection != 0)) {
                    if (_Unlikely(collection->vdata->idx_set_fn(
                                      collection, idx_val, value_val) ==
                                  0)) {
                        invalid_index_error(col_val, idx_val, p_data,
                                            &frame);
                    }
                } else {
                    indexing_non_collection_error(col_val, p_data, &frame);
                }
                DISPATCH;
            }
            // Class instructions
            CASE(AU_OP_CLASS_NEW) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t class_id = *(uint16_t *)(&frame.bc[2]);
                struct au_struct *obj_class =
                    (struct au_struct *)au_obj_class_new(
                        p_data->classes.data[class_id]);
                const au_value_t new_value = au_value_struct(obj_class);
#ifdef AU_FEAT_DELAYED_RC
                frame.regs[reg] = new_value;
                obj_class->rc = 0;
#else
            MOVE_VALUE(frame.regs[reg], new_value);
#endif
                DISPATCH;
            }
            CASE(AU_OP_CLASS_GET_INNER) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t inner = *(uint16_t *)(&frame.bc[2]);
                COPY_VALUE(frame.regs[reg], frame.self->data[inner]);
                DISPATCH;
            }
            CASE(AU_OP_CLASS_SET_INNER) : {
                const uint8_t reg = frame.bc[1];
                const uint16_t inner = *(uint16_t *)(&frame.bc[2]);
                COPY_VALUE(frame.self->data[inner], frame.regs[reg]);
                DISPATCH;
            }
            // Module instructions
            CASE(AU_OP_IMPORT) : {
                const uint16_t idx = *((uint16_t *)(&frame.bc[2]));
                const size_t relative_module_idx =
                    p_data->imports.data[idx].module_idx;
                const char *relpath = p_data->imports.data[idx].path;

                struct au_mmap_info mmap;
                char *abspath = 0;

                if (relpath[0] == '.' && relpath[1] == '/') {
                    const char *relpath_canon = &relpath[2];
                    const size_t abspath_len =
                        strlen(p_data->cwd) + strlen(relpath_canon) + 2;
                    abspath = malloc(abspath_len);
                    snprintf(abspath, abspath_len, "%s/%s", p_data->cwd,
                             relpath_canon);
                } else {
                    assert(0);
                }

                struct au_program_data *loaded_module =
                    au_vm_thread_local_get_module(tl, abspath);
                if (loaded_module != 0) {
                    free(abspath);
                    link_to_imported(p_data, relative_module_idx,
                                     loaded_module);
                    DISPATCH;
                }

                uint32_t tl_module_idx = ((uint32_t)-1);
                enum au_tl_reserve_mod_retval rmod_retval =
                    AU_TL_RESMOD_RETVAL_FAIL;
                if (relative_module_idx == AU_PROGRAM_IMPORT_NO_MODULE) {
                    rmod_retval = au_vm_thread_local_reserve_import_only(
                        tl, abspath);
                    if (rmod_retval ==
                        AU_TL_RESMOD_RETVAL_OK_MAIN_CALLED) {
                        free(abspath);
                        DISPATCH;
                    }
                } else {
                    rmod_retval = au_vm_thread_local_reserve_module(
                        tl, abspath, &tl_module_idx);
                }

                if (rmod_retval == AU_TL_RESMOD_RETVAL_FAIL) {
                    au_fatal("circular import detected");
                }

                if (!au_mmap_read(abspath, &mmap)) {
                    au_perror("mmap");
                }

                struct au_program program;
                struct au_parser_result parse_res =
                    au_parse(mmap.bytes, mmap.size, &program);
                if (parse_res.type != AU_PARSER_RES_OK) {
                    au_print_parser_error(parse_res,
                                          (struct au_error_location){
                                              .src = mmap.bytes,
                                              .len = mmap.size,
                                              .path = p_data->file,
                                          });
                    abort();
                }

                program.data.tl_constant_start = tl->const_len;
                au_vm_thread_local_add_const_cache(
                    tl, program.data.data_val.len);

                if (!au_split_path(abspath, &program.data.file,
                                   &program.data.cwd))
                    au_perror("au_split_path");
                free(abspath);

                if (rmod_retval != AU_TL_RESMOD_RETVAL_OK_MAIN_CALLED) {
                    au_vm_exec_unverified_main(tl, &program);
                }

                if (relative_module_idx == AU_PROGRAM_IMPORT_NO_MODULE) {
                    au_program_del(&program);
                } else {
                    au_bc_storage_del(&program.main);

                    struct au_program_data *loaded_module =
                        malloc(sizeof(struct au_program_data));
                    memcpy(loaded_module, &program.data,
                           sizeof(struct au_program_data));
                    au_vm_thread_local_add_module(tl, tl_module_idx,
                                                  loaded_module);

                    link_to_imported(p_data, relative_module_idx,
                                     loaded_module);
                }
                DISPATCH;
            }
            // Other
            CASE(AU_OP_PRINT) : {
                const au_value_t reg = frame.regs[frame.bc[1]];
                tl->print_fn(reg);
                DISPATCH;
            }
            CASE(AU_OP_NOP) : { DISPATCH; }
#undef COPY_VALUE
#ifndef AU_USE_DISPATCH_JMP
        }
#endif
    }
end:
#ifdef AU_USE_ALLOCA
    if (_Likely(alloca_values)) {
#ifndef AU_FEAT_DELAYED_RC
        int n_values = bcs->num_registers + bcs->locals_len;
        for (int i = 0; i < n_values; i++) {
            au_value_deref(alloca_values[i]);
        }
#endif
    } else {
#ifndef AU_FEAT_DELAYED_RC
        for (int i = 0; i < bcs->num_registers; i++) {
            au_value_deref(frame.regs[i]);
        }
        for (int i = 0; i < bcs->locals_len; i++) {
            au_value_deref(frame.locals[i]);
        }
#endif
        free(frame.regs);
        free(frame.locals);
        frame.regs = 0;
        frame.locals = 0;
    }
#else
#ifndef AU_FEAT_DELAYED_RC
    for (int i = 0; i < bcs->num_registers; i++) {
        au_value_deref(frame->regs[i]);
    }
    for (int i = 0; i < bcs->locals_len; i++) {
        au_value_deref(frame->locals[i]);
    }
#endif
    free(frame->locals);
#endif

#ifndef AU_FEAT_DELAYED_RC
    if (frame.self)
        frame.self->header.rc--;
#endif

    tl->current_frame = frame.link;
    return frame.retval;
}
