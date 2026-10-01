// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "decomposition.hpp"

namespace livehd::usyn {
bool valid_endpoint_options(const Endpoint_options& o) {
  return (o.clock_phases == 1 || o.clock_phases == 2) && o.boundaries > 0 && o.boundaries <= 4096 && o.window.inputs > 0
         && o.window.inputs <= max_logical_inputs && o.window.nodes > 0 && o.gates.logical_inputs > 0
         && o.gates.logical_inputs <= max_logical_inputs && o.gates.stack > 0 && o.gates.branches > 0 && o.functions.max_cubes > 0
         && o.functions.max_formula_nodes > 0 && o.functions.factoring_choices > 0
         && o.functions.factoring_choices <= 2 * max_logical_inputs + 1 && o.cost.static_and > 0 && o.cost.static_xor > 0
         && o.cost.static_not > 0 && o.cost.domino > 0 && o.cost.domino_latch > 0 && o.cost_nodes > 0 && o.candidate_work > 0
         && o.boundary_bytes > 0 && o.tier_work > 0 && o.divisor_partitions <= 4096 && o.care_phases <= max_completion_phases
         && o.local_divisors > 0 && o.local_divisors <= 256 && o.local_candidates <= 4096;
}

namespace {

// Work_slice (unate.hpp) shares total admission while limiting a single
// unsuccessful factoring/window attempt: a per-candidate work refusal must not
// consume every later tier. Unwinding transfers completed work without sampling
// the external guard again; the child resumed the parent's checkpoint, so
// admission is sampled once per admission_interval of work, not once per slice.

struct Static_ledger {
  std::unordered_set<Id> visited;
  std::unordered_set<Id> inverted;
  uint64_t               cost  = 0;
  uint32_t               gates = 0;
};

struct Function_key {
  Xsignal         root;
  std::vector<Id> leaves;
  auto            operator<=>(const Function_key&) const = default;
};

// Search owns an immutable graph, so root and ordered leaves identify an exact
// function for this search's lifetime. Keep the table even when factoring ran
// out of work; that refusal must not poison a later tier's gate attempt.
struct Cached_analysis {
  Truth_table                              table;
  std::shared_ptr<const Endpoint_function> gate;
  bool                                     gate_ready = false;
  bool                                     retained   = false;
};

struct Cached_divisor {
  Xag_window  window;
  Truth_table table;
};
struct Image_key {
  Xsignal         root;
  std::vector<Id> basis, divisors;
  auto            operator<=>(const Image_key&) const = default;
};

struct Pending_decomposition {
  Decomposition decomposition;
  uint64_t      remaining_work;
};

class Search {
public:
  Search(const Xag& graph, Xsignal root_arg, std::string name_arg, std::span<const Xsignal> protected_arg,
         std::span<const Id> rails_arg, const Endpoint_options& options, Budget& work, std::span<const Id> boundary_arg,
         uint32_t choice_limit_arg)
      : g(graph)
      , root(root_arg)
      , name(std::move(name_arg))
      , protected_roots(protected_arg)
      , free_rails(rails_arg.begin(), rails_arg.end())
      , analysis_boundary(boundary_arg)
      , o(options)
      , total(work)
      , active(&work)
      , choice_limit(choice_limit_arg) {}

  Endpoint_result run() {
    result.status = Status::search_exhausted;
    if (!walk_static(protected_roots, external, total)) {
      return finish("outside-consumer accounting unavailable");
    }
    // Establish a safe incumbent before more expensive exploration.
    consider(identity_endpoint(g, root, name, o));
    if (!total.available()) {
      return finish("endpoint admission budget");
    }

    Xag_window window;
    tier("window admission tier budget", 4, result.report.admission_work, [&] {
      {
        Work_slice slice(*active, o.candidate_work);
        if (analysis_boundary.empty()) {
          window = whole_cone(g, root, o.window, slice.work);
        } else {
          auto limits   = o.window;
          limits.inputs = max_logical_inputs;  // the common basis may include unused leaves
          window        = collect_subwindow(g, root, analysis_boundary, limits, slice.work);
          if (window.status == Status::feasible && window.leaves.size() > o.window.inputs) {
            window.status = Status::search_exhausted;
            window.reason = "pinned endpoint window support budget";
          }
          result.report.window_used = !window.whole_cone;
        }
        result.report.whole_admitted = window.status == Status::feasible && window.whole_cone;
        if (window.status != Status::feasible) {
          limit(window.reason);
        }
      }
      if (window.status != Status::feasible && analysis_boundary.empty() && available()) {
        Work_slice slice(*active, o.candidate_work);
        window                    = grow_window(g, root, o.window, slice.work);
        result.report.window_used = true;
        if (window.status != Status::feasible) {
          limit(window.reason);
        }
      }
      // The graph and outside-reader ledger are immutable for this search.
      // Charge this scan once instead of repeating it for each candidate.
      if (window.status == Status::feasible && o.fast_accept) {
        if (active->spend(window.interior.size())) {
          fast_eligible = std::none_of(window.interior.begin(), window.interior.end(), [&](auto id) {
            return external.visited.contains(id);
          });
        } else {
          limit("outside-sharing admission budget");
        }
      }
    });
    if (window.status != Status::feasible) {
      return finish("no admitted endpoint window");
    }
    tier("one-cell tier budget", 3, result.report.one_cell_work, [&] {
      ++result.report.one_cell_attempts;
      if (auto f = function(window)) {
        Endpoint_solution candidate;
        candidate.name   = name;
        candidate.root   = root;
        candidate.origin = window.whole_cone ? "whole-one-cell" : "window-one-cell";
        candidate.cells.push_back({f, std::vector<int32_t>(f->inputs.size(), -1), o.clock_phases, true});
        consider(std::move(candidate));
      }
    });
    if (fast_done()) {
      return finish();
    }

    // Try single-divisor functional candidates without constructing boundaries.
    // Wider encodings retain their tables, but cannot preempt existing two-cell
    // candidates. All phase construction and ranking share the same work cap.
    if (o.clock_phases == 2 && o.divisor_partitions) {
      phase_tier("new-divisor tier budget", 4, [&] { functional_decompositions(window); }, o.tier_work / 2);
    }
    if (fast_done()) {
      return finish();
    }
    std::vector<Xag_window> boundaries;
    const bool              two_rounds = o.clock_phases == 2 && o.boundaries > 1;
    tier(
        "boundary tier budget",
        3,
        result.report.boundary_work,
        [&] { boundaries = enumerate_boundaries(window); },
        two_rounds ? o.tier_work / 2 : o.tier_work);
    if (fast_done()) {
      return finish();
    }
    if (o.clock_phases == 2) {
      cell_limit = 2;
      phase_tier("existing two-cell tier budget", 4, [&] { structural_decompositions(boundaries, window); }, o.tier_work / 4);
      cell_limit = std::numeric_limits<size_t>::max();
      if (fast_done()) {
        return finish();
      }
      phase_tier(
          "parallel-divisor tier budget",
          4,
          [&] { realize_pending_decompositions(window); },
          std::min(o.tier_work / 4, two_rounds ? phase_remaining() / 2 : phase_remaining()));
      // Free the deferred table queue before the remaining boundary trials.
      std::vector<Pending_decomposition>().swap(pending);
      pending_bytes = 0;
      if (fast_done()) {
        return finish();
      }
      if (two_rounds) {
        tier("wide boundary tier budget", 3, result.report.boundary_work, [&] {
          // Release the narrow frontier before admitting another one. Exact
          // functions, gate results and the incumbent survive in their caches.
          std::vector<Xag_window>().swap(boundaries);
          boundaries = enumerate_boundaries(window, true);
        });
        if (fast_done()) {
          return finish();
        }
      }
      // Preserve at least half the remaining work for selective removal. Retry
      // bounded inconclusive two-cell trials too; their valid analysis is cached.
      phase_tier("two-phase tier budget", 2, [&] { structural_decompositions(boundaries, window); });
    }
    if (fast_done()) {
      return finish();
    }
    // The same cuts and gate results now answer what to leave in CMOS. Try
    // both a single endpoint and a mixed two-phase solution, retaining the
    // cheaper whole affected network (outside readers included).
    tier("residual tier budget", o.local_candidates ? 2 : 1, result.report.residual_work, [&] {
      for (const auto& boundary : boundaries) {
        if (!available()) {
          break;
        }
        ++result.report.residual_attempts;
        if (auto candidate = build(boundary, window, false, false)) {
          consider(std::move(*candidate));
        }
        if (o.care_phases && available()) {
          care_boundary(boundary, window, false, false);
        }
        if (o.clock_phases == 2 && available()) {
          if (auto candidate = build(boundary, window, true, false)) {
            consider(std::move(*candidate));
          }
          if (o.care_phases && available()) {
            care_boundary(boundary, window, true, false);
          }
        }
      }
    });
    if (o.local_candidates && !fast_done()) {
      tier("local divisor tier budget", 1, result.report.local_work, [&] { local_divisors(window); });
    }
    return finish();
  }

private:
  const Xag&                                                    g;
  Xsignal                                                       root;
  std::string                                                   name;
  std::span<const Xsignal>                                      protected_roots;
  std::unordered_set<Id>                                        free_rails;
  std::span<const Id>                                           analysis_boundary;
  const Endpoint_options&                                       o;
  Budget&                                                       total;
  Budget*                                                       active;
  Endpoint_result                                               result;
  Static_ledger                                                 external;
  std::map<Function_key, std::shared_ptr<Cached_analysis>>      cache;
  uint64_t                                                      cache_bytes   = 0;
  bool                                                          fast_eligible = false;
  std::map<Function_key, std::shared_ptr<const Cached_divisor>> divisor_cache;
  std::map<Image_key, std::shared_ptr<const Divisor_function>>  image_cache;
  uint64_t                                                      image_bytes = 0;

  std::vector<Pending_decomposition> pending;
  uint64_t                           pending_bytes = 0;
  size_t                             cell_limit    = std::numeric_limits<size_t>::max();

  bool available() { return active->available(); }

