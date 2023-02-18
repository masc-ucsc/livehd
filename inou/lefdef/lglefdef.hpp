//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

#include "defrReader.hpp"
#include "lefrReader.hpp"
#include "inou_def.hpp"

// user supplied callback routines

// FIXME: remove any non-class method from the hpp file. They should be in the
// cpp file where they are used.

// collect macro lef information by using "three" user-defined call function,
// this usage first looked strange, this is due to cadence api architecture. no
// other way if you want to use cadence parser.

int  lef_macro_begin_cb(lefrCallbackType_e c, const char *macroName, lefiUserData ud);
int  lef_macro_cb(lefrCallbackType_e c, lefiMacro *fmacro, lefiUserData ud);
int  lef_pin_cb(lefrCallbackType_e c, lefiPin *fpin, lefiUserData ud);
int  lef_layer_cb(lefrCallbackType_e c, lefiLayer *flayer, lefiUserData ud);
int  lef_via_cb(lefrCallbackType_e c, lefiVia *fvia, lefiUserData ud);
void lef_parsing(std::string_view lef_file_name);

int def_net_cb(defrCallbackType_e type, defiNet *fnet, defiUserData ud);
int def_component_cb(defrCallbackType_e type, defiComponent *fcompo, defiUserData ud);
int def_io_cb(defrCallbackType_e type, defiPin *fpin, defiUserData ud);
int def_track_cb(defrCallbackType_e type, defiTrack *ftrack, defiUserData ud);
int def_design_cb(defrCallbackType_e c, const char *string, defiUserData ud);
int def_row_cb(defrCallbackType_e type, defiRow *frow, defiUserData ud);
void def_parsing(Def_info &dinfo, std::string_view def_file_name);

