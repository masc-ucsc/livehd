// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "function.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace livehd::usyn {

namespace {
constexpr std::array<uint64_t, 6> variable_words{0xaaaaaaaaaaaaaaaaULL,
                                                 0xccccccccccccccccULL,
                                                 0xf0f0f0f0f0f0f0f0ULL,
                                                 0xff00ff00ff00ff00ULL,
                                                 0xffff0000ffff0000ULL,
                                                 0xffffffff00000000ULL};
uint64_t tail_mask(uint32_t inputs) { return inputs >= 6 ? ~uint64_t{0} : (uint64_t{1} << (1U << inputs)) - 1; }

// Swap the two cofactors of each complemented variable, within words for
// variables 0..5 and by word index for the remaining variables.
uint64_t phase_word(uint64_t value, uint32_t phase) {
  for (uint32_t j = 0; j < variable_words.size(); ++j) {
    if (phase & (1U << j)) {
      const auto shift = 1U << j;
      value            = ((value & ~variable_words[j]) << shift) | ((value & variable_words[j]) >> shift);
    }
  }
  return value;
}
}  // namespace

bool valid_truth_table(const Truth_table& t) {
  if (t.inputs > max_logical_inputs || t.words.size() != ((1U << t.inputs) + 63) / 64) {
    return false;
  }
  const auto bits = 1U << t.inputs;
  return bits >= 64 || (t.words[0] >> bits) == 0;
}

Function_analysis analyze_function(const Truth_table& t, Budget& work) {
  Function_analysis result;
  if (!valid_truth_table(t)) {
    return result;
  }
  result.status = Status::search_exhausted;
  for (uint32_t j = 0; j < t.inputs; ++j) {
    bool up = false, down = false;
    if (j < 6) {
      const auto mask = ~variable_words[j] & tail_mask(t.inputs);
      for (auto word : t.words) {
        if (!work.spend()) {
          return result;
        }
        const auto lo = word & mask, hi = (word >> (1U << j)) & mask;
        up   |= (~lo & hi) != 0;
        down |= (lo & ~hi) != 0;
        if (up && down) {
          break;
        }
      }
    } else {
      const auto stride = size_t{1} << (j - 6);
      for (size_t base = 0; base < t.words.size() && !(up && down); base += 2 * stride) {
        for (size_t k = 0; k < stride; ++k) {
          if (!work.spend()) {
            return result;
          }
          const auto lo = t.words[base + k], hi = t.words[base + k + stride];
          up   |= (~lo & hi) != 0;
          down |= (lo & ~hi) != 0;
          if (up && down) {
            break;
          }
        }
      }
    }
    if (up || down) {
      result.support |= 1U << j;
    }
    if (down) {
      (up ? result.binate : result.negative) |= 1U << j;
    }
  }
  result.status = Status::feasible;
  return result;
}

std::optional<Gate_formula::Metrics> Gate_formula::metrics() const {
  if (nodes.empty()) {
    return std::nullopt;
  }
  std::vector<Metrics>  m(nodes.size());
  std::vector<uint32_t> refs(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& n   = nodes[i];
    auto&       out = m[i];
    switch (n.kind) {
      case Kind::constant: break;
      case Kind::literal:
        if (n.variable >= max_logical_inputs) {
          return std::nullopt;
        }
        out.transistors = out.stack = out.branches = 1;
        out.support                                = 1U << n.variable;
        (n.inverted ? out.negative : out.positive) = out.support;
        break;
      case Kind::series  :
      case Kind::parallel: {
        if (n.left >= i || n.right >= i || ++refs[n.left] > 1 || ++refs[n.right] > 1) {
          return std::nullopt;
        }
        const auto& a   = m[n.left];
        const auto& b   = m[n.right];
        out.transistors = a.transistors + b.transistors;
        out.stack       = n.kind == Kind::series ? a.stack + b.stack : std::max(a.stack, b.stack);
        out.branches    = n.kind == Kind::parallel ? a.branches + b.branches : std::max(a.branches, b.branches);
        out.support     = a.support | b.support;
        out.positive    = a.positive | b.positive;
        out.negative    = a.negative | b.negative;
        break;
      }
      default: return std::nullopt;
    }
  }
  if (std::any_of(refs.begin(), refs.end() - 1, [](auto r) { return r != 1; })) {
    return std::nullopt;
  }
  return m.back();
}

