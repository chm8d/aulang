# Performance

*(This document is a draft.)*

Although at its core, Aument is meant to be a dynamic scripting language, it does not try to sacrifice speed solely for the sake of being dynamic. Below are a few design decisions (language or implementation wise), which makes Aument fast.

## Language design

### Static names

All names in Aument (identifiers, functions and classes) are static, they are resolved at parse time and constant at runtime. You cannot define new functions or classes at runtime, of course, but in practice, it isn't a big limitation.

### [Classes](/docs/architecture.md#classes)

Classes in Aument are flat structures, they are laid out in memory like an array of values with named keys. Accessing private values is as fast as a pointer dereference.

## Implementation

### Single pass parser

The Aument parser has only one pass which generates virtual machine bytecode directly from a source file. This keeps startup overhead low and reduces the implementation's complexity.

### Compact, uniform bytecode

Each bytecode instruction the virtual machine executes has a clear, uniform structure: a 32-bit value with 1-2 bytes per operand. Decoding each instruction is simple.

The bytecode used by the VM is also register-based. Compared to stack VMs like that in Python and Ruby, register VMs require less instructions to represent a single piece of code, which keeps interpreter overhead low for better interpreter performance.

### Computed gotos

The virtual machine uses computed gotos for fast function dispatching.

### [NaN tagging](docs/architecture.md#nan-tagging)

The virtual machine and software compiled by Aument will use NaN tagging to represent dynamically typed values. This technique is especially useful on 64-bit systems, as all values are as small as the architecture's word size.


