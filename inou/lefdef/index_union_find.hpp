// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <numeric>
#include <vector>

namespace livehd::physical {
// Disjoint sets over dense indices 0..size()-1, for geometry connectivity.
// find() halves paths iteratively: its depth never grows with the set size.
class Index_union_find {
  std::vector<size_t> parent_;

public:
  explicit Index_union_find(size_t size = 0) : parent_(size) { std::iota(parent_.begin(), parent_.end(), size_t{0}); }
  size_t size() const { return parent_.size(); }
  // Adds a singleton set and returns its index.
  size_t add() {
    parent_.push_back(parent_.size());
    return parent_.size() - 1;
  }
  size_t find(size_t i) {
    while (parent_[i] != i) {
      i = parent_[i] = parent_[parent_[i]];
    }
    return i;
  }
  // False when a and b were already in the same set.
  bool merge(size_t a, size_t b) {
    a = find(a);
    b = find(b);
    if (a == b) {
      return false;
    }
    parent_[b] = a;
    return true;
  }
};
}  // namespace livehd::physical
