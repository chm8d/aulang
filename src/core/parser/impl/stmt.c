// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information

#include "stmt.h"
#include "bc.h"
#include "def.h"
#include "expr.h"
#include "regs.h"
#include "resolve.h"

#include <stdio.h>

int au_parser_exec_with_semicolon(struct au_parser *p, struct au_lexer *l,
                                  int retval) {
    if (!retval)
        return retval;
    struct au_token tok = au_lexer_next(l);
    if (tok.type == AU_TOK_EOF)
        return 1;
    EXPECT_TOKEN(tok.type == AU_TOK_OPERATOR && tok.len == 1 &&
                     tok.src[0] == ';',
                 tok, "';'");
    return 1;
}

int au_parser_exec_statement(struct au_parser *p, struct au_lexer *l) {
#define WITH_SEMICOLON(FUNC)                                              \
    au_parser_exec_with_semicolon(p, l, FUNC(p, l))
    const struct au_token tok = au_lexer_peek(l);
    const size_t bc_from = p->bc.len;
    int retval = 0;
    if (tok.type == AU_TOK_EOF) {
        return -1;
    } else if (tok.type == AU_TOK_IDENTIFIER) {
        // Top level statements
        if (token_keyword_cmp(&tok, "struct")) {
            EXPECT_GLOBAL_SCOPE(tok);
            au_lexer_next(l);
            retval = au_parser_exec_struct_statement(p, l, 0);
        } else if (token_keyword_cmp(&tok, "func")) {
            EXPECT_GLOBAL_SCOPE(tok);
            au_lexer_next(l);
            retval = au_parser_exec_func_statement(p, l, 0);
        } else if (token_keyword_cmp(&tok, "import")) {
            EXPECT_GLOBAL_SCOPE(tok);
            au_lexer_next(l);
            retval = WITH_SEMICOLON(au_parser_exec_import_statement);
        } else if (token_keyword_cmp(&tok, "public")) {
            EXPECT_GLOBAL_SCOPE(tok);
            au_lexer_next(l);
            retval = au_parser_exec_public_statement(p, l);
        }
        // Regular statements
        else if (token_keyword_cmp(&tok, "let")) {
            au_lexer_next(l);
            retval = WITH_SEMICOLON(au_parser_exec_let_statement);
        } else if (token_keyword_cmp(&tok, "const")) {
            au_lexer_next(l);
            retval = au_parser_exec_with_semicolon(
                p, l, au_parser_exec_const_statement(p, l, 0));
        } else if (token_keyword_cmp(&tok, "if")) {
            au_lexer_next(l);
            retval = au_parser_exec_if_statement(p, l);
        } else if (token_keyword_cmp(&tok, "while")) {
            au_lexer_next(l);
            retval = au_parser_exec_while_statement(p, l);
        } else if (token_keyword_cmp(&tok, "print")) {
            au_lexer_next(l);
            retval = au_parser_exec_print_statement(p, l);
        } else if (token_keyword_cmp(&tok, "return")) {
            au_lexer_next(l);
            retval = WITH_SEMICOLON(au_parser_exec_return_statement);
        } else if (token_keyword_cmp(&tok, "raise")) {
            au_lexer_next(l);
            retval = WITH_SEMICOLON(au_parser_exec_raise_statement);
        } else {
            retval = WITH_SEMICOLON(au_parser_exec_expr);
        }
    } else {
        retval = WITH_SEMICOLON(au_parser_exec_expr);
    }
    if (retval) {
        const size_t bc_to = p->bc.len;
        const size_t source_start = tok.src - l->src;
        if (bc_from != bc_to) {
            struct au_program_source_map map =
                (struct au_program_source_map){
                    .bc_from = bc_from,
                    .bc_to = bc_to,
                    .source_start = source_start,
                    .func_idx = p->func_idx,
                };
            au_program_source_map_array_add(&p->p_data->source_map, map);
        }
    }
    return retval;
}

