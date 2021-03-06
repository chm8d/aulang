# This source file is part of the Aument language
# Copyright (c) 2021 the aument contributors
#
# Licensed under Apache License v2.0 with Runtime Library Exception
# See LICENSE.txt for license information

import glob
import subprocess
import os

source_path = os.path.dirname(os.path.abspath(__file__))

out_extension = '.out'
out_extension_len = len(out_extension)
files = glob.glob('tests/features/*' + out_extension)
n_tests = len(files)

c_src = """
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/bc.h"
#include "core/parser/parser.h"
#include "core/program.h"
#include "core/rt/exception.h"
#include "core/rt/au_array.h"
#include "core/rt/au_tuple.h"
#include "core/rt/malloc.h"
#include "core/vm/vm.h"
"""

c_src_comp_test = c_src + """
#include "os/tmpfile.h"
#include "os/spawn.h"
#include "os/cc.h"

#include "compiler/c_comp.h"

extern char *TEST_RT_CODE;
extern size_t TEST_RT_CODE_LEN;
"""

def gen_check_value(item, val_type, val_contents):
    if val_type == 'int':
        return f"assert(au_value_get_type({item}) == AU_VALUE_INT && au_value_get_int({item}) == {val_contents});"
    elif val_type == 'float':
        return f"assert(au_value_get_type({item}) == AU_VALUE_DOUBLE && au_value_get_double({item}) == {val_contents});"
    elif val_type == 'bool':
        return f"assert(au_value_get_type({item}) == AU_VALUE_BOOL && au_value_get_bool({item}) == {'1' if val_contents == 'true' else '0'});"
    elif val_type == 'nil':
        return f"assert(au_value_get_type({item}) == AU_VALUE_NONE);"
    elif val_type == 'str':
        return f"char *s={val_contents};assert(au_value_get_type({item}) == AU_VALUE_STR && au_value_get_string({item})->len == strlen(s) && memcmp(au_value_get_string({item})->data, s, strlen(s)) == 0);\n"
    raise NotImplementedError(val_type)

def bytes_to_c_array(x):
    return ','.join(map(str, map(int, x))), len(x)

test_srcs = []
input_srcs = []

for (i, fn) in enumerate(files):
    with open(fn[:-out_extension_len] + '.au', 'r', encoding='utf-8') as f:
        input_cont = f.read()
    with open(fn, 'r', encoding='utf-8') as f:
        output_lines = map(lambda s: s.rstrip(), f.readlines())
    input_bytes = input_cont.encode('utf-8')
    input_array, input_len = bytes_to_c_array(input_bytes)
    input_srcs.append((input_array, input_len))

    test_src = f"""\
static int test_{i}_idx = 0;
static void test_{i}_check(au_value_t value) {{ switch(test_{i}_idx) {{
"""
    for val_idx, value in enumerate(output_lines):
        if value.startswith("array;"):
            array_contents = value.split(';')[1:]
            test_src += f"  case {val_idx}: {{\n"
            test_src += """\
    assert(au_value_get_type(value) == AU_VALUE_STRUCT && au_value_get_struct(value)->vdata == &au_obj_array_vdata);
"""
            if len(array_contents) == 1 and not array_contents[0]:
                test_src += f"  }}\n"
            else:
                test_src += """\
    struct au_obj_array *array = (struct au_obj_array *)au_value_get_struct(value);
"""
                for item_idx in range(len(array_contents)):
                    test_src += f"au_value_t _value{item_idx}; assert(au_obj_array_get(array, au_value_int({item_idx}), &_value{item_idx}));\n"
                for item_idx, item in enumerate(array_contents):
                    val_type, val_contents = item.split(',')
                    item = f"_value{item_idx}"
                    test_src += f"    {gen_check_value(item, val_type, val_contents)}\n"
                test_src += f"  }}\n"
            continue
        elif value.startswith("tuple;"):
            array_contents = value.split(';')[1:]
            test_src += f"  case {val_idx}: {{\n"
            test_src += """\
    assert(au_value_get_type(value) == AU_VALUE_STRUCT && au_value_get_struct(value)->vdata == &au_obj_tuple_vdata);
    struct au_obj_tuple *tuple = (struct au_obj_tuple *)au_value_get_struct(value);
"""
            for item_idx in range(len(array_contents)):
                test_src += f"au_value_t _value{item_idx}; assert(au_obj_tuple_get(tuple, au_value_int({item_idx}), &_value{item_idx}));\n"
            for item_idx, item in enumerate(array_contents):
                val_type, val_contents = item.split(',')
                item = f"_value{item_idx}"
                test_src += f"    {gen_check_value(item, val_type, val_contents)}\n"
            test_src += f"  }}\n"
            continue
        val_type, val_contents = value.split(';')
        test_src += f"  case {val_idx}: {{"
        test_src += gen_check_value("value", val_type, val_contents)
        test_src += f" break; }}\n"
    test_src += f"""\
    }}
    test_{i}_idx++;
}}
"""
    c_src += test_src

    test_src = f"""
#include <assert.h>
{test_src}
void __au_value_print(au_value_t v)
{{ test_{i}_check(v); }}
"""
    test_srcs.append(test_src)

