//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef GRAPHLIBRARY_H
#define GRAPHLIBRARY_H

#include <cassert>
#include <map>
#include <vector>
#include <functional>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include "lglog.hpp"

// Description:
//
// Graph_library is the base class to keep track of lgraph names, input, outputs.
//
// It can handle multiple lgraph directories at the same time, but it does NOT allow to link across lgraph directories.
//
// The lgraph_ids are unique per lgraph directory

class LGraph;

class Graph_library {
protected:
  int   max_version;
  const std::string          path;
  const std::string          library_file;
  struct Graph_attributes {
    int id;
    int version; // In which sequence order were the graphs last modified
  };
  std::vector<std::string>   id2name;

  typedef std::map<std::string, Graph_attributes> Attribute_type;
  Attribute_type attribute;
  bool                       graph_library_clean;

  Graph_library() {
    max_version = 0;
  }

  explicit Graph_library(const std::string &_path);

  void clean_library();

  ~Graph_library() {
    clean_library();
  }

  static std::unordered_map<std::string, Graph_library *> global_instances;
  static std::map<std::string, std::map<std::string, LGraph *>> global_name2lgraph;

public:
  static LGraph *find_lgraph(const std::string &path, const std::string &name);

  int add_name(const std::string &name) {
    assert(attribute.find(name) == attribute.end());

    int id = id2name.size();

    graph_library_clean = false;
    attribute[name].id  = id;
    attribute[name].version  = ++max_version;
    id2name.push_back(name);

    assert(id2name[id] == name);

    return id;
  }

  const std::string &get_name(int id) const {
    assert(id > 0); // 0 is invalid id
    assert(id2name.size() > (size_t) id);
    return id2name[id];
  }

  LGraph *get_graph(int id) const;

  int lgraph_count() const {
    return id2name.size();
  }

  uint32_t get_id(const std::string &name) const {
    const auto &it = attribute.find(name);
    if(it != attribute.end()) {
      return it->second.id;
    }
    return 0; // -1 is invalid ID
  }

  void update(const std::string &name) {
    const auto &it = attribute.find(name);
    assert(it != attribute.end());
    if (it->second.version == max_version)
      return;

    graph_library_clean = false;
    it->second.version = ++max_version;
  }

  // FIXME: change to int32_t (32 bits at most for subgraph id
  int reset_id(const std::string &name) {
    const auto &it = attribute.find(name);
    if(it != attribute.end()) {
      graph_library_clean = false;
      it->second.version = ++max_version;
      return it->second.id;
    }
    return add_name(name);
  }

  int get_version(int id) const {
    auto &name = get_name(id);
    const auto &it = attribute.find(name);
    if(it != attribute.end()) {
      return it->second.version;
    }
    return 0; // 0 is invalid ID
  }

  // FIXME: replace this by a find / end when the iterator is working
  bool include(const std::string &name) const {
    return attribute.find(name) != attribute.end();
  }

  static Graph_library *instance(std::string path) {
    if(Graph_library::global_instances.find(path) == Graph_library::global_instances.end()) {
      Graph_library::global_instances.insert(std::make_pair(path, new Graph_library(path)));
    }
    return Graph_library::global_instances[path];
  }

  const std::vector<std::string> &list_all() const {
    return id2name;
  }

  int get_max_version() const { return max_version; }

  void each_graph(std::function<void(const std::string &,int id)> f1) const {
    for(Attribute_type::const_iterator it=attribute.begin();it!=attribute.end();it++) {
      f1(it->first, it->second.id);
    }
  }

  int register_lgraph(const std::string &name, LGraph *lg) {
    global_name2lgraph[path][name] = lg;

    return reset_id(name);
  }

  void unregister_lgraph(const std::string &name, int lgid, const LGraph *lg) {
    assert(id2name.size() > (size_t) lgid);
    assert(id2name[lgid] == name);
    assert(global_name2lgraph[path][name] == lg);

    fmt::print("TODO: garbage collect lgraph {}\n", name);
  }

  void sync() {
    clean_library();
  }

	void reload();
};

#endif
