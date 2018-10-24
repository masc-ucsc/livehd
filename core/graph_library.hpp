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
  uint32_t   max_version;
  const std::string          path;
  const std::string          library_file;
  struct Graph_attributes {
    std::string name; // NOTE: No const as names can change (reload)
    int version; // In which sequence order were the graphs last modified
    int nopen;
    Graph_attributes() {
      clear();
    }
    void clear() {
      name = "INVALID";
      nopen = 0;
      version = 0;
    }
  };
  std::map<std::string, uint32_t>   name2id;
  std::vector<uint32_t>        recycled_id;

  // WARNING: Not from name (id) because names can happen many times (multiple create)
  typedef std::vector<Graph_attributes> Attribute_type;
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

  uint32_t reset_id(const std::string &name) {
    const auto &it = name2id.find(name);
    if(it != name2id.end()) {
      graph_library_clean = false;
      attribute[it->second].version = ++max_version;
      return it->second;
    }
    return add_name(name);
  }
public:
  static LGraph *find_lgraph(const std::string &path, const std::string &name);

  uint32_t add_name(const std::string &name) {

    uint32_t id = attribute.size();
    if (!recycled_id.empty()) {
      assert(id>recycled_id.back());
      id = recycled_id.back();
      recycled_id.pop_back();
    }else{
      attribute.resize(id+1);
    }

    graph_library_clean = false;
    attribute[id].name  = name;
    attribute[id].version  = ++max_version;
    attribute[id].nopen  = 0;

    assert(name2id.find(name) == name2id.end());
    name2id[name] = id;

    return id;
  }

  const std::string &get_name(uint32_t lgid) const {
    assert(lgid > 0); // 0 is invalid lgid
    assert(attribute.size() > (size_t) lgid);
    return attribute[lgid].name;
  }

  LGraph *get_graph(uint32_t lgid) const;

  int lgraph_count() const {
    return attribute.size();
  }

  uint32_t get_id(const std::string &name) const {
    const auto &it = name2id.find(name);
    if(it != name2id.end()) {
      return it->second;
    }
    return 0; // -1 is invalid ID
  }

  void update(uint32_t lgid) {
    assert(attribute.size() < lgid);

    if (attribute[lgid].version == max_version)
      return;

    graph_library_clean = false;
    attribute[lgid].version = ++max_version;
  }

  int get_version(uint32_t lgid) const {
    if (attribute.size() >= lgid)
      return 0; // Invalid ID

    return attribute[lgid].version;
  }

  // FIXME: replace this by a find / end when the iterator is working
  bool include(const std::string &name) const {
    return name2id.find(name) != name2id.end();
  }

  static Graph_library *instance(std::string path) {
    if(Graph_library::global_instances.find(path) == Graph_library::global_instances.end()) {
      Graph_library::global_instances.insert(std::make_pair(path, new Graph_library(path)));
    }
    return Graph_library::global_instances[path];
  }

  int get_max_version() const { return max_version; }

  void each_graph(std::function<void(const std::string &, uint32_t lgid)> f1) const {
    for (const std::pair<const std::string, uint32_t> entry : name2id) {
      f1(entry.first, entry.second);
    }
  }

  bool expunge_lgraph(const std::string &name, const LGraph *lg) {
    if (global_name2lgraph[path][name] == lg) {
      console->warn("graph_library::delete_lgraph({}) for a wrong graph??? path:{}", name, path);
      return true;
    }
    global_name2lgraph[path].erase(global_name2lgraph[path].find(name));
    uint32_t id = name2id[name];
    name2id[name] = 0;

    if (attribute[id].nopen == 0) {
      attribute[id].clear();
      recycled_id.push_back(id);
      return true;
    }

    // WARNING: Do not recycle attribute (multiple ::create, and overwrite existing name)
    return false;
  }

  uint32_t register_lgraph(const std::string &name, LGraph *lg) {
    global_name2lgraph[path][name] = lg;

    uint32_t id = reset_id(name);

    const auto &it = name2id.find(name);
    assert(it != name2id.end());
    attribute[id].nopen++;

    return id;
  }

  bool unregister_lgraph(const std::string &name, uint32_t lgid, const LGraph *lg) {
    assert(attribute.size() > (size_t) lgid);

    // WARNING: NOT ALWAYS, it can ::create multiple times before calling unregister 
    // assert(name2id[name] == lgid);
    // assert(global_name2lgraph[path][name] == lg);

    attribute[lgid].nopen--;

    if (attribute[lgid].nopen==0) {
      fmt::print("TODO: garbage collect lgraph {}\n", name);
      bool done = expunge_lgraph(name, lg);
      if (done)
        return true;
      recycled_id.push_back(lgid);
      return true;
    }

    return false;
  }

  void sync() {
    clean_library();
  }

	void reload();
};

#endif