  template <typename Fn>
  void tier(const char* reason, uint32_t reserve_divisor, uint64_t& spent, Fn&& run,
            uint64_t cap = std::numeric_limits<uint64_t>::max()) {
    Work_slice slice(total, std::min(o.tier_work - spent, cap), reserve_divisor);
    active = &slice.work;
    if (available()) {
      run();
    }
    spent += slice.work.consumed;
    if (!available()) {
      limit(reason);
    }
    active = &total;
  }

  template <typename Fn>
  void phase_tier(const char* reason, uint32_t reserve_divisor, Fn&& run, uint64_t cap = std::numeric_limits<uint64_t>::max()) {
    // Boundary ranking charges the enclosing boundary tier once, and also
    // consumes phase-search admission. Stage totals remain non-overlapping.
    tier(reason, reserve_divisor, result.report.two_phase_work, run, std::min(cap, phase_remaining()));
  }

  uint64_t phase_remaining() const {
    return o.tier_work - result.report.two_phase_work - result.report.boundary_two_cell_work
           - result.report.boundary_multi_cell_work;
  }

  void limit(const std::string& reason) {
    result.report.exhausted = true;
    if (!reason.empty()
        && std::find(result.report.limits.begin(), result.report.limits.end(), reason) == result.report.limits.end()) {
      result.report.limits.push_back(reason);
    }
  }

  struct Choice {
    Endpoint_solution    solution;
    std::vector<Xsignal> interface;
    uint64_t             bytes;
  };
  uint32_t            choice_limit;
  uint64_t            choice_bytes = 0;
  std::vector<Choice> choices;

  void retain_choice(const Endpoint_solution& candidate, std::vector<Xsignal> inputs, Budget& work) {
    if (!choice_limit) {
      return;
    }
    uint64_t copy_work = candidate.cells.size() + (candidate.functional_basis ? candidate.functional_basis->size() : 0);
    uint64_t bytes     = 4 * sizeof(Choice) + candidate.name.capacity() + candidate.origin.capacity()
                         + candidate.cells.capacity() * sizeof(Endpoint_cell) + inputs.capacity() * sizeof(Xsignal);
    if (candidate.functional_basis) {
      bytes += candidate.functional_basis->capacity() * sizeof(Id);
    }
    for (const auto& cell : candidate.cells) {
      const auto& f  = *cell.function;
      copy_work     += cell.producers.size();
      // Conservatively charge shared function payload once per retained cell.
      bytes         += sizeof(Endpoint_function) + 4 * sizeof(void*) + cell.producers.capacity() * sizeof(int32_t)
                       + (f.inputs.capacity() + f.boundary.capacity()) * sizeof(Id) + f.table.words.capacity() * sizeof(uint64_t)
                       + f.formula.nodes.capacity() * sizeof(Gate_formula::Node);
    }
    if (!work.spend(copy_work + inputs.size() * (2 + std::bit_width(inputs.size())) + choices.size() * (inputs.size() + 1))) {
      limit("endpoint choice retention work budget");
      return;
    }
    std::sort(inputs.begin(), inputs.end());
    inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());
    size_t replace = choices.size();
    for (size_t i = 0; i < choices.size(); ++i) {
      if (choices[i].interface == inputs) {
        // An identical demanded static interface has identical sharing and
        // inversion cost. Keep only its cheapest DOMINO implementation.
        if (choices[i].solution.domino_cost <= candidate.domino_cost) {
          return;
        }
        replace = i;
        break;
      }
    }
    if (replace == choices.size() && choices.size() == choice_limit) {
      replace = std::max_element(choices.begin(),
                                 choices.end(),
                                 [](const auto& a, const auto& b) { return a.solution.total_cost() < b.solution.total_cost(); })
                - choices.begin();
      if (candidate.total_cost() >= choices[replace].solution.total_cost()) {
        return;
      }
    }
    const auto old_bytes = replace < choices.size() ? choices[replace].bytes : 0;
    if (bytes > o.boundary_bytes - (choice_bytes - old_bytes)) {
      limit("endpoint choice retention memory budget");
      return;
    }
    choice_bytes = choice_bytes - old_bytes + bytes;
    Choice next{candidate, std::move(inputs), bytes};
    if (replace == choices.size()) {
      choices.push_back(std::move(next));
    } else {
      choices[replace] = std::move(next);
    }
  }

  Endpoint_result finish(std::string reason = {}) {
    if (!total.available()) {
      limit("total endpoint budget");
    }
    result.status = result.selected ? Status::feasible : Status::search_exhausted;
    result.reason = result.selected ? "legal endpoint selected" : reason.empty() ? "no legal endpoint found" : std::move(reason);
    for (auto& choice : choices) {
      result.choices.push_back(std::move(choice.solution));
    }
    return std::move(result);
  }

  bool walk_static(std::span<const Xsignal> roots, Static_ledger& ledger, Budget& work, const Static_ledger* base = nullptr) {
    if (roots.size() > o.cost_nodes) {
      limit("static root budget");
      return false;
    }
    std::vector<Xsignal> todo(roots.begin(), roots.end());
    while (!todo.empty()) {
      if (!work.spend()) {
        limit("static accounting work budget");
        return false;
      }
      const auto s = todo.back();
      todo.pop_back();
      if (s.id == 0) {
        continue;
      }
      if (s.inverted && !free_rails.contains(s.id) && (!base || !base->inverted.contains(s.id))) {
        ledger.inverted.insert(s.id);
      }
      if ((base && base->visited.contains(s.id)) || ledger.visited.contains(s.id)) {
        continue;
      }
      if (ledger.visited.size() + (base ? base->visited.size() : 0) >= o.cost_nodes) {
        limit("static accounting node budget");
        return false;
      }
      ledger.visited.insert(s.id);
      ++result.report.cost_visits;
      const auto& n = g.node(s.id);
      if (n.kind == Xag::Kind::and_gate || n.kind == Xag::Kind::xor_gate) {
        ledger.cost += n.kind == Xag::Kind::and_gate ? o.cost.static_and : o.cost.static_xor;
        ++ledger.gates;
        todo.insert(todo.end(), n.inputs.begin(), n.inputs.end());
      }
    }
    return true;
  }

  bool price(Endpoint_solution& candidate) {
    if (candidate.cells.size() > cell_limit) {
      return false;
    }
    Work_slice           slice(*active, o.candidate_work);
    // The outside cone is immutable. Walk only candidate-specific additions;
    // an opposite-polarity request can still add an inverter at an outside node.
    Static_ledger        ledger;
    std::vector<Xsignal> inputs;
    // Candidates may be repriced while choosing a mixed producer subset.
    // Recompute the complete ledger rather than accumulating old estimates.
    candidate.domino_cost = 0;
    candidate.whole_cone  = true;
    for (const auto& cell : candidate.cells) {
      const auto& f          = *cell.function;
      candidate.domino_cost += uint64_t{f.metrics.transistors} + (cell.latch ? o.cost.domino_latch : o.cost.domino);
      for (size_t i = 0; i < f.inputs.size(); ++i) {
        if (cell.producers[i] >= 0) {
          continue;
        }
        const auto id = f.inputs[i];
        inputs.push_back({id, (f.metrics.negative & (1U << i)) != 0});
        candidate.whole_cone &= g.node(id).kind == Xag::Kind::source;
      }
    }
    if (!walk_static(inputs, ledger, slice.work, &external)) {
      return false;
    }
    candidate.static_cost    = external.cost + ledger.cost;
    candidate.inverter_cost  = (external.inverted.size() + ledger.inverted.size()) * uint64_t{o.cost.static_not};
    candidate.residual_nodes = ledger.gates;
    retain_choice(candidate, std::move(inputs), slice.work);
    return true;
  }

  bool accept_priced(Endpoint_solution candidate, bool absorption_tie = true) {
    if (!result.selected || candidate.total_cost() < result.selected->total_cost()
        || (absorption_tie && candidate.total_cost() == result.selected->total_cost() && candidate.whole_cone
            && !result.selected->whole_cone)) {
      result.selected = std::move(candidate);
      return true;
    }
    return false;
  }

  bool consider(Endpoint_solution candidate, bool absorption_tie = true) {
    return price(candidate) && accept_priced(std::move(candidate), absorption_tie);
  }

  bool fast_done() const { return fast_eligible && result.selected && result.selected->whole_cone; }

  std::shared_ptr<Cached_analysis> analyze(const Xag_window& window, Budget& work) {
    if (!work.spend(window.leaves.size() + 1)) {
      limit("function lookup budget");
      return {};
    }
    Function_key key{window.root, window.leaves};
    if (const auto found = cache.find(key); found != cache.end()) {
      ++result.report.analysis_hits;
      return found->second;
    }
    auto truth = window_function(g, window, work);
    if (truth.status != Status::feasible) {
      limit("window truth-table budget");
      return {};
    }
    ++result.report.analysis_tables;
    auto entry       = std::make_shared<Cached_analysis>();
    entry->table     = std::move(truth.table);
    const auto bytes = sizeof(Function_key) + sizeof(Cached_analysis) + 8 * sizeof(void*) + key.leaves.capacity() * sizeof(Id)
                       + entry->table.words.capacity() * sizeof(uint64_t);
    if (cache.size() < o.gate_cache_entries && bytes <= o.gate_cache_bytes - cache_bytes) {
      entry->retained = true;
      cache.emplace(std::move(key), entry);
      cache_bytes                        += bytes;
      result.report.analysis_cache_bytes  = cache_bytes;
    }
    return entry;
  }

