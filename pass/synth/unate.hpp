// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace livehd::synth {

using Id = uint32_t;

// The single admission bound on a bounded truth table / enumeration: every
// engine here (covers, ROBDD queries, cofactor encoding, reshaping) is limited
// by the same number, so it is declared once rather than re-typed per file.
inline constexpr uint32_t max_logical_inputs = 12;

// The table's bit i is f(x), with x[j] = (i >> j) & 1. Storage is dynamic; the
// bounded enumerator admits at most `max_logical_inputs` logical inputs.
struct Truth_table {
  uint32_t              inputs = 0;
  std::vector<uint64_t> words;
  explicit Truth_table(uint32_t n = 0, bool value = false);
  bool        get(uint32_t assignment) const;
  void        set(uint32_t assignment, bool value);
  Truth_table complement() const;
  bool        operator==(const Truth_table&) const = default;
};

struct Cube {
  uint32_t care                          = 0;
  uint32_t ones                          = 0;
  bool     operator==(const Cube&) const = default;
};

enum class Status { feasible, search_exhausted, unsupported, invalid };
const char* status_name(Status status);

// Shared deterministic work budget for cuts, tables, primes, selection and
// verification. Exhaustion is never an infeasibility certificate.
struct Budget {
  uint64_t              remaining;
  bool                  exhausted = false;
  std::function<bool()> admission{};
  uint64_t              checkpoint_work    = 0;
  bool                  resource_exhausted = false;
  // Work between admission checks. A check samples the process (a syscall), so
  // it stays off the per-cut path; tests shorten it to hit a cancellation.
  uint64_t              admission_interval = 262144;
  bool                  spend(uint64_t amount = 1);
};

struct Form {
  Status            status = Status::search_exhausted;
  std::vector<Cube> cubes;
  uint32_t          literals = 0;
  uint32_t          series   = 0;
  uint32_t          positive = 0;
  uint32_t          negative = 0;
  // Literals of a greedy algebraic factoring of `cubes` (a series-parallel
  // pull-down network); 0 unless exact_form computed it.
  uint32_t          factored = 0;
};

// Construct a total SOP completion. Both signs denote real rail demand, never
// an implicit free inverter. max_series limits the constructed form only.
// The care overload chooses a total completion; its caller must independently
// prove that this care mask covers all reachable input assignments.
Form make_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, Budget& budget);
Form make_form(const Truth_table& table, const Truth_table& care, uint32_t max_literals, uint32_t max_series, Budget& budget);
bool check_form(const Truth_table& table, const Form& form, Budget& budget);
// Exact minimum-literal SOP (ties: fewer cubes) of a total function of at most
// 8 inputs, over primes of at most max_series literals. `factored` also counts
// the literals of an algebraic factoring of that SOP -- the transistors of a
// series-parallel pull-down network -- and checks max_literals against that
// count instead of the SOP's. A wider table falls back to make_form.
Form exact_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, bool factored, Budget& budget);
// Literals of a greedy algebraic factoring (most shared literal first).
uint32_t factor_literals(const std::vector<Cube>& cubes);
bool check_form(const Truth_table& table, const Truth_table& care, const Form& form, Budget& budget);

// Technology-independent source network. IDs are topological, inputs are
// ordered, and inversion lives in a truth table, not on an edge. State and
// clock/reset semantics belong to the importing region's boundary contract.
struct Logic_node {
  bool            source = false;
  std::vector<Id> inputs;
  Truth_table     table;
};
struct Logic_network {
  std::vector<Logic_node> nodes;
  std::vector<Id>         outputs;
  Id                      add_source();
  Id                      add_function(std::vector<Id> inputs, Truth_table table);
  bool                    valid() const;
};

inline constexpr uint32_t max_symbolic_nodes   = 65536;
inline constexpr uint32_t max_symbolic_sources = 256;

struct Image_result {
  Status          status = Status::search_exhausted;
  Truth_table     care;
  std::vector<Id> sources;
  bool            symbolic = false;
};
// Exact reachable tuples by enumeration, or bounded symbolic evaluation when
// symbolic_nodes is nonzero. Limits never authorize sampled don't-cares.
Image_result exact_image(const Logic_network& source, const std::vector<Id>& leaves, uint32_t source_limit, Budget& budget,
                         uint32_t symbolic_nodes = 0);

