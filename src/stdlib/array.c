// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information

#include <stdio.h>

#include "core/rt/extern_fn.h"
#include "core/rt/value.h"
#include "core/vm/vm.h"
#include "core/rt/au_array.h"

AU_EXTERN_FUNC_DECL(au_std_array_repeat) {
    au_value_t repeat_value = _args[0];
    const au_value_t times_value = _args[1];
    if (au_value_get_type(times_value) != AU_VALUE_INT)
        goto fail;
    const int32_t times = au_value_get_int(times_value);

    struct au_obj_array *array = au_obj_array_new(times);

    for(int32_t i = 0; i < times; i++) {
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
