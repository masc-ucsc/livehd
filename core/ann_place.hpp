//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

class __attribute__((packed)) Ann_place {
private:
protected:
  float x;  // position of left side
  float y;  // position of bottom
  float width;
  float height;

public:
  constexpr Ann_place() : x(0.0), y(0.0), width(0.0), height(0.0){};
  Ann_place(float _x, float _y, float _w, float _h) : x(_x), y(_y), width(_w), height(_h){};

  void replace(float _x, float _y, float _w, float _h) {
    x      = _x;
    y      = _y;
    width  = _w;
    height = _h;
  }

  friend bool operator==(const Ann_place& lhs, const Ann_place& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
  }

  float get_x() const { return x; }
  float get_y() const { return y; }
  float get_width() const { return width; }
  float get_height() const { return height; }

  bool is_valid() const { return width > 0 && height > 0; }
};
