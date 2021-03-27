#pragma once

#include <cassert>
#include <fstream>
#include <ostream>
#include <stdexcept>

// Here is an enumeration for the optimazation goals for a layout manager.
// SliceTree: create an optimal slice tree floorplan (not implemented)
// HardAspectRatio: optimize for a given aspect ratio, possibly forming a floorplan with gaps / overlaps
enum FPOptimization { SliceTree, HardAspectRatio };

// Here is the enumeration for layout hints for the geographic layout.
enum GeographyHint {
  Center,  // make center the default, since it's considered to be valid
  Left,
  Right,
  Top,
  Bottom,
  LeftRight,
  LeftRightMirror,
  LeftRight180,
  TopBottom,
  TopBottomMirror,
  TopBottom180,
  Periphery,    // not supported
  UnknownHint,  // not supported
  InvalidHint,  // not supported
};

GeographyHint    nameToHint(std::string_view name);
std::string_view hintToName(GeographyHint hint);

// This will be used to keep track of user's request for more output during layout.
constexpr bool verbose = false;
