// This source file is part of the Aument language
// Copyright (c) 2021 the aument contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#pragma once

#include <stdlib.h>

/// [struct] A structure representing where an error occurs.
struct au_error_location {
    const char *path;
    const char *src;
    size_t len;
};
// end-struct
