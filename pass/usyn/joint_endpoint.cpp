// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "joint_endpoint.hpp"

#include <algorithm>
#include <bit>

#include "decomposition.hpp"

namespace livehd::usyn {
namespace {
using Slice = Work_slice;

struct Prepared {
  Status                                    status = Status::invalid;
  Joint_decomposition                       decomposition;
  std::vector<std::optional<Endpoint_cell>> cells;
  std::vector<Id>                           inputs;
  std::vector<int32_t>                      producers;
  uint64_t                                  bytes     = 0;
  bool                                      exhausted = false;
  bool                                      recoded   = false;
};

std::optional<Endpoint_cell> make_cell(const Truth_table& table, std::span<const Id> inputs, std::span<const int32_t> producers,
                                       std::optional<Xsignal> root, const Endpoint_options& options, Budget& work, bool& exhausted,
                                       uint64_t& bytes) {
  auto gate  = synthesize_gate(table, options.gates, work, options.functions);
  exhausted |= gate.search_exhausted;
  if (!gate.formula) {
    return {};
  }
  const uint64_t size = 2 * sizeof(Endpoint_function) + gate.formula->nodes.capacity() * sizeof(Gate_formula::Node)
                        + table.words.capacity() * sizeof(uint64_t) + inputs.size() * (sizeof(Id) + sizeof(int32_t));
  if (size > options.boundary_bytes - bytes || !work.spend(inputs.size() + gate.formula->nodes.size())) {
    exhausted = true;
    return {};
  }
  bytes             += size;
  auto function      = std::make_shared<Endpoint_function>();
  function->root     = root;
  function->formula  = std::move(*gate.formula);
  Endpoint_cell                            cell{function, {}, root ? 2U : 1U, root.has_value()};
  std::vector<uint32_t>                    positions;
  std::array<uint32_t, max_logical_inputs> rank{};
  for (uint32_t i = 0; i < inputs.size(); ++i) {
    if (gate.cost.support & (1U << i)) {
      rank[i] = positions.size();
      positions.push_back(i);
      function->inputs.push_back(inputs[i]);
      cell.producers.push_back(producers[i]);
    }
  }
  for (auto& node : function->formula.nodes) {
    if (node.kind == Gate_formula::Kind::literal) {
      node.variable = rank[node.variable];
    }
  }
  function->metrics = *function->formula.metrics();
  function->table   = Truth_table(positions.size());
  for (uint32_t x = 0; x < (1U << positions.size()); ++x) {
    if (!work.spend(positions.size() + 1)) {
      exhausted = true;
      return {};
    }
    uint32_t original = 0;
    for (uint32_t i = 0; i < positions.size(); ++i) {
      original |= ((x >> i) & 1) << positions[i];
    }
    function->table.set(x, table.get(original));
  }
  return cell;
}
Prepared prepare(const Xag& graph, const Pair_windows& windows, const std::array<Truth_table, 2>& functions, uint32_t bound_mask,
                 const Endpoint_options& options, Budget& work, bool care, std::optional<Joint_code_change> change = {}) {
  Prepared   result;
  const auto n = windows.basis.size();
  if (!valid_endpoint_options(options) || options.clock_phases != 2 || windows.status != Status::feasible || n < 2
      || n > max_logical_inputs || !bound_mask || bound_mask >= (1U << n) - 1
      || static_cast<uint32_t>(std::popcount(bound_mask)) > options.gates.logical_inputs) {
    return result;
  }
  for (auto id : windows.basis) {
    if (!id || id >= graph.size() || std::count(windows.basis.begin(), windows.basis.end(), id) != 1) {
      return result;
    }
  }
  for (size_t i = 0; i < functions.size(); ++i) {
    if (!valid_truth_table(functions[i]) || functions[i].inputs != n || windows.windows[i].status != Status::feasible
        || windows.windows[i].leaves.size() > options.window.inputs) {
      return result;
    }
    for (uint32_t j = 0; j < n; ++j) {
      if ((bound_mask & (1U << j))
          && std::find(windows.windows[i].leaves.begin(), windows.windows[i].leaves.end(), windows.basis[j])
                 == windows.windows[i].leaves.end()) {
        return result;
      }
    }
  }
  const auto refused = [&] {
    result.status    = Status::search_exhausted;
    result.exhausted = true;
    return std::move(result);
  };
  const uint64_t nb = uint64_t{1} << std::popcount(bound_mask), nr = (uint64_t{1} << n) / nb;
  // Bound the transient cofactor-class map and tables before constructing it.
  const uint64_t table_words
      = static_cast<uint64_t>(std::popcount(bound_mask)) * ((nb + 63) / 64) + 3 * (((uint64_t{1} << n) + 63) / 64);
  const uint64_t workspace = 512 + nb * (128 + 2 * ((nr + 63) / 64) * sizeof(uint64_t)) + (3 * nb + nr) * sizeof(uint32_t)
                             + (change ? 2 : 1) * table_words * sizeof(uint64_t);
  if (workspace > options.boundary_bytes || !work.spend(n * n + 1)) {
    return refused();
  }
  result.bytes = workspace;

  result.decomposition = decompose_pair(functions, bound_mask, max_logical_inputs, work);
  auto& decomposition  = result.decomposition;
  result.status        = decomposition.status;
  if (decomposition.status != Status::feasible) {
    result.exhausted = work.exhausted || decomposition.status == Status::search_exhausted;
    return result;
  }
  result.status = Status::unsupported;
  if (change) {
    if (change->target >= decomposition.divisors.size() || change->source >= decomposition.divisors.size()) {
      return result;
    }
    auto alternate = recode_pair(decomposition, *change, work);
    if (alternate.status != Status::feasible) {
      result.status    = alternate.status;
      result.exhausted = work.exhausted || alternate.status == Status::search_exhausted;
      return result;
    }
    decomposition  = std::move(alternate);
    result.recoded = true;
  }
  if (care) {
    if (!work.spend(decomposition.top_care.words.size() + 1)) {
      return refused();
    }
    if (!change && decomposition.top_care == Truth_table(decomposition.top_care.inputs, true)) {
      return result;
    }
  }
  std::vector<Id> bound;
  for (auto position : decomposition.bound) {
    bound.push_back(windows.basis[position]);
  }
  const std::vector<int32_t> native(bound.size(), -1);
  result.cells.resize(decomposition.divisors.size());
  for (size_t i = 0; i < decomposition.divisors.size(); ++i) {
    uint64_t bytes = result.bytes;
    if (care) {
      Slice trial(work, unlimited_work, decomposition.divisors.size() - i + 1);
      result.cells[i] = make_cell(decomposition.divisors[i], bound, native, {}, options, trial.work, result.exhausted, bytes);
    } else {
      result.cells[i] = make_cell(decomposition.divisors[i], bound, native, {}, options, work, result.exhausted, bytes);
    }
    if (result.cells[i]) {
      result.bytes = bytes;
    } else if (!care) {
      return result.exhausted ? refused() : std::move(result);
    }
    if (work.exhausted) {
      if (!care) {
        return refused();
      }
      result.exhausted = true;
      break;
    }
  }
  result.inputs.assign(result.cells.size(), 0);
  for (uint32_t i = 0; i < result.cells.size(); ++i) {
    result.producers.push_back(i);
  }
  for (auto position : decomposition.free) {
    result.inputs.push_back(windows.basis[position]);
    result.producers.push_back(-1);
  }
  result.status = Status::feasible;
  return result;
}

std::optional<Endpoint_solution> realize_top(const Xag& graph, const Pair_windows& windows, const Prepared& prepared, size_t root,
                                             const Truth_table& table, const std::string& name, const Endpoint_options& options,
                                             Budget& work, bool& exhausted, uint64_t& bytes, uint32_t& used) {
  auto top = make_cell(table, prepared.inputs, prepared.producers, windows.windows[root].root, options, work, exhausted, bytes);
  if (!top) {
    return {};
  }
  const uint64_t payload = 256 + sizeof(Endpoint_solution) + name.size() + windows.windows[root].leaves.size() * sizeof(Id)
                           + (prepared.cells.size() + 1) * (sizeof(Endpoint_cell) + max_logical_inputs * sizeof(int32_t));
  if (payload > options.boundary_bytes - bytes) {
    exhausted = true;
    return {};
  }
  bytes += payload;
  Endpoint_solution endpoint;
  endpoint.name   = name;
  endpoint.root   = windows.windows[root].root;
  endpoint.origin = table == prepared.decomposition.tops[root] ? "joint-functional" : "joint-care";
  if (prepared.recoded) {
    endpoint.origin = table == prepared.decomposition.tops[root] ? "joint-recode" : "joint-recode-care";
  }
  endpoint.whole_cone       = windows.windows[root].whole_cone;
  endpoint.functional_basis = windows.windows[root].leaves;
  used                      = 0;
  for (uint32_t j = 0; j < prepared.cells.size(); ++j) {
    if (!work.spend(top->producers.size() + 1)) {
      exhausted = true;
      return {};
    }
    if (std::find(top->producers.begin(), top->producers.end(), j) == top->producers.end()) {
      continue;
    }
    if (!prepared.cells[j]) {
      return {};
    }
    used |= 1U << j;
    for (auto& producer : top->producers) {
      if (producer == static_cast<int32_t>(j)) {
        producer = static_cast<int32_t>(endpoint.cells.size());
      }
    }
    endpoint.cells.push_back(*prepared.cells[j]);
  }
  endpoint.cells.push_back(std::move(*top));
  if (!validate_endpoint(graph, endpoint, options, work)) {
    exhausted |= work.exhausted;
    return {};
  }
  return endpoint;
}
}  // namespace

Joint_endpoint_result synthesize_joint_endpoints(const Xag& graph, const Pair_windows& windows,
                                                 const std::array<Truth_table, 2>& functions,
                                                 const std::array<std::string, 2>& names, uint32_t bound_mask,
                                                 const Endpoint_options& options, Budget& work) {
  Joint_endpoint_result result;
  auto                  prepared = prepare(graph, windows, functions, bound_mask, options, work, false);
  result.status                  = prepared.status;
  result.exhausted               = prepared.exhausted;
  if (prepared.status != Status::feasible) {
    return result;
  }
  result.status = Status::unsupported;
  std::array<Endpoint_solution, 2> endpoints;
  std::array<uint32_t, 2>          used{};
  for (size_t i = 0; i < endpoints.size(); ++i) {
    auto endpoint = realize_top(graph,
                                windows,
                                prepared,
                                i,
                                prepared.decomposition.tops[i],
                                names[i],
                                options,
                                work,
                                result.exhausted,
                                prepared.bytes,
                                used[i]);
    if (!endpoint) {
      if (result.exhausted || work.exhausted) {
        result.status    = Status::search_exhausted;
        result.exhausted = true;
      }
      return result;
    }
    endpoints[i] = std::move(*endpoint);
  }
  result.shared_divisors = std::popcount(used[0] & used[1]);
  if (result.shared_divisors) {
    result.status    = Status::feasible;
    result.endpoints = std::move(endpoints);
  }
  return result;
}

namespace {
Joint_endpoint_choices endpoint_choices(const Xag& graph, const Pair_windows& windows, const std::array<Truth_table, 2>& functions,
                                        const std::array<std::string, 2>& names, uint32_t bound_mask,
                                        const Endpoint_options& options, uint32_t choice_limit, Budget& work,
                                        std::optional<Joint_code_change> change) {
  Joint_endpoint_choices result;
  if (!choice_limit || choice_limit > 8 || (!options.care_phases && !change)) {
    return result;
  }
  Prepared prepared;
  {
    Slice preparation(work, unlimited_work, 3);
    prepared = prepare(graph, windows, functions, bound_mask, options, preparation.work, true, change);
  }
  result.status    = prepared.status;
  result.exhausted = prepared.exhausted || work.exhausted;
  result.bytes     = prepared.bytes;
  result.code_bits = prepared.decomposition.divisors.size();
  if (prepared.status != Status::feasible || work.exhausted) {
    // A refusal during preparation retains nothing: report the exhaustion
    // rather than a feasible result with empty pools.
    if (result.exhausted && result.status == Status::feasible) {
      result.status = Status::search_exhausted;
    }
    return result;
  }
  result.status = Status::unsupported;
  const bool try_care
      = !change
        || (options.care_phases && prepared.decomposition.top_care != Truth_table(prepared.decomposition.top_care.inputs, true));
  uint64_t retained_bytes = prepared.bytes;
  result.bytes            = retained_bytes;
  const auto retain       = [&](size_t root, const Truth_table& table, Budget& trial) {
    ++result.attempts;
    uint32_t used  = 0;
    auto     bytes = retained_bytes;
    auto endpoint  = realize_top(graph, windows, prepared, root, table, names[root], options, trial, result.exhausted, bytes, used);
    result.bytes   = std::max(result.bytes, bytes);
    if (endpoint) {
      retained_bytes = bytes;
      result.endpoints[root].push_back(std::move(*endpoint));
      ++result.retained;
      result.status = Status::feasible;
    }
  };
  for (size_t root = 0; root < 2 && !work.exhausted; ++root) {
    Slice root_work(work, unlimited_work, 2 - root);
    {
      Slice baseline(root_work.work, unlimited_work, try_care ? 4 : 1);
      retain(root, prepared.decomposition.tops[root], baseline.work);
    }
    if (!try_care) {
      continue;
    }
    const auto&    table   = prepared.decomposition.tops[root];
    // The completion helper retains at most 2+2*phase_limit tables. Reserve
    // their payload, vector growth, phase list and simultaneous closure tables
    // while keeping already retained endpoint/formula payloads charged.
    const uint64_t count   = 2 + 2 * uint64_t{options.care_phases};
    const uint64_t scratch = 512 + (count + 6) * table.words.size() * sizeof(uint64_t) + 2 * count * sizeof(Truth_table)
                             + options.care_phases * sizeof(uint32_t);
    if (scratch > options.boundary_bytes - retained_bytes) {
      result.exhausted = true;
      continue;
    }
    Function_completions completions;
    {
      Slice generation(root_work.work, unlimited_work, 3);
      completions = function_completions(table, prepared.decomposition.top_care, options.care_phases, generation.work);
    }
    result.phases         += completions.phases;
    result.exhausted      |= completions.search_exhausted;
    retained_bytes        += scratch;
    result.bytes           = std::max(result.bytes, retained_bytes);
    uint32_t alternatives  = 0;
    for (size_t i = 0; i < completions.tables.size() && !root_work.work.exhausted; ++i) {
      if (completions.tables[i] == table) {
        continue;
      }
      if (alternatives == choice_limit) {
        result.exhausted = true;
        break;
      }
      const auto before = result.retained;
      Slice      trial(root_work.work, unlimited_work, completions.tables.size() - i);
      retain(root, completions.tables[i], trial.work);
      alternatives += result.retained != before;
    }
    retained_bytes -= scratch;
  }
  result.exhausted |= work.exhausted;
  if (result.status != Status::feasible && result.exhausted) {
    result.status = Status::search_exhausted;
  }
  return result;
}
}  // namespace

Joint_endpoint_choices complete_joint_endpoints(const Xag& graph, const Pair_windows& windows,
                                                const std::array<Truth_table, 2>& functions,
                                                const std::array<std::string, 2>& names, uint32_t bound_mask,
                                                const Endpoint_options& options, uint32_t choice_limit, Budget& work) {
  return endpoint_choices(graph, windows, functions, names, bound_mask, options, choice_limit, work, {});
}

Joint_endpoint_choices recode_joint_endpoints(const Xag& graph, const Pair_windows& windows,
                                              const std::array<Truth_table, 2>& functions, const std::array<std::string, 2>& names,
                                              uint32_t bound_mask, Joint_code_change change, const Endpoint_options& options,
                                              uint32_t choice_limit, Budget& work) {
  return endpoint_choices(graph, windows, functions, names, bound_mask, options, choice_limit, work, change);
}
}  // namespace livehd::usyn
