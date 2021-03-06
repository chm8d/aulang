// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "bc.h"
#include "fn/main.h"
#include "hm_vars.h"
#include "platform/platform.h"
#include "rt/au_class.h"
#include "rt/value.h"
#include "str_array.h"

struct au_program_data_val {
    au_value_t real_value;
    uint32_t buf_idx;
    uint32_t buf_len;
};

AU_ARRAY_COPY(struct au_program_data_val, au_program_data_vals, 1)

#define AU_SM_FUNC_ID_MAIN ((size_t)-1)

struct au_program_source_map {
    size_t bc_from;
    size_t bc_to;
    size_t source_start;
    size_t func_idx;
};

AU_ARRAY_STRUCT(struct au_program_source_map, au_program_source_map_array,
                1)

#define AU_PROGRAM_IMPORT_NO_MODULE ((size_t)-1)

struct au_program_import {
    char *path;
    size_t module_idx;
};

/// [func] Deinitializes an au_program_import instance
/// @param data instance to be deinitialized
AU_PUBLIC void au_program_import_del(struct au_program_import *data);

AU_ARRAY_STRUCT(struct au_program_import, au_program_import_array, 1)

#define AU_IMPORTED_MODULE_NOT_STDLIB ((size_t)-1)

struct au_program_data;
struct au_imported_module {
    struct au_hm_vars fn_map;
    struct au_hm_vars class_map;
    struct au_hm_vars const_map;
    size_t stdlib_module_idx;
};

AU_ARRAY_STRUCT(struct au_imported_module, au_imported_module_array, 1)

/// [func] Initializes an au_imported_module instance
/// @param data instance to be initialized
AU_PUBLIC void au_imported_module_init(struct au_imported_module *data);

/// [func] Deinitializes an au_imported_module instance
/// @param data instance to be deinitialized
AU_PUBLIC void au_imported_module_del(struct au_imported_module *data);

struct au_program_data {
    struct au_fn_array fns;
    struct au_hm_vars fn_map;
    struct au_program_data_vals data_val;
    uint8_t *data_buf;
    size_t data_buf_len;
    size_t tl_constant_start;
    struct au_program_import_array imports;
    struct au_hm_vars imported_module_map;
    struct au_imported_module_array imported_modules;
    char *file;
    char *cwd;
    struct au_program_source_map_array source_map;
    struct au_str_array fn_names;
    struct au_class_interface_ptr_array classes;
    struct au_hm_vars class_map;
    struct au_hm_vars exported_consts;
};

/// [func] Initializes an au_program_data instance
/// @param data instance to be initialized
AU_PUBLIC void au_program_data_init(struct au_program_data *data);

/// [func] Deinitializes an au_program_data instance
/// @param data instance to be deinitialized
AU_PUBLIC void au_program_data_del(struct au_program_data *data);

/// [func] Adds constant value data into a au_program_data instance
/// @param p_data au_program_data instance
/// @param value au_value_t representation of constant
/// @param v_data byte array of internal constant data
/// @param v_len size of `v_data`
/// @return index of the data
AU_PRIVATE int au_program_data_add_data(struct au_program_data *p_data,
                                        au_value_t value, uint8_t *v_data,
                                        size_t v_len);

struct au_program {
    struct au_bc_storage main;
    struct au_program_data data;
};

/// [func] Debugs an au_program instance
AU_PUBLIC void au_program_dbg(const struct au_program *p);

/// [func] Initializes an au_program instance
/// @param p instance to be deinitialized
AU_PUBLIC void au_program_del(struct au_program *p);