  std::shared_ptr<const Endpoint_function> function(const Xag_window& window) {
    Work_slice slice(*active, o.candidate_work);
    const auto analysis = analyze(window, slice.work);
    if (!analysis) {
      return {};
    }
    if (analysis->gate_ready) {
      ++result.report.function_hits;
      return analysis->gate;
    }
    ++result.report.functions;
    auto gate = synthesize_gate(analysis->table, o.gates, slice.work, o.functions);
    if (gate.search_exhausted) {
      limit("gate factoring budget");
    }
    std::shared_ptr<Endpoint_function> f;
    if (gate.formula) {
      f           = std::make_shared<Endpoint_function>();
      f->root     = window.root;
      f->boundary = window.leaves;
      f->formula  = std::move(*gate.formula);
      std::array<uint32_t, max_logical_inputs> rank{};
      for (uint32_t j = 0; j < window.leaves.size(); ++j) {
        if (gate.cost.support & (1U << j)) {
          rank[j] = static_cast<uint32_t>(f->inputs.size());
          f->inputs.push_back(window.leaves[j]);
        }
      }
      for (auto& node : f->formula.nodes) {
        if (node.kind == Gate_formula::Kind::literal) {
          node.variable = rank[node.variable];
        }
      }
      f->metrics = *f->formula.metrics();
      f->table   = Truth_table(static_cast<uint32_t>(f->inputs.size()));
      for (uint32_t x = 0; x < (1U << f->table.inputs); ++x) {
        if (!slice.work.spend(window.leaves.size() + 1)) {
          limit("support compaction budget");
          return {};
        }
        uint32_t original = 0;
        for (uint32_t j = 0; j < window.leaves.size(); ++j) {
          if (gate.cost.support & (1U << j)) {
            original |= ((x >> rank[j]) & 1) << j;
          }
        }
        f->table.set(x, analysis->table.get(original));
      }
    }
    const uint64_t bytes
        = f ? sizeof(Endpoint_function) + 4 * sizeof(void*) + (f->inputs.capacity() + f->boundary.capacity()) * sizeof(Id)
                  + f->table.words.capacity() * sizeof(uint64_t) + f->formula.nodes.capacity() * sizeof(Gate_formula::Node)
            : 0;
    // A later tier may have more work available. Do not turn a resource
    // refusal into a cached claim that this boundary cannot be implemented.
    if (analysis->retained && (f || (!slice.work.exhausted && !gate.search_exhausted))
        && bytes <= o.gate_cache_bytes - cache_bytes) {
      analysis->gate                      = f;
      analysis->gate_ready                = true;
      cache_bytes                        += bytes;
      result.report.analysis_cache_bytes  = cache_bytes;
    }
    return f;
  }

  std::optional<Endpoint_cell> functional_cell(const Truth_table& table, std::span<const Id> inputs,
                                               std::span<const int32_t> producers, std::optional<Xsignal> represented, bool latch,
                                               Budget& work) {
    ++result.report.functions;
    auto gate = synthesize_gate(table, o.gates, work, o.functions);
    if (gate.search_exhausted) {
      limit("functional-cell factoring budget");
    }
    if (!gate.formula) {
      return {};
    }
    auto f     = std::make_shared<Endpoint_function>();
    f->root    = represented;
    f->formula = std::move(*gate.formula);
    Endpoint_cell                            cell{f, {}, latch ? o.clock_phases : 1, latch};
    std::vector<uint32_t>                    positions;
    std::array<uint32_t, max_logical_inputs> rank{};
    for (uint32_t i = 0; i < inputs.size(); ++i) {
      if (gate.cost.support & (1U << i)) {
        rank[i] = positions.size();
        positions.push_back(i);
        f->inputs.push_back(inputs[i]);
        cell.producers.push_back(producers[i]);
      }
    }
    for (auto& node : f->formula.nodes) {
      if (node.kind == Gate_formula::Kind::literal) {
        node.variable = rank[node.variable];
      }
    }
    f->metrics = *f->formula.metrics();
    f->table   = Truth_table(positions.size());
    for (uint32_t x = 0; x < (1U << positions.size()); ++x) {
      if (!work.spend(positions.size() + 1)) {
        limit("functional-cell compaction budget");
        return {};
      }
      uint32_t original = 0;
      for (uint32_t j = 0; j < positions.size(); ++j) {
        original |= ((x >> j) & 1) << positions[j];
      }
      f->table.set(x, table.get(original));
    }
    return cell;
  }

  void evaluate_functional_top(const Endpoint_solution& base, const Truth_table& table, std::span<const Id> inputs,
                               std::span<const int32_t> producers, Budget& work, std::span<const Id> static_producers = {},
                               uint64_t* rank_cost = nullptr) {
    auto top = functional_cell(table, inputs, producers, root, true, work);
    if (!top || !work.spend(base.cells.size())) {
      return;
    }
    auto                       candidate = base;
    std::vector<Endpoint_cell> live;
    std::vector<Id>            live_static;
    for (uint32_t i = 0; i < candidate.cells.size(); ++i) {
      if (std::find(top->producers.begin(), top->producers.end(), i) == top->producers.end()) {
        continue;
      }
      if (!candidate.cells[i].function) {
        return;  // completion still needs a divisor that could not be realized
      }
      for (auto& producer : top->producers) {
        if (producer == static_cast<int32_t>(i)) {
          producer = static_cast<int32_t>(live.size());
        }
      }
      live.push_back(std::move(candidate.cells[i]));
      if (!static_producers.empty()) {
        live_static.push_back(static_producers[i]);
      }
    }
    candidate.cells = std::move(live);
    if (candidate.cells.size() >= cell_limit && static_producers.empty()) {
      return;  // compare actual surviving producers after care/support reduction
    }
    candidate.cells.push_back(std::move(*top));
    if (!validate_endpoint(g, candidate, o, work)) {
      if (work.exhausted) {
        limit("functional composition validation budget");
      }
      return;
    }
    auto* parent = active;
    active       = &work;
    if (!live_static.empty()) {
      if (auto best = mixed_producers(candidate, rank_cost != nullptr, live_static); best && rank_cost) {
        *rank_cost = std::min(*rank_cost, best->total_cost());
      }
      active = parent;
      return;
    }
    const bool local = candidate.origin.starts_with("local-divisor");
    if (price(candidate)) {
      if (rank_cost) {
        *rank_cost = std::min(*rank_cost, candidate.total_cost());
      }
      const bool accepted          = accept_priced(std::move(candidate), !local);
      result.report.local_wins    += local && accepted;
      result.report.boundary_wins += rank_cost && accepted;
    }
    active = parent;
  }

  void evaluate_care_top(const Endpoint_solution& candidate, const Truth_table& table, const Truth_table& care,
                         std::span<const Id> inputs, std::span<const int32_t> producers, Budget& work,
                         std::span<const Id> static_producers = {}, uint64_t* rank_cost = nullptr) {
    const bool search_care = o.care_phases && care != Truth_table(table.inputs, true);
    {
      Work_slice baseline(work, unlimited_work, search_care ? 2 : 1);
      evaluate_functional_top(candidate, table, inputs, producers, baseline.work, static_producers, rank_cost);
    }
    if (!search_care || work.exhausted) {
      return;
    }
    Function_completions completions;
    {
      Work_slice completion_work(work, unlimited_work, 3);
      completions = function_completions(table, care, o.care_phases, completion_work.work);
    }
    result.report.completion_phases += completions.phases;
    if (completions.search_exhausted) {
      limit("care completion search budget");
    }
    for (size_t i = 0; i < completions.tables.size() && !work.exhausted; ++i) {
      if (completions.tables[i] == table) {
        continue;
      }
      ++result.report.completion_attempts;
      Work_slice trial(work, unlimited_work, completions.tables.size() - i);
      evaluate_functional_top(candidate, completions.tables[i], inputs, producers, trial.work, static_producers, rank_cost);
    }
  }

  void realize_decomposition(const Xag_window& window, const Decomposition& decomposition, Budget& work) {
    if (decomposition.divisors.size() == 1) {
      ++result.report.single_divisor_attempts;
    } else {
      ++result.report.parallel_divisor_attempts;
    }
    Endpoint_solution candidate;
    candidate.name             = name;
    candidate.root             = root;
    candidate.origin           = "functional-two-phase";
    candidate.functional_basis = window.leaves;
    std::vector<Id> bound;
    for (auto position : decomposition.bound) {
      bound.push_back(window.leaves[position]);
    }
    const std::vector<int32_t> native(bound.size(), -1);
    for (const auto& table : decomposition.divisors) {
      auto cell = functional_cell(table, bound, native, {}, false, work);
      if (!cell) {
        break;
      }
      candidate.cells.push_back(std::move(*cell));
    }
    if (candidate.cells.size() != decomposition.divisors.size()) {
      return;
    }
    std::vector<Id>      inputs(candidate.cells.size(), 0);
    std::vector<int32_t> producers;
    for (uint32_t i = 0; i < candidate.cells.size(); ++i) {
      producers.push_back(i);
    }
    for (auto position : decomposition.free) {
      inputs.push_back(window.leaves[position]);
      producers.push_back(-1);
    }
    // Establish the zero-filled incumbent before spending work on care
    // alternatives. Every completion is scored as a complete mixed network,
    // including rail costs and first-phase cells, not only its top formula.
    evaluate_care_top(candidate, decomposition.top, decomposition.top_care, inputs, producers, work);
  }

