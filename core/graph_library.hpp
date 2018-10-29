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
  uint32_t   max_next_version;
  const std::string          path;
  const std::string          library_file;
  struct Graph_attributes {
    uint64_t    nentries;
    std::string name; // NOTE: No const as names can change (reload)
    uint32_t version; // In which sequence order were the graphs last modified
    int nopen;
    Graph_attributes() {
      clear();
    }
    void clear() {
      name     = "INVALID";
      nopen    = 0;
      version  = 0;
      nentries = 0;
    }
  };
  std::map<std::string, uint32_t>  name2id;
  std::vector<uint32_t>            recycled_id;

  // WARNING: Not from name (id) because names can happen many times (multiple create)
  typedef std::vector<Graph_attributes> Attribute_type;
  Attribute_type attribute;

  bool           graph_library_clean;

  Graph_library() {
    max_next_version = 1;
  }

  explicit Graph_library(const std::string &_path);

  void clean_library();

  ~Graph_library() {
    clean_library();
  }

  static std::unordered_map<std::string, Graph_library *> global_instances;
  static std::map<std::string, std::map<std::string, LGraph *>> global_name2lgraph;

  uint32_t reset_id(const std::string &name);

public:
  static LGraph *try_find_lgraph(const std::string &path, const std::string &name);

  uint32_t add_name(const std::string &name);

  const std::string &get_name(uint32_t lgid) const {
    assert(lgid > 0); // 0 is invalid lgid
    assert(attribute.size() > (size_t) lgid);
    return attribute[lgid].name;
  }

  int lgraph_count() const {
    return attribute.size()-1;
  }

  uint32_t get_id(const std::string &name) const {
    const auto &it = name2id.find(name);
    if(it != name2id.end()) {
      return it->second;
    }
    return 0; // -1 is invalid ID
  }

  void update(uint32_t lgid);

  uint32_t get_version(uint32_t lgid) const {
    if (attribute.size() >= lgid)
      return 0; // Invalid ID

    return attribute[lgid].version;
  }

  // FIXME: replace this by a find / end when the iterator is working
  bool include(const std::string &name) const {
    return name2id.find(name) != name2id.end();
  }

  // FIXME: Change to Graph_library &instance...
  static Graph_library *instance(std::string path) {
    if(Graph_library::global_instances.find(path) == Graph_library::global_instances.end()) {
      Graph_library::global_instances.insert(std::make_pair(path, new Graph_library(path)));
    }
    return Graph_library::global_instances[path];
  }

  uint32_t get_max_version() const { assert(max_next_version>0); return max_next_version-1; }

  void each_graph(std::function<void(const std::string &, uint32_t lgid)> f1) const;

  bool expunge_lgraph(const std::string &name, const LGraph *lg);

  uint32_t register_lgraph(const std::string &name, LGraph *lg);
  bool     unregister_lgraph(const std::string &name, uint32_t lgid, const LGraph *lg);

  void update_nentries(uint32_t lgid, uint64_t nentries);

  void sync() {
    clean_library();
  }

	void reload();
};

#endif
