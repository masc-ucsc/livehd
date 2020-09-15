#pragma once

#include <functional>  // for std::hash
#include <iostream>
#include <unordered_map>
#include <vector>

#include "lgraph_base_core.hpp"

// the dimensions of a module or collection of modules
constexpr double max_aspect_ratio = 1.0 / 5.0;
class Dim {
public:
  double width;
  double height;

  Dim() : width(0.0), height(0.0) {}
  Dim(const double new_width, const double new_height) : width(new_width), height(new_height) {}
  friend bool operator==(const Dim& l, const Dim& r) { return cmpd(l.width, r.width) && cmpd(l.height, r.height); }
  //friend bool operator!=(const Dim& l, const Dim& r) { return !(l == r); }

private:
  static constexpr double epsilon = 0.00001;
  // quick and dirty comparison method
  static bool             cmpd(const double l, const double r) { return std::abs(l - r) < epsilon; }
};

// a collection of modules
class Pattern {
public:
  // a list of each kind of module in the pattern, as well as how many modules there are of that type
  std::unordered_map<Lg_type_id::type, unsigned int> verts;

  // dimensions of the pattern
  Dim d;

  // in order to use a Pattern as a key in an unordered_map, we need it to have operator() (for hashing) and operator==
  // (in case the hash ends up being the same by coincidence)
  friend bool operator==(const Pattern& l, const Pattern& r) { return l.verts == r.verts && l.d == r.d; }

  Pattern() : verts(), d() {}
  
  unsigned int count() const {
    unsigned int total = 0;
    for (auto p : verts) {
      total += p.second;
    }
    return total;
  }
};

// the specialization for hashing Patterns needs to be included in the std namespace
namespace std {
template <>
struct hash<Pattern> {
  size_t operator()(const Pattern& p) const {
    size_t i    = 0;
    size_t partial_hash = 0;
    for (auto vpair : p.verts) {
      size_t pair_hash = std::hash<Lg_type_id::type>()(vpair.first) ^ std::hash<unsigned int>()(vpair.second) << 1;
      partial_hash ^= pair_hash << ++i;
    }
    return partial_hash;
  }
};
}  // namespace std