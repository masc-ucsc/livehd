// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "native_cost.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

#include "liberty_dff.hpp"

namespace livehd::usyn {
namespace {
// Evaluate the entire independent pin basis at once. Reject unknown tokens,
// absent pins and unfinished expressions; never use a sampled signature.
class Expression {
public:
  Expression(std::string_view expression, const std::vector<std::string>& pins) : text(expression) {
    for (uint32_t i = 0; i < pins.size(); ++i) {
      uint64_t bits = 0;
      for (uint32_t p = 0; p < (1U << pins.size()); ++p) {
        bits |= uint64_t{(p >> i) & 1} << p;
      }
      variables.emplace(pins[i], bits);
    }
  }
  std::optional<uint64_t> parse() {
    auto value = disjunction();
    space();
    if (!valid || position != text.size()) {
      return {};
    }
    return value;
  }

private:
  std::string_view                text;
  size_t                          position = 0;
  bool                            valid    = true;
  std::map<std::string, uint64_t> variables;
  void                            space() {
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
      ++position;
    }
  }
  bool take(char token) {
    space();
    if (position < text.size() && text[position] == token) {
      ++position;
      return true;
    }
    return false;
  }
  // Liberty precedence, loosest to tightest: OR (`+` `|`), AND (`*` `&` and
  // juxtaposition), XOR (`^`), then inversion (`!` prefix, `'` postfix) --
  // OpenSTA's LibExprParse.yy declares `%left '+' '|'`, `%left '*' '&'`,
  // `%left '^'`, `%left '!' '\''` in that order. So `A^B*C` is `(A^B)*C`.
  uint64_t disjunction() {
    auto value = conjunction();
    while (take('+') || take('|')) {
      value |= conjunction();
    }
    return value;
  }
  uint64_t conjunction() {
    auto value = exclusive();
    while (valid) {
      if (take('*') || take('&')) {
        value &= exclusive();
        continue;
      }
      space();
      if (position < text.size()
          && (text[position] == '(' || text[position] == '!' || text[position] == '~'
              || std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '_')) {
        value &= exclusive();
        continue;
      }
      break;
    }
    return value;
  }
  uint64_t exclusive() {
    auto value = literal();
    while (take('^')) {
      value ^= literal();
    }
    return value;
  }
  uint64_t literal() {
    const bool inverse = take('!') || take('~');
    if (inverse) {
      return ~literal();
    }
    uint64_t value = 0;
    if (take('(')) {
      value = disjunction();
      if (!take(')')) {
        valid = false;
      }
    } else {
      space();
      const auto begin = position;
      while (position < text.size() && (std::isalnum(static_cast<unsigned char>(text[position])) || text[position] == '_')) {
        ++position;
      }
      const auto name  = std::string(text.substr(begin, position - begin));
      const auto found = variables.find(name);
      if (name == "0") {
        value = 0;
      } else if (name == "1") {
        value = ~uint64_t{0};
      } else if (found != variables.end()) {
        value = found->second;
      } else {
        valid = false;
      }
    }
    while (take('\'')) {
      value = ~value;
    }
    return value;
  }
};
struct Cover {
  bool                             valid = false;
  double                           area = 0, intrinsic_area = 0;
  uint64_t                         gates = 0, intrinsic_gates = 0;
  uint32_t                         depth = 0;
  std::vector<std::pair<Id, bool>> inputs;
};
bool better(const Cover& a, const Cover& b, bool gates) {
  if (!a.valid) {
    return false;
  }
  if (!b.valid) {
    return true;
  }
  if (gates && a.gates != b.gates) {
    return a.gates < b.gates;
  }
  if (a.area != b.area) {
    return a.area < b.area;
  }
  if (!gates && a.gates != b.gates) {
    return a.gates < b.gates;
  }
  return a.depth < b.depth;
}
}  // namespace

