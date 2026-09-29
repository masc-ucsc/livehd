// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The satopt coordinator (todo/livehd/2s-satopt K): one engine, independently
// selectable stages, one shared budget and a per-stage report. The stages run
// in a fixed order whatever order a caller lists them in; the set only selects
// which searches run. Correctness rules are never options: every enabled
// rewrite keeps the observable checks and semantics of the profile (B).
#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::satopt {

struct Mux_prover;  // satopt_mux.hpp

enum class Stage : uint8_t {
  constants,   // selectors (and later values) proven constant
  equiv,       // values proven equal to an existing value
  complement,  // values proven the complement of an existing value
  odc,         // shallow observability don't-cares (contextual)
  hotmux,      // mux-arm facts and Hotmux collapse
  memory,      // memory-port simplification
  resub,       // bounded simulation-guided resubstitution (experimental)
  simp_ctrl,   // small-support Boolean resynthesis of one-bit controls
};
inline constexpr size_t                         kStageCount = 8;
inline constexpr std::array<Stage, kStageCount> kStageOrder{
    Stage::constants, Stage::equiv, Stage::complement, Stage::odc, Stage::hotmux, Stage::memory, Stage::resub, Stage::simp_ctrl};
[[nodiscard]] std::string_view stage_name(Stage s);

class Stage_set {
public:
  constexpr Stage_set() = default;
  constexpr Stage_set(std::initializer_list<Stage> stages) {
    for (auto s : stages) {
      add(s);
    }
  }
  [[nodiscard]] constexpr bool     has(Stage s) const { return (bits_ >> static_cast<uint32_t>(s)) & 1U; }
  constexpr void                   add(Stage s) { bits_ |= 1U << static_cast<uint32_t>(s); }
  constexpr void                   remove(Stage s) { bits_ &= ~(1U << static_cast<uint32_t>(s)); }
  [[nodiscard]] constexpr bool     empty() const { return bits_ == 0; }
  [[nodiscard]] constexpr uint32_t raw() const { return bits_; }
  [[nodiscard]] constexpr Stage_set intersect(Stage_set other) const {
    Stage_set r;
    r.bits_ = bits_ & other.bits_;
    return r;
  }
  // "none", or the stage names in execution order joined by ','.
  [[nodiscard]] std::string text() const;
  constexpr bool            operator==(const Stage_set&) const = default;

private:
  uint32_t bits_ = 0;
};

// What a rewrite may assume about observability.
enum class Profile : uint8_t {
  // The shared optimizer (compile, standalone, sim, LEC): every rewrite keeps
  // every graph output, state update, memory behavior, opaque interface and
  // observable check (Hotmux exclusivity obligations, assertions), even when
  // their data has no consumer.
  shared,
  // pass.abc's private synthesis copy: obligations are ignored (they never
  // reach a netlist), refinements of don't-care memory values are allowed,
  // and a mux-arm fact inside one color is left to the mapper.
  synthesis,
};

// Every stage.
[[nodiscard]] Stage_set all_stages();
// The stages a profile runs when the caller names none.
[[nodiscard]] Stage_set default_stages(Profile p);
// "none" (nothing), "default" (the profile's set), "all" (every stage), or a
// comma-separated list of stage names. An unknown or duplicate name is an
// error (nullopt, `error` says which).
[[nodiscard]] std::optional<Stage_set> parse_stages(std::string_view text, Profile profile, std::string* error = nullptr);

// Deterministic effort limits (H). Work units count simulation evaluations,
// graph-walk steps and solver-cone pins; a stage past its share, or the run
// past the total, stops searching, keeps every rewrite it already proved and
// reports itself `exhausted`.
struct Budget {
  uint64_t work     = 200'000'000;  // the whole run
  uint64_t queries  = 200'000;      // solver queries, the whole run
  int      budget_k = 256;          // per-query solver resource factor (formal::Prove_options)
  int      cone_max = 50'000;       // per-query cone size (formal::Prove_options)
  uint32_t samples  = 64;           // simulation patterns per value (C)
  uint64_t time_ms  = 0;            // wall-clock backstop for the run, 0 = none (not deterministic)

  // What a proof's verdict depends on besides the graph and the code: the
  // part of a cache key the options contribute. The totals are not in it: a
  // row records whether its search completed instead.
  [[nodiscard]] std::string proof_key() const;
  // No limit on work, queries or time (the per-query solver limits remain).
  [[nodiscard]] static Budget unlimited();
};

