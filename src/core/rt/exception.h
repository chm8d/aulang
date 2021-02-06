// This source file is part of the aulang project
// Copyright (c) 2021 the aulang contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#ifdef AU_IS_INTERPRETER
#pragma once
#include "platform/platform.h"
#endif

_NoReturn void au_fatal(const char *fmt, ...);
_NoReturn void au_perror(const char *msg);