// This source file is part of the aulang project
// Copyright (c) 2021 the aulang contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#pragma once
#include <stdint.h>

#include "array.h"
#include "hm_vars.h"

#define AU_REGS 256
#define AU_MAX_LOCALS 65536
#define AU_MAX_ARRAY 65536

enum au_opcode {
    OP_EXIT = 0,
    OP_MOV_U16 = 1,
    OP_MUL = 2,
    OP_DIV = 3,
    OP_ADD = 4,
    OP_SUB = 5,
    OP_MOD = 6,
    OP_MOV_REG_LOCAL = 7,
    OP_MOV_LOCAL_REG = 8,
    OP_PRINT = 9,
    OP_EQ = 10,
    OP_NEQ = 11,
    OP_LT = 12,
    OP_GT = 13,
    OP_LEQ = 14,
    OP_GEQ = 15,
    /// In bytecode, operation follows the format:
    /// [  op (8)  ] [  reg (8)  ] [  addr' (16)  ]
    ///   where addr = addr' * 4
    ///   (note that endianness of addr' depends on the platform)
    OP_JIF = 16,
    /// Bytecode is same as OP_JIF
    OP_JNIF = 17,
    OP_JREL = 18,
    OP_JRELB = 19,
    OP_LOAD_CONST = 20,
    OP_MOV_BOOL = 21,
    OP_NOP = 22,
    OP_MUL_ASG = 23,
    OP_DIV_ASG = 24,
    OP_ADD_ASG = 25,
    OP_SUB_ASG = 26,
    OP_MOD_ASG = 27,
    OP_PUSH_ARG = 28,
    OP_CALL = 29,
    OP_RET_LOCAL = 30,
    OP_RET = 31,
    OP_RET_NULL = 32,
    OP_IMPORT = 33,
    OP_ARRAY_NEW = 34,
    OP_ARRAY_PUSH = 35,
    OP_IDX_GET = 36,
    OP_IDX_SET = 37,
    OP_NOT = 38,
    PRINTABLE_OP_LEN,
};

extern const char *au_opcode_dbg[256];

ARRAY_TYPE_COPY(uint8_t, au_bc_buf, 4)

struct au_bc_storage {
    int num_args;
    int locals_len;
    struct au_bc_buf bc;
    int num_registers;
};

/// [func] Initializes an au_bc_storage instance
/// @param bc_storage instance to be initialized
void au_bc_storage_init(struct au_bc_storage *bc_storage);

/// [func] Deinitializes an au_bc_storage instance
/// @param bc_storage instance to be deinitialized
void au_bc_storage_del(struct au_bc_storage *bc_storage);

struct au_program_data;

/// [func] Debugs an bytecode storage container
/// @param bcs the bytecode storage
/// @param data program data
void au_bc_dbg(const struct au_bc_storage *bcs,
               const struct au_program_data *data);
