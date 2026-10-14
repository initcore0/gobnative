// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "script/expression.h"
#include "script/script.h"

#include <string>

namespace gob {

// Renders expression bytecode as readable infix text without evaluating it,
// advancing the script cursor exactly as Expression::skip() would.
//
// Variable notation:  v[N]   32-bit slot N (byte offset N*4)
//                     w@B    16-bit at byte offset B
//                     b@B    8-bit at byte offset B
//                     s[N]   string starting at slot N
//                     v[N](i,j)  array access with dimension indices
std::string printExpr(Script &script, uint8_t stopToken = tok::Stop);

// Renders a variable reference as parsed by Expression::parseVarIndex().
std::string printVarIndex(Script &script);

} // namespace gob