int au_parser_exec_import_statement(struct au_parser *p,
                                    struct au_lexer *l) {
    const struct au_token path_tok = au_lexer_next(l);
    EXPECT_TOKEN(path_tok.type == AU_TOK_STRING, path_tok, "string");

    char *path_dup = au_data_malloc(path_tok.len + 1);
    memcpy(path_dup, path_tok.src, path_tok.len);
    path_dup[path_tok.len] = 0;

    const size_t idx = p->p_data->imports.len;
    struct au_token tok = au_lexer_peek(l);
    if (token_keyword_cmp(&tok, "as")) {
        au_lexer_next(l);
        const struct au_token module_tok = au_lexer_next(l);

        const size_t module_idx = p->p_data->imported_modules.len;
        struct au_imported_module module;
        au_imported_module_init(&module);
        au_imported_module_array_add(&p->p_data->imported_modules, module);

        const au_hm_var_value_t *old_value =
            au_hm_vars_add(&p->p_data->imported_module_map, module_tok.src,
                           module_tok.len, module_idx);
        if (old_value != 0) {
            p->res = (struct au_parser_result){
                .type = AU_PARSER_RES_DUPLICATE_MODULE,
                .data.duplicate_id.name_token = module_tok,
            };
            return 0;
        }

        const struct au_program_import import = (struct au_program_import){
            .path = path_dup,
            .module_idx = module_idx,
        };
        au_program_import_array_add(&p->p_data->imports, import);
    } else {
        const struct au_program_import import = (struct au_program_import){
            .path = path_dup,
            .module_idx = AU_PROGRAM_IMPORT_NO_MODULE,
        };
        au_program_import_array_add(&p->p_data->imports, import);
    }

    au_parser_emit_bc_u8(p, AU_OP_IMPORT);
    au_parser_emit_pad8(p);
    au_parser_emit_bc_u16(p, (uint16_t)idx);

    return 1;
}

int au_parser_exec_public_statement(struct au_parser *p,
                                    struct au_lexer *l) {
    struct au_token tok = au_lexer_next(l);
    if (token_keyword_cmp(&tok, "func")) {
        return au_parser_exec_func_statement(p, l, 1);
    } else if (token_keyword_cmp(&tok, "struct")) {
        return au_parser_exec_struct_statement(p, l, 1);
    } else if (token_keyword_cmp(&tok, "const")) {
        return au_parser_exec_with_semicolon(
            p, l, au_parser_exec_const_statement(p, l, 1));
    } else {
        EXPECT_TOKEN(0, tok, "'func', 'struct', 'const'");
    }

    return 1;
}

int au_parser_exec_struct_statement(struct au_parser *p,
                                    struct au_lexer *l, int exported) {
    uint32_t class_flags = 0;
    if (exported)
        class_flags |= AU_CLASS_FLAG_EXPORTED;

    // Identifier:

    const struct au_token id_tok = au_lexer_next(l);
    EXPECT_TOKEN(id_tok.type == AU_TOK_IDENTIFIER, id_tok, "identifier");

    au_hm_var_value_t class_value = p->p_data->classes.len;
    au_hm_var_value_t *old_value = au_hm_vars_add(
        &p->p_data->class_map, id_tok.src, id_tok.len, class_value);
    if (old_value != 0) {
        p->res = (struct au_parser_result){
            .type = AU_PARSER_RES_DUPLICATE_CLASS,
            .data.duplicate_id.name_token = id_tok,
        };
        return 0;
    }
    au_class_interface_ptr_array_add(&p->p_data->classes, 0);

    struct au_class_interface *interface =
        au_data_malloc(sizeof(struct au_class_interface));
    au_class_interface_init(interface,
                            au_data_strndup(id_tok.src, id_tok.len));
    interface->flags = class_flags;

    // Body:

    const struct au_token struct_body_start = au_lexer_next(l);
    if (struct_body_start.type == AU_TOK_OPERATOR &&
        struct_body_start.len == 1 && struct_body_start.src[0] == ';') {
        au_class_interface_ptr_array_set(&p->p_data->classes, class_value,
                                         interface);
        return 1;
    }
    EXPECT_TOKEN(struct_body_start.type == AU_TOK_OPERATOR &&
                     struct_body_start.len == 1 &&
                     struct_body_start.src[0] == '{',
                 struct_body_start, "'{'");

#define RAISE_DUPLICATE_PROP(name_tok)                                    \
    do {                                                                  \
        p->res = (struct au_parser_result){                               \
            .type = AU_PARSER_RES_DUPLICATE_PROP,                         \
            .data.duplicate_id.name_token = name_tok,                     \
        };                                                                \
        return 0;                                                         \
    } while (0)

    PARSE_COMMA_LIST('}', "struct field", {
        const struct au_token name_tok = au_lexer_next(l);
        EXPECT_TOKEN(name_tok.type == AU_TOK_IDENTIFIER, name_tok,
                     "struct field name");
        au_hm_var_value_t prop_value = interface->map.nitems;

        const au_hm_var_value_t *old_prop_value = au_hm_vars_add(
            &interface->map, name_tok.src, name_tok.len, prop_value);
        if (old_prop_value != 0)
            RAISE_DUPLICATE_PROP(name_tok);
    });

#undef RAISE_DUPLICATE_PROP

    au_class_interface_ptr_array_set(&p->p_data->classes, class_value,
                                     interface);
    return 1;
}