bool Gate_formula::evaluate(uint32_t assignment) const {
  std::vector<bool> values;
  values.reserve(nodes.size());
  for (const auto& n : nodes) {
    switch (n.kind) {
      case Kind::constant: values.push_back(n.inverted); break;
      case Kind::literal:
        if (n.variable >= max_logical_inputs) {
          throw std::invalid_argument("invalid gate literal");
        }
        values.push_back(((assignment >> n.variable) & 1) != n.inverted);
        break;
      case Kind::series  : values.push_back(values.at(n.left) && values.at(n.right)); break;
      case Kind::parallel: values.push_back(values.at(n.left) || values.at(n.right)); break;
      default            : throw std::invalid_argument("invalid gate operator");
    }
  }
  if (values.empty()) {
    throw std::invalid_argument("empty gate formula");
  }
  return values.back() != output_inverted;
}

std::optional<Truth_table> Gate_formula::evaluate_table(uint32_t inputs, Budget& work) const {
  if (inputs > max_logical_inputs || !work.spend(nodes.size())) {
    return {};
  }
  const auto shape = metrics();
  if (!shape || (shape->support >> inputs) != 0) {
    return {};
  }
  Truth_table           table(inputs);
  std::vector<uint64_t> values(nodes.size());
  for (size_t word = 0; word < table.words.size(); ++word) {
    if (!work.spend(nodes.size())) {
      return {};
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto& n = nodes[i];
      switch (n.kind) {
        case Kind::constant: values[i] = n.inverted ? ~uint64_t{0} : 0; break;
        case Kind::literal : {
          auto value = n.variable < 6 ? variable_words[n.variable] : ((word >> (n.variable - 6)) & 1) ? ~uint64_t{0} : 0;
          values[i]  = n.inverted ? ~value : value;
          break;
        }
        case Kind::series  : values[i] = values[n.left] & values[n.right]; break;
        case Kind::parallel: values[i] = values[n.left] | values[n.right]; break;
      }
    }
    table.words[word] = values.back() ^ (output_inverted ? ~uint64_t{0} : 0);
  }
  table.words.back() &= tail_mask(inputs);
  return table;
}