# Interpreter test

for (i, (input_src_array, input_src_len)) in enumerate(input_srcs):
    c_src += f"""
static void test_{i}() {{
    const char source[] = {{{input_src_array}}};
    struct au_program program;
    assert(au_parse(source, {input_src_len}, &program).type == AU_PARSER_RES_OK);
    struct au_vm_thread_local tl;
    au_malloc_init();
    au_vm_thread_local_init(&tl, &program.data);
    au_vm_thread_local_set(&tl);
    tl.print_fn = test_{i}_check;
    au_vm_thread_local_install_stdlib(&tl);
    au_malloc_set_collect(1);
    au_vm_exec_unverified_main(&tl, &program);
#ifdef AU_FEAT_DELAYED_RC
    au_vm_thread_local_del_const_cache(&tl);
    au_obj_malloc_collect();
#endif
    au_malloc_set_collect(0);
    au_program_del(&program);
    au_vm_thread_local_del(&tl);
    au_vm_thread_local_set(0);
}}\n\n"""

c_src += """
int main() {
"""

for (i, fn) in enumerate(files):
    fn = fn.replace("\\","/")
    c_src += f"  printf(\"[{i+1}/{len(files)}] {fn}\\n\"); test_{i}();\n"

c_src += """
    return 0;
}
"""

with open("build/tests.c", "w", encoding='utf-8') as f:
    f.write(c_src)

# Compiler test
stdlib_cache_file = os.path.join(source_path, "../build/libau_runtime.a")
stdlib_cache_file = stdlib_cache_file.replace("\\","/")
c_src_comp_test += f"""
struct au_cc_options cc;

void setup() {{
    au_cc_options_default(&cc);
    cc._stdlib_cache = au_data_strdup("{stdlib_cache_file}");
}}

void cleanup() {{
    au_cc_options_del(&cc);
}}

void run_gcc(const char *source, const size_t source_len) {{
    struct au_tmpfile c_file;
    assert(au_tmpfile_new(&c_file));

    struct au_tmpfile c_file_out;
    assert(au_tmpfile_exec(&c_file_out));

    struct au_c_comp_state c_state = {{0}};
    struct au_program program;
    assert(au_parse(source, source_len, &program).type == AU_PARSER_RES_OK);
    struct au_c_comp_options options = {{0}};
    au_c_comp(&c_state, &program, &options, 0);
    fwrite(c_state.str.data, 1, c_state.str.len, c_file.f);
    fflush(c_file.f);
    au_c_comp_state_del(&c_state);
    au_program_del(&program);

    au_tmpfile_close(&c_file);
    au_tmpfile_close(&c_file_out);

#ifdef AU_SANITIZER
    au_str_array_add(&cc.cflags, "-fsanitize=" AU_SANITIZER);
#endif

#ifdef AU_COVERAGE
    au_str_array_add(&cc.cflags, "-fprofile-arcs");
    au_str_array_add(&cc.cflags, "-ftest-coverage");
#endif

#ifdef AU_FEAT_LIBDL
    au_str_array_add(&cc.ldflags, "-ldl");
#endif

#ifdef AU_FEAT_MATH_LIB
    au_str_array_add(&cc.ldflags, "-lm");
#endif

    int status = au_spawn_cc(&cc, c_file_out.path, c_file.path);
    assert(status == 0);

    struct au_str_array args = {{0}};
    au_str_array_add(&args, c_file_out.path);
    status = au_spawn(&args);
    assert(status == 0);
    au_data_free(args.data);

    au_tmpfile_del(&c_file);
    au_tmpfile_del(&c_file_out);
}}"""
for (i, ((input_src_array, input_src_len), test_src)) in enumerate(zip(input_srcs, test_srcs)):
    test_src_array, test_src_len = bytes_to_c_array(test_src.encode('utf-8'))
    c_src_comp_test += f"""
static void test_{i}() {{
    char rt_code[] = {{{test_src_array}}};
    TEST_RT_CODE = rt_code;
    TEST_RT_CODE_LEN = {test_src_len};
    const char source[] = {{{input_src_array}}};
    run_gcc(source, {input_src_len});
}}\n\n"""

c_src_comp_test += f"""
int main(int argc, char **argv) {{
    setup();
    assert(argc>1);
    const int sel = atoi(argv[1]);
    if(sel==-1){{ printf("{len(files)}");return 0; }}
"""

for (i, fn) in enumerate(files):
    fn = fn.replace("\\","/")
    c_src_comp_test += f"    if(sel=={i}) {{ printf(\"[{i+1}/{len(files)}] {fn}\\n\"); test_{i}(); }}\n"

c_src_comp_test += """
    cleanup();
    return 0;
}
"""

with open("build/tests_comp.c", "w", encoding='utf-8') as f:
    f.write(c_src_comp_test)