  void functional_decompositions(const Xag_window& window) {
    const auto n = window.leaves.size();
    if (n < 3 || n > 2 * o.gates.logical_inputs - 1) {
      return;
    }
    const auto analysis = analyze(window, *active);
    if (!analysis) {
      return;
    }
    {
      // A failed one-divisor search cannot consume the wider decomposition
      // allowance. The enclosing tier also reserves structural/removal work.
      Work_slice single(*active, unlimited_work, 2);
      auto*      parent = active;
      active            = &single.work;
      uint32_t attempts = 0;
      for (uint32_t mask = 1; mask < (1U << n) - 1 && !fast_done(); ++mask) {
        if (!active->spend()) {
          limit("single-divisor tier budget");
          break;
        }
        const auto count = static_cast<uint32_t>(std::popcount(mask));
        if (count < 2 || count > o.gates.logical_inputs || n - count + 1 > o.gates.logical_inputs) {
          continue;
        }
        if (attempts == o.divisor_partitions) {
          limit("new-divisor partition candidate budget");
          break;
        }
        ++attempts;
        ++result.report.new_divisor_attempts;
        ++result.report.two_phase_attempts;
        Work_slice slice(*active, o.candidate_work);
        auto       decomposition = decompose_function(analysis->table, mask, o.gates.logical_inputs, slice.work);
        if (decomposition.status != Status::feasible) {
          if (decomposition.status == Status::search_exhausted) {
            limit("new-divisor decomposition budget");
          }
          continue;
        }
        if (decomposition.divisors.size() == 1) {
          realize_decomposition(window, decomposition, slice.work);
          continue;
        }
        // Keep the already computed cofactor encoding and top care relation.
        // Only table construction happens before the one-divisor fast path;
        // wider physical factoring/completion waits until that path finishes.
        uint64_t bytes = 2 * sizeof(Pending_decomposition)
                         + (decomposition.bound.capacity() + decomposition.free.capacity()) * sizeof(uint32_t)
                         + decomposition.divisors.capacity() * sizeof(Truth_table)
                         + (decomposition.top.words.capacity() + decomposition.top_care.words.capacity()) * sizeof(uint64_t);
        for (const auto& table : decomposition.divisors) {
          bytes += table.words.capacity() * sizeof(uint64_t);
        }
        // Leave at least half the shared storage allowance for existing-divisor
        // boundaries, which run before these wider functional candidates.
        if (bytes > o.boundary_bytes / 2 - pending_bytes) {
          limit("pending decomposition memory budget");
          continue;
        }
        pending_bytes                        += bytes;
        result.report.deferred_divisor_bytes  = pending_bytes;
        // Only actual generation work is debited from the candidate cap. A
        // short discovery slice must not strand its reserved realization work.
        const auto generation_spent           = slice.work.consumed;
        pending.push_back({std::move(decomposition), o.candidate_work - generation_spent});
      }
      active = parent;
    }
  }

  void realize_pending_decompositions(const Xag_window& window) {
    for (const auto& candidate : pending) {
      if (!available()) {
        break;
      }
      // Table generation and later realization share the original per-candidate
      // cap. Deferral never grants a second full candidate work allowance.
      Work_slice slice(*active, candidate.remaining_work);
      realize_decomposition(window, candidate.decomposition, slice.work);
      if (fast_done()) {
        break;
      }
    }
  }

  // Normalize before applying the support cap: introducing an ancestor can
  // hide old leaves. No table is constructed until the reachable cut fits.
  std::optional<Xag_window> normalize_boundary(const Xag_window& window, const std::unordered_set<Id>& stops, Budget& work) {
    std::unordered_set<Id> visited;
    std::vector<Id>        todo{window.root.id}, leaves;
    while (!todo.empty()) {
      if (!work.spend()) {
        limit("boundary traversal budget");
        return std::nullopt;
      }
      const auto id = todo.back();
      todo.pop_back();
      if (id == 0 || visited.contains(id)) {
        continue;
      }
      if (visited.size() >= o.window.nodes) {
        limit("boundary traversal node budget");
        return std::nullopt;
      }
      visited.insert(id);
      if (stops.contains(id)) {
        leaves.push_back(id);
        if (leaves.size() > o.window.inputs) {
          return std::nullopt;
        }
      } else {
        const auto& n = g.node(id);
        if (n.kind == Xag::Kind::source) {
          return std::nullopt;
        }
        todo.push_back(n.inputs[0].id);
        todo.push_back(n.inputs[1].id);
      }
    }
    std::sort(leaves.begin(), leaves.end());
    auto next = collect_window(g, window.root, leaves, o.window, work);
    if (next.status != Status::feasible) {
      limit(next.reason);
      return std::nullopt;
    }
    return next;
  }

  // Shared leaves remain until their final inside use disappears. Merely
  // subtracting the collapsed nodes' supports is incorrect.
  std::optional<Xag_window> collapse(const Xag_window& window, std::span<const Id> inserted) {
    Work_slice slice(*active, o.candidate_work);
    if (!slice.work.spend(window.leaves.size() + inserted.size())) {
      limit("boundary contraction budget");
      return {};
    }
    std::unordered_set<Id> stops(window.leaves.begin(), window.leaves.end());
    stops.insert(inserted.begin(), inserted.end());
    return normalize_boundary(window, stops, slice.work);
  }

  // Expand related frontier signals together. A coordinated expansion can
  // fit even when every proper subset exceeds the input limit. Normalize
  // the cut through reachability: a shared signal is one input, and leaves
  // hidden behind another frontier signal must not remain as unused inputs.
  std::optional<Xag_window> expand(const Xag_window& window, std::span<const Id> selected) {
    Work_slice slice(*active, o.candidate_work);
    if (!slice.work.spend(window.leaves.size() * (selected.size() + 1))) {
      limit("boundary expansion budget");
      return {};
    }
    for (auto id : selected) {
      if (std::find(analysis_boundary.begin(), analysis_boundary.end(), id) != analysis_boundary.end()) {
        return {};  // this input's upstream implementation is outside the admitted joint window
      }
    }
    std::unordered_set<Id> stops;
    for (auto id : window.leaves) {
      if (std::find(selected.begin(), selected.end(), id) != selected.end()) {
        for (auto input : g.node(id).inputs) {
          if (input.id) {
            stops.insert(input.id);
          }
        }
      } else {
        stops.insert(id);
      }
    }
    return normalize_boundary(window, stops, slice.work);
  }

  // At most 16 candidates, fixed scratch space, no powerset search. Try entire
  // connected components first (cycles may need every branch), then connected
  // prefixes from each seed. The caller shares its move cap with single/pair
  // proposals, including duplicate and unsuccessful resulting cuts.
  template <typename Propose>
  void coordinated_boundaries(std::span<const Id> candidates, Propose&& propose) {
    std::array<Id, max_logical_inputs> nodes{};
    unsigned                           count = 0;
    for (auto id : candidates) {
      if (!active->spend()) {
        return;
      }
      if (id != root.id && g.node(id).kind != Xag::Kind::source) {
        nodes[count++] = id;
        if (count == nodes.size()) {
          break;
        }
      }
    }
    if (count < 3) {
      return;
    }
    std::array<uint32_t, max_logical_inputs> adjacent{};
    for (unsigned i = 0; i < count; ++i) {
      for (unsigned j = i + 1; j < count; ++j) {
        if (!active->spend(4)) {
          return;
        }
        for (auto a : g.node(nodes[i]).inputs) {
          for (auto b : g.node(nodes[j]).inputs) {
            if (a.id && a.id == b.id) {
              adjacent[i] |= 1U << j;
              adjacent[j] |= 1U << i;
            }
          }
        }
      }
    }
    std::array<uint32_t, max_logical_inputs * max_logical_inputs> seen{};
    unsigned                                                      seen_count = 0;
    const auto                                                    submit     = [&](uint32_t mask) {
      if (!active->spend(seen_count + count)) {
        return false;
      }
      if (std::find(seen.begin(), seen.begin() + seen_count, mask) != seen.begin() + seen_count) {
        return true;
      }
      // Each seed supplies at most count-2 masks including its component.
      seen[seen_count++] = mask;
      std::array<Id, max_logical_inputs> group{};
      unsigned                           size = 0;
      for (unsigned i = 0; i < count; ++i) {
        if (mask & (1U << i)) {
          group[size++] = nodes[i];
        }
      }
      return propose(std::span<const Id>(group.data(), size));
    };
    uint32_t visited = 0;
    for (unsigned start = 0; start < count; ++start) {
      if (visited & (1U << start)) {
        continue;
      }
      uint32_t component = 1U << start, unexplored = component;
      while (unexplored) {
        if (!active->spend()) {
          return;
        }
        auto index  = std::countr_zero(unexplored);
        unexplored &= unexplored - 1;
        unexplored |= adjacent[index] & ~component;
        component  |= adjacent[index];
      }
      visited |= component;
      if (std::popcount(component) < 3) {
        continue;
      }
      if (!submit(component)) {
        return;
      }
      for (unsigned seed = start; seed < count; ++seed) {
        if (!(component & (1U << seed))) {
          continue;
        }
        uint32_t group = 1U << seed;
        while (group != component) {
          unsigned best = count, links = 0;
          for (unsigned i = 0; i < count; ++i) {
            if (!active->spend()) {
              return;
            }
            const auto score = static_cast<unsigned>(std::popcount(adjacent[i] & group));
            if (!(group & (1U << i)) && score > links) {
              best  = i;
              links = score;
            }
          }
          if (best == count) {
            break;
          }
          group |= 1U << best;
          if (group != component && std::popcount(group) >= 3 && !submit(group)) {
            return;
          }
        }
      }
    }
  }

  struct Boundary_entry {
    Xag_window window;
    uint64_t   cost   = std::numeric_limits<uint64_t>::max();
    uint64_t   credit = 0, reconvergence = 0;
    uint32_t   spread     = 0;
    bool       contracted = false, grown = false;
  };

