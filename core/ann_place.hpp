//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

class __attribute__((packed)) Ann_place {
private:
protected:
  // uint32_t is enough for 1nm resolution and 100mm**2 die, but having fixed precision is annoying
  float pos_x;  // position of left side
  float pos_y;  // position of bottom
  float len_x;  // width
  float len_y;  // height

public:
  constexpr Ann_place() : pos_x(0.0), pos_y(0.0), len_x(0.0), len_y(0.0){};
  Ann_place(float px, float py, float lx, float ly) : pos_x(px), pos_y(py), len_x(lx), len_y(ly){};

  void replace(float px, float py, float lx, float ly) {
    pos_x = px;
    pos_y = py;
    len_x = lx;
    len_y = ly;
  }

  float get_pos_x() const { return pos_x; }
  float get_pos_y() const { return pos_y; }
  float get_len_x() const { return len_x; }
  float get_len_y() const { return len_y; }
};
