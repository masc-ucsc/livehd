//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <vector>

class Tech_via_layer {
public:
  class Tech_rect {
  public:
    double x, y, xh, yh;
  };

  std::string layer_name;
  Tech_rect   rect;
};

class Tech_layer {
public:
  std::string         name;
  bool                horizontal;
  double              minwidth;
  double              area;
  double              width;
  std::vector<double> spacing_eol;
  std::vector<double> spacing_tb;
  std::vector<double> pitches;
  double              spctb_prl;      // parallel running length
  std::vector<double> spctb_width;    // width in spacing table
  std::vector<double> spctb_spacing;  // spacing in spacing table

  // void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>&) const;
};

class Tech_via {
public:
  std::string                 name;
  std::vector<Tech_via_layer> vlayers;
  // void to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>&) const;
};

// Not an attribute because it is per pin class
struct Tech_pin {
  Tech_pin() : x(0), y(0), xw(0), yh(0), layer_id(0) {}
  float   x;
  float   y;
  float   xw;  // Width
  float   yh;  // height
  uint8_t layer_id;

  bool overlap(const Tech_pin &o) const {
    if (layer_id != o.layer_id)
      return false;

    if (o.x < x + xw && x < o.x + o.xw && o.y < y + yh)
      return y < o.y + o.yh;

    return false;
  }
};