int au_parser_exec_func_statement(struct au_parser *p, struct au_lexer *l,
                                  int exported) {
    uint32_t fn_flags = 0;
    if (exported)
        fn_flags |= AU_FN_FLAG_EXPORTED;

    size_t class_idx = CLASS_ID_NONE;
    struct au_class_interface *class_interface = 0;
    struct au_token self_tok = (struct au_token){.type = AU_TOK_EOF};

    // FuncHead:

    const struct au_token lookahead = au_lexer_peek(l);
    if (lookahead.type == AU_TOK_OPERATOR && lookahead.len == 1 &&
        lookahead.src[0] == '(') {
        au_lexer_next(l);
        fn_flags |= AU_FN_FLAG_HAS_CLASS;

        self_tok = au_lexer_next(l);
        EXPECT_TOKEN(self_tok.type == AU_TOK_IDENTIFIER, self_tok,
                     "identifier");

        const struct au_token colon = au_lexer_next(l);
        EXPECT_TOKEN(colon.type == AU_TOK_OPERATOR && colon.len == 1 &&
                         colon.src[0] == ':',
                     colon, "':'");

        struct au_token module_tok = (struct au_token){.type = AU_TOK_EOF};
        struct au_token name_tok = (struct au_token){.type = AU_TOK_EOF};
        if (!au_parser_exec_fixed_element_name(p, l, &module_tok,
                                               &name_tok))
            return 0;

        if (module_tok.type != AU_TOK_EOF) {
            struct au_imported_module *module =
                au_parser_resolve_module(p, module_tok, 0);
            if (module == 0) {
                p->res = (struct au_parser_result){
                    .type = AU_PARSER_RES_UNKNOWN_MODULE,
                    .data.unknown_id.name_token = module_tok,
                };
                return 0;
            }
            au_hm_var_value_t class_val = p->p_data->classes.len;
            const au_hm_var_value_t *old_class_val = au_hm_vars_add(
                &module->class_map, name_tok.src, name_tok.len, class_val);
            if (old_class_val != 0) {
                class_val = *old_class_val;
            } else {
                au_class_interface_ptr_array_add(&p->p_data->classes, 0);
            }
            class_idx = class_val;
        } else {
            const au_hm_var_value_t *class_val = au_hm_vars_get(
                &p->p_data->class_map, name_tok.src, name_tok.len);
            if (class_val == 0) {
                p->res = (struct au_parser_result){
                    .type = AU_PARSER_RES_UNKNOWN_CLASS,
                    .data.unknown_id.name_token = name_tok,
                };
                return 0;
            }
            class_idx = *class_val;
            class_interface = p->p_data->classes.data[class_idx];
        }

        const struct au_token func_head_end = au_lexer_next(l);
        EXPECT_TOKEN(func_head_end.type == AU_TOK_OPERATOR &&
                         func_head_end.len == 1 &&
                         func_head_end.src[0] == ')',
                     func_head_end, "')'");
    }

    // Identifier:

    const struct au_token id_tok = au_lexer_next(l);
    EXPECT_TOKEN(id_tok.type == AU_TOK_IDENTIFIER, id_tok, "identifier");

    int expected_no_fail = 0;
    int expected_num_args = -1;

    au_hm_var_value_t func_value = p->p_data->fns.len;
    au_hm_var_value_t *old_value = au_hm_vars_add(
        &p->p_data->fn_map, id_tok.src, id_tok.len, func_value);
    struct au_token old_id_tok = (struct au_token){.type = AU_TOK_EOF};

    if (old_value) {
        struct au_fn *old = &p->p_data->fns.data[*old_value];
        expected_num_args = au_fn_num_args(old);

        if ((old->flags & AU_FN_FLAG_MAY_FAIL) == 0)
            expected_no_fail = 1;

        const int has_same_visibility =
            (old->flags & AU_FN_FLAG_EXPORTED) ==
            (fn_flags & AU_FN_FLAG_EXPORTED);

        if (old->type == AU_FN_NONE) {
            func_value = *old_value;
            old_id_tok = old->as.none_func.name_token;
        }
        // Record the new function into a multi-dispatch function, and
        // turn the regular old function into multi-dispatch function if
        // necessary
        else if (has_same_visibility &&
                 ((old->flags & AU_FN_FLAG_HAS_CLASS) != 0 ||
                  (fn_flags & AU_FN_FLAG_HAS_CLASS) != 0)) {
            if (old->type == AU_FN_DISPATCH) {
                struct au_dispatch_func_instance el =
                    (struct au_dispatch_func_instance){
                        .function_idx = func_value,
                        .class_idx = class_idx,
                        .class_interface_cache = class_interface,
                    };
                au_dispatch_func_instance_array_add(
                    &old->as.dispatch_func.data, el);
            } else {
                const size_t fallback_fn_idx = p->p_data->fns.len;
                const size_t new_fn_idx = p->p_data->fns.len + 1;

                const struct au_fn fallback_fn = *old;

                struct au_dispatch_func dispatch_func = {0};
                dispatch_func.num_args = expected_num_args;

                if ((fallback_fn.flags & AU_FN_FLAG_HAS_CLASS) != 0 &&
                    (fallback_fn.type == AU_FN_BC)) {
                    dispatch_func.fallback_fn =
                        AU_DISPATCH_FUNC_NO_FALLBACK;
                    struct au_dispatch_func_instance el =
                        (struct au_dispatch_func_instance){
                            .function_idx = fallback_fn_idx,
                            .class_idx = fallback_fn.as.bc_func.class_idx,
                            .class_interface_cache =
                                fallback_fn.as.bc_func
                                    .class_interface_cache,
                        };
                    au_dispatch_func_instance_array_add(
                        &dispatch_func.data, el);
                } else {
                    dispatch_func.fallback_fn = fallback_fn_idx;
                }
                struct au_dispatch_func_instance el =
                    (struct au_dispatch_func_instance){
                        .function_idx = new_fn_idx,
                        .class_idx = class_idx,
                        .class_interface_cache = class_interface,
                    };
                au_dispatch_func_instance_array_add(&dispatch_func.data,
                                                    el);

                old->type = AU_FN_DISPATCH;
                old->flags |= AU_FN_FLAG_HAS_CLASS;
                old->as.dispatch_func = dispatch_func;
                old = 0;

                au_fn_array_add(&p->p_data->fns, fallback_fn);
                func_value = new_fn_idx;
            }

            struct au_fn none_func = (struct au_fn){
                .type = AU_FN_NONE,
                .as.none_func.num_args = 0,
                .as.none_func.name_token.type = AU_TOK_EOF,
            };
            au_fn_array_add(&p->p_data->fns, none_func);
            au_str_array_add(&p->p_data->fn_names,
                             au_data_strndup(id_tok.src, id_tok.len));
        } else {
            func_value = *old_value;
            au_fn_del(old);
            *old = (struct au_fn){
                .type = AU_FN_NONE,
                .as.none_func.num_args = 0,
                .as.none_func.name_token.type = AU_TOK_EOF,
            };
        }
    } else {
        struct au_fn none_func = (struct au_fn){
            .type = AU_FN_NONE,
            .as.none_func.num_args = 0,
            .as.none_func.name_token.type = AU_TOK_EOF,
        };
        au_fn_array_add(&p->p_data->fns, none_func);
        au_str_array_add(&p->p_data->fn_names,
                         au_data_strndup(id_tok.src, id_tok.len));
    }

    struct au_parser func_p = {0};
    au_parser_init(&func_p, p->p_data);
    func_p.top_level = p;
    func_p.self_name = id_tok.src;
    func_p.self_len = id_tok.len;
    func_p.func_idx = func_value;
    func_p.class_interface = class_interface;
    struct au_bc_storage bcs = {0};

    if (self_tok.type != AU_TOK_EOF) {
        if (!token_keyword_cmp(&self_tok, "_")) {
            func_p.self_keyword = self_tok.src;
            func_p.self_keyword_len = self_tok.len;
            au_hm_vars_add(&func_p.vars.data[func_p.vars.len - 1],
                           self_tok.src, self_tok.len, 0);
        }
        bcs.num_args++;
        au_parser_bump_local(&func_p);
    }

    // FuncArgs:

    const struct au_token func_args_start = au_lexer_next(l);
    EXPECT_TOKEN(func_args_start.type == AU_TOK_OPERATOR &&
                     func_args_start.len == 1 &&
                     func_args_start.src[0] == '(',
                 func_args_start, "'('");

#define RAISE_DUPLICATE_ARG(TOK)                                          \
    do {                                                                  \
        p->res = (struct au_parser_result){                               \
            .type = AU_PARSER_RES_DUPLICATE_ARG,                          \
            .data.duplicate_id.name_token = TOK,                          \
        };                                                                \
        return 0;                                                         \
    } while (0)

    PARSE_COMMA_LIST(')', "arguments", {
        struct au_token tok = au_lexer_next(l);
        EXPECT_TOKEN(tok.type == AU_TOK_IDENTIFIER, tok, "identifier");
        const au_hm_var_value_t *old =
            au_hm_vars_add(&func_p.vars.data[func_p.vars.len - 1], tok.src,
                           tok.len, bcs.num_args);
        if (old != NULL)
            RAISE_DUPLICATE_ARG(tok);
        func_p.local_placement++;
        EXPECT_BYTECODE(func_p.local_placement < AU_MAX_LOCALS);
        bcs.num_args++;
    });

#undef RAISE_DUPLICATE_ARG

    if (expected_num_args != -1 && bcs.num_args != expected_num_args) {
        if (old_id_tok.type != AU_TOK_EOF) {
            // The old_id_tok token is the identifier token belonging to
            // the call expression that forward-declared this function. As
            // such, our parser error should be reported differently.
            p->res = (struct au_parser_result){
                .type = AU_PARSER_RES_WRONG_ARGS,
                .data.wrong_args.got_args = expected_num_args,
                .data.wrong_args.expected_args = bcs.num_args,
                .data.wrong_args.at_token = old_id_tok,
            };
            return 0;
        } else {
            p->res = (struct au_parser_result){
                .type = AU_PARSER_RES_WRONG_ARGS,
                .data.wrong_args.got_args = bcs.num_args,
                .data.wrong_args.expected_args = expected_num_args,
                .data.wrong_args.at_token = id_tok,
            };
            return 0;
        }
    }
    func_p.self_num_args = bcs.num_args;

    if ((fn_flags & AU_FN_FLAG_HAS_CLASS) != 0) {
        au_parser_emit_bc_u8(&func_p, AU_OP_LOAD_SELF);
        au_parser_emit_pad8(&func_p);
        au_parser_emit_pad8(&func_p);
        au_parser_emit_pad8(&func_p);
    }
    func_p.self_flags = fn_flags;

    // Block:

    const size_t source_map_start = p->p_data->source_map.len;
    if (!au_parser_exec_block(&func_p, l, 0)) {
        p->res = func_p.res;
        func_p.res = (struct au_parser_result){0};
        au_parser_del(&func_p);
        return 0;
    }
    au_parser_emit_bc_u8(&func_p, AU_OP_RET_NULL);

    if (expected_no_fail &&
        (func_p.self_flags & AU_FN_FLAG_MAY_FAIL) != 0) {
        printf("expected %.*s not to fail\n", (int)id_tok.len, id_tok.src);
        abort(); // TODO
    }

    bcs.num_locals = func_p.max_locals;
    bcs.num_registers = func_p.max_register + 1;
    bcs.num_values = bcs.num_locals + bcs.num_registers;
    bcs.bc = func_p.bc;
    bcs.class_idx = class_idx;
    bcs.class_interface_cache = class_interface;
    bcs.source_map_start = source_map_start;
    bcs.func_idx = func_p.func_idx;
    func_p.bc = (struct au_bc_buf){0};

    for (size_t i = 0; i < func_p.self_fill_call.len; i++) {
        const size_t offset = func_p.self_fill_call.data[i];
        au_replace_bc_u16(&bcs.bc, offset, func_value);
    }
    *au_fn_array_at_mut(&p->p_data->fns, func_value) = (struct au_fn){
        .type = AU_FN_BC,
        .flags = func_p.self_flags,
        .as.bc_func = bcs,
    };

    au_parser_del(&func_p);

    return 1;
}

