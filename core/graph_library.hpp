//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <cstdint>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "explicit_type.hpp"

// Description:
//
// Graph_library is the base class to keep track of lgraph names, input, outputs.
//
// It can handle multiple lgraph directories at the same time, but it does NOT allow to link across lgraph directories.
//
// The lgraph_ids are unique per lgraph directory

class LGraph;
using Lg_type_id =  Explicit_type<uint32_t, struct Lg_type_id_struct>;

class Graph_library {
protected:
  Lg_type_id        max_next_version;
  const std::string path;
  const std::string library_file;
  struct Graph_attributes {
    uint64_t    nentries;
    std::string name;    // NOTE: No const as names can change (reload)
    std::string source;  // File were this module came from. If file updated (all the associated lgraphs must be deleted)
    Lg_type_id  version; // In which sequence order were the graphs last modified
    int         nopen;
    Graph_attributes() {
      clear();
    }
    void clear() {
      name     = "INVALID";
      nopen    = 0;
      version  = 0;
      nentries = 0;
      source   = "";
    }
  };
  std::unordered_map<std::string, Lg_type_id::type> name2id;
  std::vector<Lg_type_id::type>           recycled_id;

  // WARNING: Not from name (id) because names can happen many times (multiple create)
  typedef std::vector<Graph_attributes> Attribute_type;
  Attribute_type                        attribute;

  bool graph_library_clean;

  Graph_library() {
    max_next_version = 1;
  }

  explicit Graph_library(const std::string &_path);

  void clean_library();

  ~Graph_library() {
    clean_library();
  }

  static std::unordered_map<std::string, Graph_library *>       global_instances;
  static std::unordered_map<std::string, std::unordered_map<std::string, LGraph *>> global_name2lgraph;

  Lg_type_id reset_id(const std::string &name, const std::string &source);

public:
  static bool    exists(const std::string &path, const std::string &name);
  static LGraph *try_find_lgraph(const std::string &path, const std::string &name);
  LGraph *       try_find_lgraph(const std::string &name);

  Lg_type_id add_name(const std::string &name, const std::string &source);
  bool     rename_name(const std::string &orig, const std::string &dest);

  const std::string &get_name(Lg_type_id lgid) const {
    assert(lgid > 0); // 0 is invalid lgid
    assert(attribute.size() > lgid);
    return attribute[lgid].name;
  }

  Lg_type_id get_id(const std::string &name) const {
    const auto &it = name2id.find(name);
    if(it != name2id.end()) {
      return it->second;
    }
    return 0; // Invalid ID
  }

  const std::string &get_source(Lg_type_id lgid) const {
    assert(lgid > 0); // 0 is invalid lgid
    assert(attribute.size() > lgid);
    return attribute[lgid].source;
  }

  const std::string &get_source(const std::string &name) const {
    return get_source(get_id(name));
  }

  int lgraph_count() const {
    return attribute.size() - 1;
  }


  void update(Lg_type_id lgid);

  Lg_type_id get_version(Lg_type_id lgid) const {
    if(attribute.size() < lgid)
      return 0; // Invalid ID

    return attribute[lgid].version;
  }

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

  Lg_type_id get_max_version() const {
    assert(max_next_version > 0);
    return max_next_version - 1;
  }

  void each_graph(std::function<void(const std::string &, Lg_type_id lgid)> f1) const;

  bool expunge_lgraph(const std::string &name, const LGraph *lg);

  Lg_type_id register_lgraph(const std::string &name, const std::string &source, LGraph *lg);
  bool     unregister_lgraph(const std::string &name, Lg_type_id lgid, const LGraph *lg);

  void     update_nentries(Lg_type_id lgid, uint64_t nentries);
  uint64_t get_nentries(Lg_type_id lgid) const {
    assert(attribute.size() >= lgid);

    return attribute[lgid].nentries;
  };

  void sync() {
    clean_library();
  }

  static void sync_all(); // Called when running out of mmaps

  void reload();
};

