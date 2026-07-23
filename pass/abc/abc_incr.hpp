// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// INCREMENTAL pass.abc (todo/livehd/2opt-incr.html subtasks A + C) -- lgraph
// compare edition.
//
// The decision-oracle loop is: edit a little RTL, resynthesize, compare QoR.
// A full pass.abc pays ABC for every region on every iteration, but a small
// edit changes a handful of regions -- the rest map to identical netlists.
//
// This engine keeps, per region (keyed by its module name <top>__c<color>), a
// persistent cache of two bodies: the region's PRE-ABC logic and its mapped
// netlist. On the next run it rebuilds the fresh pre-ABC region and asks
// semdiff::structural_identical whether it matches the cached one (name-blind on
// internal temporaries, name-anchored on the IO ports the partitioner now names
// content-stably, and on state cells) AND whether the resolved ABC recipe is
// byte-identical. If both hold, the cached mapped body REPLACES the fresh region
// module in place (hhds copy_body_from -- no clone, no port stitch) and ABC
// never runs.
//
// The cache is a SPEEDUP, never an oracle of record: a miss only costs an ABC
// run. Soundness rests on (a) the partitioner's content-stable port names +
// its reuse_eligible refusal of automorphic boundaries, and (b) the structural
// compare -- a real node-set bijection with all compare-point obligations
// discharged, not a hash whose collision would be a miscompile.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "pass_partition.hpp"  // livehd::partition::Region_body

namespace livehd::abc {

struct Region_qor;  // abc_map.hpp

// The persistent cache: a directory holding
//   * abc_cache.json -- {schema, salt, regions: {module_name: {module, pre,
//     recipe, in[], out[], qor...}}}
//   * an hhds GraphLibrary with, per cached region, the MAPPED body (name ==
//     module_name) and its PRE-ABC body (name "p_"+module_name).
class Incr_cache {
public:
  struct Row {
    std::string              module;   // cache-lib name of the mapped body (== module_name)
    std::string              pre;      // cache-lib name of the pre-abc body ("p_"+module_name)
    std::string              recipe;   // verbatim resolved ABC recipe (the recipe gate)
    std::vector<std::string> in, out;  // cached module port names (existence-checked on reuse)
    int                      gates = 0;
    double                   area  = 0.0;
    float                    delay = -1.0f;
    std::string              crit_output;  // region output port with the worst arrival (a name)
    std::string              crit_src;
    int                      div_blackbox = 0;
  };

  struct Compare_result {
    bool        hit = false;
    const Row*  row = nullptr;
    std::string crit_output;
  };

  Incr_cache(std::string dir, uint64_t salt);

  // Miss (default result) unless ALL hold: rb.reuse_eligible, a row keyed by
  // rb.module_name exists, its recipe matches VERBATIM, the cached pre-abc body
  // is structurally identical to `pre_body` (semdiff::structural_identical,
  // matching_names), and every cached port name still exists on the fresh region.
  [[nodiscard]] Compare_result lookup_compare(const livehd::partition::Region_body& rb, hhds::Graph* pre_body,
                                              std::string_view recipe);

  // Snapshot a freshly mapped region: copy rb.body (mapped, in `outlib`, under
  // module_name) and `pre_body` (in `pre_lib`, under `pre_name`) into the cache
  // library, and add the metadata row. Best-effort; returns false on failure.
  bool store(const livehd::partition::Region_body& rb, hhds::GraphLibrary& pre_lib, std::string_view pre_name,
             const Region_qor& q, std::string_view recipe, hhds::GraphLibrary* outlib);

  // Diagnostic (ABC_INCR_COMPARE_ONLY): snapshot ONLY the pre-abc body + a row
  // (no mapped body, no ABC), so a second compare-only run can exercise the
  // rebuild/copy/save/load/compare path without paying for ABC.
  bool store_pre(const livehd::partition::Region_body& rb, hhds::GraphLibrary& pre_lib, std::string_view pre_name,
                 std::string_view recipe);

  // Fill rb.body (the freshly-partitioned region shell in `outlib`) IN PLACE from
  // the cached mapped body -- no clone, no port stitch, name-hash gid preserved.
  // Returns false if the cached body is missing.
  [[nodiscard]] bool reuse_hit(const livehd::partition::Region_body& rb, const Compare_result& res,
                               hhds::GraphLibrary* outlib);

  // Persist abc_cache.json (atomic tmp+rename) and the cache library. No-op when
  // nothing was stored.
  void save();

  [[nodiscard]] int  hits() const { return hits_; }
  [[nodiscard]] int  misses() const { return misses_; }
  void               note_miss() { ++misses_; }

  [[nodiscard]] const std::string& dir() const { return dir_; }

  // Salt for the whole cache: the global inputs the per-region compare does not
  // see. Library CONTENT, sequential-mapping mode, DFF cell, assume feeding,
  // plus a schema tag bumped when the mapper's read-back or the cache shape
  // changes.
  [[nodiscard]] static uint64_t make_salt(std::string_view library_path, bool map_register, bool map_memory,
                                          std::string_view dff_cell, bool use_proven_assume, bool use_all_assume);

private:
  std::string dir_;
  std::string pre_dir_;  // dir_ + "_pre": the pre-body library (see cached_pre_lib)
  uint64_t    salt_  = 0;
  bool        dirty_ = false;
  int         hits_ = 0, misses_ = 0;

  absl::flat_hash_map<std::string, Row> rows_;  // module_name -> row

  // The cache is TWO libraries, deliberately kept in separate namespaces:
  //   lib()            -- the MAPPED region bodies (reuse_hit fills from here).
  //   cached_pre_lib() -- the PRE-ABC bodies + their body-less Sub child decls
  //                       (the structural compare reads from here).
  // They must not share one library: a mapped child body and a parent pre-body's
  // Sub child decl have the SAME module name but DIFFERENT port names (mapped =
  // partition-boundary names, pre = original def names), so merging them makes the
  // cached pre-body's Subs resolve to the wrong IO and the compare mismatches the
  // fresh side. Separate libs => both compare sides resolve the body-less decls.
  [[nodiscard]] hhds::GraphLibrary& lib();
  [[nodiscard]] hhds::GraphLibrary& cached_pre_lib();

  // Copy the pre-body's body-less Sub child decls into cached_pre_lib() next to the
  // pre-body, so the cached copy is self-contained (its Subs resolve
  // get_subnode_io()). Without it the structural compare's IO signature is
  // asymmetric cached-vs-fresh -> spurious cut_violated. `src_pre_lib` is the
  // partitioner's throwaway lib holding the fresh pre-body. No-op if rb.pre_body null.
  void copy_pre_children(const livehd::partition::Region_body& rb, hhds::GraphLibrary& src_pre_lib);
};

}  // namespace livehd::abc
