// This source file is part of the aulang project
// Copyright (c) 2021 the aulang contributors
//
// Licensed under Apache License v2.0 with Runtime Library Exception
// See LICENSE.txt for license information
#pragma once

#include "core/parser/exception.h"
#include "error_location.h"
#include <stdlib.h>

void au_print_parser_error(struct au_parser_result res,
                           struct au_error_location loc);