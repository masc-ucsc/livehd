// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "sim_tune_vector.hpp"

namespace livehd::sim {

// Read-only occurrence-wide discovery for the simulator color schedule.
//
// This is deliberately a separate object from Cgen_sim.  It is built only
// after the simulator-private graph library has completed every structural
// rewrite, and its occurrence handles must never survive another rewrite.
// Fine-grain state-version sites ride on top of discovery. Coarsening and the
// final control/derived-clock cases remain explicit report status instead of
// pretending a base node has one phase when it may execute at several versions.
class Color_plan {
public:
  static constexpr size_t invalid_index = static_cast<size_t>(-1);

  enum class Site_kind : uint8_t {
    data,
    state,
    instance,
    conditional_control,
    loop_control,
  };

  enum class Dependency_kind : uint8_t {
    data,
    state_read,
    state_update,
    control,
    loop_carry,
  };

  enum class State_version : uint8_t {
    pre_rise,
    post_rise,
    post_fall,
  };

  enum class Execution_slot : uint8_t {
    pre_rise_eval,
    rise_commit,
    post_rise_eval,
    fall_commit,
    post_fall_publish,
  };

  enum class Version_role : uint8_t {
    data,
    state_read,
    state_update,
  };

  enum class Boundary_kind : uint8_t {
    color_value,
    top_input,
    top_output,
    observation_input,
    observation_output,
    state_current,
    state_pending,
  };

  struct Site {
    std::string           structural_id;
    std::string           schedule_id;  // topology-only ordering; excludes operation and literal contents
    std::string           storage_id;   // occurrence-unique ABI identity; excluded from kernel reuse
    Site_kind             kind = Site_kind::data;
    hhds::Occurrence_node node;
    uint64_t              gate_equivalents = 0;
    bool                  live             = false;
  };

  struct Dependency {
    size_t          producer      = 0;
    size_t          consumer      = 0;
    hhds::Port_id   producer_port = 0;
    hhds::Port_id   consumer_port = 0;
    Dependency_kind kind          = Dependency_kind::data;
    bool            cut           = false;
  };

  struct Observation {
    std::string name;
    std::string structural_id;
    bool        input = false;
    uint32_t    port  = 0;
  };

  struct Version_site {
    std::string    structural_id;
    size_t         base_site       = 0;
    // Outermost structural conditional region containing this occurrence.
    // Coarsening may never cross this boundary: doing so would erase the
    // activation contract and force an otherwise idle region to execute.
    size_t         control_owner   = invalid_index;
    hhds::Port_id  output_port     = 0;  // data versions may split a multi-output node by protocol port
    State_version  version         = State_version::pre_rise;
    Execution_slot slot            = Execution_slot::pre_rise_eval;
    Version_role   role            = Version_role::data;
    bool           latch_settle    = false;  // level-sensitive update at an evaluation barrier
    bool           latch_input     = false;  // guarded value version; distinct from the settled observer
    uint64_t       execution_order = 0;
  };
  // The edge a state update COMMITS on. A rise-only design evaluates its flop
  // captures in pre-rise-eval (fused with their input cones), but they still
  // commit at the rise barrier.
  [[nodiscard]] static Execution_slot commit_slot_of(const Version_site& v) noexcept {
    return v.role == Version_role::state_update && !v.latch_settle && v.slot == Execution_slot::pre_rise_eval
               ? Execution_slot::rise_commit
               : v.slot;
  }

  struct Version_dependency {
    size_t   producer      = 0;
    size_t   consumer      = 0;
    uint64_t boundary_bits = 0;
  };

