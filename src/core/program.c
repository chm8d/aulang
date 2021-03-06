// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#include <stdlib.h>
#include <string.h>

#include "core/rt/malloc.h"
#include "program.h"
#include "stdlib/au_stdlib.h"
#include "vm/vm.h"

void au_program_import_del(struct au_program_import *data) {
    au_data_free(data->path);
    memset(data, 0, sizeof(struct au_program_import));
}

void au_imported_module_init(struct au_imported_module *data) {
    memset(data, 0, sizeof(struct au_imported_module));
    au_hm_vars_init(&data->fn_map);
    au_hm_vars_init(&data->class_map);
    data->stdlib_module_idx = AU_IMPORTED_MODULE_NOT_STDLIB;
}

void au_imported_module_del(struct au_imported_module *data) {
    au_hm_vars_del(&data->fn_map);
    au_hm_vars_del(&data->class_map);
}

void au_program_data_init(struct au_program_data *data) {
    memset(data, 0, sizeof(struct au_program_data));
    au_hm_vars_init(&data->fn_map);
    au_hm_vars_init(&data->class_map);
}

void au_program_data_del(struct au_program_data *data) {
    au_hm_vars_del(&data->fn_map);
    for (size_t i = 0; i < data->fns.len; i++)
        au_fn_del(&data->fns.data[i]);
    au_data_free(data->fns.data);
    au_data_free(data->data_val.data);
    au_data_free(data->data_buf);
    for (size_t i = 0; i < data->imports.len; i++)
        au_program_import_del(&data->imports.data[i]);
    au_data_free(data->imports.data);
    au_hm_vars_del(&data->imported_module_map);
    for (size_t i = 0; i < data->imported_modules.len; i++)
        au_imported_module_del(&data->imported_modules.data[i]);
    au_data_free(data->imported_modules.data);
    au_data_free(data->cwd);
    au_data_free(data->file);
    au_data_free(data->source_map.data);
    for (size_t i = 0; i < data->fn_names.len; i++)
        au_data_free(data->fn_names.data[i]);
    au_data_free(data->fn_names.data);
    for (size_t i = 0; i < data->classes.len; i++) {
        if (data->classes.data[i] == 0)
            continue;
        au_class_interface_deref(data->classes.data[i]);
    }
    au_data_free(data->classes.data);
    au_hm_vars_del(&data->class_map);
    au_hm_vars_del(&data->exported_consts);
    memset(data, 0, sizeof(struct au_program_data));
}

int au_program_data_add_data(struct au_program_data *p_data,
                             au_value_t value, uint8_t *v_data,
                             size_t v_len) {
    size_t buf_idx = 0;
    if (v_len != 0) {
        buf_idx = p_data->data_buf_len;
        p_data->data_buf = au_data_realloc(p_data->data_buf,
                                           p_data->data_buf_len + v_len);
        memcpy(&p_data->data_buf[buf_idx], v_data, v_len);
        p_data->data_buf_len += v_len;
    }

    const size_t idx = p_data->data_val.len;
    struct au_program_data_val new_val = (struct au_program_data_val){
        .real_value = value,
        .buf_idx = buf_idx,
        .buf_len = v_len,
    };
    au_program_data_vals_add(&p_data->data_val, new_val);
    return idx;
}

void au_program_del(struct au_program *p) {
    au_bc_storage_del(&p->main);
    au_program_data_del(&p->data);
    memset(p, 0, sizeof(struct au_program));
}
