//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <string_view>
#include <map>
#include <string>

#include "iassert.hpp"
#include "dense.hpp"
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

  virtual uint16_t get_offset(Index_ID nid) const {
    assert(nid < offsets.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());

    return offsets[nid];
  }

  friend LGraph;

public:
  LGraph_WireNames() = delete;
  explicit LGraph_WireNames(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_WireNames(){};
  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  std::string_view get_wirename(WireName_ID wid) const;

  WireName_ID get_wid(Index_ID nid) const;
  void        set_node_wirename(Index_ID nid, WireName_ID wid);

  WireName_ID set_node_wirename(Index_ID nid, std::string_view name) {
    assert(nid < wires.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    WireName_ID wid = names.create_id(name, nid);
    set_node_wirename(nid, wid);
    return wid;
  }

  std::string_view get_node_wirename(Index_ID nid) const {
    I(nid < wires.size());
    I(node_internal[nid].is_node_state());
    I(node_internal[nid].is_root());

    return get_wirename(get_wid(nid));
  }

  bool    has_wirename(std::string_view name) const { return names.include(name); }
  Index_ID get_node_id(std::string_view name) const { return names.get_field(names.get_id(name)); }

  void dump_wirenames() const {
    fmt::print("wirenames {} \n", name);
    for(auto it=names.begin(); it!=names.end(); ++it) {
      fmt::print(" {}\n", it.get_name());
    }
  }
};

