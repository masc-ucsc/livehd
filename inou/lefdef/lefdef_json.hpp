//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef GUARD_LEFDEF_JSON
#define GUARD_LEFDEF_JSON

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "rapidjson/prettywriter.h"

// class Tech_rect{
//  public:
//    double xl,yl,xh,yh;
//};
//
// class Tech_port{
//  public:
//    std::string metal_name;
//    std::vector<Tech_rect> rects; //rectangles
//};
//
// class Tech_pin{
//  public:
//  bool                   has_port;
//  std::string            pin_name;
//  std::string            pin_use;
//  int                    pin_direction; // 0:input, 1:output, 2:inout
//  std::vector<Tech_port> ports;
//};
//
// class Tech_macro{
//  public:
//  std::string               name;
//  std::vector<double>       size; //size[0] =x, size[1] =y
//  std::vector<Tech_pin>     pins;
//
//  void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>&) const;
//};

// class Tech_file{
//  public:
//  std::string str;
//  std::vector<Tech_layer> layers;
//  std::vector<Tech_macro> macros;
//  std::vector<Tech_via>   vias;
//  void to_json();
//};

#endif
