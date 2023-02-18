//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

#include "defrReader.hpp"
#include "inou_def.hpp"

int  def_net_cb(defrCallbackType_e type, defiNet *fnet, defiUserData ud);
int  def_component_cb(defrCallbackType_e type, defiComponent *fcompo, defiUserData ud);
int  def_io_cb(defrCallbackType_e type, defiPin *fpin, defiUserData ud);
int  def_track_cb(defrCallbackType_e type, defiTrack *ftrack, defiUserData ud);
int  def_design_cb(defrCallbackType_e c, const char *string, defiUserData ud);
int  def_row_cb(defrCallbackType_e type, defiRow *frow, defiUserData ud);
void def_parsing(Def_info &dinfo, std::string_view def_file_name);
