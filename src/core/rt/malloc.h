// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifndef AU_MALLOC_H
#define AU_MALLOC_H

#ifdef AU_IS_INTERPRETER
#include "platform/platform.h"
#include <stdlib.h>
#include <string.h>
#endif

typedef void (*au_obj_del_fn_t)(void *self);

AU_PUBLIC void au_malloc_init();
AU_PUBLIC void au_malloc_set_collect(int do_collect);
AU_PUBLIC size_t au_malloc_heap_size();

// ** objects **

AU_PUBLIC void au_obj_malloc_collect();

// [func] Allocates a new object in the heap. The first element of the
// object must be a uint32_t reference counter.
AU_PUBLIC __attribute__((malloc)) void *
au_obj_malloc(size_t size, au_obj_del_fn_t free_fn);

AU_PUBLIC void *au_obj_realloc(void *ptr, size_t size);
AU_PUBLIC void au_obj_free(void *ptr);

AU_PUBLIC void au_obj_ref(void *ptr);
AU_PUBLIC void au_obj_deref(void *ptr);

// ** data **

AU_PUBLIC __attribute__((malloc)) void *au_data_malloc(size_t size);
AU_PUBLIC __attribute__((malloc)) void *au_data_calloc(size_t count,
                                                       size_t size);
AU_PUBLIC void *au_data_realloc(void *ptr, size_t size);
AU_PUBLIC void au_data_free(void *ptr);

static AU_UNUSED inline char *au_data_strdup(const char *other) {
    const size_t len = strlen(other);
    char *dest = (char *)au_data_malloc(len + 1);
    memcpy(dest, other, len);
    dest[len] = 0;
    return dest;
}

static AU_UNUSED char *au_data_strndup(const char *str, size_t len) {
    char *output = (char *)au_data_malloc(len + 1);
    memcpy(output, str, len);
    output[len] = 0;
    return output;
}

#endif