namespace {
using Kind = Gate_formula::Kind;

void remove_redundant_cubes(std::vector<Cube>& cubes, uint32_t inputs, Budget& work, bool& exhausted) {
  if (cubes.size() < 2) {
    return;
  }
  // Optional irredundancy must leave work for factoring. A refused reduction
  // keeps the complete original cover; no partially edited cover is exposed.
  Budget     local    = work.slice(unlimited_work, 4);
  const bool complete = [&] {
    const auto count = 1U << inputs;
    if (!local.spend(count + cubes.size())) {
      return false;
    }
    std::vector<uint32_t> references(count);
    std::vector<bool>     removed(cubes.size());
    const auto            assignments = [&](const Cube& cube, auto&& visit) {
      const auto free = (count - 1) & ~cube.care;
      for (uint32_t sub = free;; sub = (sub - 1) & free) {
        if (!local.spend()) {
          return false;
        }
        visit(cube.ones | sub);
        if (sub == 0) {
          return true;
        }
      }
    };
    for (const auto& cube : cubes) {
      if (!assignments(cube, [&](auto x) { ++references[x]; })) {
        return false;
      }
    }
    for (size_t i = 0; i < cubes.size(); ++i) {
      bool redundant = true;
      if (!assignments(cubes[i], [&](auto x) { redundant &= references[x] > 1; })) {
        return false;
      }
      if (redundant) {
        removed[i] = true;
        if (!assignments(cubes[i], [&](auto x) { --references[x]; })) {
          return false;
        }
      }
    }
    size_t next = 0;
    for (size_t i = 0; i < cubes.size(); ++i) {
      if (!removed[i]) {
        cubes[next++] = cubes[i];
      }
    }
    cubes.resize(next);
    return true;
  }();
  work.absorb(local);
  exhausted |= !complete;
}

std::optional<std::vector<Cube>> cover(const Truth_table& t, const Function_analysis& a, Budget& work, uint32_t max_cubes,
                                       bool& exhausted) {
  std::vector<Cube> cubes;
  const auto        count = 1U << t.inputs;
  Truth_table       covered(t.inputs);
  for (uint32_t x = 0; x < count; ++x) {
    if (!work.spend()) {
      return std::nullopt;
    }
    Cube cube;
    if (a.binate == 0) {
      // In normalized coordinates x is a minimal true point iff clearing
      // any set bit makes the function false. Irrelevant inputs disappear.
      if (!t.get(x ^ a.negative)) {
        continue;
      }
      bool minimal = true;
      for (auto mask = x; mask; mask &= mask - 1) {
        if (!work.spend()) {
          return std::nullopt;
        }
        if (t.get((x & ~(1U << std::countr_zero(mask))) ^ a.negative)) {
          minimal = false;
          break;
        }
      }
      if (!minimal) {
        continue;
      }
      cube = {x, x & ~a.negative};
    } else {
      if (!t.get(x) || covered.get(x)) {
        continue;
      }
      cube = {a.support, x & a.support};
      // Greedy signed implicant expansion. Enumerate only assignments in
      // the proposed cube; every visit is charged to the common work budget.
      for (uint32_t j = 0; j < t.inputs; ++j) {
        if (!(cube.care & (1U << j))) {
          continue;
        }
        const auto care  = cube.care & ~(1U << j);
        const auto ones  = cube.ones & care;
        const auto free  = (count - 1) & ~care;
        bool       valid = true;
        for (uint32_t sub = free;; sub = (sub - 1) & free) {
          if (!work.spend()) {
            return std::nullopt;
          }
          if (!t.get(ones | sub)) {
            valid = false;
            break;
          }
          if (sub == 0) {
            break;
          }
        }
        if (valid) {
          cube = {care, ones};
        }
      }
      const auto free = (count - 1) & ~cube.care;
      for (uint32_t sub = free;; sub = (sub - 1) & free) {
        if (!work.spend()) {
          return std::nullopt;
        }
        covered.set(cube.ones | sub, true);
        if (sub == 0) {
          break;
        }
      }
    }
    if (cubes.size() >= max_cubes) {
      return std::nullopt;
    }
    cubes.push_back(cube);
  }
  if (a.binate) {
    remove_redundant_cubes(cubes, t.inputs, work, exhausted);
  }
  return cubes;
}

class Factor {
public:
  Factor(Budget& work_arg, uint32_t max_nodes_arg) : work(work_arg), max_nodes(max_nodes_arg) {}
  Gate_formula formula;

  bool run(const std::vector<Cube>& cubes, int preference) { return build(cubes, preference).has_value(); }

private:
  Budget&  work;
  uint32_t max_nodes;

  std::optional<uint32_t> add(Gate_formula::Node n) {
    if (formula.nodes.size() >= max_nodes || !work.spend()) {
      return std::nullopt;
    }
    formula.nodes.push_back(n);
    return static_cast<uint32_t>(formula.nodes.size() - 1);
  }