  // Exact value-flow record retained alongside the precedence-only version
  // DAG. A single version dependency may cover several of these uses, while
  // state-transition precedence has no Value_use at all.
  struct Value_use {
    size_t        producer_version    = invalid_index;
    size_t        consumer_version    = invalid_index;
    State_version version             = State_version::pre_rise;
    hhds::Port_id producer_port       = 0;
    uint32_t      producer_shift      = 0;  // position an LSB-aligned slice in the consumer's whole word
    uint32_t      producer_extract_lo = 0;  // non-empty [lo,hi): store only this fixed source lane
    uint32_t      producer_extract_hi = 0;
    hhds::Port_id consumer_port       = 0;
    uint32_t      consumer_input      = 0;  // exact inp_edges() position; ports may be variadic
    uint32_t      width               = 1;  // physical producer/storage width
    uint32_t      consumer_width      = 1;  // width after an erased GraphIO boundary cast
    bool          unsign              = false;
    bool          top_input           = false;
    bool          preextracted        = false;  // consumer Get_mask is the identity on this lane
    std::string   literal;                      // fixed packed lane selected at an occurrence boundary
  };

  struct Boundary_consumer {
    size_t        version_site = invalid_index;
    size_t        color        = invalid_index;
    hhds::Port_id port         = 0;
    uint32_t      input        = 0;  // exact inp_edges() position within the consumer
    uint32_t      width        = 1;  // consumer-visible width after boundary casting
    bool          preextracted = false;
  };

  // Direct color ABI. Each slot has exactly one writer. For color values the
  // producer color owns storage; current and pending state slots are owned by
  // the state occurrence (one pending next-value slot, not one per operand);
  // top I/O slots are public root storage. Consumers bind directly to a slot --
  // they never call another module's settle/eval routine.
  struct Boundary_slot {
    std::string                    structural_id;
    Boundary_kind                  kind                = Boundary_kind::color_value;
    State_version                  version             = State_version::pre_rise;
    size_t                         owner_site          = invalid_index;
    size_t                         producer_version    = invalid_index;
    size_t                         producer_color      = invalid_index;
    hhds::Port_id                  producer_port       = 0;
    uint32_t                       producer_shift      = 0;
    uint32_t                       producer_extract_lo = 0;
    uint32_t                       producer_extract_hi = 0;
    hhds::Port_id                  public_port         = 0;
    uint32_t                       width               = 1;
    bool                           unsign              = false;
    std::string                    literal;  // non-empty for a constant source (observation or packed lane)
    std::vector<Boundary_consumer> consumers;
  };

  struct Color {
    std::string         structural_id;
    std::string         storage_id;  // terminal topology; activation allocation, never a kernel cache key
    Execution_slot      slot = Execution_slot::pre_rise_eval;
    std::vector<size_t> members;
    uint64_t            gate_equivalents = 0;
    uint64_t            execution_order  = 0;
    uint64_t            peak_live_words  = 0;
    uint64_t            live_in_words    = 0;
  };

  struct Color_dependency {
    size_t   producer      = 0;
    size_t   consumer      = 0;
    uint64_t boundary_bits = 0;
  };

  struct Kernel_class {
    std::string         signature;
    size_t              representative = invalid_index;
    std::vector<size_t> colors;
  };

  struct Summary {
    uint64_t grouped_sites             = 0;
    uint64_t outer_sites               = 0;
    uint64_t live_sites                = 0;
    uint64_t physical_occurrence_sites = 0;
    uint64_t compact_loops             = 0;
    uint64_t conditional_regions       = 0;
    uint64_t carry_edges_cut           = 0;
    // Version edges dropped because producer == consumer. A precedence edge from
    // a site to ITSELF is UNSATISFIABLE, not merely vacuous: it is a genuine
    // self-dependency that reached version identity. Dropping it keeps the Kahn
    // stall diagnostic readable; the plan is still failed (versioning_complete =
    // false, one witness in errors_) so the module is refused rather than
    // silently scheduled against a stale value.
    // MEASURED: no design has produced one (XiangShan `Backend` is 0).
    uint64_t self_edges_dropped        = 0;
    uint64_t version_sites             = 0;
    uint64_t version_edges             = 0;
    uint64_t fine_colors               = 0;
    uint64_t colors                    = 0;
    uint64_t color_merges              = 0;
    uint64_t value_uses                = 0;
    uint64_t boundary_slots            = 0;
    uint64_t boundary_bits             = 0;
    uint64_t kernel_classes            = 0;
    uint64_t kernel_reuses             = 0;
    bool     boundary_one_writer       = true;
    bool     boundary_dominance        = true;
    bool     runtime_random            = false;
    bool     complete                  = true;
    bool     versioning_complete       = true;
    bool     version_dag_acyclic       = true;
    bool     color_dag_acyclic         = true;
  };

