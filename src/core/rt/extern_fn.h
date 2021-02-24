// This source file is part of the aulang project
// Copyright (c) 2021 the aulang contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#pragma once
#include "platform/platform.h"
#include "value.h"
#endif

struct au_vm_thread_local;
struct au_program_data;
struct au_vm_frame_link;

typedef au_value_t (*au_extern_func_t)(struct au_vm_thread_local *tl,
                                       const au_value_t *args);

struct au_lib_func {
    int num_args;
    au_extern_func_t func;
    const char *name;
    const char *symbol;
};

#define AU_EXTERN_FUNC_DECL(NAME)                                         \
    au_value_t NAME(_Unused struct au_vm_thread_local *_tl,               \
                    _Unused const au_value_t *_args)

#define AU_C_COMP_EXTERN_FUNC_DECL                                        \
    "#define AU_EXTERN_FUNC_DECL(NAME) au_value_t NAME"                  \
    "(struct au_vm_thread_local*,"                                         \
    "const au_value_t*)"                                                  