  void rank_boundary(Boundary_entry& entry, const Xag_window& base, bool wide) {
    // Reserve work for proposals and subsequent tiers. An inconclusive gate
    // attempt keeps a structural rank and may be retried through the same cache.
    Work_slice slice(*active, o.candidate_work, 4);
    auto*      parent                          = active;
    active                                     = &slice.work;
    const auto&                              w = entry.window;
    std::array<uint32_t, max_logical_inputs> uses{};
    for (auto id : w.interior) {
      if (!active->spend(3)) {
        limit("boundary ranking budget");
        break;
      }
      const auto& node = g.node(id);
      if (!external.visited.contains(id)) {
        entry.credit += node.kind == Xag::Kind::xor_gate ? o.cost.static_xor : o.cost.static_and;
      }
      for (auto input : node.inputs) {
        const auto it = std::lower_bound(w.leaves.begin(), w.leaves.end(), input.id);
        if (it != w.leaves.end() && *it == input.id) {
          ++uses[static_cast<size_t>(it - w.leaves.begin())];
        }
      }
    }
    uint32_t low = std::numeric_limits<uint32_t>::max(), high = 0;
    for (size_t i = 0; i < w.leaves.size(); ++i) {
      entry.reconvergence += uses[i] > 1 ? uses[i] - 1 : 0;
      low                  = std::min(low, g.node(w.leaves[i]).level);
      high                 = std::max(high, g.node(w.leaves[i]).level);
    }
    entry.spread = w.leaves.empty() ? 0 : high - low;
    if (available()) {
      if (auto candidate = build(w, base, false, false); candidate && price(*candidate)) {
        entry.cost                   = candidate->total_cost();
        // A constructed and priced legal solution survives frontier eviction or a
        // later budget refusal. Its exact gate was constructed on this cut.
        result.report.boundary_wins += accept_priced(std::move(*candidate));
      }
    }
    if (o.clock_phases == 2 && available()) {
      // The first round prices only two-cell realizations. The second round
      // admits wider realizations after the two-cell tier finishes. Each round
      // can use at most one eighth of the shared phase allowance.
      auto&      used = wide ? result.report.boundary_multi_cell_work : result.report.boundary_two_cell_work;
      Work_slice phase_slice(*active, std::min(o.tier_work / 8 - used, phase_remaining()), 2);
      if (phase_slice.work.has()) {  // a nonzero allowance
        auto* rank_work           = active;
        active                    = &phase_slice.work;
        const auto old_cell_limit = cell_limit;
        cell_limit                = wide ? std::numeric_limits<size_t>::max() : 2;
        const bool care           = o.care_phases && w.leaves != base.leaves;
        {
          Work_slice structural(*active, unlimited_work, care ? 2 : 1);
          active = &structural.work;
          ++result.report.two_phase_attempts;
          if (auto candidate = build(w, base, true, true); candidate && candidate->cells.size() >= 2 && price(*candidate)) {
            entry.cost                   = std::min(entry.cost, candidate->total_cost());
            result.report.boundary_wins += accept_priced(std::move(*candidate));
          }
          if (available()) {
            if (auto candidate = build(w, base, true, false, true); candidate && candidate->cells.size() >= 2) {
              // mixed_producers already priced and published the winner.
              entry.cost = std::min(entry.cost, candidate->total_cost());
            }
          }
          active = &phase_slice.work;
        }
        if (care && available()) {
          // Compare full absorption and mixed static/phase-one inputs. Care
          // simplification may remove proposed producers before the cell cap.
          {
            Work_slice covered(*active, unlimited_work, 2);
            active = &covered.work;
            care_boundary(w, base, true, true, false, &entry.cost);
            active = &phase_slice.work;
          }
          if (available()) {
            care_boundary(w, base, true, false, false, &entry.cost);
          }
        }
        cell_limit  = old_cell_limit;
        used       += phase_slice.work.consumed;
        active      = rank_work;
      }
    }
    if (o.care_phases && available() && w.leaves != base.leaves) {
      // Static correlated inputs are also useful in one-phase mode. This work
      // belongs only to the enclosing boundary tier, not the phase allowance.
      Work_slice care(*active, unlimited_work, 2);
      active = &care.work;
      care_boundary(w, base, false, false, false, &entry.cost);
      active = &slice.work;
    }
    active = parent;
  }

  // Alternate actual mixed-network cost with reconvergence/asymmetry ranks.
  // These latter lanes retain bridges whose intermediate gate is unattractive
  // or inconclusive; no structural score establishes Boolean legality.
  static bool better_boundary(const Boundary_entry& a, const Boundary_entry& b, unsigned lane) {
    if (lane == 1 && a.reconvergence != b.reconvergence) {
      return a.reconvergence > b.reconvergence;
    }
    if (lane == 2 && a.spread != b.spread) {
      return a.spread > b.spread;
    }
    if (a.cost != b.cost) {
      return a.cost < b.cost;
    }
    if (a.credit != b.credit) {
      return a.credit > b.credit;
    }
    if (a.reconvergence != b.reconvergence) {
      return a.reconvergence > b.reconvergence;
    }
    if (a.spread != b.spread) {
      return a.spread > b.spread;
    }
    return a.window.leaves < b.window.leaves;
  }

  std::vector<Xag_window> enumerate_boundaries(const Xag_window& initial, bool wide = false) {
    const auto byte_limit = o.boundary_bytes - pending_bytes;
    const auto bytes      = [](const Xag_window& w) {
      return sizeof(Boundary_entry) + 16 * sizeof(void*) + (w.interior.capacity() + w.leaves.capacity()) * sizeof(Id)
             + w.reason.capacity();
    };
    const auto key_bytes
        = [](const std::vector<Id>& leaves) { return sizeof(std::vector<Id>) + 8 * sizeof(void*) + leaves.size() * sizeof(Id); };
    if (bytes(initial) + key_bytes(initial.leaves) > byte_limit) {
      limit("boundary memory budget");
      return {};
    }
    using Entry                          = std::shared_ptr<Boundary_entry>;
    auto first                           = std::make_shared<Boundary_entry>();
    first->window                        = initial;
    uint64_t                  used_bytes = bytes(first->window) + key_bytes(initial.leaves), held_bytes = 0;
    std::vector<Entry>        frontier{first};
    std::set<std::vector<Id>> seen{initial.leaves};
    const auto                peak
        = [&] { result.report.boundary_bytes_peak = std::max(result.report.boundary_bytes_peak, used_bytes + held_bytes); };
    peak();
    if (o.boundaries == 1) {
      result.report.boundaries = 1;
      return {initial};
    }
    rank_boundary(*first, initial, wide);
    Entry      seed;
    const auto retain = [&](Xag_window next) -> Entry {
      if (!active->spend(next.leaves.size() + 1) || seen.contains(next.leaves)) {
        return {};
      }
      const auto key_size = key_bytes(next.leaves);
      const auto payload  = bytes(next);
      // Include the new proposal before replacing a retained record. Never
      // assume an eviction will pay for temporarily overlapping live payloads.
      if (key_size + payload > byte_limit - used_bytes - held_bytes) {
        limit("boundary memory budget");
        return {};
      }
      used_bytes += key_size;
      seen.insert(next.leaves);
      auto entry    = std::make_shared<Boundary_entry>();
      entry->window = std::move(next);
      rank_boundary(*entry, initial, wide);
      used_bytes += payload;
      peak();
      frontier.push_back(entry);
      if (frontier.size() <= o.boundaries) {
        return entry;
      }

      // The initial admitted window and the unexpanded root seed are anchors.
      // Fill the other slots from cost, reconvergence and asymmetric-depth lanes.
      std::vector<Entry> kept{first};
      if (seed && !seed->grown) {
        kept.push_back(seed);
      }
      std::unordered_set<const Boundary_entry*> kept_set;
      for (const auto& anchor : kept) {
        kept_set.insert(anchor.get());
      }
      unsigned slot = 0;
      while (kept.size() < o.boundaries) {
        if (!active->spend(frontier.size())) {
          limit("boundary ranking budget");
          break;
        }
        Entry          best;
        const unsigned lane = slot % 4 == 3 ? 2 : slot % 2;
        ++slot;
        for (const auto& candidate : frontier) {
          if (kept_set.contains(candidate.get())) {
            continue;
          }
          if (!best || better_boundary(*candidate, *best, lane)) {
            best = candidate;
          }
        }
        if (!best) {
          break;
        }
        kept.push_back(best);
        kept_set.insert(best.get());
      }
      // If ranking exhausted, keep the existing frontier instead of replacing
      // it with an incomplete ranking. The endpoint incumbent is separate too.
      if (kept.size() < o.boundaries) {
        frontier.pop_back();
        used_bytes -= payload;
        return {};
      }
      const bool accepted = kept_set.contains(entry.get());
      for (const auto& old : frontier) {
        if (!kept_set.contains(old.get())) {
          used_bytes -= bytes(old->window);
        }
      }
      frontier                             = std::move(kept);
      result.report.boundary_replacements += accepted;
      return accepted ? entry : Entry{};
    };
    const auto pick = [&](bool growth, unsigned turn) -> Entry {
      Entry          best;
      const unsigned lane = turn % 4 == 3 ? 2 : turn % 2;
      for (const auto& entry : frontier) {
        if (!active->spend()) {
          return {};
        }
        if (growth ? entry->grown : entry->contracted) {
          continue;
        }
        if (!best || better_boundary(*entry, *best, lane)) {
          best = entry;
        }
      }
      return best;
    };
    // Proposal count is distinct from retained count. Each direction has at
    // most 4*boundaries attempts in the original round and 2*boundaries in
    // the wide round. Duplicate and failed moves count against these limits.
    const uint64_t direction_limit = uint64_t{o.boundaries} * (wide ? 2 : 4);
    if (root.id && g.node(root.id).kind != Xag::Kind::source && available()) {
      Xag_window root_seed;
      root_seed.root   = root;
      root_seed.leaves = {root.id};
      ++result.report.boundary_trials;
      if (auto expanded = expand(root_seed, std::array{root.id})) {
        seed = retain(std::move(*expanded));
      }
    }
    {
      Work_slice slice(*active, unlimited_work, 2);
      auto*      parent = active;
      active            = &slice.work;
      uint64_t trials   = 0;
      for (unsigned turn = 0; available() && trials < direction_limit; ++turn) {
        auto current = pick(false, turn);
        if (!current) {
          break;
        }
        current->contracted = true;
        held_bytes          = bytes(current->window);
        if (held_bytes > byte_limit - used_bytes) {
          held_bytes = 0;
          limit("boundary memory budget");
          break;
        }
        peak();
        const auto propose = [&](std::span<const Id> group) {
          if (!available() || trials >= direction_limit) {
            return false;
          }
          ++trials;
          ++result.report.boundary_trials;
          if (auto next = collapse(current->window, group)) {
            retain(std::move(*next));
          }
          return available() && trials < direction_limit;
        };
        coordinated_boundaries(current->window.interior, propose);
        for (auto id : current->window.interior) {
          if (id == root.id) {
            continue;
          }
          if (!available() || trials >= direction_limit) {
            break;
          }
          propose(std::array{id});
        }
        held_bytes = 0;
      }
      if (trials == direction_limit) {
        limit("boundary contraction candidate budget");
      }
      if (!available()) {
        limit("boundary contraction tier budget");
      }
      active = parent;
    }
    uint64_t trials = 0;
    for (unsigned turn = 0; available() && trials < direction_limit; ++turn) {
      auto current = seed && !seed->grown ? seed : pick(true, turn);
      if (!current) {
        break;
      }
      current->grown = true;
      held_bytes     = bytes(current->window);
      if (held_bytes > byte_limit - used_bytes) {
        held_bytes = 0;
        limit("boundary memory budget");
        break;
      }
      peak();
      const auto propose = [&](std::span<const Id> group) {
        if (!available() || trials >= direction_limit) {
          return false;
        }
        ++trials;
        ++result.report.boundary_trials;
        if (auto next = expand(current->window, group)) {
          retain(std::move(*next));
        }
        return available() && trials < direction_limit;
      };
      coordinated_boundaries(current->window.leaves, propose);
      for (size_t j = 0; j < current->window.leaves.size() && available() && trials < direction_limit; ++j) {
        const auto a = current->window.leaves[j];
        if (g.node(a).kind == Xag::Kind::source) {
          continue;
        }
        for (size_t k = j + 1; k < current->window.leaves.size() && available() && trials < direction_limit; ++k) {
          if (!active->spend()) {
            break;
          }
          const auto b = current->window.leaves[k];
          if (g.node(b).kind == Xag::Kind::source) {
            continue;
          }
          const auto& ai = g.node(a).inputs;
          const auto& bi = g.node(b).inputs;
          if (ai[0].id == bi[0].id || ai[0].id == bi[1].id || ai[1].id == bi[0].id || ai[1].id == bi[1].id) {
            propose(std::array{a, b});
          }
        }
      }
      for (auto id : current->window.leaves) {
        if (!active->spend() || trials >= direction_limit) {
          break;
        }
        if (g.node(id).kind != Xag::Kind::source) {
          propose(std::array{id});
        }
      }
      held_bytes = 0;
      seed.reset();  // an evicted root seed must not retain an unaccounted payload
    }
    if (trials == direction_limit) {
      limit("boundary growth candidate budget");
    }
    std::vector<Xag_window> result_windows;
    for (auto& entry : frontier) {
      result_windows.push_back(std::move(entry->window));
    }
    result.report.boundaries = result_windows.size();
    return result_windows;
  }