static std::optional<Native_cost_model> build_native_cost_model(const std::vector<liberty::Comb_cell>& cells, Budget& work) {
  Native_cost_model model;
  for (const auto& cell : cells) {
    if (!work.spend(cell.function.size() + cell.inputs.size() + 1)) {
      return {};
    }
    if (cell.inputs.size() > 4 || cell.function.size() > 4096 || !std::isfinite(cell.area)) {
      ++model.skipped_cells;
      continue;
    }
    const auto function = Expression(cell.function, cell.inputs).parse();
    if (!function) {
      ++model.skipped_cells;
      continue;
    }
    const auto n     = static_cast<uint32_t>(cell.inputs.size());
    const auto mask  = static_cast<uint16_t>((uint32_t{1} << (1U << n)) - 1);
    const auto truth = static_cast<uint16_t>(*function) & mask;
    if (n == 1 && truth == 1 && (!model.inverter_area || cell.area < model.inverter_area)) {
      model.inverter_area = cell.area;
    }
    std::vector<uint32_t> permutation(n);
    std::iota(permutation.begin(), permutation.end(), 0U);
    do {
      for (uint32_t phase = 0; phase < (1U << n); ++phase) {
        if (!work.spend(1 + (1U << n))) {
          return {};
        }
        uint16_t transformed = 0;
        for (uint32_t assignment = 0; assignment < (1U << n); ++assignment) {
          uint32_t pins = 0;
          for (uint32_t i = 0; i < n; ++i) {
            pins |= (((assignment ^ phase) >> permutation[i]) & 1) << i;
          }
          transformed |= ((truth >> pins) & 1) << assignment;
        }
        auto&      costs = model.cells[n][transformed];
        const auto found = std::find_if(costs.begin(), costs.end(), [&](const auto& cost) { return cost.input_phases == phase; });
        if (found == costs.end()) {
          costs.push_back({static_cast<uint8_t>(phase), cell.area, 1});
        } else if (cell.area < found->area) {
          found->area = cell.area;
        }
      }
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    ++model.admitted_cells;
  }
  if (!model.inverter_area || !model.admitted_cells) {
    return {};
  }
  return model;
}

std::optional<Native_cost_model> read_native_cost_model(const std::string& libraries, Budget& work) {
  return build_native_cost_model(liberty::scan_comb_cells(libraries), work);
}
std::optional<Native_cost_model> read_native_cost_text_model(const std::string& contents, Budget& work) {
  if (contents.size() > 64 * 1024 * 1024 || !work.spend(contents.size())) {
    return {};
  }
  return build_native_cost_model(liberty::scan_comb_cells_text(contents), work);
}

Native_estimate estimate_native_cost(const Xag& graph, std::span<const Xsignal> outputs, const Native_cost_model& model,
                                     Budget& work, bool gate_objective) {
  Native_estimate result;
  if (!model.inverter_area || graph.size() > 2000000) {
    return result;
  }
  if (!work.spend(4 * graph.size() + outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  std::vector<bool> live(graph.size());
  for (auto output : outputs) {
    if (output.id >= graph.size()) {
      return result;
    }
    live[output.id] = true;
  }
  for (size_t end = graph.size(); end > 0; --end) {
    const auto  id   = static_cast<Id>(end - 1);
    const auto& node = graph.node(id);
    if (live[id] && (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate)) {
      live[node.inputs[0].id] = live[node.inputs[1].id] = true;
    }
  }
  std::vector<std::array<Cover, 2>> covers(graph.size());
  covers[0][0].valid = covers[0][1].valid = true;
  for (Id id = 1; id < graph.size(); ++id) {
    if (!live[id]) {
      continue;
    }
    const auto& node = graph.node(id);
    if (node.kind == Xag::Kind::source) {
      covers[id][0].valid = true;
      auto& inverse       = covers[id][1];
      inverse.valid       = true;
      inverse.area = inverse.intrinsic_area = model.inverter_area;
      inverse.gates = inverse.intrinsic_gates = inverse.depth = 1;
      continue;
    }
    std::vector<Id> direct;
    for (auto input : node.inputs) {
      if (input.id && std::find(direct.begin(), direct.end(), input.id) == direct.end()) {
        direct.push_back(input.id);
      }
    }
    std::sort(direct.begin(), direct.end());
    auto fallback = collect_window(graph, {id, false}, direct, {4, 64}, work);
    if (fallback.status != Status::feasible) {
      result.status = fallback.status;
      return result;
    }
    auto cut_work = work.slice(5000, 1, 64);
    auto cuts     = priority_windows(graph, {id, false}, {4, 64}, cut_work, 4);
    work.absorb(cut_work);
    result.limited |= cuts.exhausted || cuts.status != Status::feasible;
    // Primitive AND/XOR support stays available even if higher-coverage cuts
    // cannot map to a single legal cell. A refused optional cut search cannot
    // make an otherwise coverable circuit appear unsupported.
    if (cuts.status != Status::feasible) {
      cuts.windows.clear();
    }
    cuts.windows.insert(cuts.windows.begin(), std::move(fallback));
    for (const auto& cut : cuts.windows) {
      if (cut.leaves.empty()) {
        continue;
      }
      if (std::find(cut.leaves.begin(), cut.leaves.end(), id) != cut.leaves.end()) {
        continue;
      }
      auto function = window_function(graph, cut, work);
      if (function.status != Status::feasible) {
        result.status = function.status;
        return result;
      }
      const auto n     = static_cast<uint32_t>(cut.leaves.size());
      const auto mask  = static_cast<uint16_t>((uint32_t{1} << (1U << n)) - 1);
      const auto truth = static_cast<uint16_t>(function.table.words[0]) & mask;
      for (uint32_t phase = 0; phase < 2; ++phase) {
        const auto key   = static_cast<uint16_t>(phase ? truth ^ mask : truth);
        const auto found = model.cells[n].find(key);
        if (found == model.cells[n].end()) {
          continue;
        }
        for (const auto& cost : found->second) {
          if (!work.spend(1 + n)) {
            result.status = Status::search_exhausted;
            return result;
          }
          Cover candidate;
          candidate.valid = true;
          candidate.area = candidate.intrinsic_area = cost.area;
          candidate.gates = candidate.intrinsic_gates = cost.gates;
          for (uint32_t i = 0; i < n; ++i) {
            const bool  inverted  = (cost.input_phases >> i) & 1;
            const auto& leaf      = covers[cut.leaves[i]][inverted];
            candidate.valid      &= leaf.valid;
            candidate.area       += leaf.area;
            candidate.gates      += leaf.gates;
            candidate.depth       = std::max(candidate.depth, leaf.depth);
            candidate.inputs.emplace_back(cut.leaves[i], inverted);
          }
          ++candidate.depth;
          if (better(candidate, covers[id][phase], gate_objective)) {
            covers[id][phase] = std::move(candidate);
          }
        }
      }
    }
    // If only the inverted cell function exists (e.g. NAND), the demanded
    // opposite output rail has one real inverter, not a free complemented edge.
    for (uint32_t phase = 0; phase < 2; ++phase) {
      if (!covers[id][!phase].valid) {
        continue;
      }
      auto candidate  = covers[id][!phase];
      candidate.area += model.inverter_area;
      ++candidate.gates;
      ++candidate.depth;
      candidate.intrinsic_area  = model.inverter_area;
      candidate.intrinsic_gates = 1;
      candidate.inputs          = {
          {id, !phase}
      };
      if (better(candidate, covers[id][phase], gate_objective)) {
        covers[id][phase] = std::move(candidate);
      }
    }
  }
  std::vector<std::array<bool, 2>> counted(graph.size());
  std::vector<std::pair<Id, bool>> pending;
  for (auto output : outputs) {
    if (!covers[output.id][output.inverted].valid) {
      result.status = Status::unsupported;
      return result;
    }
    result.depth = std::max(result.depth, covers[output.id][output.inverted].depth);
    pending.emplace_back(output.id, output.inverted);
  }
  while (!pending.empty()) {
    if (!work.spend()) {
      result.status = Status::search_exhausted;
      return result;
    }
    const auto [id, phase] = pending.back();
    pending.pop_back();
    if (counted[id][phase]) {
      continue;
    }
    counted[id][phase]  = true;
    const auto& cover   = covers[id][phase];
    result.area        += cover.intrinsic_area;
    result.gates       += cover.intrinsic_gates;
    pending.insert(pending.end(), cover.inputs.begin(), cover.inputs.end());
  }
  result.status = Status::feasible;
  return result;
}
}  // namespace livehd::usyn