int au_parser_exec_let_statement(struct au_parser *p, struct au_lexer *l) {
    // Identifier:

    const struct au_token id_tok = au_lexer_next(l);
    EXPECT_TOKEN(id_tok.type == AU_TOK_IDENTIFIER, id_tok, "identifier");

    // "=":

    const struct au_token eq_tok = au_lexer_next(l);
    EXPECT_TOKEN(eq_tok.type == AU_TOK_OPERATOR &&
                     (eq_tok.len == 1 && eq_tok.src[0] == '='),
                 eq_tok, "'='");

    // Expression:

    if (!au_parser_exec_expr(p, l))
        return 0;
    const uint8_t new_reg = au_parser_last_reg(p);

    au_parser_emit_bc_u8(p, AU_OP_MOV_REG_LOCAL);
    au_parser_emit_bc_u8(p, new_reg);

    const au_hm_var_value_t *old_value = au_hm_vars_get(
        &p->vars.data[p->vars.len - 1], id_tok.src, id_tok.len);
    if (old_value) {
        if (*old_value < p->local_to_reg.len) {
            const uint8_t old_reg = p->local_to_reg.data[*old_value];
            AU_BA_RESET_BIT(p->pinned_regs, old_reg);
            p->local_to_reg.data[*old_value] = new_reg;
            AU_BA_SET_BIT(p->pinned_regs, new_reg);
        }
        au_parser_emit_bc_u16(p, *old_value);
    } else {
        const au_hm_var_value_t new_value =
            (au_hm_var_value_t)au_parser_bump_local(p);
        EXPECT_BYTECODE(new_value <= AU_MAX_LOCALS);
        au_hm_vars_add(&p->vars.data[p->vars.len - 1], id_tok.src,
                       id_tok.len, new_value);
        au_parser_emit_bc_u16(p, new_value);
    }

    return 1;
}

