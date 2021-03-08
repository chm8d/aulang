// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "platform/platform.h"
#include "rt/exception.h"
#endif

#define ARRAY_TYPE_COPY(INNER, NAME, IN_CAP)                              \
    struct NAME {                                                         \
        INNER *data;                                                      \
        size_t len;                                                       \
        size_t cap;                                                       \
    };                                                                    \
    static _Unused void NAME##_add(struct NAME *array, INNER el) {        \
        if (array->cap == 0) {                                            \
            array->data = (INNER *)malloc(sizeof(INNER) * IN_CAP);        \
            array->cap = IN_CAP;                                          \
        } else if (array->len == array->cap) {                            \
            array->data = (INNER *)realloc(                               \
                array->data, array->cap * 2 * sizeof(INNER));             \
            array->cap *= 2;                                              \
        }                                                                 \
        array->data[array->len++] = el;                                   \
    }                                                                     \
    static _Unused INNER NAME##_at(const struct NAME *array,              \
                                   size_t idx) {                          \
        if (idx >= array->len)                                            \
            au_fatal_index((void *)array, idx, array->len);               \
        return array->data[idx];                                          \
    }                                                                     \
    static _Unused void NAME##_set(const struct NAME *array, size_t idx,  \
                                   INNER thing) {                         \
        if (idx >= array->len)                                            \
            au_fatal_index((void *)array, idx, array->len);               \
        array->data[idx] = thing;                                         \
    }

#define ARRAY_TYPE_STRUCT(INNER, NAME, IN_CAP)                            \
    ARRAY_TYPE_COPY(INNER, NAME, IN_CAP)                                  \
    static _Unused const INNER *NAME##_at_ptr(const struct NAME *array,   \
                                              size_t idx) {               \
        if (idx >= array->len)                                            \
            au_fatal_index((void *)array, idx, array->len);               \
        return &array->data[idx];                                         \
    }                                                                     \
    static _Unused INNER *NAME##_at_mut(struct NAME *array, size_t idx) { \
        if (idx >= array->len)                                            \
            au_fatal_index((void *)array, idx, array->len);               \
        return &array->data[idx];                                         \
    }