  void structural_decompositions(std::span<const Xag_window> boundaries, const Xag_window& window) {
    for (const auto& boundary : boundaries) {
      if (!available()) {
        break;
      }
      ++result.report.two_phase_attempts;
      if (auto candidate = build(boundary, window, true, true)) {
        consider(std::move(*candidate));
      }
      if (o.care_phases && available()) {
        care_boundary(boundary, window, true, true);
      }
      if (fast_done()) {
        break;
      }
    }
  }

  // Compare all-static and all-available-producer seeds, then monotonically
  // add/remove producers by their actual shared-network gain. At most two
  // triangular sweeps over <=16 producers, not a powerset search. Starting at
  // both ends keeps jointly profitable producers whose individual gain is poor.
  std::optional<Endpoint_solution> mixed_producers(const Endpoint_solution& all, bool ranking,
                                                   std::span<const Id> static_producers = {}) {
    if (all.cells.empty() || all.cells.size() > max_logical_inputs + 1
        || (all.functional_basis && static_producers.size() + 1 != all.cells.size())) {
      return {};
    }
    Work_slice slice(*active, o.candidate_work);
    auto*      parent        = active;
    active                   = &slice.work;
    const auto     count     = static_cast<unsigned>(all.cells.size() - 1);
    const uint32_t full      = (1U << count) - 1;
    uint64_t       copy_work = all.cells.size();
    for (const auto& cell : all.cells) {
      copy_work += cell.producers.size();
    }
    if (all.functional_basis) {
      const auto& top  = *all.cells.back().function;
      copy_work       += all.functional_basis->size() + top.table.words.size() + top.formula.nodes.size() + top.boundary.size();
    }
    std::optional<Endpoint_solution> best;
    const auto                       better = [](const Endpoint_solution& a, const Endpoint_solution& b) {
      return a.total_cost() < b.total_cost() || (a.total_cost() == b.total_cost() && a.whole_cone && !b.whole_cone);
    };
    const auto evaluate = [&](uint32_t mask) -> std::optional<Endpoint_solution> {
      if (static_cast<size_t>(std::popcount(mask)) >= cell_limit || !active->spend(copy_work)) {
        return {};
      }
      Endpoint_solution candidate;
      candidate.name             = all.name;
      candidate.root             = all.root;
      candidate.origin           = all.origin;
      candidate.functional_basis = all.functional_basis;
      std::array<int32_t, max_logical_inputs> bindings;
      bindings.fill(-1);
      for (unsigned i = 0; i < count; ++i) {
        if (mask & (1U << i)) {
          bindings[i] = static_cast<int32_t>(candidate.cells.size());
          candidate.cells.push_back(all.cells[i]);
        }
      }
      auto                               endpoint = all.cells.back();
      std::shared_ptr<Endpoint_function> remapped;
      if (all.functional_basis && mask != full) {
        remapped          = std::make_shared<Endpoint_function>(*endpoint.function);
        endpoint.function = remapped;
      }
      for (size_t i = 0; i < endpoint.producers.size(); ++i) {
        auto& producer = endpoint.producers[i];
        if (producer >= 0) {
          if (remapped && bindings[producer] < 0) {
            // A demoted existing divisor regains its native identity. Its
            // table/care relation and the independent basis remain unchanged.
            remapped->inputs[i] = static_producers[producer];
          }
          producer = bindings[producer];
        }
      }
      candidate.cells.push_back(std::move(endpoint));
      if (all.functional_basis && !validate_endpoint(g, candidate, o, *active)) {
        if (!available()) {
          limit("mixed functional composition budget");
        }
        return {};
      }
      if (!price(candidate)) {
        return {};
      }
      if (!best || better(candidate, *best)) {
        // Preserve each legal improvement before the next budget checkpoint.
        best                         = candidate;
        const bool local             = candidate.origin.starts_with("local-divisor");
        const bool accepted          = accept_priced(candidate, !local);
        result.report.boundary_wins += ranking && accepted;
        result.report.local_wins    += local && accepted;
      }
      return candidate;
    };
    // Preserve both seeds before either greedy sweep can consume the budget.
    auto all_converted = evaluate(full);
    auto all_static    = evaluate(0);
    for (bool remove : {false, true}) {
      uint32_t selected  = remove ? full : 0;
      auto     incumbent = remove ? std::move(all_converted) : std::move(all_static);
      if (!incumbent) {
        continue;
      }
      for (unsigned step = 0; step < count && available(); ++step) {
        uint32_t next_mask = selected;
        auto     next      = *incumbent;
        for (unsigned i = 0; i < count && available(); ++i) {
          if (bool(selected & (1U << i)) != remove) {
            continue;
          }
          const auto mask = selected ^ (1U << i);
          if (auto candidate = evaluate(mask); candidate && better(*candidate, next)) {
            next      = std::move(*candidate);
            next_mask = mask;
          }
        }
        if (next_mask == selected) {
          break;
        }
        incumbent = std::move(next);
        selected  = next_mask;
      }
    }
    if (!available()) {
      limit("mixed producer selection budget");
    }
    active = parent;
    return best;
  }

  std::optional<Endpoint_solution> build(const Xag_window& boundary, const Xag_window& base, bool two_phase, bool require_covered,
                                         bool rank_mixed = false) {
    auto top = function(boundary);
    if (!top) {
      return std::nullopt;
    }
    Endpoint_solution candidate;
    candidate.name   = name;
    candidate.root   = root;
    candidate.origin = require_covered ? "two-phase" : two_phase ? "mixed-residual" : "residual";
    Endpoint_cell                endpoint{top, std::vector<int32_t>(top->inputs.size(), -1), o.clock_phases, true};
    const std::unordered_set<Id> base_leaves(base.leaves.begin(), base.leaves.end());
    if (two_phase && require_covered) {
      const auto producers
          = std::count_if(top->inputs.begin(), top->inputs.end(), [&](auto id) { return !base_leaves.contains(id); });
      if (static_cast<size_t>(producers) >= cell_limit) {
        return {};  // defer wider realizations before factoring their producers
      }
    }
    for (size_t j = 0; j < top->inputs.size(); ++j) {
      const auto id = top->inputs[j];
      if (!two_phase || base_leaves.contains(id)) {
        continue;
      }
      Xag_window subwindow;
      {
        Work_slice slice(*active, o.candidate_work);
        subwindow = collect_subwindow(g, {id, false}, base.leaves, o.window, slice.work);
        if (subwindow.status == Status::search_exhausted) {
          limit(subwindow.reason);
        }
      }
      auto sub = subwindow.status == Status::feasible ? function(subwindow) : nullptr;
      if (!sub) {
        if (require_covered) {
          return std::nullopt;
        }
        continue;
      }
      endpoint.producers[j] = static_cast<int32_t>(candidate.cells.size());
      candidate.cells.push_back({sub, std::vector<int32_t>(sub->inputs.size(), -1), 1, false});
    }
    candidate.cells.push_back(std::move(endpoint));
    if (two_phase && !require_covered && candidate.cells.size() > 1) {
      return mixed_producers(candidate, rank_mixed);
    }
    return candidate;
  }

  bool retain_image_bytes(uint64_t bytes) {
    if (divisor_cache.size() + image_cache.size() >= o.image_cache_entries || bytes > o.image_cache_bytes - image_bytes) {
      return false;
    }
    image_bytes                     += bytes;
    result.report.image_cache_bytes  = image_bytes;
    return true;
  }

  std::shared_ptr<const Cached_divisor> divisor_analysis(Id id, const Xag_window& base, Budget& work) {
    if (!work.spend(base.leaves.size() + 1)) {
      limit("divisor lookup budget");
      return {};
    }
    Function_key key{
        {id, false},
        base.leaves
    };
    if (const auto found = divisor_cache.find(key); found != divisor_cache.end()) {
      return found->second;
    }
    auto window = collect_subwindow(g, {id, false}, base.leaves, o.window, work);
    if (window.status != Status::feasible) {
      if (window.status == Status::search_exhausted) {
        limit(window.reason);
      }
      return {};
    }
    const auto analysis = analyze(window, work);
    if (!analysis) {
      return {};
    }
    auto lifted = lift_function(analysis->table, window.leaves, base.leaves, work);
    if (lifted.status != Status::feasible) {
      limit("divisor image lifting budget");
      return {};
    }
    auto entry       = std::make_shared<Cached_divisor>();
    entry->window    = std::move(window);
    entry->table     = std::move(lifted.table);
    const auto bytes = sizeof(Function_key) + sizeof(Cached_divisor) + 8 * sizeof(void*)
                       + (key.leaves.capacity() + entry->window.leaves.capacity() + entry->window.interior.capacity()) * sizeof(Id)
                       + entry->window.reason.capacity() + entry->table.words.capacity() * sizeof(uint64_t);
    if (retain_image_bytes(bytes)) {
      divisor_cache.emplace(std::move(key), entry);
    }
    return entry;
  }

