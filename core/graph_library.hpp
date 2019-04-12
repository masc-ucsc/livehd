//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bm.h"

#include "lgraphbase.hpp"
#include "tech_library.hpp"
#include "sub_node.hpp"

class LGraph;

class Graph_library {
protected:
  struct Graph_attributes {
    Sub_node    sub_node;
    uint64_t    nentries; // TODO: deprecate with the new attributes (once all the old attributes are gone)
    std::string source;   // File were this module came from. If file updated (all the associated lgraphs must be deleted). If empty, it ies not present (blackbox)
    Lg_type_id  version;  // In which sequence order were the graphs last modified
    int         nopen;
    Graph_attributes() { expunge(); }
    void expunge() {
      sub_node.expunge();
      nopen    = 0;
      version  = 0;
      nentries = 0;
      source   = "-";
    }
  };

  // BEGIN: common attributes or properties shared across all graphs in this library
  std::vector<std::string> liberty_list;
  std::vector<std::string> sdc_list;
  std::vector<std::string> spef_list;

  std::vector<Tech_layer>  layer_list;  // only for routing
  std::vector<Tech_via>    via_list;    // only for routing
  // END: common attributes

  using Global_instances   = absl::flat_hash_map<std::string, Graph_library *>;
  using Global_name2lgraph = absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, LGraph *>>;
  using Name2id            = absl::flat_hash_map<std::string, Lg_type_id::type>;
  using Recycled_id        = bm::bvector<>;
  using Attribute_type     = std::vector<Graph_attributes>;

  Lg_type_id        max_next_version;
  const std::string path;
  const std::string library_file;

  Name2id        name2id;
  Recycled_id    recycled_id;
  Attribute_type attribute;

  static Global_instances   global_instances;
  static Global_name2lgraph global_name2lgraph;

  bool graph_library_clean;

  Graph_library() { max_next_version = 1; }

  explicit Graph_library(std::string_view _path);

  void clean_library();

  ~Graph_library() { clean_library(); }

  Lg_type_id reset_id(std::string_view name, std::string_view source);

  Lg_type_id try_get_recycled_id();
  void       recycle_id(Lg_type_id lgid);

  static std::string get_lgraph_filename(std::string_view path, std::string_view name, std::string_view ext);

public:
  static bool    exists(std::string_view path, std::string_view name);
  static LGraph *try_find_lgraph(std::string_view path, std::string_view name);
  LGraph *       try_find_lgraph(std::string_view name);

  Sub_node      &setup_sub(std::string_view name);
  Sub_node      &get_sub(Lg_type_id lgid) {
    graph_library_clean = false;
    I(lgid > 0);  // 0 is invalid lgid
    I(attribute.size() > lgid);
    I(attribute[lgid].sub_node.get_lgid() == lgid);
    return attribute[lgid].sub_node;
  }
  Sub_node      &get_sub(std::string_view name) {
    graph_library_clean = false;
    return get_sub(get_lgid(name));
  }
  const Sub_node      &get_sub(Lg_type_id lgid) const {
    I(lgid > 0);  // 0 is invalid lgid
    I(attribute.size() > lgid);
    I(attribute[lgid].sub_node.get_lgid() == lgid);
    return attribute[lgid].sub_node;
  }
  const Sub_node      &get_sub(std::string_view name) const {
    return get_sub(get_lgid(name));
  }

  Lg_type_id     add_name(std::string_view name, std::string_view source);
  bool           rename_name(std::string_view orig, std::string_view dest);

  std::string_view get_name(Lg_type_id lgid) const {
    I(lgid > 0);  // 0 is invalid lgid
    I(attribute.size() > lgid);
    I(attribute[lgid].sub_node.get_lgid() == lgid);
    return attribute[lgid].sub_node.get_name();
  }

  Lg_type_id get_lgid(std::string_view name) const {
    const auto &it = name2id.find(name);
    if (it != name2id.end()) {
      return it->second;
    }
    return 0;  // Invalid ID
  }

  std::string_view get_source(Lg_type_id lgid) const {
    assert(lgid > 0);  // 0 is invalid lgid
    assert(attribute.size() > lgid);
    return attribute[lgid].source;
  }

  std::string_view get_source(std::string_view name) const { return get_source(get_lgid(name)); }

  int lgraph_count() const { return attribute.size() - 1; }

  void update(Lg_type_id lgid);

  Lg_type_id get_version(Lg_type_id lgid) const {
    if (attribute.size() < lgid) return 0;  // Invalid ID

    return attribute[lgid].version;
  }

  bool include(std::string_view name) const { return name2id.find(name) != name2id.end(); }

  // TODO: Change to Graph_library &instance...
  static Graph_library *instance(std::string_view path);

  Lg_type_id get_max_version() const {
    assert(max_next_version > 0);
    return max_next_version - 1;
  }

  bool expunge_lgraph(std::string_view name, LGraph *lg);

  Lg_type_id copy_lgraph(std::string_view name, std::string_view new_name);
  Lg_type_id register_sub(std::string_view name);
  Lg_type_id register_lgraph(std::string_view name, std::string_view source, LGraph *lg);
  void       unregister(std::string_view name, Lg_type_id lgid); // unregister open instance
  void       expunge(std::string_view name); // Delete completely, even if open instances exists

  void     update_nentries(Lg_type_id lgid, uint64_t nentries);
  uint64_t get_nentries(Lg_type_id lgid) const {
    assert(attribute.size() >= lgid);

    return attribute[lgid].nentries;
  };

  void sync() { clean_library(); }

  static void sync_all();  // Called when running out of mmaps

  // FIXME: SFINAE
  void each_type(std::function<void(Lg_type_id, std::string_view)> fn) const;
  void each_type(std::function<bool(Lg_type_id, std::string_view)> fn) const;
  void each_type(std::string_view match, std::function<void(Lg_type_id, std::string_view)> fn) const;
  void each_type(std::string_view match, std::function<bool(Lg_type_id, std::string_view)> fn) const;

  const std::vector<std::string> &get_liberty() const { return liberty_list; };
  const std::vector<std::string> &get_sdc()     const { return sdc_list; };
  const std::vector<std::string> &get_spef()    const { return spef_list; };

  const std::vector<Tech_layer> &get_layer()    const { return layer_list; };
  const std::vector<Tech_via>   &get_via()      const { return via_list; };

  void reload();
};