int au_parser_exec_const_statement(struct au_parser *p, struct au_lexer *l,
                                   int exported) {
    // Identifier:

    const struct au_token id_tok = au_lexer_next(l);
    EXPECT_TOKEN(id_tok.type == AU_TOK_IDENTIFIER, id_tok, "identifier");

    // "=":

    const struct au_token eq_tok = au_lexer_next(l);
    EXPECT_TOKEN(eq_tok.type == AU_TOK_OPERATOR &&
                     (eq_tok.len == 1 && eq_tok.src[0] == '='),
                 eq_tok, "'='");

    // Expression:

    if (!au_parser_exec_expr(p, l))
        return 0;
    const uint8_t right_reg = au_parser_pop_reg(p);

    if (p->func_idx == AU_SM_FUNC_ID_MAIN) {
        // Constant in main function
        size_t data_len = p->p_data->data_val.len;
        au_hm_var_value_t *old =
            au_hm_vars_add(&p->consts, id_tok.src, id_tok.len, data_len);

        if (old == 0) {
            au_program_data_add_data(p->p_data, au_value_none(), 0, 0);
            au_parser_emit_bc_u8(p, AU_OP_SET_CONST);
            au_parser_emit_bc_u8(p, right_reg);
            au_parser_emit_bc_u16(p, data_len);
        } else {
            p->res = (struct au_parser_result){
                .type = AU_PARSER_RES_DUPLICATE_CONST,
                .data.duplicate_id.name_token = id_tok,
            };
            return 0;
        }

        if (exported) {
            au_hm_vars_add(&p->p_data->exported_consts, id_tok.src,
                           id_tok.len, data_len);
        }
    } else {
        abort(); // TODO
    }

    return 1;
}

