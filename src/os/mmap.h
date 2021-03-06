// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#pragma once

#include "platform/platform.h"
#include <stdlib.h>
#endif

struct au_mmap_info {
    char *bytes;
    size_t size;
#ifdef AU_USE_MMAP
    int _fd;
#endif
};

/// [func] Loads a file into memory and stores a reference
/// into a au_mmap_info instance
/// @param path path of file
/// @param info info
/// @return 1 if success, 0 if errored
AU_PUBLIC int au_mmap_read(const char *path, struct au_mmap_info *info);

/// [func] Deinitializes an au_mmap_info instance
/// @param info instance to be deinitialized
AU_PUBLIC void au_mmap_del(struct au_mmap_info *info);