  std::optional<uint32_t> build(const std::vector<Cube>& cubes, int preference = -1) {
    if (!work.spend(cubes.size() + 1)) {
      return std::nullopt;
    }
    if (cubes.empty()) {
      return add({Kind::constant});
    }
    if (std::any_of(cubes.begin(), cubes.end(), [](auto c) { return c.care == 0; })) {
      return add({Kind::constant, 0, 0, 0, true});
    }
    std::array<uint32_t, 2 * max_logical_inputs> counts{};
    for (const auto& c : cubes) {
      for (auto mask = c.care; mask; mask &= mask - 1) {
        if (!work.spend()) {
          return std::nullopt;
        }
        const auto j = std::countr_zero(mask);
        ++counts[2 * j + ((c.ones >> j) & 1)];
      }
    }
    auto choice = static_cast<uint32_t>(std::max_element(counts.begin(), counts.end()) - counts.begin());
    if (preference >= 0 && counts[static_cast<size_t>(preference)] != 0) {
      choice = static_cast<uint32_t>(preference);
    }
    const auto        bit      = 1U << (choice / 2);
    const auto        positive = choice & 1;
    std::vector<Cube> quotient, remainder;
    for (const auto& c : cubes) {
      if ((c.care & bit) && ((c.ones & bit) != 0) == (positive != 0)) {
        quotient.push_back({c.care & ~bit, c.ones & ~bit});
      } else {
        remainder.push_back(c);
      }
    }
    // Each recursive step removes a literal from the quotient and all its
    // occurrences from the remainder. Recursion depth is bounded by 2*n.
    auto literal = add({Kind::literal, 0, 0, choice / 2, positive == 0});
    if (!literal) {
      return std::nullopt;
    }
    auto product = literal;
    if (std::none_of(quotient.begin(), quotient.end(), [](auto c) { return c.care == 0; })) {
      auto q = build(quotient);
      if (!q) {
        return std::nullopt;
      }
      product = add({Kind::series, *literal, *q});
    }
    if (!product || remainder.empty()) {
      return product;
    }
    auto r = build(remainder);
    return r ? add({Kind::parallel, *product, *r}) : std::nullopt;
  }
};
}  // namespace

Gate_candidate synthesize_gate(const Truth_table& table, const Gate_constraints& constraints, Budget& work,
                               const Function_search_limits& limits) {
  Gate_candidate result;
  if (!valid_truth_table(table) || constraints.logical_inputs > max_logical_inputs || constraints.logical_inputs == 0
      || constraints.stack == 0 || constraints.branches == 0 || limits.max_cubes == 0 || limits.max_formula_nodes == 0
      || limits.factoring_choices == 0 || limits.factoring_choices > 2 * max_logical_inputs + 1) {
    result.status = Status::invalid;
    result.reason = "invalid function or limits";
    return result;
  }
  const auto analysis = analyze_function(table, work);
  if (analysis.status != Status::feasible) {
    result.search_exhausted = true;
    result.reason           = "function analysis budget";
    return result;
  }
  if (static_cast<uint32_t>(std::popcount(analysis.support)) > constraints.logical_inputs) {
    result.status = Status::unsupported;
    result.reason = "logical input limit";
    return result;
  }
  // Try SOP(F), its output-complement realization, and the dual POS forms.
  // Each realization exposes the other polarity through the same cell.
  for (uint32_t polarity = 0; polarity < 2 && !work.exhausted; ++polarity) {
    const auto t = polarity ? table.complement() : table;
    const auto a = polarity ? analyze_function(t, work) : analysis;
    if (a.status != Status::feasible) {
      break;
    }
    auto cubes = cover(t, a, work, limits.max_cubes, result.search_exhausted);
    if (!cubes) {
      result.search_exhausted = true;
      continue;
    }
    for (uint32_t choice = 0; choice < limits.factoring_choices && !work.exhausted; ++choice) {
      Factor f(work, limits.max_formula_nodes);
      if (!f.run(*cubes, static_cast<int>(choice) - 1)) {
        result.search_exhausted = true;
        continue;
      }
      for (uint32_t dual = 0; dual < 2; ++dual) {
        auto candidate            = f.formula;
        candidate.output_inverted = (polarity != 0) != (dual != 0);
        if (dual) {
          for (auto& n : candidate.nodes) {
            if (n.kind == Kind::series) {
              n.kind = Kind::parallel;
            } else if (n.kind == Kind::parallel) {
              n.kind = Kind::series;
            } else {
              n.inverted = !n.inverted;
            }
          }
        }
        if (!work.spend(candidate.nodes.size())) {
          break;
        }
        const auto cost = candidate.metrics();
        if (!cost || cost->stack > constraints.stack || cost->branches > constraints.branches
            || (result.formula && cost->transistors >= result.cost.transistors)) {
          continue;
        }
        const auto evaluated = candidate.evaluate_table(table.inputs, work);
        if (evaluated && *evaluated != table) {
          throw std::logic_error("USYN factoring changed the logical function");
        }
        if (evaluated) {
          result.cost    = *cost;
          result.formula = std::move(candidate);
          result.status  = Status::feasible;
        }
      }
    }
  }
  result.search_exhausted |= work.exhausted;
  result.reason = result.formula ? "legal formula" : result.search_exhausted ? "function search budget" : "no legal formula found";
  return result;
}

