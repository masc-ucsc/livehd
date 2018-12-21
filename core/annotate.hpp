
#pragma once

#include <cstdint>
#include <utility>
#include <functional>

#include "char_array.hpp"
#include "dense.hpp"
#include "explicit_type.hpp"
#include "graph_library.hpp"

using Name_id =  Explicit_type<uint64_t, struct Name_name_struct>;

using Index_ID = size_t;

// It can be build dynamically, or off-line as a pass traversing all the lgraphs
//
// TODO: Redundant information inside lgraph. If LGraph has a "fast" way to get
// children, this data structure may change, but keep same API

class Annotate_hierarchy {
  // FIXME: MOVE THIS TO lgraph_library
  // FIXME: the add_child should be removed. As it should request the info to the lgraph (avoid replication)

  struct Track_parent {
    Char_Array_ID parent_cid;  // 0 if no name id (annonymous)
    size_t        next_parent; // 0 no parent
  };

  Char_Array<Lg_type_id>  type_ca;
  Char_Array<Name_id>  name_ca;

  Dense<Track_parent> parent_type_tree;
  Dense<Track_parent> parent_name_tree;

public:
  Annotate_hierarchy(const std::string &path)
    : type_ca(path + "/hierarchy_type_ca")
    , name_ca(path + "/hierarchy_name_ca")
    , parent_type_tree(path + "/hierarchy_parent_type")
    , parent_name_tree(path + "/hierarchy_parent_name") {
  }

  Lg_type_id add_root(const std::string &type);
  std::pair<Lg_type_id,Name_id> add_root(const std::string &type, const std::string &name);
  Lg_type_id add_child(const std::string &parent_type, const std::string &child_type);
  std::pair<Lg_type_id,Name_id> add_child(const std::string &parent_type, const std::string &child_type, const std::string &child_name);

  void each_name(const std::string &name, std::function<bool(Lg_type_id,Name_id)> fn) const;
  void each_type(const std::string &type, std::function<bool(Lg_type_id,Name_id)> fn) const;

  static Annotate_hierarchy &get(const std::string &path);
};

class Meta_base {
protected:
  Meta_base() {
  }
public:

};

class Meta_per_type : public Meta_base {
public:
  const Lg_type_id lg_type_id;

  Meta_per_type() = delete;
  Meta_per_type(Lg_type_id tid)
  : type_id(tid) {
  }

};

class Meta_per_name : public Meta_base {
public:
  const Name_id name_id;

  Meta_per_name() = delete;
  Meta_per_name(Name_id nid)
  : name_id(nid) {
  }

};

class Meta_flatten : public Meta_per_name {
  Dense<bool> variable_internal;

public:
  Meta_flatten(const std::string &path, Name_id nid)
    : Meta_per_name(nid)
    , variable_internal(path + std::string("/flatten_") + std::to_string(nid.value)) {

  }
};

#if 0
class Annotate {
private:
  std::vector<std::any> meta_type;
  std::vector<std::any> meta_name;
  std::vector<std::function<void(const std::string &)> meta_create_fn;

public:

  std::any get_meta_type(const std::string &name);
  std::any get_meta_name(const std::string &name);
};
#endif

void sample() {

  const std::string path = {"lgdb"};

  auto hier = Annotate_hierarchy::get(path);

  // name hierarchy
  hier.each_name("foo/bar", [&path](Lg_type_id tid, Name_id nid) {
    Meta_flatten flat1(path,nid);

    return true; // continue
  });

  // Type hierarchy
  hier.each_name("foo.bar", [&path](Lg_type_id tid, Name_id nid) {
    Meta_flatten flat1(path,nid);

    return false; // stop
  });

  // compile error unclear Type vs Name hierarchy
  hier.each_name("foo.bar/xxx", [&path](Lg_type_id tid, Name_id nid) {
    Meta_flatten flat1(path,nid);
    return false; // stop
  });

}