enum class Dependency_status { proven, refuted, exhausted, unsupported, invalid };
struct Dependency_result {
  Dependency_status status = Dependency_status::exhausted;
  Truth_table       table, care;
  std::vector<Id>   sources;
  bool              symbolic = false;
};
// Prove root is constant on every block induced by the ordered divisors.
// Divisors must precede root, but need not structurally separate its fan-in.
Dependency_result exact_dependency(const Logic_network& source, Id root, const std::vector<Id>& divisors, uint32_t source_limit,
                                   Budget& budget, uint32_t symbolic_nodes = 0);

// levels caps the function-network depth; 0 = unbounded (the default: depth
// is a future constraint, the objective is the fewest unate functions).
struct Recipe {
  uint32_t levels                          = 0;
  uint32_t support                         = 6;
  uint32_t literals                        = 16;
  uint32_t series                          = 4;
  bool     operator==(const Recipe&) const = default;
};
inline uint32_t level_limit(const Recipe& recipe) {
  return recipe.levels ? recipe.levels : std::numeric_limits<uint32_t>::max();
}
// Transistor proxies of the LUT cover (pass.synth, split=false). Inverters on
// inputs and outputs are free: every signal is available in both polarities.
// A domino gate costs its factored pull-down literals plus a fixed overhead
// (precharge, foot, keeper, output inverter); a function no domino gate builds
// is a static CMOS LUT: cmos_factor transistors per factored literal (pull-up
// and pull-down) plus an output inverter, all times the non-unate penalty.
struct Cover_cost {
  uint32_t domino_overhead                     = 5;
  uint32_t cmos_factor                         = 2;
  uint32_t static_overhead                     = 2;
  uint32_t nonunate_penalty                    = 2;
  bool     operator==(const Cover_cost&) const = default;
};
struct Search_options {
  std::vector<Recipe> recipes{
      {0, 6, 16, 4}
  };
  uint32_t              cuts_per_node       = 32;
  uint32_t              max_nodes           = 100000;
  uint64_t              work                = 5000000;
  uint32_t              recovery_rounds     = 2;     // bounded exact shared-DAG recounts; 0 disables
  uint32_t              joint_limit         = 256;   // max combinations in a complete retained-choice closure; 0 disables
  uint32_t              joint_windows       = 8;     // bounded related-choice windows when the full closure exceeds joint_limit
  uint32_t              image_inputs        = 8;     // image/dependency source admission (0 disables; maximum 12)
  uint32_t              divisor_limit       = 64;    // proposed paid-divisor sets per demanded function (0 disables; max 4096)
  uint32_t              cover_limit         = 32;    // divisor sets per uncovered output (0 disables; max 4096)
  uint32_t              encoding_limit      = 16;    // bound-set proposals for an uncovered multi-output cone; 0 disables
  uint32_t              encoding_code_limit = 8;     // injective class-code assignments per bound-set proposal (1..4096)
  uint32_t              encoding_pair_limit = 16;    // disjoint two-bound-set proposals after single-set search; 0 disables
  uint32_t              reshape_limit       = 32;    // admitted associative output roots; 0 disables
  uint32_t              symbolic_nodes      = 4096;  // bounded ROBDD fallback above image_inputs; 0 disables
  // SELECTION, not search: the relative delay tie band used when a mapped
  // candidate is compared against the ABC baseline (see abc_timing.hpp
  // timing_better). 0.01 = delays within 1% are tied and area decides.
  double                delay_tolerance     = 0.01;
  // pass.synth.split: per-cone bounded split (split_cones) instead of the
  // fewest-function cover of the whole region (optimize).
  bool                  split               = false;
  // Split-mode knobs. exact: exact minimum-literal gate SOPs (else the greedy
  // make_form). factor: a gate's literal limit applies to its factored form.
  // cut: widest cut a node may be decomposed over as two gates G(H(B), rest)
  // (support..12; support disables). share: prefer a bound-set function H an
  // earlier node already uses, so cones with the same shared part share H.
  bool                  split_exact         = true;
  bool                  split_factor        = true;
  uint32_t              split_cut           = 10;
  bool                  split_share         = true;
  // Cover mode (split=false, lut_cover.hpp). cover_cuts: priority cuts kept
  // per node. domino_levels: an output that some cover builds in at most this
  // many chained domino gates must be built so (a half-cycle each: 2 = one
  // cycle); deeper outputs only minimize transistors. 0 ignores depth.
  // reference: also map each region with ABC `&if -K support -a` (after `&dch`
  // structural choices with reference_choices) and classify its LUTs with the
  // same costs -- insight only, never the netlist.
  Cover_cost            cover_cost{};
  uint32_t              cover_cuts          = 12;
  uint32_t              domino_levels       = 2;
  // depth_slack >= 0: every deeper output is also required at its minimum
  // domino depth plus this many levels (depth-optimal, then area recovery);
  // -1 leaves deeper outputs unconstrained (transistors only).
  int32_t               depth_slack         = -1;
  // A node with at least this many sinks is always a LUT boundary: consumers
  // read it as a leaf and never replicate its logic. 0 lets the cost decide.
  uint32_t              fanout_boundary     = 0;
  // false: a gate may absorb a node with several readers only when all its
  // readers are inside that gate, so no logic is ever computed twice
  // (reconvergent fan-out is absorbed; other multi-reader nodes are gate outputs).
  bool                  duplicate           = true;
  // false: a blasted memory region (a register file) is left to the pass.abc
  // flow, as the domino plan keeps memories as separate wares.
  bool                  cover_memories      = true;
  bool                  reference           = false;
  // How the cover reaches ABC. gate: each LUT is technology-mapped on its own
  // (pass.synth's netlist). tmap: the whole cover network, each gate as its
  // minimum SOP, technology-mapped only (`&nf`). opt: that network through the
  // full pass.abc optimize-and-map flow. only: no cover -- the original region
  // logic through the pass.abc flow (same partition as the other modes).
  // source: a control -- the imported source network itself (no cover) through
  // the same export as opt, then the pass.abc flow.
  // source_tmap: the same source network, technology-mapped only (`&nf`).
  enum class Abc_mode { gate, tmap, opt, only, source, source_tmap };
  Abc_mode              abc_mode            = Abc_mode::gate;
  bool                  reference_choices   = false;
  std::function<bool()> admission{};
  // Telemetry only: balanced enter/leave notifications for each attempted
  // recipe (including unsupported/exhausted attempts), in recipe order.
  std::function<void(size_t, bool)> observe_recipe{};
};