Divisor_function derive_divisor_function(const Truth_table& root, std::span<const Truth_table> divisors, Budget& work,
                                         bool conflict_witness) {
  Divisor_function result;
  if (!valid_truth_table(root) || divisors.size() > max_logical_inputs
      || std::any_of(divisors.begin(), divisors.end(), [&](const auto& d) {
           return d.inputs != root.inputs || !valid_truth_table(d);
         })) {
    return result;
  }
  result.function = Truth_table(static_cast<uint32_t>(divisors.size()));
  result.care     = Truth_table(static_cast<uint32_t>(divisors.size()));
  for (uint32_t x = 0; x < (1U << root.inputs); ++x) {
    if (!work.spend(divisors.size() + 1)) {
      result.status = Status::search_exhausted;
      return result;
    }
    uint32_t image = 0;
    for (uint32_t j = 0; j < divisors.size(); ++j) {
      image |= static_cast<uint32_t>(divisors[j].get(x)) << j;
    }
    if (result.care.get(image) && result.function.get(image) != root.get(x)) {
      if (conflict_witness) {
        // Recover one witness without a 2^K array of original assignments.
        // At most one additional bounded scan precedes the same conflict.
        for (uint32_t previous = 0; previous < x; ++previous) {
          if (!work.spend(divisors.size() + 1)) {
            result.status = Status::search_exhausted;
            return result;
          }
          if (root.get(previous) == root.get(x)) {
            continue;
          }
          uint32_t previous_image = 0;
          for (uint32_t j = 0; j < divisors.size(); ++j) {
            previous_image |= static_cast<uint32_t>(divisors[j].get(previous)) << j;
          }
          if (previous_image == image) {
            result.conflict = std::pair{previous, x};
            break;
          }
        }
      }
      result.status = Status::unsupported;
      return result;
    }
    result.care.set(image, true);
    result.function.set(image, root.get(x));
  }
  result.status = Status::feasible;
  return result;
}

Function_completion monotone_completion(const Truth_table& function, const Truth_table& care, uint32_t negative_mask,
                                        Budget& work) {
  Function_completion result;
  if (!valid_truth_table(function) || !valid_truth_table(care) || function.inputs != care.inputs
      || negative_mask >= (1U << function.inputs)) {
    return result;
  }
  Truth_table closure(function.inputs);
  result.status         = Status::search_exhausted;
  const auto word_phase = negative_mask >> 6;
  for (size_t w = 0; w < closure.words.size(); ++w) {
    if (!work.spend(1 + std::popcount(negative_mask & 63U))) {
      return result;
    }
    const auto original = w ^ word_phase;
    closure.words[w]    = phase_word(care.words[original] & function.words[original], negative_mask);
  }
  // Upward closure of required true points; compare against required zeros
  // only after restoring the original input phases.
  for (uint32_t j = 0; j < function.inputs; ++j) {
    if (j < 6) {
      for (auto& word : closure.words) {
        if (!work.spend()) {
          return result;
        }
        word |= (word & ~variable_words[j]) << (1U << j);
      }
    } else {
      const auto stride = size_t{1} << (j - 6);
      for (size_t base = 0; base < closure.words.size(); base += 2 * stride) {
        for (size_t k = 0; k < stride; ++k) {
          if (!work.spend()) {
            return result;
          }
          closure.words[base + stride + k] |= closure.words[base + k];
        }
      }
    }
  }
  result.table = Truth_table(function.inputs);
  for (size_t w = 0; w < closure.words.size(); ++w) {
    if (!work.spend(1 + std::popcount(negative_mask & 63U))) {
      return result;
    }
    const auto value = phase_word(closure.words[w ^ word_phase], negative_mask) & tail_mask(function.inputs);
    if (((value ^ function.words[w]) & care.words[w]) != 0) {
      result.status = Status::unsupported;
      return result;
    }
    result.table.words[w] = value;
  }
  result.status = Status::feasible;
  return result;
}

