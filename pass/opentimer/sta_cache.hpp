// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// INCREMENTAL pass.opentimer (docs/opt_loop_incr.md): a whole-design STA result
// cache, the third persistent reuse tier under `--workdir` + `lhd.incremental`
// (next to the compile cache and pass.abc's `abc_cache/`).
//
// Why it exists: on the big XiangShan/minion blocks `pass.opentimer` is 70-97%
// of a warm `lhd synth`. pass.abc reuses 100% of its colored regions on a
// comment-only edit and lands at ~3 s, and then STA re-times the identical
// netlist from scratch for another 200-400 s -- which is why whole-flow synth
// reuse measured 1.2-1.4x while its dominant pass had no cache at all.
//
// The key is the netlist ITSELF (semdiff::canonical_digest over the timed top,
// Merkle-folded through every region body) plus the timing environment: the
// Liberty/SDC/SPEF/VCD file CONTENT, the analyzed top, `hier`, `margin`, and a
// build-time content hash of pass/opentimer + the @opentimer pin (kStaSrcSalt).
// Keying on the netlist and not on the upstream sources is what makes this
// sound: pass.abc's own intra-run cross-name reuse means a cold and a warm run
// can legitimately produce slightly different netlists, and only the graph that
// is about to be timed decides the timing.
//
// What is stored is the pass's whole observable output for that netlist: the
// rendered per-design JSON block (critical path + worst endpoints), the
// `slowest delay:` summary line's two values, the per-color `--stats` rows, and
// the `native-comb-boundary` warning payload -- so a hit re-emits the identical
// report and the identical diagnostics instead of silently dropping them.
// `resynth` is the ONE field NOT taken from the cache: it describes what THIS
// run's pass.abc did, not the netlist, so the hit path re-stamps it from the
// graph.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace livehd::opentimer {

// One cached analysis. Every field is a pure function of (netlist, environment)
// -- see the resynth note above for the deliberate exception.
struct Sta_record {
  std::string time_unit;        // "ns"/"ps"/"us" ("" = library declared none)
  double      max_delay = 0.0;  // the summary line's delay
  std::string max_pin;          // the summary line's pin ("" = no timed endpoint)
  std::string block;            // design JSON block, WITHOUT the `,"colors":[...]` tail and its closing brace
  // Per-color `--stats` rows, keyed by (module,color) on replay. `resynth` is
  // deliberately absent: the hit path takes it from the current graph.
  struct Color_row {
    std::string module;
    int         color       = 0;
    uint64_t    cells       = 0;
    double      max_arrival = -1.0;
    std::string critical_pin;
    std::string critical_src;
  };
  std::vector<Color_row>   colors;
  // `native-comb-boundary` warning payload (0 = the run emitted no warning).
  uint64_t                 opaque_nodes    = 0;
  uint64_t                 ambiguous_nodes = 0;
  std::vector<std::string> opaque_examples;
  std::vector<std::string> or_examples;
};

class Sta_cache {
public:
  // Loads <dir>/sta_cache.json when present. A salt mismatch drops every record
  // (a new timing engine must re-time), leaving an empty cache to repopulate.
  Sta_cache(std::string dir, uint64_t salt);

  [[nodiscard]] const Sta_record* lookup(const std::string& key) const;
  void                            insert(const std::string& key, Sta_record rec);

  // Persist (atomic tmp+rename). No-op when nothing was inserted.
  void save();

  [[nodiscard]] const std::string& dir() const { return dir_; }

  // The environment half of the key: file CONTENT (not path/mtime) of every
  // timing file, plus the option values that change the analysis.
  [[nodiscard]] static uint64_t env_hash(const std::vector<std::string>& files, std::string_view top, std::string_view hier,
                                         int margin, bool stats);

private:
  std::string                                  dir_;
  uint64_t                                     salt_  = 0;
  bool                                         dirty_ = false;
  // Insertion order, for the size cap: an option-sweep loop over one design
  // mints a record per distinct netlist and nothing else would collect them.
  std::vector<std::string>                     order_;
  absl::flat_hash_map<std::string, Sta_record> recs_;
};

}  // namespace livehd::opentimer
