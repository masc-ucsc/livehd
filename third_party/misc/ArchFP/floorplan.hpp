#pragma once

#include <ostream>
#include <string>

#include <cassert>
#include <fstream>
#include <stdexcept>

// Here is an enumeration for the optimazation goals for a layout manager.
// Area: optimize for smallest area (not implemented)
// SliceTree: create an optimal slice tree floorplan (not implemented)
// HardAspectRatio: optimize for a given aspect ratio, possibly forming a floorplan with gaps / overlaps
enum FPOptimization { Area, SliceTree, HardAspectRatio };

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
  Periphery,         // not supported
  UnknownGeography,  // not supported
};

// This will be used to keep track of user's request for more output during layout.
constexpr bool verbose = false;

// Temporary local for crazy mirror reflection stuff.
constexpr int maxMirrorDepth = 20;

extern bool   xReflect;
extern double xLeft[];
extern double xRight[];
extern int    xMirrorDepth;
extern bool   yReflect;
extern double yBottom[];
extern double yTop[];
extern int    yMirrorDepth;

int    Name2Count(const std::string& arg);
void   clearCount();
std::string getStringFromInt(int in);
void   setNameMode(bool);

// Output Helper Functions.
std::ostream& outputHotSpotHeader(const char* filename);
void     outputHotSpotFooter(std::ostream& o);
