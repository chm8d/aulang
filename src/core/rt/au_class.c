// This source file is part of the aulang project
// Copyright (c) 2021 the aulang contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#include "au_class.h"
#include "au_struct.h"
#include "platform/platform.h"
#include "value.h"
#endif

void au_class_interface_init(struct au_class_interface *interface,
                             char *name) {
    interface->name = name;
    au_hm_vars_init(&interface->map);
}

void au_class_interface_del(struct au_class_interface *interface) {
    free(interface->name);
    au_hm_vars_del(&interface->map);
}

struct au_obj_class {
    struct au_struct header;
    const struct au_class_interface *interface;
    au_value_t data[];
};

struct au_struct_vdata au_obj_class_vdata;
static int au_obj_class_vdata_inited = 0;
static void au_obj_class_vdata_init() {
    if (!au_obj_class_vdata_inited) {
        au_obj_class_vdata = (struct au_struct_vdata){
            .del_fn = (au_struct_del_fn_t)au_obj_class_del,
            .idx_get_fn = (au_struct_idx_get_fn_t)au_obj_class_get,
            .idx_set_fn = (au_struct_idx_set_fn_t)au_obj_class_set,
            .len_fn = (au_struct_len_fn_t)au_obj_class_len,
        };
        au_obj_class_vdata_inited = 1;
    }
}

struct au_obj_class *
au_obj_class_new(const struct au_class_interface *interface) {
    const size_t len = interface->map.entries_occ;
    struct au_obj_class *obj_class =
        malloc(sizeof(struct au_obj_class) + sizeof(au_value_t) * len);
    au_obj_class_vdata_init();
    obj_class->header = (struct au_struct){
        .rc = 1,
        .vdata = &au_obj_class_vdata,
    };
    obj_class->interface = interface;
    au_value_clear(obj_class->data, len);
    return obj_class;
}

void au_obj_class_del(struct au_obj_class *obj_class) {
    const size_t len = obj_class->interface->map.entries_occ;
    for (size_t i = 0; i < len; i++) {
        au_value_deref(obj_class->data[i]);
    }
    free(obj_class);
}

int au_obj_class_get(_Unused struct au_obj_class *obj_class,
                     _Unused const au_value_t idx,
                     _Unused au_value_t *result) {
    return 0;
}

int au_obj_class_set(_Unused struct au_obj_class *obj_class,
                     _Unused au_value_t idx_val,
                     _Unused au_value_t value) {
    return 0;
}

int32_t au_obj_class_len(struct au_obj_class *obj_class) {
    return (int32_t)obj_class->interface->map.entries_occ;
}
