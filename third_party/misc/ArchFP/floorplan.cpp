#include "floorplan.hpp"

#include "absl/container/flat_hash_map.h"
#include "helpers.hpp"
#include "lgedgeiter.hpp"

bool   xReflect = false;
double xLeft[maxMirrorDepth];
double xRight[maxMirrorDepth];
int    xMirrorDepth = 0;
bool   yReflect     = false;
double yBottom[maxMirrorDepth];
double yTop[maxMirrorDepth];
int    yMirrorDepth = 0;

absl::flat_hash_map<std::string, unsigned int> NameCounts;

int  Name2Count(const std::string& arg) { return ++NameCounts[arg]; }
void clearCount() { NameCounts.clear(); }

std::ostream& outputHotSpotHeader(const char* filename) {
  // Reset the name to counts map for this output.
  NameCounts.clear();

  std::ofstream& out = *(new std::ofstream(filename));
  out << "# FloorPlan output from ArchFP: UVA's Rapid Prototyping FloorPlanner.\n";
  out << "# Formatted for Input to HotSpot.\n";
  out << "# Line Format: <unit-name>\\t<width>\\t<height>\\t<left-x>\\t<bottom-y>\n";
  out << "# Module Format: <start/end>\\t<name>\\t<type>\\t<items>\\t<grid width (if grid)>\\t<grid height (if grid)>\n";
  out << "# all dimensions are in meters\n";
  out << "# comment lines begin with a '#'\n";
  out << "# comments and empty lines are ignored\n\n";

  return out;
}

void outputHotSpotFooter(std::ostream& o) { delete (&o); }

constexpr std::string_view GeographyName[] = {
    "Center",
    "Left",
    "Right",
    "Top",
    "Bottom",
    "LeftRight",
    "LeftRightMirror",
    "LeftRight180",
    "TopBottom",
    "TopBottomMirror",
    "TopBottom180",
    "Periphery",
    "UnknownHint",
    "InvalidHint",
};

GeographyHint nameToHint(std::string_view name) {
  for (size_t i = 0; i < (size_t)InvalidHint; i++) {
    if (GeographyName[i] == name) {
      return (GeographyHint)i;
    }
  }

  return InvalidHint;
}

std::string_view hintToName(GeographyHint hint) { return GeographyName[(size_t)hint]; }