int au_parser_exec_if_statement(struct au_parser *p, struct au_lexer *l) {
    au_parser_flush_cached_regs(p);

    // Expression (if condition):

    int has_else_part = 0;
    if (!au_parser_exec_expr(p, l))
        return 0;

    const size_t c_len = p->bc.len;
    au_parser_flush_cached_regs(p);

    au_parser_emit_bc_u8(p, AU_OP_JNIF);
    au_parser_emit_bc_u8(p, au_parser_pop_reg(p));
    const size_t c_replace_idx = p->bc.len;
    au_parser_emit_pad8(p);
    au_parser_emit_pad8(p);

    // Block:

    size_t body_len;
    size_t body_replace_idx = (size_t)-1;
    if (!au_parser_exec_block(p, l, 1))
        return 0;
    au_parser_flush_cached_regs(p);

    if (!is_return_op(p->bc.data[p->bc.len - 4])) {
        body_len = p->bc.len;

        au_parser_emit_bc_u8(p, AU_OP_JREL);
        au_parser_emit_pad8(p);
        body_replace_idx = p->bc.len;
        au_parser_emit_pad8(p);
        au_parser_emit_pad8(p);
    }

    // IfTail:

    {
        const struct au_token tok = au_lexer_peek(l);
        if (token_keyword_cmp(&tok, "else")) {
            au_lexer_next(l);

            const size_t else_start = p->bc.len;
            {
                const struct au_token tok = au_lexer_peek(l);
                if (token_keyword_cmp(&tok, "if")) {
                    au_lexer_next(l);
                    if (!au_parser_exec_if_statement(p, l))
                        return 0;
                } else {
                    if (!au_parser_exec_block(p, l, 1))
                        return 0;
                }
            }
            au_parser_flush_cached_regs(p);
            has_else_part = 1;

            const size_t else_len = p->bc.len;
            au_parser_flush_cached_regs(p);

            size_t else_replace_idx = (size_t)-1;
            if (!is_return_op(p->bc.data[p->bc.len - 4])) {
                au_parser_emit_bc_u8(p, AU_OP_JREL);
                au_parser_emit_pad8(p);
                else_replace_idx = p->bc.len;
                au_parser_emit_pad8(p);
                au_parser_emit_pad8(p);
            }

            const size_t end_len = p->bc.len;

            // Else jump
            if (else_replace_idx != (size_t)-1) {
                const size_t offset = (end_len - else_len) / 4;
                EXPECT_BYTECODE(offset <= (uint16_t)-1);
                au_parser_replace_bc_u16(p, else_replace_idx,
                                         (uint16_t)offset);
            }

            // Condition jump
            {
                const size_t offset = (else_start - c_len) / 4;
                EXPECT_BYTECODE(offset <= (uint16_t)-1);
                au_parser_replace_bc_u16(p, c_replace_idx,
                                         (uint16_t)offset);
            }
        }
    }

    const size_t end_len = p->bc.len;

    // Condition jump
    if (!has_else_part) {
        const size_t offset = (end_len - c_len) / 4;
        EXPECT_BYTECODE(offset <= (uint16_t)-1);
        au_parser_replace_bc_u16(p, c_replace_idx, (uint16_t)offset);
    }

    // Block jump
    if (body_replace_idx != (size_t)-1) {
        const size_t offset = (end_len - body_len) / 4;
        EXPECT_BYTECODE(offset <= (uint16_t)-1);
        au_parser_replace_bc_u16(p, body_replace_idx, (uint16_t)offset);
    }

    // The resulting bytecode should look like this:
    //       (flush locals)
    //   condition:
    //       ...
    //       (flush locals)
    //       jnif [cond], else
    //   body:
    //       ...
    //       (flush locals)
    //       jmp if_end
    //   else:
    //       ...
    //       (flush locals)
    //       jmp if_end
    //   if_end:
    //       ...
    return 1;
}

