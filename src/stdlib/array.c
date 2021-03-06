// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information

#include <stdio.h>

#include "core/rt/au_array.h"
#include "core/rt/extern_fn.h"
#include "core/rt/value.h"
#include "core/vm/vm.h"

AU_EXTERN_FUNC_DECL(au_std_array_is) {
    const au_value_t value = _args[0];
    const au_value_t retval =
        au_value_bool(au_obj_array_coerce(value) != 0);
    au_value_deref(value);
    return retval;
}

AU_EXTERN_FUNC_DECL(au_std_array_repeat) {
    au_value_t repeat_value = _args[0];

    const au_value_t times_value = _args[1];
    if (au_value_get_type(times_value) != AU_VALUE_INT)
        goto fail;
    const int32_t times = au_value_get_int(times_value);

    struct au_obj_array *array = au_obj_array_new(times);

    for (int32_t i = 0; i < times; i++) {
        au_value_ref(repeat_value);
        au_obj_array_push(array, repeat_value);
    }

    au_value_deref(times_value);
    return au_value_struct((struct au_struct *)array);
fail:
    au_value_deref(repeat_value);
    au_value_deref(times_value);
    return au_value_none();
}

AU_EXTERN_FUNC_DECL(au_std_array_push) {
    const au_value_t array_value = _args[0];
    struct au_obj_array *array = au_obj_array_coerce(array_value);
    if (array == 0)
        goto fail;

    const au_value_t item = _args[1];

    au_obj_array_push(array, item);

    au_value_deref(item);
    return array_value;
fail:
    au_value_deref(array_value);
    return au_value_none();
}

AU_EXTERN_FUNC_DECL(au_std_array_pop) {
    const au_value_t array_value = _args[0];
    struct au_obj_array *array = au_obj_array_coerce(array_value);
    if (array == 0)
        goto fail;

    const au_value_t retval = au_obj_array_pop(array);
    au_value_deref(array_value);
    return retval;
fail:
    au_value_deref(array_value);
    return au_value_none();
}

AU_EXTERN_FUNC_DECL(au_std_array_insert) {
    const au_value_t array_value = _args[0];
    const au_value_t idx_value = _args[1];
    const au_value_t item_value = _args[2];

    struct au_obj_array *array = au_obj_array_coerce(array_value);
    if (array == 0)
        goto fail;

    if (au_value_get_type(idx_value) != AU_VALUE_INT)
        goto fail;
    const int32_t idx = au_value_get_int(idx_value);

    if (!au_obj_array_insert(array, idx, item_value))
        goto fail;

    au_value_deref(idx_value);
    au_value_deref(item_value);
    return array_value;
fail:
    au_value_deref(array_value);
    au_value_deref(idx_value);
    au_value_deref(item_value);
    return au_value_none();
}