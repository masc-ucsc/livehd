//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node_pin.hpp"

class Node;

class XEdge {  // FIXME: s/XEdge/Edge/g
protected:
  friend class Lgraph;
  friend class Node;
  friend class Node_pin;

public:
  struct __attribute__((packed)) Compact {
    Hierarchy_index driver_hidx;
    uint64_t        driver_idx : Index_bits;
    uint16_t        pad1 : 1;  // Just to improve alignment of
    Hierarchy_index sink_hidx;
    uint64_t        sink_idx : Index_bits;
    uint16_t        pad2 : 1;  // Just to improve alignment of

    Compact(const Hierarchy_index &d_hidx, const Index_id &d_idx, const Hierarchy_index &s_hidx, const Index_id &s_idx)
        : driver_hidx(d_hidx), driver_idx(d_idx), pad1(0), sink_hidx(s_hidx), sink_idx(s_idx), pad2(0) {}
    Compact() : driver_idx(0), pad1(0), sink_idx(0), pad2(0) {}

    Compact &operator=(const Compact &obj) {
      if (this != &obj) {
        driver_hidx = obj.driver_hidx;
        driver_idx  = obj.driver_idx;
        sink_hidx   = obj.sink_hidx;
        sink_idx    = obj.sink_idx;
      };

      return *this;
    }

    constexpr bool is_invalid() const { return driver_idx == 0; }

    constexpr bool operator==(const Compact &other) const {
      return (driver_hidx == other.driver_hidx) && (driver_idx == other.driver_idx) && (sink_hidx == other.sink_hidx)
             && (sink_idx == other.sink_idx);
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); };

    template <typename H>
    friend H AbslHashValue(H h, const Compact &s) {
      return H::combine(std::move(h), s.driver_hidx.hash(), s.driver_idx, s.sink_hidx.hash(), s.sink_idx);
    }
  };

  template <typename H>
  friend H AbslHashValue(H h, const XEdge &s) {
    return H::combine(std::move(h), s.driver, s.sink);
  }

  Node_pin driver;
  Node_pin sink;

  constexpr XEdge(){};
  XEdge(Lgraph *g, const Compact &c);
  XEdge(const Node_pin &src_, const Node_pin &dst_);

  constexpr bool is_invalid() const { return driver.is_invalid(); }

  bool operator==(const XEdge &other) const { return (driver == other.driver) && (sink == other.sink); }
  bool operator!=(const XEdge &other) const { return !(*this == other); };

  inline Compact get_compact() const {
    return Compact(driver.get_hidx(), driver.get_root_idx(), sink.get_hidx(), sink.get_root_idx());
  }

  void del_edge();
  void add_edge();
  void add_edge(uint32_t bits);

  static void del_edge(Node_pin &dpin, Node_pin &spin);

  // BEGIN ATTRIBUTE ACCESSORS

  uint32_t get_bits() const { return driver.get_bits(); }

  // END ATTRIBUTE ACCESSORS
};

namespace mmap_lib {
template <>
struct hash<XEdge::Compact> {
  size_t operator()(XEdge::Compact const &o) const {
    auto key = woothash64(static_cast<const void *>(&o), sizeof(o));
    return hash<uint64_t>{}(key);
  }
};

}  // namespace mmap_lib