int au_parser_exec_while_statement(struct au_parser *p,
                                   struct au_lexer *l) {
    au_parser_flush_cached_regs(p);

    // Expression (while condition):

    const size_t cond_part = p->bc.len;
    if (!au_parser_exec_expr(p, l))
        return 0;

    au_parser_flush_cached_regs(p);

    const size_t c_len = p->bc.len;
    au_parser_emit_bc_u8(p, AU_OP_JNIF);
    au_parser_emit_bc_u8(p, au_parser_pop_reg(p));
    const size_t c_replace_idx = p->bc.len;
    au_parser_emit_pad8(p);
    au_parser_emit_pad8(p);

    // Block:

    if (!au_parser_exec_block(p, l, 1))
        return 0;
    au_parser_flush_cached_regs(p);

    size_t body_len = p->bc.len;
    size_t body_replace_idx = (size_t)-1;
    au_parser_emit_bc_u8(p, AU_OP_JRELB);
    au_parser_emit_pad8(p);
    body_replace_idx = p->bc.len;
    au_parser_emit_pad8(p);
    au_parser_emit_pad8(p);

    const size_t end_len = p->bc.len;

    // Condition jump
    {
        const size_t offset = (end_len - c_len) / 4;
        EXPECT_BYTECODE(offset <= (uint16_t)-1);
        au_parser_replace_bc_u16(p, c_replace_idx, (uint16_t)offset);
    }

    // Block jump
    if (body_replace_idx != (size_t)-1) {
        const size_t offset = (body_len - cond_part) / 4;
        EXPECT_BYTECODE(offset <= (uint16_t)-1);
        au_parser_replace_bc_u16(p, body_replace_idx, (uint16_t)offset);
    }

    // The resulting bytecode should look like this:
    //       (flush locals)
    //   condition:
    //       [cond] = ...
    //       (flush locals)
    //       jnif [cond], end
    //   block:
    //       ...
    //       (flush locals)
    //       jrelb condition
    //   end:
    return 1;
}

