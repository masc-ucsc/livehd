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
#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"

#include "lgraphbase.hpp"
#include "tech_library.hpp"
#include "sub_node.hpp"

class LGraph;

// FIXME: lgid keep increasing. We may want a garbage collector once the lgraph is shutdown to remap
// or at least find holes in lgids no longer used

class Graph_library {
protected:
  struct Graph_attributes {
    LGraph     *lg;
    std::string source;   // File were this module came from. If file updated (all the associated lgraphs must be deleted). If empty, it ies not present (blackbox)
    Lg_type_id  version;  // In which sequence order were the graphs last modified
    Graph_attributes() { expunge(); }
    void expunge() {
      lg       = 0;
      version  = 0;
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
  using Recycled_id        = absl::flat_hash_set<uint64_t>;

  Lg_type_id                    max_next_version;
  const std::string             path;
  const std::string             library_file;

  Name2id                       name2id;
  Recycled_id                   recycled_id;
  std::vector<Graph_attributes> attributes;
  std::vector<Sub_node>         sub_nodes;

  static Global_instances       global_instances;
  static Global_name2lgraph     global_name2lgraph;

  bool                          graph_library_clean;

  Graph_library() { max_next_version = 1; }

  explicit Graph_library(std::string_view _path);

  void clean_library();

  ~Graph_library() { clean_library(); }

  Lg_type_id reset_id(std::string_view name, std::string_view source);

  Lg_type_id try_get_recycled_id();
  void       recycle_id(Lg_type_id lgid);

  static std::string get_lgraph_filename(std::string_view path, std::string_view name, std::string_view ext);

public:
  Graph_library(const Graph_library &s) = delete;
  Graph_library & operator=(const Graph_library&) = delete;

  static bool    exists(std::string_view path, std::string_view name);
  static LGraph *try_find_lgraph(std::string_view path, std::string_view name);
  static LGraph *try_find_lgraph(std::string_view path, Lg_type_id lgid);
  LGraph        *try_find_lgraph(std::string_view name) const;
  LGraph        *try_find_lgraph(Lg_type_id lgid) const;

  bool          exists(Lg_type_id lgid) const {
    if (attributes.size() >= lgid || lgid.is_invalid())
      return false;
    return sub_nodes[lgid].get_lgid() == lgid;
  }

  Sub_node &reset_sub(std::string_view name, std::string_view source);
  Sub_node &setup_sub(std::string_view name, std::string_view source);
  Sub_node &setup_sub(std::string_view name) { return setup_sub(name, "-"); }
  Sub_node &get_sub(Lg_type_id lgid) {
    graph_library_clean = false;
    I(lgid > 0);  // 0 is invalid lgid
    I(attributes.size() > lgid);
    I(attributes.size() == sub_nodes.size());
    I(sub_nodes[lgid].get_lgid() == lgid);
    return sub_nodes[lgid];
  }
  Sub_node      &get_sub(std::string_view name) {
    graph_library_clean = false;
    return get_sub(get_lgid(name));
  }
  const Sub_node      &get_sub(Lg_type_id lgid) const {
    I(lgid > 0);  // 0 is invalid lgid
    I(lgid > 0);  // 0 is invalid lgid
    I(attributes.size() > lgid);
    I(attributes.size() == sub_nodes.size());
    I(sub_nodes[lgid].get_lgid() == lgid);
    return sub_nodes[lgid];
  }
  const Sub_node      &get_sub(std::string_view name) const {
    return get_sub(get_lgid(name));
  }

  Lg_type_id     add_name(std::string_view name, std::string_view source);
  bool           rename_name(std::string_view orig, std::string_view dest);

  std::string_view get_name(Lg_type_id lgid) const {
    I(lgid > 0);  // 0 is invalid lgid
    I(sub_nodes.size() > lgid);
    I(sub_nodes[lgid].get_lgid() == lgid);
    return sub_nodes[lgid].get_name();
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
    assert(attributes.size() > lgid);
    return attributes[lgid].source;
  }

  std::string_view get_source(std::string_view name) const { return get_source(get_lgid(name)); }

  //int lgraph_count() const { return attributes.size() - 1; }

  void update(Lg_type_id lgid);

  Lg_type_id get_version(Lg_type_id lgid) const {
    if (attributes.size() < lgid) return 0;  // Invalid ID

    return attributes[lgid].version;
  }

  bool has_name(std::string_view name) const { return name2id.find(name) != name2id.end(); }

  // TODO: Change to Graph_library &instance...
  static Graph_library *instance(std::string_view path);

  Lg_type_id get_max_version() const {
    assert(max_next_version > 0);
    return max_next_version - 1;
  }

#if 0
  // DEPRECATED
  unsigned int get_lgid_bits() const {
    // FIXME: if the lgid_bits increases (more lgraphs) all the attributes must be recomputed (create callback interface)
    uint32_t sz = attributes.size();
    int val = sizeof(sz)*8 - __builtin_clz(sz+1);
    if (val<8)
      return 8;
    return val;
  }
#endif

  //deprecated bool expunge_lgraph(std::string_view name, LGraph *lg);

  Lg_type_id copy_lgraph(std::string_view name, std::string_view new_name);
  Lg_type_id register_sub(std::string_view name);
  Lg_type_id register_lgraph(std::string_view name, std::string_view source, LGraph *lg);
  void       unregister(std::string_view name, Lg_type_id lgid, LGraph *lg=0); // unregister open instance
  void       expunge(std::string_view name); // Delete completely, even if open instances exists

  void     clear(Lg_type_id lgid);

  void sync() { clean_library(); }

  static void sync_all();  // Called when running out of mmaps

  absl::Span<const Sub_node>    get_sub_nodes() const {
    I(sub_nodes.size()>=1);
    return absl::MakeSpan(sub_nodes).subspan(1);
  };

  absl::Span<const std::string>  get_liberty() const { return absl::MakeSpan(liberty_list); };
  absl::Span<const std::string>  get_sdc()     const { return absl::MakeSpan(sdc_list); };
  absl::Span<const std::string>  get_spef()    const { return absl::MakeSpan(spef_list); };

  absl::Span<const Tech_layer>  get_layer()    const { return absl::MakeSpan(layer_list); };
  absl::Span<const Tech_via>    get_via()      const { return absl::MakeSpan(via_list); };

  void each_lgraph(std::function<void(Lg_type_id lgid, std::string_view name)> f1) const;
  void each_lgraph(std::string_view match, std::function<void(Lg_type_id lgid, std::string_view name)> f1) const;

  void reload();
};

