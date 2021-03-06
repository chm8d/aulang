// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#include "au_array.h"
#include "au_struct.h"
#include "value.h"

#include "../value_array.h"
#endif

struct au_obj_array {
    struct au_struct header;
    struct au_value_array array;
};

AU_THREAD_LOCAL struct au_struct_vdata au_obj_array_vdata;
static AU_THREAD_LOCAL int au_obj_array_vdata_inited = 0;
static void au_obj_array_vdata_init() {
    if (!au_obj_array_vdata_inited) {
        au_obj_array_vdata = (struct au_struct_vdata){
            .del_fn = (au_struct_del_fn_t)au_obj_array_del,
            .idx_get_fn = (au_struct_idx_get_fn_t)au_obj_array_get,
            .idx_set_fn = (au_struct_idx_set_fn_t)au_obj_array_set,
            .len_fn = (au_struct_len_fn_t)au_obj_array_len,
        };
        au_obj_array_vdata_inited = 1;
    }
}

struct au_obj_array *au_obj_array_new(size_t capacity) {
    struct au_obj_array *obj_array = au_obj_malloc(
        sizeof(struct au_obj_array), (au_obj_del_fn_t)au_obj_array_del);
    au_obj_array_vdata_init();
    obj_array->header = (struct au_struct){
        .vdata = &au_obj_array_vdata,
    };
    obj_array->array.data = au_value_calloc(capacity);
    obj_array->array.len = 0;
    obj_array->array.cap = capacity;
    return obj_array;
}

void au_obj_array_del(struct au_obj_array *obj_array) {
    for (size_t i = 0; i < obj_array->array.len; i++) {
        au_value_deref(obj_array->array.data[i]);
    }
    au_data_free(obj_array->array.data);
}

void au_obj_array_push(struct au_obj_array *obj_array, au_value_t el) {
    au_value_ref(el);
    au_value_array_add(&obj_array->array, el);
}

int au_obj_array_insert(struct au_obj_array *obj_array, int32_t idx,
                        au_value_t el) {
    if ((size_t)idx == obj_array->array.len) {
        au_obj_array_push(obj_array, el);
        return 1;
    } else if ((size_t)idx > obj_array->array.len) {
        return 0;
    }
    au_value_ref(el);
    au_value_array_add(&obj_array->array, au_value_none());
    memmove(&obj_array->array.data[idx + 1], &obj_array->array.data[idx],
            obj_array->array.len - idx - 1);
    obj_array->array.data[idx] = el;
    return 1;
}

au_value_t au_obj_array_pop(struct au_obj_array *obj_array) {
    if (obj_array->array.len == 0)
        return au_value_none();
    return obj_array->array.data[--obj_array->array.len];
}

int au_obj_array_get(struct au_obj_array *obj_array,
                     const au_value_t idx_val, au_value_t *result) {
    if (AU_UNLIKELY(au_value_get_type(idx_val) != AU_VALUE_INT))
        return 0;
    const size_t idx = au_value_get_int(idx_val);
    if (AU_UNLIKELY(idx >= obj_array->array.len))
        return 0;
    au_value_ref(obj_array->array.data[idx]);
    *result = obj_array->array.data[idx];
    return 1;
}

int au_obj_array_set(struct au_obj_array *obj_array, au_value_t idx_val,
                     au_value_t value) {
    if (AU_UNLIKELY(au_value_get_type(idx_val) != AU_VALUE_INT))
        return 0;
    const size_t idx = au_value_get_int(idx_val);
    if (AU_UNLIKELY(idx >= obj_array->array.len))
        return 0;
    au_value_ref(value);
    obj_array->array.data[idx] = value;
    au_value_deref(obj_array->array.data[idx]);
    return 1;
}

int32_t au_obj_array_len(struct au_obj_array *obj_array) {
    return (int32_t)obj_array->array.len;
}

#ifdef _AUMENT_H
struct au_obj_array *au_obj_array_coerce(au_value_t value) {
    if (au_value_get_type(value) != AU_VALUE_STRUCT ||
        au_value_get_struct(value)->vdata != &au_obj_array_vdata)
        return 0;
    return (struct au_obj_array *)au_value_get_struct(value);
}
#endif