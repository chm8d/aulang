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

/// [func] Print fatal exception and abort
/// @param fmt printf formatted string
AU_PUBLIC AU_NO_RETURN void au_fatal(const char *fmt, ...);

/// [func] Print fatal exception from perror and exit program
/// @param msg what the program was trying to do when the error occured
AU_PUBLIC AU_NO_RETURN void au_perror(const char *msg);

/// [func] Print index error and abort
/// @param array the array
/// @param idx the index the code is accessing
/// @param len the length of the array
AU_PUBLIC AU_NO_RETURN void au_fatal_index(const void *array, size_t idx,
                                           size_t len);
