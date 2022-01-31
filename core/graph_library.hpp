//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"
#include "lgraphbase.hpp"
#include "sub_node.hpp"
#include "tech_library.hpp"

class Lgraph;

// FIXME: lgid keep increasing. We may want a garbage collector once the Lgraph is shutdown to remap
// or at least find holes in lgids no longer used

class Graph_library {
protected:
  inline static std::mutex lgs_mutex;

  struct Graph_attributes {
    Lgraph *    lg;
    mmap_lib::str source;  // File were this module came from. If file updated (all the associated Lgraphs must be deleted). If empty,
                         // it ies not present (blackbox)
    Lg_type_id version;  // In which sequence order were the graphs last modified
    Graph_attributes() { expunge(); }
    void expunge() {
      lg      = 0;
      version = 0;
      source  = "-";
    }
  };

  // BEGIN: common attributes or properties shared across all graphs in this library
  std::vector<std::string> liberty_list;
  std::vector<std::string> sdc_list;
  std::vector<std::string> spef_list;

  std::vector<Tech_layer> layer_list;  // only for routing
  std::vector<Tech_via>   via_list;    // only for routing
  // END: common attributes

  using Global_instances   = absl::flat_hash_map<mmap_lib::str, Graph_library *>;
  using Global_name2lgraph = absl::flat_hash_map<mmap_lib::str, absl::flat_hash_map<mmap_lib::str, Lgraph *>>;
  using Name2id            = absl::flat_hash_map<mmap_lib::str, Lg_type_id::type>;
  using Recycled_id        = absl::flat_hash_set<uint64_t>;

  const mmap_lib::str path;
  const std::string library_file;

  // Begin protected for MT
  Name2id                       name2id;      // WR protect on add entries, RD protect any access
  Recycled_id                   recycled_id;  // WR protect on add entries, RD protect any access
  std::vector<Graph_attributes> attributes;   // WR protect on add entries, RD protect any access
  std::vector<Sub_node *>       sub_nodes;    // WR protect on add entries, RD protect any access

  static Global_instances   global_instances;    // WR protect on add entries, RD protect any access
  static Global_name2lgraph global_name2lgraph;  // WR protect on add entries, RD protect any access
  // End protect for MT

  std::atomic<uint32_t> max_next_version;     // Atomic, no need to lock for this
  bool                  graph_library_clean;  // No need to worry, atomic, no need to protect

  Graph_library() { max_next_version = 1; }

  explicit Graph_library(const mmap_lib::str & _path);

  void clean_library_int();

  ~Graph_library() {}

  Lg_type_id reset_id_int(const mmap_lib::str &name, const mmap_lib::str &source);

  Lg_type_id try_get_recycled_id_int();
  void       recycle_id_int(Lg_type_id lgid);

  static bool exists_int(const mmap_lib::str & path, const mmap_lib::str &name);
  bool        exists_int(Lg_type_id lgid) const;

  static Lgraph *try_find_lgraph_int(const mmap_lib::str & path, const mmap_lib::str &name);
  static Lgraph *try_find_lgraph_int(const mmap_lib::str & path, Lg_type_id lgid);
  Lgraph *       try_find_lgraph_int(const mmap_lib::str &name) const;
  Lgraph *       try_find_lgraph_int(const Lg_type_id lgid) const;

  Sub_node &      reset_sub_int(const mmap_lib::str &name, const mmap_lib::str &source);
  Sub_node &      ref_or_create_sub_int(const mmap_lib::str &name, const mmap_lib::str &source);
  Sub_node &      ref_or_create_sub_int(const mmap_lib::str &name);
  Sub_node *      ref_sub_int(Lg_type_id lgid);
  const Sub_node &get_sub_int(Lg_type_id lgid) const;

  Lg_type_id add_name_int(const mmap_lib::str &name, const mmap_lib::str &source);
  bool       rename_name_int(const mmap_lib::str &orig, const mmap_lib::str &dest);

  mmap_lib::str get_name_int(Lg_type_id lgid) const {
    I(lgid > 0);  // 0 is invalid lgid
    I(sub_nodes.size() > lgid);
    // fmt::print("DEBUG0 sub_nodes[1]->get_lgid:{}, name:{}\n", sub_nodes[1]->get_lgid(), sub_nodes[1]->get_name());
    // fmt::print("DEBUG0 sub_nodes[2]->get_lgid:{}, name:{}\n", sub_nodes[2]->get_lgid(), sub_nodes[2]->get_name());
    I(sub_nodes[lgid]->get_lgid() == lgid);
    return sub_nodes[lgid]->get_name();
  }

  Lg_type_id get_lgid_int(const mmap_lib::str &name) const {
    const auto &it = name2id.find(name);
    if (it != name2id.end()) {
      return it->second;
    }
    return 0;  // Invalid ID
  }

  mmap_lib::str get_source_int(Lg_type_id lgid) const {
    assert(lgid > 0);  // 0 is invalid lgid
    assert(attributes.size() > lgid);
    return attributes[lgid].source;
  }

  void       update_int(Lg_type_id lgid);
  Lg_type_id get_version_int(Lg_type_id lgid) const {
    if (attributes.size() < lgid)
      return 0;  // Invalid ID

    return attributes[lgid].version;
  }

  bool has_name_int(const mmap_lib::str &name) const { return name2id.find(name) != name2id.end(); }

  static Graph_library *instance_int(const mmap_lib::str & path);

