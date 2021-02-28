//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "floorplan.hpp"

class __attribute__((packed)) Ann_hint {
private:
protected:
  GeographyHint hint;

public:
  constexpr Ann_hint() : hint(InvalidHint){};
  Ann_hint(GeographyHint _hint) : hint(_hint){};

  void replace(GeographyHint _hint) { hint = _hint; }
};