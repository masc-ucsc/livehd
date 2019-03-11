//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <map>
#include <string>
#include <string_view>

#include "dense.hpp"
#include "iassert.hpp"
#include "lgraphbase.hpp"

class LGraph;

typedef Char_Array_ID WireName_ID;

class LGraph_WireNames : virtual public LGraph_Base {
private:
  Char_Array<Index_ID> names;
  Dense<WireName_ID>   wires;
  Dense<uint16_t>      offsets;

protected:
  void set_offset(Index_ID nid, uint16_t offset) {
    assert(nid < offsets.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());

    offsets[nid] = offset;
  }

public:
  LGraph_WireNames() = delete;
  explicit LGraph_WireNames(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_WireNames(){};
  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  std::string_view get_wirename(WireName_ID wid) const;

#if 1
  // WARNING: deprecated (Node_pin)
  WireName_ID get_wid(Index_ID nid) const; // Move to protected, use has_wirename(Node_pin)

  void        set_node_wirename(Index_ID nid, WireName_ID wid);

  uint16_t get_offset(Index_ID idx) const {
    assert(idx < offsets.size());
    assert(node_internal[idx].is_node_state());
    assert(node_internal[idx].is_root());

    return offsets[idx];
  }
#endif

  bool has_wirename(const Node_pin &pin) const {
    assert(node_internal[pin.get_idx()].is_node_state());
    assert(node_internal[pin.get_idx()].is_root());
    assert(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());

    return get_wid(pin.get_idx())!=0;
  }

  uint16_t get_offset(const Node_pin &pin) const {
    assert(pin.get_idx() < offsets.size());
    assert(node_internal[pin.get_idx()].is_node_state());
    assert(node_internal[pin.get_idx()].is_root());
    assert(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());

    return offsets[pin.get_idx()];
  }

  WireName_ID set_node_wirename(Index_ID idx, std::string_view name) {
    assert(idx < wires.size());
    assert(node_internal[idx].is_node_state());
    assert(node_internal[idx].is_root());
    WireName_ID wid = names.create_id(name, idx);
    set_node_wirename(idx, wid);
    return wid;
  }

  void set_node_wirename(const Node_pin &pin, std::string_view name) {
    assert(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());
    set_node_wirename(pin.get_idx(), name);
  }

#if 1
  // WARNING: deprecated used Node_pin
  std::string_view get_node_wirename(Index_ID idx) const {
    I(idx < wires.size());
    I(node_internal[idx].is_node_state());
    I(node_internal[idx].is_root());

    return get_wirename(get_wid(idx));
  }
#endif
  std::string_view get_node_wirename(const Node_pin &pin) const {
    assert(pin.get_idx() < offsets.size());
    assert(node_internal[pin.get_idx()].is_node_state());
    assert(node_internal[pin.get_idx()].is_root());
    assert(node_internal[pin.get_idx()].get_dst_pid() == pin.get_pid());

    return get_wirename(get_wid(pin.get_idx()));
  }

  bool     has_wirename(std::string_view name) const { return names.include(name); }
  Index_ID get_node_id(std::string_view name) const { return names.get_field(names.get_id(name)); }

  void dump_wirenames() const {
    fmt::print("wirenames {} \n", name);
    for (auto it = names.begin(); it != names.end(); ++it) {
      fmt::print(" {}\n", it.get_name());
    }
  }
};
