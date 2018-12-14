//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef LGWIRENAME_H
#define LGWIRENAME_H

#include <assert.h>

#include "dense.hpp"
#include <map>
#include <string>

#include "lgraphbase.hpp"
#include <string>

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
  explicit LGraph_WireNames(const std::string &path, const std::string &name) noexcept;
  virtual ~LGraph_WireNames(){};
  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  // WireName_ID get_wirename_id(const char *wirename);
  const char *get_wirename(WireName_ID wid) const;

  WireName_ID get_wid(Index_ID nid) const;
  void        set_node_wirename(Index_ID nid, WireName_ID wid);

  WireName_ID set_node_wirename(Index_ID nid, const char *name) {
    assert(nid < wires.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    WireName_ID wid = names.create_id(name, nid);
    set_node_wirename(nid, wid);
    return wid;
  }

  const char *get_node_wirename(Index_ID nid) const {
    assert(nid < wires.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());


    WireName_ID wid = get_wid(nid);
    if(wid == 0)
      return nullptr;
    return get_wirename(wid);
  }

  bool has_name(const char *name) const;
  bool has_name(const std::string &name) const {
    return has_name(name.c_str());
  }

  Index_ID get_node_id(const char *name) const;
  Index_ID get_node_id(const std::string &name) const {
    return get_node_id(name.c_str());
  }

  void dump_lgwires() const {
    fmt::print("lgwires {} \n", name);
    for(auto it=names.begin(); it!=names.end(); ++it) {
      fmt::print(" {}\n", it.get_char());
    }
  }
};

#endif