// The budget knobs by name (`--set pass.satopt.<name>`), all decimal.
inline constexpr std::array<std::string_view, 6> kBudgetKeys{"work", "queries", "time_ms", "budget_k", "cone_max", "samples"};
// Sets one knob; false (with `error`) for an unknown name or a bad value.
bool set_budget(Budget& budget, std::string_view key, std::string_view value, std::string* error = nullptr);
// "name=value,..." (how lhd hands the knobs to pass.abc); empty = defaults.
[[nodiscard]] std::optional<Budget> parse_budget(std::string_view text, std::string* error = nullptr);

// Effort accounting for one run. Every stage charges the work it does and
// each solver query; a charge past the stage's share or the run's total fails
// and marks the run exhausted, and the stage then stops searching and keeps
// what it already proved. A stage may spend at most an equal share of what is
// left among itself and the applicable stages still waiting (what it leaves
// unspent flows to them); the last one gets all of it.
class Meter {
public:
  Meter() : Meter(Budget::unlimited()) {}
  explicit Meter(const Budget& budget);

  // Opens the next stage's share, left / (waiting + 1); `waiting` =
  // applicable stages after it.
  void begin_stage(int waiting);
  // Charge `units` of work done or one solver query about to be asked; false
  // once over a limit. Work always counts; a refused query is not asked and
  // does not count. An exhausted stage refuses every later charge.
  bool                          work(uint64_t units);
  bool                          query();
  // A cached search reused: charges what it cost when it ran, when the stage
  // can still afford all of it (false: nothing charged, search again). Warm
  // and cold runs then leave the same budget to what follows.
  bool                          replay(uint64_t work, uint64_t queries);
  [[nodiscard]] uint64_t        work_done() const { return work_; }
  [[nodiscard]] uint64_t        queries_done() const { return queries_; }
  [[nodiscard]] bool            exhausted() const { return exhausted_; }
  [[nodiscard]] uint64_t        stage_work() const { return stage_work_; }
  [[nodiscard]] uint64_t        stage_queries() const { return stage_queries_; }
  [[nodiscard]] const Budget&   budget() const { return budget_; }

private:
  bool     over(bool clock);
  Budget   budget_;
  uint64_t work_ = 0, queries_ = 0;  // the run so far
  uint64_t stage_work_ = 0, stage_queries_ = 0, stage_work_cap_ = 0, stage_query_cap_ = 0;
  uint64_t charges_   = 0;
  bool     exhausted_ = false;  // this stage (a stage past its share leaves the next one its own)
  bool     timed_out_ = false;  // the run
  int64_t  start_ns_  = 0;
};

struct Options {
  Stage_set         stages  = default_stages(Profile::shared);
  Profile           profile = Profile::shared;
  Budget            budget;
  std::string       cache_dir;             // persistent proof cache; empty = none
  const Mux_prover* mux_prover  = nullptr;  // hotmux arm facts; null = registered_mux_prover()
  bool              all_regions = true;     // hotmux arm facts on every mux (false: only across colors)
  // Stages a later run() on the same Meter still charges: they count as
  // waiting, so this run's stages leave them their share.
  int               later_stages = 0;
};

enum class Stage_state : uint8_t { disabled, inapplicable, exhausted, completed };
[[nodiscard]] std::string_view state_name(Stage_state s);

struct Stage_report {
  Stage_state state = Stage_state::disabled;
  double      ms    = 0;
  uint64_t    work  = 0;
  uint64_t    candidates = 0, sim_rejects = 0, queries = 0, proven = 0, refuted = 0, unknown = 0, reused = 0;
  uint64_t    applied = 0, bits = 0, nodes_removed = 0, budget_skips = 0;
};

struct Report {
  std::array<Stage_report, kStageCount> stages{};
  uint64_t                              graphs = 0, changed_graphs = 0;
  double                                ms     = 0;
  [[nodiscard]] Stage_report&                at(Stage s) { return stages.at(static_cast<size_t>(s)); }
  [[nodiscard]] const Stage_report&          at(Stage s) const { return stages.at(static_cast<size_t>(s)); }
  [[nodiscard]] std::string             json() const;
  // The inverse of json(); nullopt for anything json() cannot have written.
  [[nodiscard]] static std::optional<Report> parse(std::string_view text);
  // Adds `other` (a later run over the same design): counters and times add
  // up, `graphs` counts graph visits, and a stage keeps its most telling
  // state (exhausted, then completed, then inapplicable, then disabled).
  void                                  merge(const Report& other);
};

// Optimizes `graphs` in place: every enabled stage in kStageOrder over every
// graph. Each graph is optimized as a definition: its inputs are free, so a
// fact never depends on one instance's context. The budget is opts.budget, or
// `meter` when a caller spreads one budget over several runs (single thread).
Report run(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, const Options& opts, Meter* meter = nullptr);
Report run(hhds::Graph* graph, const Options& opts, Meter* meter = nullptr);

}  // namespace livehd::satopt
