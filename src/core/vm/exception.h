// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#pragma once
#include "core/rt/value.h"
#include "platform/platform.h"

struct au_vm_thread_local;
struct au_bc_storage;
struct au_program_data;
struct au_vm_frame;

#define X(NAME) AU_INT_ERR_##NAME
enum au_interpreter_result_type {
    X(OK),
    X(INCOMPAT_BIN_OP),
    X(INCOMPAT_CALL),
    X(INDEXING_NON_COLLECTION),
    X(INVALID_INDEX),
};
#undef X

struct au_interpreter_result {
    union {
        struct {
            au_value_t left;
            au_value_t right;
        } incompat_bin_op;
        struct {
            au_value_t value;
        } invalid_collection;
        struct {
            au_value_t collection;
            au_value_t idx;
        } invalid_index;
    } data;
    size_t pos;
    enum au_interpreter_result_type type;
};

AU_PRIVATE size_t au_vm_locate_error(const struct au_vm_frame *frame,
                                     const struct au_bc_storage *bcs,
                                     const struct au_program_data *p_data);