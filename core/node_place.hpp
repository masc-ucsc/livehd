//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

class Node_place {
private:

protected:
  uint32_t posx;  // Enough for 1nm resolution and 100mm**2 die
  uint32_t posy;

public:
  constexpr Node_place() : posx(0), posy(0) {};
  Node_place(uint32_t x, uint32_t y) : posx(x), posy(y){};

  void replace(uint32_t x, uint32_t y) {
    posx = x;
    posy = y;
  }

  uint32_t get_x() const { return posx; }
  uint32_t get_y() const { return posy; }
};

