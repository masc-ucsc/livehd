// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace livehd::physical {
// Append-only occupancy of a directional strip. Closed intervals represent
// every uint32_t coordinate, including UINT32_MAX, without an end sentinel.
// Owner and exact DRC geometry remain in Routing_occupancy. Callers provide
// first <= last; compact(extent) fixes the bound for subsequent operations.
// Routing_occupancy clips insertions and rejects out-of-frame queries before
// calling this helper.
class Interval_occupancy {
  template <typename Coordinate>
  struct Run {
    Coordinate first, last;
  };
  using Runs16 = std::vector<Run<uint16_t>>;
  using Runs32 = std::vector<Run<uint32_t>>;
  using Words  = std::vector<uint64_t>;
  std::variant<Runs16, Runs32, Words> storage_;

  static uint64_t low_mask(uint32_t bit) { return ~uint64_t{0} << (bit % 64); }
  static uint64_t high_mask(uint32_t bit) { return ~uint64_t{0} >> (63 - bit % 64); }
  static void     mark(Words& words, uint32_t first, uint32_t last) {
    const size_t a = first / 64, b = last / 64;
    if (a == b) {
      words[a] |= low_mask(first) & high_mask(last);
    } else {
      words[a] |= low_mask(first);
      std::fill(words.begin() + a + 1, words.begin() + b, ~uint64_t{0});
      words[b] |= high_mask(last);
    }
  }
  template <typename Coordinate>
  static void merge(std::vector<Run<Coordinate>>& runs, uint32_t first, uint32_t last) {
    auto begin = std::lower_bound(runs.begin(), runs.end(), first, [](const auto& run, uint32_t value) {
      return uint64_t(run.last) + 1 < value;
    });
    auto end   = begin;
    while (end != runs.end() && uint64_t(end->first) <= uint64_t(last) + 1) {
      first = std::min(first, uint32_t(end->first));
      last  = std::max(last, uint32_t(end->last));
      ++end;
    }
    const Run<Coordinate> merged{Coordinate(first), Coordinate(last)};
    if (begin == end) {
      runs.insert(begin, merged);
    } else {
      *begin = merged;
      runs.erase(begin + 1, end);
    }
  }

public:
  void insert(uint32_t first, uint32_t last) {
    if (auto* runs = std::get_if<Runs16>(&storage_); runs && last > UINT16_MAX) {
      Runs32 wide;
      wide.reserve(runs->size() + 1);
      for (const auto& run : *runs) {
        wide.push_back({run.first, run.last});
      }
      storage_ = std::move(wide);
    }
    std::visit(
        [&](auto& values) {
          if constexpr (std::is_same_v<std::decay_t<decltype(values)>, Words>) {
            mark(values, first, last);
          } else {
            merge(values, first, last);
          }
        },
        storage_);
  }
  bool intersects(uint32_t first, uint32_t last) const {
    return std::visit(
        [&](const auto& values) -> bool {
          if constexpr (std::is_same_v<std::decay_t<decltype(values)>, Words>) {
            const size_t a = first / 64, b = last / 64;
            if (a == b) {
              return values[a] & low_mask(first) & high_mask(last);
            }
            if ((values[a] & low_mask(first)) || (values[b] & high_mask(last))) {
              return true;
            }
            return std::any_of(values.begin() + a + 1, values.begin() + b, [](uint64_t value) { return value != 0; });
          } else {
            const auto it = std::lower_bound(values.begin(), values.end(), first, [](const auto& run, uint32_t value) {
              return run.last < value;
            });
            return it != values.end() && it->first <= last;
          }
        },
        storage_);
  }
  // Dense storage is bounded and chosen only if cheaper than merged runs.
  // No per-design policy; local windows reach this threshold sooner.
  void compact(uint64_t extent, bool dense = true) {
    Words bits;
    bool  convert = false;
    std::visit(
        [&](auto& values) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(values)>, Words>) {
            const auto words = (extent + 63) / 64;
            if (dense && words <= 1024
                && words * sizeof(uint64_t) <= values.size() * sizeof(typename std::decay_t<decltype(values)>::value_type)) {
              bits.resize(words);
              for (const auto& run : values) {
                mark(bits, run.first, run.last);
              }
              convert = true;
            } else {
              values.shrink_to_fit();
            }
          }
        },
        storage_);
    if (convert) {
      storage_ = std::move(bits);
    }
  }
  size_t bytes() const {
    return std::visit(
        [](const auto& values) { return values.capacity() * sizeof(typename std::decay_t<decltype(values)>::value_type); },
        storage_);
  }
  bool dense() const { return std::holds_alternative<Words>(storage_); }
};
}  // namespace livehd::physical
