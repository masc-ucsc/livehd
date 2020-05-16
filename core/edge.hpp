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
    uint32_t driver_pid : Port_bits;
    uint16_t pad3 : 1;  // Just to improve alignment of
    uint32_t sink_pid : Port_bits;
    uint32_t pad4 : 1;  // Just to improve alignment of

    Compact(const Index_ID &d_idx, const Port_ID &d_pid, const Index_ID &s_idx, const Port_ID &s_pid)
        : driver_idx(d_idx), pad1(0), sink_idx(s_idx), pad2(0), driver_pid(d_pid), pad3(0), sink_pid(s_pid), pad4(0){};
    Compact() : driver_idx(0), pad1(0), sink_idx(0), pad2(0), driver_pid(0), pad3(0), sink_pid(0), pad4(0){};

    Compact &operator=(const Compact &obj) {
      I(this != &obj);
      driver_idx = obj.driver_idx;
      driver_pid = obj.driver_pid;
      sink_idx   = obj.sink_idx;
      sink_pid   = obj.sink_pid;

      return *this;
    }

    constexpr bool is_invalid() const { return driver_idx == 0; }

    constexpr bool operator==(const Compact &other) const {
      return (driver_idx == other.driver_idx) && (driver_pid == other.driver_pid) && (sink_idx == other.sink_idx) &&
             (sink_pid == other.sink_pid);
    }
    constexpr bool operator!=(const Compact &other) const { return !(*this == other); };

    template <typename H>
    friend H AbslHashValue(H h, const Compact &s) {
      return H::combine(std::move(h), s.driver_idx, s.sink_idx, s.driver_pid, s.sink_pid);
    }
  };

  template <typename H>
  friend H AbslHashValue(H h, const XEdge &s) {
    return H::combine(std::move(h), s.driver, s.sink);
  }

  Node_pin driver;
  Node_pin sink;

  XEdge(const Node_pin &src_, const Node_pin &dst_);

  constexpr bool is_invalid() const { return driver.is_invalid(); }

  constexpr bool operator==(const XEdge &other) const { return (driver == other.driver) && (sink == other.sink); }
  constexpr bool operator!=(const XEdge &other) const { return !(*this == other); };

  inline Compact get_compact() const { return Compact(driver.get_idx(), driver.get_pid(), sink.get_idx(), sink.get_pid()); }

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
    h          = (h << 12) ^ o.driver_pid;
    auto h1    = hash<uint64_t>{}(h);

    h = o.sink_idx;
    h = (h << 12) ^ o.sink_pid;

    return h1 ^ hash<uint64_t>{}(h);
  }
};
}  // namespace mmap_lib