  Lg_type_id  copy_lgraph_int(const mmap_lib::str &name, const mmap_lib::str &new_name);
  Lg_type_id  register_lgraph_int(const mmap_lib::str &name, const mmap_lib::str &source, Lgraph *lg);
  void        unregister_int(const mmap_lib::str &name, Lg_type_id lgid, Lgraph *lg = nullptr);
  void        expunge_int(const mmap_lib::str &name);
  void        clear_int(Lg_type_id lgid);
  void        sync_int() { clean_library_int(); }
  static void sync_all_int();
  static void shutdown_int();
  void        reload_int();

public:
  Graph_library(const Graph_library &s) = delete;
  Graph_library &operator=(const Graph_library &) = delete;

  static bool exists(const mmap_lib::str & path, const mmap_lib::str &name) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return exists_int(path, name);
  }

  bool exists(Lg_type_id lgid) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return exists_int(lgid);
  }

  static Lgraph *try_find_lgraph(const mmap_lib::str & path, const mmap_lib::str &name) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return try_find_lgraph_int(path, name);
  }

  static Lgraph *try_find_lgraph(const mmap_lib::str & path, Lg_type_id lgid) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return try_find_lgraph_int(path, lgid);
  }

  Lgraph *try_find_lgraph(const mmap_lib::str &name) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return try_find_lgraph_int(name);
  }

  Lgraph *try_find_lgraph(const Lg_type_id lgid) const {
    // WARNING: This is a frequent case to build netlists (designed to not require lock)
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    return try_find_lgraph_int(lgid);
  }

  Lg_type_id get_max_version() const { return max_next_version - 1; }

  Sub_node &reset_sub(const mmap_lib::str &name, const mmap_lib::str &source) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return reset_sub_int(name, source);
  }

  Sub_node &ref_or_create_sub(const mmap_lib::str &name, const mmap_lib::str &source) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return ref_or_create_sub_int(name, source);
  }

  Sub_node &ref_or_create_sub(const mmap_lib::str &name) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return ref_or_create_sub_int(name);
  }

  Sub_node *ref_sub(Lg_type_id lgid) {
    // WARNING: This is a frequent case to build netlists (designed to not require lock)
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    return ref_sub_int(lgid);
  }

  const Sub_node &get_sub(Lg_type_id lgid) const {
    // WARNING: This is a frequent case to build netlists (designed to not require lock)
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_sub_int(lgid);
  }

  Sub_node *ref_sub(const mmap_lib::str &name) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return ref_sub_int(get_lgid_int(name));
  }

  const Sub_node &get_sub(const mmap_lib::str &name) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_sub_int(get_lgid_int(name));
  }

  Lg_type_id add_name(const mmap_lib::str &name, const mmap_lib::str &source) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return add_name_int(name, source);
  }

  bool rename_name(const mmap_lib::str &orig, const mmap_lib::str &dest) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return rename_name_int(orig, dest);
  }

  mmap_lib::str get_name(Lg_type_id lgid) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_name_int(lgid);
  }

  Lg_type_id get_lgid(const mmap_lib::str &name) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_lgid_int(name);
  }

  mmap_lib::str get_source(Lg_type_id lgid) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_source_int(lgid);
  }

  mmap_lib::str get_source(const mmap_lib::str &name) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_source_int(get_lgid_int(name));
  }

  void update(Lg_type_id lgid) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    update_int(lgid);
  }

  Lg_type_id get_version(Lg_type_id lgid) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return get_version_int(lgid);
  }

  bool has_name(const mmap_lib::str &name) const {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return has_name_int(name);
  }

  // TODO: Change to Graph_library &instance...
  static Graph_library *instance(const mmap_lib::str & path) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return instance_int(path);
  }

  Lg_type_id copy_lgraph(const mmap_lib::str &name, const mmap_lib::str &new_name) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    return copy_lgraph_int(name, new_name);
  }

  Lgraph *setup_lgraph(const mmap_lib::str &name, const mmap_lib::str &source);

  void unregister(const mmap_lib::str &name, Lg_type_id lgid, Lgraph *lg = 0) {  // unregister open instance
    std::lock_guard<std::mutex> guard(lgs_mutex);
    unregister_int(name, lgid, lg);
  }

  void expunge(const mmap_lib::str &name) {  // Delete completely, even if open instances exists
    std::lock_guard<std::mutex> guard(lgs_mutex);
    expunge_int(name);
  }

  void clear(Lg_type_id lgid) {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    clear_int(lgid);
  }

  void sync() {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    sync_int();
  }

  static void sync_all() {  // Called when running out of mmaps
    std::lock_guard<std::mutex> guard(lgs_mutex);
    sync_all_int();
  }

  static void shutdown() {  // Called on program exit to clean pointers (asan)
    // std::lock_guard<std::mutex> guard(lgs_mutex);
    shutdown_int();
  }

  absl::Span<const std::string> get_liberty() const { return absl::MakeSpan(liberty_list); };
  absl::Span<const std::string> get_sdc() const { return absl::MakeSpan(sdc_list); };
  absl::Span<const std::string> get_spef() const { return absl::MakeSpan(spef_list); };

  absl::Span<const Tech_layer> get_layer() const { return absl::MakeSpan(layer_list); };
  absl::Span<const Tech_via>   get_via() const { return absl::MakeSpan(via_list); };

  void each_lgraph(std::function<void(Lg_type_id lgid, const mmap_lib::str &name)> f1) const;
  void each_lgraph(const mmap_lib::str &match, std::function<void(Lg_type_id lgid, const mmap_lib::str &name)> f1) const;

  void reload() {
    std::lock_guard<std::mutex> guard(lgs_mutex);
    reload_int();
  }
};
