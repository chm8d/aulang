# This source file is part of the Aument language
# Copyright (c) 2021 the aument contributors
#
# Licensed under Apache License v2.0 with Runtime Library Exception
# See LICENSE.txt for license information
import re
import os

def cleanup_params(array):
    def each_line(x):
        return CMT_CONT_REGEX.sub("", x)
    return list(map(lambda inner: (inner[0], " ".join(map(each_line, inner[1]))), array))

AT_REGEX = re.compile(r'/// @([^ ]*) (.*)')
CMT_CONT_REGEX = re.compile(r'^///\s+')
BEGIN_DESC_REGEX = re.compile(r'^/// \[([^\]]+)\]\s*')
TWO_ARG_REGEX = re.compile(r'([^\s]+)\s+(.*)', re.S)
FUNC_NAME_REGEX = re.compile(r'([a-zA-Z_$][a-zA-Z_$0-9]*)\(')
STRUCT_NAME_REGEX = re.compile(r'struct ([a-zA-Z_$][a-zA-Z_$0-9]*)')
AU_FUNC_NAME_REGEX = re.compile(r'\(([a-zA-Z_$][a-zA-Z_$0-9]*)\)')

STATE_NONE = 0
STATE_FUNC_GROUP = 1
STATE_STRUCT_GROUP = 2

def parse(src, path):
    groups = []
    cur_group = []
    state = STATE_NONE
    for i in src.split("\n"):
        if i.startswith("/// [func]") or i.startswith("/// [func-au]"):
            if cur_group:
                groups.append(cur_group)
                cur_group = []
            state = STATE_FUNC_GROUP
        elif i.startswith("/// [struct]"):
            if cur_group:
                groups.append(cur_group)
                cur_group = []
            state = STATE_STRUCT_GROUP

        if state != STATE_NONE:
            cur_group.append(i)

        if state == STATE_FUNC_GROUP:
            if not i.startswith("///") and i.endswith(";"):
                groups.append(cur_group)
                cur_group = []
                state = STATE_NONE
        elif state == STATE_STRUCT_GROUP:
            if i == "// end-struct":
                groups.append(cur_group)
                cur_group = []
                state = STATE_NONE

    if cur_group:
        groups.append(cur_group)
    
    functions = []
    structs = []

    for group in groups:
        line_idx = 0
        line_len = len(group)
        while line_idx < line_len:
            if group[line_idx].startswith("/// @") or not group[line_idx].startswith("///"):
                break
            line_idx += 1
        desc = group[0:line_idx]
        doc_type = BEGIN_DESC_REGEX.match(desc[0]).group(1)
        desc[0] = BEGIN_DESC_REGEX.sub("", desc[0])
        desc = ' '.join(map(lambda x: CMT_CONT_REGEX.sub("", x), desc))
        desc = desc.replace('\\n', '\n')

        params = []
        while line_idx < line_len:
            line = group[line_idx]
            if line.startswith("/// @"):
                matches = AT_REGEX.match(line)
                params.append((matches.group(1), [matches.group(2)]))
            elif not line.startswith("///"):
                break
            else:
                params[-1][1].append(line)
            line_idx += 1
        params = cleanup_params(params)

        signature = group[line_idx:]

        if doc_type == "func" or doc_type == "func-au":
            signature = "\n".join(signature)

            func_params = []
            func_returns = None
            func_name = None
            for (key, value) in params:
                if key == 'param':
                    x = TWO_ARG_REGEX.match(value)
                    if x == None:
                        func_params.append((value, []))
                    else:
                        func_params.append((x.group(1), x.group(2)))
                elif key == 'return':
                    func_returns = value
                elif key=='name':
                    func_name = value

            func = {
                "path": path,
                "desc": desc,
                "params": func_params,
                "returns": func_returns,
            }
            func["type"] = doc_type
            if doc_type == "func":
                func["signature"] = signature
                func["name"] = FUNC_NAME_REGEX.search(signature).group(1)
            else:
                func["name"] = func_name
            functions.append(func)
        elif doc_type == "struct":
            signature.pop()
            signature = "\n".join(signature)

            structs.append({
                "path": path,
                "desc": desc,
                "name": STRUCT_NAME_REGEX.search(signature).group(1),
                "signature": signature,
            })
    return {
        "functions": functions,
        "structs": structs,
    }

functions = []
structs = []

for root, _, files in os.walk("src/"):
    for file in files:
        if file.endswith(".h"):
            path = os.path.join(root, file)
            path = path.replace("\\", "/")
            with open(path, "r") as f:
                result = parse(f.read(), path)
                functions += result["functions"]
                structs += result["structs"]

functions.sort(key=lambda x: x['name'])
structs.sort(key=lambda x: x['name'])

md_src = """\
# C API

The section below was generated automatically (devs: *gen_api.py*).
Please don't modify it by hand!

## Functions
"""

au_std_md_src = """\
# aument standard library reference

The section below was generated automatically (devs: *gen_api.py*).
Please don't modify it by hand!

## Functions
"""

NL = "\n"

# * Functions *

for f in functions:
    if f["type"] == "func":
        md_src += f"""
### {f['name']}

```c
{f['signature']}
```

Defined in *{f['path']}*.

{f['desc']}

#### Arguments

{NL.join(map(lambda x: f" * **{x[0]}:** {x[1]}",  f['params'])) if f['params'] else "*none*"}

#### Return value

{f['returns'] if f['returns'] else "*none*"}
"""
    elif f["type"] == "func-au":
        au_std_md_src += f"""
### {f['name']}

Defined in *{f['path']}*.

{f['desc']}

#### Arguments

{NL.join(map(lambda x: f" * **{x[0]}:** {x[1]}",  f['params'])) if f['params'] else "*none*"}

#### Return value

{f['returns'] if f['returns'] else "*none*"}
"""

# * Structs *
md_src += "\n## Structures\n"
for struct in structs:
    md_src += f"""
### {struct["name"]}

{struct["desc"]}

```c
{struct["signature"]}
```

Defined in *{struct['path']}*.
"""

with open("docs/c-api.md", "w") as f:
    f.write(md_src)

with open("docs/au-stdlib.md", "w") as f:
    f.write(au_std_md_src)
