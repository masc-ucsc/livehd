//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <cassert>
#include <map>
#include <string>

#include "char_array.hpp"
#include "dense.hpp"

#include "lgraph_base_core.hpp"

class LGraph_InstanceNames : virtual public Lgraph_base_core {
private:
  Char_Array<Index_ID> inames;
  Dense<Index_ID>      instances;

protected:
public:
  LGraph_InstanceNames() = delete;
  explicit LGraph_InstanceNames(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept;
  virtual ~LGraph_InstanceNames(){};

  void clear();
  void reload(size_t sz);
  void sync();
  void emplace_back();

  std::string_view get_instancename(Char_Array_ID wid) const;

  Char_Array_ID get_instance_name_id(Index_ID nid) const;
  void          set_node_instance(Index_ID nid, Char_Array_ID instance_name_id);

  Char_Array_ID set_node_instance_name(Index_ID nid, std::string_view name) {
    assert(nid < instances.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    assert(node_internal[nid].get_nid() == nid);
    assert(!name.empty());

    Char_Array_ID cid = inames.create_id(name, nid);
    set_node_instance(nid, cid);
    assert(inames.get_name(cid) == name);
    return cid;
  }

  std::string_view get_node_instancename(Index_ID nid) const {
    assert(nid < instances.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    assert(node_internal[nid].get_nid() == nid);

    return get_instancename(get_instance_name_id(nid));
  }

  bool has_instance_name(std::string_view name) const;

  Index_ID get_node_from_instance_name(std::string_view name) const;
};