  std::shared_ptr<const Divisor_function> divisor_image(std::span<const Id> ids, const Xag_window& base, Budget& work) {
    if (!work.spend(ids.size() + base.leaves.size() + 1)) {
      limit("divisor image lookup budget");
      return {};
    }
    Image_key key{
        base.root,
        base.leaves,
        {ids.begin(), ids.end()}
    };
    if (const auto found = image_cache.find(key); found != image_cache.end()) {
      ++result.report.divisor_image_hits;
      return found->second;
    }
    const auto root_analysis = analyze(base, work);
    if (!root_analysis) {
      return {};
    }
    std::vector<Truth_table> divisors;
    for (auto id : ids) {
      const auto divisor = divisor_analysis(id, base, work);
      if (!divisor || !work.spend(divisor->table.words.size())) {
        return {};
      }
      divisors.push_back(divisor->table);
    }
    auto image = std::make_shared<Divisor_function>(derive_divisor_function(root_analysis->table, divisors, work, true));
    if (image->status == Status::search_exhausted) {
      limit("divisor image construction budget");
      return {};
    }
    ++result.report.divisor_images;
    // Exact dependence conflicts can be reused; resource refusals cannot.
    if (image->status == Status::feasible || image->status == Status::unsupported) {
      const auto bytes = sizeof(Image_key) + sizeof(Divisor_function) + 8 * sizeof(void*)
                         + (key.basis.capacity() + key.divisors.capacity()) * sizeof(Id)
                         + (image->function.words.capacity() + image->care.words.capacity()) * sizeof(uint64_t);
      if (retain_image_bytes(bytes)) {
        image_cache.emplace(std::move(key), image);
      }
    }
    return image;
  }

  void local_divisors(const Xag_window& base) {
    if (!result.selected
        || result.selected->total_cost()
               <= external.cost + external.inverted.size() * uint64_t{o.cost.static_not} + o.cost.domino_latch) {
      return;
    }
    const auto better = [&](Id a, Id b) {
      const auto& na       = g.node(a);
      const auto& nb       = g.node(b);
      // Sources have no implementation to reuse. Their often-large fanout
      // must not evict already-paid shared gates from a small divisor pool.
      const bool  shared_a = external.visited.contains(a) && na.kind != Xag::Kind::source;
      const bool  shared_b = external.visited.contains(b) && nb.kind != Xag::Kind::source;
      if (shared_a != shared_b) {
        return shared_a;
      }
      if (na.fanouts != nb.fanouts) {
        return na.fanouts > nb.fanouts;
      }
      if (na.level != nb.level) {
        return na.level > nb.level;
      }
      return a < b;
    };
    // Keep a bounded pool while visiting the admitted cone once. In this heap
    // the least promising retained divisor is at the top.
    std::priority_queue<Id, std::vector<Id>, decltype(better)> retained(better);
    const auto                                                 collect = [&](std::span<const Id> ids) {
      for (auto id : ids) {
        if (!active->spend(1 + 2 * std::bit_width(o.local_divisors))) {
          limit("local divisor collection budget");
          return false;
        }
        if (id == root.id) {
          continue;
        }
        if (retained.size() < o.local_divisors) {
          retained.push(id);
        } else if (better(id, retained.top())) {
          retained.pop();
          retained.push(id);
        }
      }
      return true;
    };
    if (!collect(base.leaves) || !collect(base.interior)) {
      return;
    }
    if (!active->spend(retained.size() * (1 + 2 * std::bit_width(retained.size())))) {
      limit("local divisor ordering budget");
      return;
    }
    std::vector<Id> pool;
    while (!retained.empty()) {
      pool.push_back(retained.top());
      retained.pop();
    }
    std::sort(pool.begin(), pool.end(), better);
    const auto attempt = [&](std::vector<Id> ids) -> std::shared_ptr<const Divisor_function> {
      if (result.report.local_attempts >= o.local_candidates || !available()) {
        limit("local divisor candidate budget");
        return {};
      }
      ++result.report.local_attempts;
      // A set need not separate every structural path. Its image must still
      // determine F exactly; conflicting values reject it before factoring.
      Xag_window proposed;
      proposed.root    = root;
      proposed.leaves  = std::move(ids);
      const auto image = care_boundary(proposed, base, false, false, true);
      if (image && image->status == Status::feasible && o.clock_phases == 2 && available()) {
        care_boundary(proposed, base, true, false, true);
      }
      return image;
    };
    struct Partial {
      std::array<Id, max_logical_inputs> ids{};
      std::array<uint64_t, 4>            forbidden{};  // local_divisors <= 256
      std::pair<uint32_t, uint32_t>      conflict{};
      unsigned                           size = 0, next = 0;
    };
    // Reserve trials for extension rather than spending the entire allowance
    // on singletons. Tiny one/two-divisor searches keep their original order.
    const auto singles
        = std::min<size_t>(pool.size(),
                           pool.size() <= 2 || o.local_candidates <= 2 ? o.local_candidates : (o.local_candidates + 1) / 2);
    std::array<Partial, max_logical_inputs> roots{};
    unsigned                                root_count = 0;
    for (unsigned i = 0; i < singles && available(); ++i) {
      const auto image = attempt({pool[i]});
      if (image && image->status == Status::unsupported && image->conflict) {
        if (root_count == roots.size()) {
          limit("local divisor seed storage budget");
          continue;
        }
        if (!active->spend(i + 1)) {
          break;
        }
        auto& state    = roots[root_count++];
        state.ids[0]   = pool[i];
        state.size     = 1;
        state.conflict = *image->conflict;
        // Each tree owns sets whose first pool-ranked divisor is this seed.
        for (unsigned earlier = 0; earlier < i; ++earlier) {
          state.forbidden[earlier / 64] |= uint64_t{1} << (earlier % 64);
        }
      }
    }
    std::array<Partial, max_logical_inputs> stack{};
    for (unsigned r = 0; r < root_count && available() && result.report.local_attempts < o.local_candidates; ++r) {
      const auto remaining = o.local_candidates - result.report.local_attempts;
      const auto end       = result.report.local_attempts + (r + 1 == root_count ? remaining : (remaining + 1) / 2);
      unsigned   depth     = 1;
      stack[0]             = roots[r];
      while (depth && available() && result.report.local_attempts < end) {
        auto& state = stack[depth - 1];
        if (!active->spend(state.size + 5)) {
          break;
        }
        if (state.size >= o.window.inputs || state.next >= pool.size()) {
          --depth;
          continue;
        }
        const auto index = state.next++;
        const auto id    = pool[index];
        if ((state.forbidden[index / 64] & (uint64_t{1} << (index % 64)))
            || std::find(state.ids.begin(), state.ids.begin() + state.size, id) != state.ids.begin() + state.size) {
          continue;
        }
        bool separates = false;
        {
          Work_slice projection(*active, o.candidate_work);
          if (const auto divisor = divisor_analysis(id, base, projection.work)) {
            separates = divisor->table.get(state.conflict.first) != divisor->table.get(state.conflict.second);
          }
        }
        if (!separates || !available()) {
          continue;
        }
        auto child                   = state;
        child.ids[child.size++]      = id;
        child.next                   = 0;
        // Sibling branches exclude earlier separating choices, but retain
        // earlier non-separators: those may separate the child's next witness.
        state.forbidden[index / 64] |= uint64_t{1} << (index % 64);
        if (!active->spend(child.size * (1 + std::bit_width(child.size)))) {
          break;
        }
        std::vector<Id> ids(child.ids.begin(), child.ids.begin() + child.size);
        std::sort(ids.begin(), ids.end());
        const auto image = attempt(std::move(ids));
        if (image && image->status == Status::unsupported && image->conflict) {
          child.conflict = *image->conflict;
          stack[depth++] = child;
        }
      }
      if (depth && available() && result.report.local_attempts >= end) {
        limit("local divisor seed candidate budget");
      }
    }
    if (result.report.local_attempts == o.local_candidates) {
      limit("local divisor candidate budget");
    }
    if (!available()) {
      limit("local divisor forest budget");
    }
  }

  std::shared_ptr<const Divisor_function> care_boundary(const Xag_window& boundary, const Xag_window& base, bool two_phase,
                                                        bool require_covered, bool local = false, uint64_t* rank_cost = nullptr) {
    if (boundary.leaves == base.leaves) {
      return {};  // independent inputs provide no unreachable combinations
    }
    if (!local) {
      ++result.report.existing_care_attempts;
    }
    Work_slice slice(*active, o.candidate_work);
    auto*      parent = active;
    active            = &slice.work;
    std::shared_ptr<const Divisor_function> image;
    [&] {
      image = divisor_image(boundary.leaves, base, slice.work);
      if (!image || image->status != Status::feasible) {
        return;
      }
      const bool partial = image->care != Truth_table(image->care.inputs, true);
      if (!local && !partial) {
        return;
      }
      result.report.existing_care_images += !local && partial;
      Endpoint_solution candidate;
      candidate.name   = name;
      candidate.root   = root;
      candidate.origin = require_covered ? "existing-care-two-phase" : two_phase ? "existing-care-mixed" : "existing-care-residual";
      if (local) {
        candidate.origin = two_phase ? "local-divisor-mixed" : "local-divisor-residual";
      }
      candidate.functional_basis  = base.leaves;
      std::vector<Id>      inputs = boundary.leaves;
      std::vector<int32_t> producers(inputs.size(), -1);
      std::vector<Id>      static_producers;
      for (size_t i = 0; i < inputs.size(); ++i) {
        if (!two_phase || std::find(base.leaves.begin(), base.leaves.end(), inputs[i]) != base.leaves.end()) {
          continue;
        }
        const auto divisor = divisor_analysis(inputs[i], base, slice.work);
        if (!divisor) {
          return;
        }
        auto sub = function(divisor->window);
        if (!sub && !require_covered) {
          continue;
        }
        std::shared_ptr<Endpoint_function> selected;
        if (sub) {
          selected = std::make_shared<Endpoint_function>(*sub);
          selected->root.reset();
          selected->boundary.clear();
        }
        producers[i] = static_cast<int32_t>(candidate.cells.size());
        if (!require_covered) {
          static_producers.push_back(inputs[i]);
        }
        inputs[i] = 0;
        candidate.cells.push_back(
            {selected, sub ? std::vector<int32_t>(sub->inputs.size(), -1) : std::vector<int32_t>{}, 1, false});
      }
      evaluate_care_top(candidate, image->function, image->care, inputs, producers, slice.work, static_producers, rank_cost);
    }();
    active = parent;
    return image;
  }
};
}  // namespace

