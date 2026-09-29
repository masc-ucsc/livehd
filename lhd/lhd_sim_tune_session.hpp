//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// `lhd sim`'s side of the profile-guided tuner (sim_profile.md §8-§10): the
// session that sim_command drives. It owns the workdir lock, ingests drv.bin's
// raw run files into the tune store, resolves the vector handed to
// inou.cgen.sim, applies a pending trial with copy-on-write retention of the
// incumbent tree, judges and reverts trials, forwards the run-time `--set`s to
// drv.bin, and writes the envelope's `sim_tune` member. The policy itself is
// the pure model in lhd_sim_tune.{hpp,cpp}.
//
// Kept OUT of the compile-salt sources (lhd/BUILD compile_salt), so iterating
// on the tuner never cold-starts every compile scope.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lhd.hpp"

namespace lhd {

// The value grammar of one `sim.<flag>=value` (check_known_set_passes). Returns
// {message, hint}; an empty message means the value is valid.
std::pair<std::string, std::string> sim_set_value_error(const Sim_set_option& opt, std::string_view value);

// An old sim.* spelling (sim.color_dirty, sim.fence_ratio, sim.live_words,
// sim.backend, sim.vcdfakedelay) -> the replacement `sim.<flag>=<value>` in the
// new grammar, ready for a "use --set X instead" hint.
std::optional<std::pair<std::string, std::string>> renamed_sim_set(std::string_view flag, std::string_view value);

// The inou.cgen.sim labels of the resolved tune vector (color_dirty,
// fence_ratio, live_words, backend). Uses opts.sim_tune when `lhd sim`
// resolved it; otherwise (`lhd compile --emit-dir sim:`) resolves explicit
// --set > sim.tune.file > default itself. Throws Lhd_error on a bad tune file.
// A sim.tune.file is an input of the generated code: it joins res.inputs (so
// the --depfile lists it) either way.
std::vector<std::pair<std::string, std::string>> sim_tune_codegen_labels(const Options& opts, Result& res);

// What sim_command knows about this invocation before the tuner starts.
struct Sim_tune_shape {
  bool                     user_workdir        = false;  // captured at sim_command ENTRY (before a scratch dir is minted)
  bool                     setup_only          = false;
  bool                     run_only            = false;
  bool                     compile_only        = false;
  bool                     observation         = false;  // vcd / probe / break-when / query / restart / vcd-from / list-signals
  bool                     unknown_zero_set    = false;  // the user set sim.unknown_zero (ruling 3)
  bool                     unknown_zero        = false;
  bool                     checkpoint_explicit = false;  // the user asked for checkpoints explicitly
  std::string              simroot;
  std::string              simdir;
  std::vector<std::string> sources;  // the .prp positionals (the compile scope's seeds)
};

class Sim_tune_session {
public:
  // Resolves the vector into opts.sim_tune (always) and, when the tuner is
  // enabled, takes the workdir lock, ingests pending raw runs, judges or
  // proposes trials and prepares a pending trial's retention. Writes the
  // initial res.sim_tune_json so every early return carries it.
  Sim_tune_session(Options& opts, Result& res, Sim_tune_shape shape);
  Sim_tune_session(const Sim_tune_session&)            = delete;
  Sim_tune_session& operator=(const Sim_tune_session&) = delete;
  // Finishes: an attempt this invocation left open (its build failed, or its
  // run never completed) is closed and reverted, the export is written, the
  // envelope member finalized and the lock released.
  ~Sim_tune_session();

  void after_compile();   // compile_sources (incl. inou.cgen.sim) succeeded
  void after_generate();  // the driver was generated: records what the tree now holds
  // Inspect the generated driver (capability markers, testbench digest).
  // `observation_baked`: a --run-only driver carrying VCD/observation code.
  void inspect_driver(bool observation_baked);
  void after_build();  // the host build succeeded

  [[nodiscard]] bool        profiling() const;
  // A --run-only whose tree holds a trial vector the tuner cannot stand behind
  // (a trial closed without a swap back, e.g. a diverged one): res already
  // carries the usage error; the caller returns without building or running.
  [[nodiscard]] bool        refused() const;
  // The drv.bin arguments this run adds (leading space; "" when none).
  [[nodiscard]] std::string driver_args() const;

  void before_run();  // the run itself holds the lock SHARED
  // Re-lock, ingest this run's raw file, then verdict / revert / propose. `rc`
  // is drv.bin's exit code (<0: killed by a signal): a trial run that did not
  // finish normally is a failed attempt, reverted at once.
  void after_run(int rc);

private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace lhd