int au_parser_exec_print_statement(struct au_parser *p,
                                   struct au_lexer *l) {
    PARSE_COMMA_LIST(';', "print statement", {
        if (!au_parser_exec_expr(p, l))
            return 0;
        au_parser_emit_bc_u8(p, AU_OP_PRINT);
        au_parser_emit_bc_u8(p, au_parser_pop_reg(p));
        au_parser_emit_pad8(p);
        au_parser_emit_pad8(p);
    });
    return 1;
}

int au_parser_exec_return_statement(struct au_parser *p,
                                    struct au_lexer *l) {
    // Expression:

    if (!au_parser_exec_expr(p, l))
        return 0;
    const uint8_t reg = au_parser_pop_reg(p);

    if (p->bc.len > 4 &&
        (p->bc.data[p->bc.len - 4] == AU_OP_MOV_LOCAL_REG &&
         p->bc.data[p->bc.len - 3] == reg)) {
        // OPTIMIZE: peephole optimization for local returns
        p->bc.data[p->bc.len - 4] = AU_OP_RET_LOCAL;
    } else {
        au_parser_emit_bc_u8(p, AU_OP_RET);
        au_parser_emit_bc_u8(p, reg);
        au_parser_emit_pad8(p);
        au_parser_emit_pad8(p);
    }
    return 1;
}

int au_parser_exec_raise_statement(struct au_parser *p,
                                   struct au_lexer *l) {
    // Expression:

    if (!au_parser_exec_expr(p, l))
        return 0;

    au_parser_emit_bc_u8(p, AU_OP_RAISE);
    au_parser_emit_bc_u8(p, au_parser_pop_reg(p));
    au_parser_emit_pad8(p);
    au_parser_emit_pad8(p);
    p->self_flags |= AU_FN_FLAG_MAY_FAIL;
    return 1;
}

int au_parser_exec_block(struct au_parser *p, struct au_lexer *l,
                         const int allocate_local_vars) {
    p->block_level++;

    if (allocate_local_vars) {
        struct au_hm_vars vars = {0};
        au_hm_vars_init(&vars);
        vars_array_add(&p->vars, vars);
    }

    struct au_token tok = au_lexer_next(l);
    EXPECT_TOKEN(tok.type == AU_TOK_OPERATOR && tok.len == 1 &&
                     tok.src[0] == '{',
                 tok, "'{'");

    while (1) {
        tok = au_lexer_peek(l);
        if (tok.type == AU_TOK_OPERATOR && tok.len == 1 &&
            tok.src[0] == '}') {
            au_lexer_next(l);
            break;
        }

        int retval = au_parser_exec_statement(p, l);
        if (retval == 0)
            return 0;
        else if (retval == -1)
            break;
        au_parser_flush_free_regs(p);
    }

    if (allocate_local_vars) {
        struct au_hm_vars last_vars = p->vars.data[--p->vars.len];
        au_parser_pop_locals(p, last_vars.nitems);
        au_hm_vars_del(&last_vars);
    }

    p->block_level--;
    return 1;
}
