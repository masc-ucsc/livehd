//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

class __attribute__((packed)) Ann_place {
private:
protected:
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

  friend bool operator==(const Ann_place& lhs, const Ann_place& rhs){ return lhs.pos_x == rhs.pos_x && lhs.pos_y == rhs.pos_y && lhs.len_x == rhs.len_x && lhs.len_y == rhs.len_y; }

  float get_pos_x() const { return pos_x; }
  float get_pos_y() const { return pos_y; }
  float get_len_x() const { return len_x; }
  float get_len_y() const { return len_y; }

  bool is_valid() const {
    return len_x > 0 && len_y > 0;
  }
};