// Exported nodes have ONLY positive edges. Each term ANDs port indices and
// the function ORs terms. Source complements have explicit inverter producers;
// internal complements are separately synthesized twin functions. Identity is
// per source occurrence, not per truth table. A tmap cache may share templates,
// but must instantiate one fragment per node.
enum class Node_kind { source, source_inverter, function };
struct Unate_node {
  Node_kind                          kind     = Node_kind::source;
  Id                                 origin   = 0;
  bool                               negative = false;
  std::vector<Id>                    ports;
  std::vector<std::vector<uint32_t>> terms;
  std::vector<Id>                    logical_inputs;
  std::vector<Id>                    witness_inputs;  // original cut or proved divisors, before exact support shrinking
  Truth_table                        completion;
  uint32_t                           level = 0;
  std::optional<Truth_table>         care;  // exact image in witness_inputs order; absent means total care
  std::vector<Id>                    image_sources;
  bool                               functional = false;  // witness_inputs are proved divisors, not a separating cut
};
// Bound synthetic definition DAGs independently of per-function support.
inline constexpr uint32_t max_encoder_nodes = 256;
struct Unate_network {
  std::vector<Unate_node> nodes;
  std::vector<Id>         outputs;
  uint32_t                depth            = 0;
  uint32_t                max_support      = 0;
  uint32_t                max_literals     = 0;
  uint32_t                max_series       = 0;
  uint32_t                source_inverters = 0;
  // Synthetic identities are source.nodes.size() + index. Each encoder is an
  // owning total function of original primary sources or preceding encoders.
  // Definitions never reference original internal logic or introduce new inputs.
  std::vector<Logic_node> encodings;
};
struct Attempt {
  Recipe                       recipe;
  Status                       status = Status::search_exhausted;
  std::string                  reason;
  uint64_t                     work            = 0;
  uint32_t                     covered_outputs = 0;
  // Rails that took their node's own fan-in cut because the search found no
  // retained cut for them or ran out of work (see fanin_choice).
  uint64_t                     fanin_fallbacks = 0;
  Unate_network                network;
  // Keep the verified seed for physical comparison: a structural cost decrease
  // does not imply a smaller or faster mapped network.
  std::optional<Unate_network> recovered;
  uint64_t                     recovery_checks       = 0;
  uint64_t                     recovery_improvements = 0;
  bool                         recovery_exhausted    = false;
  // Keep local recovery for physical comparison even if joint selection finds
  // a better structural cost. Complete means optimal over the retained choices
  // in this recipe's possible dependency closure, not over all decompositions.
  std::optional<Unate_network> joint_recovered;
  uint64_t                     joint_combinations = 0, joint_checks = 0, joint_improvements = 0;
  bool                         joint_complete = false, joint_exhausted = false;
  std::string                  joint_reason  = "not_run";
  uint32_t                     joint_windows = 0, joint_windows_complete = 0;
  std::string                  joint_scope   = "retained_closure";
  uint64_t                     image_queries = 0, image_proven = 0, image_limited = 0, image_candidates = 0;
  std::optional<Unate_network> divisor_recovered;
  uint64_t    divisor_queries = 0, divisor_refuted = 0, divisor_limited = 0, divisor_checks = 0, divisor_improvements = 0;
  bool        divisor_exhausted = false;
  std::string divisor_reason    = "not_run";
  uint64_t    cover_queries = 0, cover_refuted = 0, cover_limited = 0;
  uint32_t    cover_roots     = 0;
  bool        cover_exhausted = false;
  std::string cover_reason    = "not_run";
  uint64_t    image_symbolic = 0, divisor_symbolic = 0, cover_symbolic = 0;
  uint64_t    divisor_symbolic_limited = 0, cover_symbolic_limited = 0;
  uint32_t    encoding_queries = 0, encoding_classes = 0, encoding_bits = 0;
  uint32_t    encoding_pair_queries = 0, encoding_bound_sets = 0;
  uint32_t    encoding_code_queries = 0;
  bool        encoding_code_limited = false;
  uint32_t    reshape_roots = 0, reshape_groups = 0;
  std::string reshape_reason  = "not_run";
  std::string encoding_reason = "not_run";
};
struct Search_result {
  Status               status = Status::search_exhausted;
  std::vector<Attempt> attempts;
};

