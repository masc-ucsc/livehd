//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "inou_def.hpp"
#include "lefrReader.hpp"

int  lef_macro_begin_cb(lefrCallbackType_e c, const char *macroName, lefiUserData ud);
int  lef_macro_cb(lefrCallbackType_e c, lefiMacro *fmacro, lefiUserData ud);
int  lef_pin_cb(lefrCallbackType_e c, lefiPin *fpin, lefiUserData ud);
int  lef_layer_cb(lefrCallbackType_e c, lefiLayer *flayer, lefiUserData ud);
int  lef_via_cb(lefrCallbackType_e c, lefiVia *fvia, lefiUserData ud);
void lef_parsing(std::string_view lef_file_name);