Function_completions function_completions(const Truth_table& function, const Truth_table& care, uint32_t phase_limit,
                                          Budget& work) {
  Function_completions out;
  if (!valid_truth_table(function) || !valid_truth_table(care) || function.inputs != care.inputs
      || phase_limit > max_completion_phases) {
    return out;
  }
  out.status        = Status::search_exhausted;
  const auto count  = 1U << function.inputs;
  const auto retain = [&](Truth_table table) {
    if (!work.spend(table.words.size() * (out.tables.size() + 1))) {
      out.search_exhausted = true;
      return false;
    }
    if (std::find(out.tables.begin(), out.tables.end(), table) == out.tables.end()) {
      out.tables.push_back(std::move(table));
      out.status = Status::feasible;
    }
    return true;
  };
  Truth_table zero(function.inputs), one(function.inputs);
  bool        partial = false;
  for (size_t w = 0; w < function.words.size(); ++w) {
    if (!work.spend()) {
      out.search_exhausted = true;
      return out;
    }
    const auto mask  = tail_mask(function.inputs);
    zero.words[w]    = care.words[w] & function.words[w];
    one.words[w]     = (~care.words[w] | function.words[w]) & mask;
    partial         |= care.words[w] != mask;
  }

  if (!retain(std::move(zero)) || !partial || !retain(std::move(one))) {
    return out;
  }
  std::vector<uint32_t> phases;
  const auto            visit = [&](uint32_t phase) {
    if (phases.size() >= phase_limit || std::find(phases.begin(), phases.end(), phase) != phases.end()) {
      return true;
    }
    phases.push_back(phase);
    ++out.phases;
    for (bool inverted : {false, true}) {
      if (!work.spend(function.words.size())) {
        out.search_exhausted = true;
        return false;
      }
      // Complementing the result reverses the order. Reverse the input order
      // too, so both extrema obey the same requested phase assignment.
      auto completed
          = monotone_completion(inverted ? function.complement() : function, care, inverted ? phase ^ (count - 1) : phase, work);
      if (completed.status == Status::search_exhausted) {
        out.search_exhausted = true;
        return false;
      }
      if (completed.status == Status::feasible && !retain(inverted ? completed.table.complement() : std::move(completed.table))) {
        return false;
      }
    }
    return true;
  };
  // Cover both global polarities and single-rail changes before spending the
  // remaining bounded trials on numeric masks, which favor low-index inputs.
  if (!visit(0) || !visit(count - 1)) {
    return out;
  }
  for (uint32_t i = 0; i < function.inputs && phases.size() < phase_limit; ++i) {
    if (!visit(1U << i) || !visit((count - 1) ^ (1U << i))) {
      return out;
    }
  }
  for (uint32_t phase = 0; phase < count && phases.size() < phase_limit; ++phase) {
    if (!work.spend() || !visit(phase)) {
      out.search_exhausted = true;
      return out;
    }
  }
  out.search_exhausted |= phases.size() < count;
  return out;
}

}  // namespace livehd::usyn