  // ---- sim.tune support tables (sim_profile.md §6.1 / §8.1) ----
  //
  // Both are computed by discover() BEFORE the module-fence decision, from the
  // versions, value uses and version DAG alone. None of those depends on
  // sim.tune.dirty / fence / live_words / backend (those only coarsen), so both
  // tables are a pure function of the design: the color root's
  // `<stem>.tune.cpp` is byte-identical across tune vectors.

  // One executable body occurrence -- an instance path from the root. The root
  // (path "") is always present; the others are the occurrences that own at
  // least one executable site. A compact loop and an opaque call belong to the
  // body that CALLS them. An anonymous wrapper is transparent in the formatted
  // path, so two occurrences can share a path; they still differ in `def` or
  // in their position.
  struct Occurrence {
    std::string path;  // hhds::format_occurrence_path spelling, "" = the root
    std::string def;   // LGraph definition name
    // GE of the own executable sites (live, with at least one version). A
    // compact loop weighs its native body's GE x its lane count, not the 1 GE
    // of its Sub node: the rolled work is otherwise invisible.
    uint64_t    ge          = 0;
    uint64_t    cost        = 0;  // simulation word cost of the own sites (Support_class::cost)
    uint64_t    cost_flat   = 0;  // the same without the loop/opaque body multiplier
    uint64_t    sites       = 0;  // own executable sites
    uint64_t    state_sites = 0;  // of which flops, latches and memories
    uint64_t    sources     = 0;  // support sources this occurrence owns
  };

  // The single-period fan-in frontier ("support") of every executable site
  // over the plan's own value-flow graph: which stored values (state at the
  // start of the period, root inputs) a site's computation in one period can
  // read. A site whose support did not change between two sampled periods
  // computes the same value again -- idle work a dirty-gated schedule skips.
  struct Support_source {
    enum class Kind : uint8_t {
      state,      // a flop, latch or memory (its stored value)
      opaque,     // a compact loop or an opaque call: the instance's whole walked state
      top_input,  // a root input port
    };
    Kind     kind       = Kind::state;
    size_t   site       = invalid_index;  // state / opaque: the Site index
    uint32_t port       = 0;              // top_input: the root input port id
    uint32_t bucket     = 0;              // activity-mask bit in [0, 64 * words)
    uint32_t occurrence = 0;              // occurrences() index; occurrences().size() for a top input
  };
  // Executable sites hash-consed by (support bits, occurrence) -- by the bits
  // alone when that split would exceed kMaxSupportClasses (exact: equal rows
  // are idle in the same pairs), then folded (see Support::fold_*). Every
  // executable site joins a class, with four weights the profiler can average
  // idleness over (the tuner picks one; sim_profile.md §8.1 I_s):
  //   ge        -- the synthesis gate estimate (0 for pure wiring);
  //   sites     -- 1 per site;
  //   cost      -- simulation words: max(1, ceil(widest output bits / 64)) (a
  //                memory: its data width), a compact loop its native body's
  //                cost x its lanes (nested loops multiply), an opaque call
  //                with a body its body's cost; always >= 1 (wiring is still
  //                emitted code);
  //   cost_flat -- cost with no body multiplier: a loop or opaque call weighs
  //                only its own words.
  struct Support_class {
    uint64_t ge         = 0;
    uint64_t sites      = 0;
    uint64_t cost       = 0;
    uint64_t cost_flat  = 0;
    uint32_t occurrence = 0;  // occurrences().size() = spans several occurrences (merged or folded over the cap)
  };
  struct Support {
    bool                        available       = false;  // false: the version DAG is cyclic (the plan failed)
    bool                        exact           = true;   // one bucket per source; false = contiguous bucketing
    uint32_t                    words           = 0;      // activity-mask words per class
    uint64_t                    total_ge        = 0;      // sum of the classes' GE
    uint64_t                    total_sites     = 0;      // sum of the classes' sites (every executable site)
    uint64_t                    total_cost      = 0;      // sum of the classes' cost
    uint64_t                    total_cost_flat = 0;      // sum of the classes' cost_flat
    // Over kMaxSupportClasses the lightest classes fold into fold_classes
    // TRAILING groups whose row ORs their members' rows (conservative: a group
    // is idle only when every member is). fold_* is the weight that sits in
    // them -- the share of each total the idle columns can only under-report
    // (the I_s ceiling the profiler cannot see past). All zero below the cap.
    uint32_t                    fold_classes    = 0;  // trailing folded (approximate-row) classes
    uint32_t                    fold_members    = 0;  // row classes folded into them
    uint64_t                    fold_ge         = 0;
    uint64_t                    fold_sites      = 0;
    uint64_t                    fold_cost       = 0;
    uint64_t                    fold_cost_flat  = 0;
    std::vector<Support_source> sources;  // in hashing order: sites by path rank, then root inputs
    std::vector<Support_class>  classes;
    std::vector<uint64_t>       class_bits;  // classes.size() * words
  };
  // Bounds that keep support() cheap on the largest designs: a class table the
  // sampler can AND in microseconds, and per-version bitmaps (freed before
  // discover() returns) that never exceed kMaxSupportBitmapBytes.
  static constexpr size_t   kMaxSupportClasses     = 4096;
  static constexpr size_t   kSupportFoldGroups     = 128;  // table slots the over-the-cap fold may use
  static constexpr uint32_t kMaxSupportWords       = 16;
  static constexpr uint64_t kMaxSupportBitmapBytes = uint64_t{64} << 20;

