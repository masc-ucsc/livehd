// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <optional>
#include <span>

#include "function.hpp"
#include "xag.hpp"

namespace livehd::usyn {

struct Endpoint_cost_model {
  uint32_t static_and   = 2;
  uint32_t static_xor   = 4;
  uint32_t static_not   = 1;
  uint32_t domino       = 5;
  uint32_t domino_latch = 8;
};

struct Endpoint_options {
  Gate_constraints       gates;
  Function_search_limits functions;
  Window_limits          window;
  Endpoint_cost_model    cost;
  uint32_t               clock_phases        = 2;
  uint32_t               boundaries          = 32;  // retained frontier; at most 12*boundaries+2 move trials
  uint32_t               divisor_partitions  = 32;  // 0 disables new functional divisors
  uint32_t               care_phases         = 16;  // 0 disables alternative top completions
  // Shared by boundary candidates and the deferred functional-decomposition queue.
  uint64_t               boundary_bytes      = 8 * 1024 * 1024;
  // Tables and gate results share one cache per immutable endpoint search.
  uint32_t               gate_cache_entries  = 64;
  uint64_t               gate_cache_bytes    = 8 * 1024 * 1024;
  uint32_t               image_cache_entries = 64;
  uint64_t               image_cache_bytes   = 8 * 1024 * 1024;
  uint32_t               local_divisors      = 32;
  uint32_t               local_candidates    = 64;  // 0 disables local non-cut sets
  uint32_t               cost_nodes          = 2000000;
  uint64_t               candidate_work      = 2000000;
  // Each tier has a cap and reserves part of the remaining total for later
  // tiers. Mandatory endpoint construction runs before these searches.
  uint64_t               tier_work           = 8000000;
  // A whole-cone legal result may stop early after checking external reuse.
  // False spends the remaining budget comparing alternative boundaries too.
  bool                   fast_accept         = true;
};

bool valid_endpoint_options(const Endpoint_options& options);

// A total function of ordered native signals or synthesized cell outputs.
// Unused inputs have been removed. The formula includes any output-polarity choice; both
// logical outputs are available from the same physical DOMINO cell.
struct Endpoint_function {
  std::optional<Xsignal> root;  // absent for a newly synthesized divisor
  // Structural boundary used to establish the function before support
  // reduction. A functionally irrelevant input can still have a graph path.
  std::vector<Id>        boundary;
  std::vector<Id>        inputs;
  Truth_table            table;
  Gate_formula           formula;
  Gate_formula::Metrics  metrics;
};

struct Endpoint_cell {
  std::shared_ptr<const Endpoint_function> function;
  // -1: upstream static/source signal. Otherwise an earlier cell's Q/!Q.
  // Indices correspond to function->inputs; formula literals choose polarity.
  std::vector<int32_t>                     producers;
  uint32_t                                 phase = 1;
  bool                                     latch = false;
};

struct Endpoint_solution {
  std::string                    name;  // original register bit/stage name, not a search-order ID
  Xsignal                        root;
  // The final element is the mandatory DominoLatch. Earlier elements, if
  // present, are parallel phase-1 cells, never additional state elements.
  std::vector<Endpoint_cell>     cells;
  // Functional decompositions are checked by composition over this original
  // graph cut. Produced input slots use Id 0; their producer index owns them.
  // Static ports may also be interior nodes of this cone; their functions are
  // rederived on the same basis so correlated boundary values stay correlated.
  std::optional<std::vector<Id>> functional_basis;
  uint64_t                       static_cost   = 0;
  uint64_t                       domino_cost   = 0;
  uint64_t                       inverter_cost = 0;
  uint64_t                       total_cost() const { return static_cost + domino_cost + inverter_cost; }
  uint32_t                       residual_nodes = 0;
  bool                           whole_cone     = false;
  std::string                    origin;
};

struct Endpoint_report {
  bool                     operator==(const Endpoint_report&) const = default;
  bool                     whole_admitted                           = false;
  bool                     window_used                              = false;
  bool                     exhausted                                = false;
  uint64_t                 one_cell_attempts                        = 0;
  uint64_t                 two_phase_attempts                       = 0;
  uint64_t                 new_divisor_attempts                     = 0;
  uint64_t                 single_divisor_attempts                  = 0;  // functional two-cell realizations
  uint64_t                 parallel_divisor_attempts                = 0;  // wider functional realizations
  uint64_t                 deferred_divisor_bytes                   = 0;  // peak retained wider decompositions
  uint64_t                 existing_care_attempts                   = 0;
  uint64_t                 existing_care_images                     = 0;
  uint64_t                 divisor_images                           = 0;
  uint64_t                 divisor_image_hits                       = 0;
  uint64_t                 image_cache_bytes                        = 0;
  uint64_t                 local_attempts                           = 0;
  uint64_t                 local_wins                               = 0;
  uint64_t                 local_work                               = 0;
  uint64_t                 completion_phases                        = 0;
  uint64_t                 completion_attempts                      = 0;
  uint64_t                 residual_attempts                        = 0;
  uint64_t                 boundaries                               = 0;
  uint64_t                 boundary_trials                          = 0;  // includes duplicate and unsuccessful moves
  uint64_t                 boundary_replacements                    = 0;
  uint64_t                 boundary_bytes_peak                      = 0;  // admitted windows, history and active expansion payload
  uint64_t                 boundary_wins                            = 0;  // legal incumbent improvements during ranking
  uint64_t                 functions                                = 0;
  uint64_t                 function_hits                            = 0;
  uint64_t                 analysis_tables          = 0;  // search table construction, excluding independent validation
  uint64_t                 analysis_hits            = 0;
  uint64_t                 analysis_cache_bytes     = 0;  // retained payload plus conservative container overhead
  uint64_t                 cost_visits              = 0;
  uint64_t                 admission_work           = 0;
  uint64_t                 one_cell_work            = 0;
  uint64_t                 boundary_work            = 0;
  // Subsets of boundary_work. Their sum with two_phase_work shares the
  // phase-search cap; do not add them again when totaling stage work.
  uint64_t                 boundary_two_cell_work   = 0;
  uint64_t                 boundary_multi_cell_work = 0;
  uint64_t                 two_phase_work           = 0;
  uint64_t                 residual_work            = 0;
  std::vector<std::string> limits;
};

struct Endpoint_result {
  Status                           status = Status::invalid;
  std::optional<Endpoint_solution> selected;
  // Optional bounded pool of differently shared static interfaces. Candidates
  // are priced, but callers must validate composition before publication.
  std::vector<Endpoint_solution>   choices;
  Endpoint_report                  report;
  std::string                      reason;
};

// The minimal legal endpoint of `root`: one DominoLatch implementing the
// identity (or the constant) of the next-state signal, so the whole D cone
// stays static CMOS. It needs no search and no work; validate_endpoint accepts
// it. select_endpoint starts from it as its incumbent, and logical synthesis
// builds its credit-free baseline selection from it.
Endpoint_solution identity_endpoint(const Xag& graph, Xsignal root, const std::string& name, const Endpoint_options& options);

// protected_roots includes every outside consumer, including other endpoints,
// POs and special-state ports. Its reachable logic earns no removal credit.
// free_rails identifies signals already supplied as Q/!Q (typically source
// state outputs). Ordinary residual producers pay for a shared explicit NOT.
// A caller must provide semantically eligible endpoints and legal domains;
// this graph-only search does not infer clocks or source-state eligibility.
// A nonempty analysis_boundary pins a caller-admitted structural window. Search
// may contract inside it but cannot expand its leaves. Unused basis signals are
// allowed; each reached subset must fit options.window. Correlated leaves are
// conservatively treated as independent, with no inferred upstream care set.
// choice_limit (0..8) optionally retains differently shared interfaces under a
// separate boundary_bytes-sized payload cap; zero leaves ordinary search alone.
Endpoint_result select_endpoint(const Xag& graph, Xsignal root, std::string name, std::span<const Xsignal> protected_roots,
                                std::span<const Id> free_rails, const Endpoint_options& options, Budget& work,
                                std::span<const Id> analysis_boundary = {}, uint32_t choice_limit = 0);

// Exact bounded structural/function validation for artifact construction.
// This is not whole-design LEC. Structural cells implement their declared XAG
// roots; newly synthesized cells are checked by composing the whole endpoint
// over functional_basis. No residual logic consumes a same-run DOMINO output
// before another phase. No ordinary register fallback is represented.
bool validate_endpoint(const Xag& graph, const Endpoint_solution& solution, const Endpoint_options& options, Budget& work);

}  // namespace livehd::usyn