Endpoint_solution identity_endpoint(const Xag& graph, Xsignal root, const std::string& name, const Endpoint_options& options) {
  // Even an identity/constant endpoint has one integrated latch and the source name.
  auto identity  = std::make_shared<Endpoint_function>();
  identity->root = root;
  if (root.id == 0) {
    identity->table = Truth_table(0, root.inverted);
    identity->formula.nodes.push_back({Gate_formula::Kind::constant, 0, 0, 0, root.inverted});
  } else {
    identity->inputs   = {root.id};
    identity->boundary = identity->inputs;
    identity->table    = Truth_table(1);
    identity->table.set(root.inverted ? 0 : 1, true);
    // A complemented root selects the latch's free !Q rail; it demands no
    // static inverse of its input.
    identity->formula.nodes.push_back({Gate_formula::Kind::literal, 0, 0, 0, false});
    identity->formula.output_inverted = root.inverted;
  }
  identity->metrics = *identity->formula.metrics();
  Endpoint_solution minimal;
  minimal.name       = name;
  minimal.root       = root;
  minimal.origin     = "identity";
  // What pricing would record: only a source (or constant) input is a whole cone.
  minimal.whole_cone = root.id == 0 || (root.id < graph.size() && graph.node(root.id).kind == Xag::Kind::source);
  minimal.cells.push_back({identity, std::vector<int32_t>(identity->inputs.size(), -1), options.clock_phases, true});
  return minimal;
}

Endpoint_result select_endpoint(const Xag& graph, Xsignal root, std::string name, std::span<const Xsignal> protected_roots,
                                std::span<const Id> free_rails, const Endpoint_options& options, Budget& work,
                                std::span<const Id> analysis_boundary, uint32_t choice_limit) {
  if (choice_limit > 8 || !valid_endpoint_options(options) || root.id >= graph.size() || free_rails.size() > options.cost_nodes
      || analysis_boundary.size() > max_logical_inputs
      || std::any_of(analysis_boundary.begin(),
                     analysis_boundary.end(),
                     [&](auto id) {
                       return id == 0 || id >= graph.size()
                              || std::count(analysis_boundary.begin(), analysis_boundary.end(), id) != 1;
                     })
      || std::any_of(protected_roots.begin(), protected_roots.end(), [&](auto s) { return s.id >= graph.size(); })
      || std::any_of(free_rails.begin(), free_rails.end(), [&](auto id) { return id >= graph.size(); })) {
    Endpoint_result invalid;
    invalid.reason = "invalid endpoint request";
    return invalid;
  }
  return Search(graph, root, std::move(name), protected_roots, free_rails, options, work, analysis_boundary, choice_limit).run();
}

bool validate_endpoint(const Xag& graph, const Endpoint_solution& solution, const Endpoint_options& options, Budget& work) {
  if (!valid_endpoint_options(options) || solution.cells.empty() || solution.root.id >= graph.size()
      || (options.clock_phases == 1 && solution.cells.size() != 1) || !work.spend(solution.cells.size())) {
    return false;
  }
  std::vector<bool> used(solution.cells.size(), false);
  used.back() = true;
  for (size_t i = 0; i < solution.cells.size(); ++i) {
    const auto& cell = solution.cells[i];
    if (!cell.function || cell.latch != (i + 1 == solution.cells.size()) || cell.phase != (cell.latch ? options.clock_phases : 1)) {
      return false;
    }
    const auto& f = *cell.function;
    if (!valid_truth_table(f.table) || f.table.inputs != f.inputs.size() || f.inputs.size() > options.gates.logical_inputs
        || cell.producers.size() != f.inputs.size() || (cell.latch && f.root != solution.root)
        || f.formula.nodes.size() > options.functions.max_formula_nodes) {
      return false;
    }
    const auto metrics = f.formula.metrics();
    if (!metrics || *metrics != f.metrics || metrics->support != (1U << f.inputs.size()) - 1 || metrics->stack > options.gates.stack
        || metrics->branches > options.gates.branches) {
      return false;
    }
    for (size_t j = 0; j < f.inputs.size(); ++j) {
      for (size_t k = 0; k < j; ++k) {
        if (f.inputs[k] == f.inputs[j] && (!solution.functional_basis || cell.producers[k] == cell.producers[j])) {
          return false;
        }
      }
      const auto producer = cell.producers[j];
      if (producer < -1
          || (producer >= 0
              && (static_cast<size_t>(producer) >= i || solution.cells[producer].phase >= cell.phase
                  || (!solution.functional_basis && solution.cells[producer].function->root != Xsignal{f.inputs[j], false})))) {
        return false;
      }
      if (producer >= 0) {
        used[producer] = true;
      }
    }
    if (!solution.functional_basis) {
      if (!f.root) {
        return false;
      }
      const auto window = collect_window(graph, *f.root, f.boundary, options.window, work);
      if (window.status != Status::feasible) {
        return false;
      }
      const auto truth = window_function(graph, window, work);
      if (truth.status != Status::feasible) {
        return false;
      }
      std::vector<uint32_t> positions;
      for (auto id : f.inputs) {
        const auto found = std::find(f.boundary.begin(), f.boundary.end(), id);
        if (found == f.boundary.end()) {
          return false;
        }
        positions.push_back(static_cast<uint32_t>(found - f.boundary.begin()));
      }
      for (uint32_t x = 0; x < (1U << truth.table.inputs); ++x) {
        if (!work.spend(positions.size() + 1)) {
          return false;
        }
        uint32_t assignment = 0;
        for (uint32_t j = 0; j < positions.size(); ++j) {
          assignment |= ((x >> positions[j]) & 1) << j;
        }
        if (truth.table.get(x) != f.table.get(assignment)) {
          return false;
        }
      }
    } else if (!cell.latch && f.root) {
      return false;  // a synthesized divisor makes no claim about a native root
    }
    const auto evaluated = f.formula.evaluate_table(f.table.inputs, work);
    if (!evaluated || *evaluated != f.table) {
      return false;
    }
  }
  if (!std::all_of(used.begin(), used.end(), [](bool value) { return value; })) {
    return false;
  }
  if (!solution.functional_basis) {
    return true;
  }
  const auto& basis  = *solution.functional_basis;
  const auto  window = collect_window(graph, solution.root, basis, options.window, work);
  if (window.status != Status::feasible) {
    return false;
  }
  const auto truth = window_function(graph, window, work);
  if (truth.status != Status::feasible) {
    return false;
  }
  std::vector<std::vector<uint32_t>> positions;
  std::unordered_map<Id, uint32_t>   native_positions;
  std::vector<Truth_table>           native_tables;
  for (const auto& cell : solution.cells) {
    if (!work.spend(cell.producers.size() + 1)) {
      return false;
    }
    std::vector<uint32_t> row;
    for (size_t j = 0; j < cell.producers.size(); ++j) {
      const auto id = cell.function->inputs[j];
      if (cell.producers[j] >= 0) {
        if (id != 0) {
          return false;  // produced slots have no native graph identity
        }
        row.push_back(0);
      } else {
        const auto found = std::find(basis.begin(), basis.end(), id);
        if (id == 0) {
          return false;
        }
        if (found != basis.end()) {
          row.push_back(static_cast<uint32_t>(found - basis.begin()));
        } else if (const auto cached = native_positions.find(id); cached != native_positions.end()) {
          row.push_back(cached->second);
        } else {
          // Correlated static ports must belong to the admitted cone. This
          // excludes endpoint fanouts or unrelated signals even when they
          // happen to be computable from the same primary inputs.
          if (!work.spend(std::bit_width(window.interior.size()) + 1)
              || !std::binary_search(window.interior.begin(), window.interior.end(), id)) {
            return false;
          }
          auto function = basis_function(graph, {id, false}, basis, options.window, work);
          if (function.status != Status::feasible) {
            return false;
          }
          const auto position = static_cast<uint32_t>(basis.size() + native_tables.size());
          native_positions.emplace(id, position);
          native_tables.push_back(std::move(function.table));
          row.push_back(position);
        }
      }
    }
    positions.push_back(std::move(row));
  }
  std::vector<bool> values(solution.cells.size());
  for (uint32_t x = 0; x < (1U << basis.size()); ++x) {
    for (size_t i = 0; i < solution.cells.size(); ++i) {
      const auto& cell = solution.cells[i];
      if (!work.spend(cell.producers.size() + 1)) {
        return false;
      }
      uint32_t input = 0;
      for (size_t j = 0; j < cell.producers.size(); ++j) {
        const auto producer  = cell.producers[j];
        const auto position  = positions[i][j];
        const bool bit       = producer >= 0             ? values[producer]
                               : position < basis.size() ? ((x >> position) & 1)
                                                         : native_tables[position - basis.size()].get(x);
        input               |= uint32_t{bit} << j;
      }
      values[i] = cell.function->table.get(input);
    }
    if (values.back() != truth.table.get(x)) {
      return false;
    }
  }
  return true;
}

}  // namespace livehd::usyn