  Color_plan() = default;

  // `root` must already be fully prepared.  The plan holds lazy occurrence
  // handles, so structurally mutating root's library afterwards is a contract
  // violation caught by HHDS's mutation-epoch assertion in debug builds.
  // Separate clock runtime calls when the backend uses a distinct data ABI
  // (LLVM). Compact loops may share colors with surrounding logic; their
  // runtime guards each stateful advance once per phase across output colors.
  // `live_words` = the per-color live-word budget (0 = the built-in default).
  // `fence_ratio` = sites per interface word a single-use module needs to keep
  // its own colors (<0 = the built-in default, 0 = fence every such module).
  static Color_plan         discover(hhds::Graph* root, bool include_observations = true, bool separate_runtime_calls = false,
                                     uint64_t live_words = 0, int64_t fence_ratio = -1);
  static constexpr uint64_t kDefaultLiveWords  = livehd::sim::kTuneDefaultLiveWords;
  // Best weighted average (pyrope2 x4, pyrope x2, verilog x1) over lhdsuite
  // and lhdtrack on 2026-09-18: within 1% of each benchmark's best on 19/21,
  // the outliers being tuned per benchmark (xs_renametable 0, cdc_fifo_flops
  // never). Values <= 3 cost the LFSR-driven lhdtrack DUTs 5-10x.
  static constexpr int64_t  kDefaultFenceRatio = livehd::sim::kTuneDefaultFenceRatio;
  // No module fences at all. A fence exists only so dirty-bit gating can skip
  // an idle module; with sim.tune.dirty=off every color runs every period,
  // so a fence is pure cost (each crossing value becomes a stored slot and the
  // module seam cuts colors). inou.cgen selects this when dirty gating is off
  // and sim.tune.fence was not set explicitly.
  static constexpr int64_t  kNoFences          = livehd::sim::kTuneNoFences;

