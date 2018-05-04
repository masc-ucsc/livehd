
#ifndef INSTANCE_NAME_H
#define INSTANCE_NAME_H

#include <assert.h>

#include <string>
#include <map>
#include "dense.hpp"

#include "lgraphbase.hpp"
#include <string>

class LGraph;

class LGraph_InstanceNames : virtual public LGraph_Base {
private:
  Char_Array<Index_ID> names;
  Dense<Index_ID>      instances;

protected:

  friend LGraph;

public:
  LGraph_InstanceNames(std::string path, std::string name);

  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  const char * get_instancename(Char_Array_ID wid) const;

  Char_Array_ID get_instance_name_id(Index_ID nid) const;
  void set_node_instance(Index_ID nid, Char_Array_ID instance_name_id);

  Char_Array_ID set_node_instance_name(Index_ID nid, const char* name) {
    assert(nid<instances.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    assert(node_internal[nid].get_nid() == nid);
    assert(std::string(name) != "");

    Char_Array_ID cid = names.create_id(name, nid);
    set_node_instance(nid, cid);
    return cid;
  }

  const char* get_node_instancename(Index_ID nid) const {
    assert(nid<instances.size());
    assert(node_internal[nid].is_node_state());
    assert(node_internal[nid].is_root());
    assert(node_internal[nid].get_nid() == nid);

    if(get_instance_name_id(nid) == 0)
      return nullptr;
    return get_instancename(get_instance_name_id(nid));
  }

  bool has_instance_name(const char* name) const;

  Index_ID get_node_from_instance_name(const char* name) const;
};

#endif