// Bounded cofactor-class synthesis for the residue of structural cover search.
// Produces a complete, independently verified network or no network.
void encode_residue(const Logic_network& source, uint32_t limit, Attempt& attempt, Budget& budget, uint32_t pair_limit = 0,
                    uint32_t code_limit = 8);
void reshape_residue(const Logic_network& source, uint32_t limit, uint32_t symbolic_nodes, Attempt& attempt, Budget& budget);

// Every feasible attempt is retained for mapping/QoR comparison. This is a
// bounded cut/divisor search; failure says nothing about decompositions outside
// its enumerated choices. Local source-space checks prove composition by induction.
Search_result optimize(const Logic_network& source, const Search_options& options = {});

// Per-cone bounded split (pass.synth.split). A GATE is one unate function: a
// positive SOP over its <= recipe.support leaves within recipe.literals and
// recipe.series, where every leaf -- a source or another gate -- is available
// in either polarity. Each output cone is classified by the fewest gates that
// build it (a tree count, capped at 4): 0 (a wire or constant), 1-2, 3 or more.
// A cone of at most 3 keeps all its gates. A larger cone keeps its root gate
// and one gate under it, both chosen to cover the most logic, and the logic
// below them becomes a non-unate REMAINDER that ABC only technology-maps.
struct Split_gate {
  Id              root = 0;
  std::vector<Id> leaves;  // the form's leaf order
  Form            form;
  bool            complemented = false;  // the form computes !root
  uint32_t        volume       = 0;      // source-network nodes the gate covers
};
struct Split_result {
  Status                  status = Status::search_exhausted;
  std::string             reason;
  // Kept gates in mapping (topological) order. A root >= the source's node
  // count is a SYNTHETIC bound-set gate H of a decomposition G(H(B), rest).
  std::vector<Split_gate> gates;
  uint32_t                synthetic = 0;  // synthetic roots are [n, n + synthetic)
  uint64_t                bound_gates = 0, shared_bound_gates = 0;
  std::vector<Id>         remainder_outputs;  // non-gate internal nodes a gate or an output reads, ascending
  std::vector<uint8_t>    cone_gates;         // per source output: 0..3, or 4 for more
  uint64_t                cones_wire = 0, cones_2 = 0, cones_3 = 0, cones_more = 0;
  uint64_t                remainder_nodes = 0;  // source-network nodes the remainder must compute
  uint64_t                fanin_fallbacks = 0;  // nodes that took their own fan-in (work exhausted)
  uint64_t                work            = 0;
};
Split_result split_cones(const Logic_network& source, const Recipe& recipe, const Search_options& options);

// Independent structural/semantic witness check against the original network.
// Checks each selected cut over independent leaves or an exact original-source
// image, then checks its TOTAL completion and all rail correspondence.
bool verify(const Logic_network& source, const Unate_network& network, const Recipe& recipe, Budget& budget);

}  // namespace livehd::synth