  [[nodiscard]] const std::vector<Site>&                sites() const noexcept { return sites_; }
  [[nodiscard]] const std::vector<Dependency>&          dependencies() const noexcept { return dependencies_; }
  [[nodiscard]] const std::vector<Observation>&         observations() const noexcept { return observations_; }
  [[nodiscard]] const std::vector<Version_site>&        version_sites() const noexcept { return version_sites_; }
  [[nodiscard]] const std::vector<Version_dependency>&  version_dependencies() const noexcept { return version_dependencies_; }
  [[nodiscard]] const std::vector<Value_use>&           value_uses() const noexcept { return value_uses_; }
  [[nodiscard]] const std::vector<Boundary_slot>&       boundary_slots() const noexcept { return boundary_slots_; }
  [[nodiscard]] const std::vector<Color>&               colors() const noexcept { return colors_; }
  // Color indices in EXECUTION ORDER. `Color::execution_order` is already a
  // DENSE rank (assign_color_order stamps 0..N-1), so this is an O(n)
  // placement, not a sort -- and it is computed once instead of at each of the
  // emitter's five "walk the colors in order" sites.
  [[nodiscard]] const std::vector<size_t>&              colors_in_execution_order() const;
  [[nodiscard]] const std::vector<Color_dependency>&    color_dependencies() const noexcept { return color_dependencies_; }
  [[nodiscard]] const std::vector<Kernel_class>&        kernel_classes() const noexcept { return kernel_classes_; }
  // Per color, version-site indices in the canonical rank order used by its
  // verified kernel class. Equal kernel signatures therefore have an exact,
  // name-free member correspondence for generated ABI binding.
  [[nodiscard]] const std::vector<std::vector<size_t>>& canonical_members() const noexcept { return canonical_members_; }
  [[nodiscard]] const Summary&                          summary() const noexcept { return summary_; }
  [[nodiscard]] const std::vector<std::string>&         errors() const noexcept { return errors_; }
  [[nodiscard]] bool                                    complete() const noexcept { return summary_.complete; }
  // sim.tune: see Occurrence / Support above. Occurrences are ordered by path.
  [[nodiscard]] const std::vector<Occurrence>&          occurrences() const noexcept { return occurrences_; }
  [[nodiscard]] const Support&                          support() const noexcept { return support_; }

  // Re-resolves lazy edges after construction.  Besides checking the retained
  // handles, this pins the policy-lifetime contract: moving a Color_plan must
  // not leave HHDS's function_ref pointing at a builder-local lambda.
  [[nodiscard]] bool validate_retained_handles() const;

  // Stable, line-oriented bring-up artifact.  Raw Gid/Nid/Class_index values
  // and user names are excluded from schedule identity; names occur only in
  // the final observation-map section.
  [[nodiscard]] std::string report() const;
  void                      write_report(std::string_view path) const;

private:
  using Policy = std::function<hhds::Instance_action(const hhds::Instance_site&)>;

  // Fills occurrences_ and support_. discover() calls it once the version DAG
  // is ordered and before the fence decision; `compact_body[s]` = site s sits
  // inside a compact loop's native body.
  void build_tune_tables(hhds::Graph* root, const std::vector<std::vector<size_t>>& versions_by_base,
                         const std::vector<size_t>& site_path_rank, const std::vector<bool>& compact_body);

  // Heap allocation is intentional. Hierarchy_policy is function_ref, so the
  // callable address -- not merely the Color_plan object -- must remain stable
  // across return-value moves and later lazy edge resolution.
  std::shared_ptr<Policy> outer_policy_;
  std::shared_ptr<Policy> discovery_policy_;

  // Cache for colors_in_execution_order(); built on first use.
  mutable std::vector<size_t> colors_in_execution_order_;

  std::vector<hhds::Occurrence_node> outer_nodes_;
  std::vector<Site>                  sites_;
  std::vector<Dependency>            dependencies_;
  std::vector<Observation>           observations_;
  std::vector<Version_site>          version_sites_;
  std::vector<Version_dependency>    version_dependencies_;
  std::vector<Value_use>             value_uses_;
  std::vector<Boundary_slot>         boundary_slots_;
  std::vector<Color>                 colors_;
  std::vector<Color_dependency>      color_dependencies_;
  std::vector<Kernel_class>          kernel_classes_;
  std::vector<std::vector<size_t>>   canonical_members_;
  Summary                            summary_;
  std::vector<std::string>           errors_;
  std::vector<Occurrence>            occurrences_;
  Support                            support_;
  // Live machine words this plan's coarsener was allowed to keep alive across
  // one color's members. Per-PLAN, not a global: report() prints the budget the
  // colors below were actually built at, so a later discover() at a different
  // budget cannot desynchronize this plan's report from this plan.
  uint64_t                           live_word_budget_ = kDefaultLiveWords;
};

}  // namespace livehd::sim
