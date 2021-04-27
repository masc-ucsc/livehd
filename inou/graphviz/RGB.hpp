// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Code based (significantly changed) on https://github.com/louiskenzo/dye/blob/master/dye.hpp
// CC0 1.0 Universal license

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>

class RGB {
protected:

  int r;
  int g;
  int b;

  float upramp(float x, float center, float width) {
    return 255.0f * (1.0f + std::tanh(6.0f / width * (x - center))) / 2.0f;
  }

  float downramp(float x, float center, float width) {
    return 255.0f * (1.0f - std::tanh(6.0f / width * (x - center))) / 2.0f;
  }

public:
  RGB(float x) { // 0..1 range
    if (x <= 0.5) {
      r = upramp(x, 5/8.0, 1/4.0);
      g = upramp(x, 1/8.0, 1/4.0);
      b = downramp(x, 3/8.0, 1/4.0);
    }else{
      r = upramp(x, 5/8.0, 1/4.0);
      g = downramp(x, 7/8.0, 1/4.0);
      b = downramp(x, 3/8.0, 1/4.0);
    }
  }
  std::string to_s() const {
    std::stringstream stream;
    stream << "#"
      << std::hex << std::setfill('0')
      << std::setw(2) << r
      << std::setw(2) << g
      << std::setw(2) << b;

    return stream.str();
  }
};

