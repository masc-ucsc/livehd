//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node.hpp"
#include "node_pin.hpp"

class XEdge {  // FIXME: s/XEdge/Edge/g
protected:
  friend class LGraph;
  friend class Node;
  friend class Node_pin;

public:
  struct __attribute__((packed)) Compact {
    uint64_t driver_idx : Index_bits;
    uint16_t pad1 : 1;  // Just to improve alignment of
    uint64_t sink_idx : Index_bits;
    uint16_t pad2 : 1;  // Just to improve alignment of

    Compact(const Index_ID &d_idx, const Index_ID &s_idx)
        : driver_idx(d_idx), pad1(0), sink_idx(s_idx), pad2(0) {}
    Compact() : driver_idx(0), pad1(0), sink_idx(0), pad2(0) {}

    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      driver_idx = obj.driver_idx;
      sink_idx   = obj.sink_idx;

      return *this;
    }

    constexpr bool is_invalid() const { return driver_idx == 0; }

    constexpr bool operator==(const Compact &other) const {
      return (driver_idx == other.driver_idx) && (sink_idx == other.sink_idx);
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); };

    template <typename H>
    friend H AbslHashValue(H h, const Compact &s) {
      return H::combine(std::move(h), s.driver_idx, s.sink_idx);
    }
  };

  template <typename H>
  friend H AbslHashValue(H h, const XEdge &s) {
    return H::combine(std::move(h), s.driver, s.sink);
  }

  Node_pin driver;
  Node_pin sink;

  XEdge(LGraph *g, const Compact &c);
  XEdge(const Node_pin &src_, const Node_pin &dst_);

  constexpr bool is_invalid() const { return driver.is_invalid(); }

  constexpr bool operator==(const XEdge &other) const { return (driver == other.driver) && (sink == other.sink); }
  constexpr bool operator!=(const XEdge &other) const { return !(*this == other); };

  inline Compact get_compact() const { return Compact(driver.get_idx(), sink.get_idx()); }

  void del_edge();
  void add_edge();
  void add_edge(uint32_t bits);

  // BEGIN ATTRIBUTE ACCESSORS

  uint32_t get_bits() const { return driver.get_bits(); }

  // END ATTRIBUTE ACCESSORS
};

namespace mmap_lib {
template <>
struct hash<XEdge::Compact> {
  size_t operator()(XEdge::Compact const &o) const {
    uint64_t h = o.driver_idx;
    h <<=32;
    h |= o.sink_idx;
    return hash<uint64_t>{}(h);
  }
};

}  // namespace mmap_lib
