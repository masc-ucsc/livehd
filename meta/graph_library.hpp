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

  static std::unordered_map<std::string, Graph_library *> instances;

public:
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
    if(Graph_library::instances.find(path) == Graph_library::instances.end()) {
      Graph_library::instances.insert(std::make_pair(path, new Graph_library(path)));
    }
    return Graph_library::instances[path];
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

  void sync() {
    clean_library();
  }

	void reload();
};

#endif
