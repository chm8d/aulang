// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#pragma once

#include <stdint.h>
#include <string.h>

#include "core/rt/au_string.h"
#include "core/rt/struct/vdata.h"
#include "platform/platform.h"

#include "main.h"
#endif

struct au_obj_rc {
    uint32_t rc;
};

static AU_ALWAYS_INLINE void au_value_ref(const au_value_t v) {
    struct au_obj_rc *obj_rc = 0;
    switch (au_value_get_type(v)) {
    case AU_VALUE_STR: {
        obj_rc = (struct au_obj_rc *)au_value_get_string(v);
        break;
    }
    case AU_VALUE_FN: {
        obj_rc = (struct au_obj_rc *)au_value_get_fn(v);
        break;
    }
    case AU_VALUE_STRUCT: {
        obj_rc =
            (struct au_obj_rc *)(struct au_struct *)au_value_get_struct(v);
        break;
    }
    default:
        return;
    }
    if (AU_UNLIKELY(obj_rc->rc == INT32_MAX))
        abort();
    obj_rc->rc++;
}

static AU_ALWAYS_INLINE void au_value_deref(const au_value_t v) {
    struct au_obj_rc *obj_rc = 0;
    switch (au_value_get_type(v)) {
    case AU_VALUE_STR: {
        obj_rc = (struct au_obj_rc *)au_value_get_string(v);
        break;
    }
    case AU_VALUE_FN: {
        obj_rc = (struct au_obj_rc *)au_value_get_fn(v);
        break;
    }
    case AU_VALUE_STRUCT: {
        obj_rc =
            (struct au_obj_rc *)(struct au_struct *)au_value_get_struct(v);
        break;
    }
    default:
        return;
    }

    if (AU_UNLIKELY(obj_rc == 0))
        abort();
    obj_rc->rc--;

    if (obj_rc->rc == 0)
        au_obj_free(obj_rc);
}