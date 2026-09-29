// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "unate.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace livehd::usyn {
namespace {
bool valid_table(const Truth_table& t) {
  if (t.inputs > max_logical_inputs || t.words.size() != ((uint32_t{1} << t.inputs) + 63) / 64) {
    return false;
  }
  const auto bits = uint32_t{1} << t.inputs;
  return bits >= 64 || (t.words.back() >> bits) == 0;
}

bool covers(const Cube& c, uint32_t v) { return (v & c.care) == c.ones; }
}  // namespace


Truth_table::Truth_table(uint32_t n, bool value) : inputs(n) {
  if (n > max_logical_inputs) {
    throw std::invalid_argument("unate truth table exceeds supported 12 inputs");
  }
  const uint32_t bits = uint32_t{1} << n;
  words.assign((bits + 63) / 64, value ? ~uint64_t{0} : 0);
  if (value && bits < 64) {
    words.back() &= (uint64_t{1} << bits) - 1;
  }
}
bool Truth_table::get(uint32_t x) const { return ((words.at(x / 64) >> (x % 64)) & 1) != 0; }
void Truth_table::set(uint32_t x, bool value) {
  auto&      w    = words.at(x / 64);
  const auto mask = uint64_t{1} << (x % 64);
  w               = value ? w | mask : w & ~mask;
}
Truth_table Truth_table::complement() const {
  Truth_table result(inputs, true);
  for (size_t i = 0; i < words.size(); ++i) {
    result.words[i] ^= words[i];
  }
  return result;
}
const char* status_name(Status status) {
  switch (status) {
    case Status::feasible        : return "feasible";
    case Status::search_exhausted: return "search_exhausted";
    case Status::unsupported     : return "unsupported";
    case Status::invalid         : return "invalid";
  }
  return "invalid";
}
bool Budget::spend(uint64_t amount) {
  if (exhausted) {
    return false;
  }
  if (admission && (checkpoint_work == 0 || amount >= checkpoint_work)) {
    if (!admission()) {
      resource_exhausted = exhausted = true;
      return false;
    }
    checkpoint_work = admission_interval;
  }
  checkpoint_work -= std::min(checkpoint_work, amount);
  if (exhausted || amount > remaining) {
    exhausted = true;
    return false;
  }
  remaining -= amount;
  return true;
}

Form make_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, Budget& budget) {
  if (!valid_table(table)) {
    Form result;
    result.status = Status::invalid;
    return result;
  }
  return make_form(table, Truth_table(table.inputs, true), max_literals, max_series, budget);
}

Form make_form(const Truth_table& table, const Truth_table& care_mask, uint32_t max_literals, uint32_t max_series, Budget& budget) {
  Form result;
  if (!valid_table(table) || !valid_table(care_mask) || table.inputs != care_mask.inputs) {
    result.status = Status::invalid;
    return result;
  }
  // Enumerate primes by merging adjacent cubes. This is exhaustive only when
  // the shared work budget permits completion. No depth-infeasibility claim is
  // made here: selection and the later decomposition are heuristic.
  using Key = std::pair<uint32_t, uint32_t>;
  std::set<Key>  layer, primes;
  const uint32_t count = uint32_t{1} << table.inputs;
  for (uint32_t x = 0; x < count; ++x) {
    if (!budget.spend()) {
      return result;
    }
    if (table.get(x) || !care_mask.get(x)) {
      layer.emplace(count - 1, x);
    }
  }
  while (!layer.empty()) {
    std::set<Key> next;
    for (auto [care, ones] : layer) {
      bool merged = false;
      for (uint32_t j = 0; j < table.inputs; ++j) {
        if (!budget.spend()) {
          return result;
        }
        const auto bit = uint32_t{1} << j;
        if ((care & bit) && layer.contains({care, ones ^ bit})) {
          merged = true;
          next.emplace(care ^ bit, ones & ~bit);
        }
      }
      if (!merged && static_cast<uint32_t>(std::popcount(care)) <= max_series) {
        primes.emplace(care, ones);
      }
    }
    layer = std::move(next);
  }
  std::vector<uint32_t> uncovered;
  for (uint32_t x = 0; x < count; ++x) {
    if (table.get(x) && care_mask.get(x)) {
      uncovered.push_back(x);
    }
  }
  while (!uncovered.empty()) {
    Cube     best;
    uint32_t best_gain = 0;
    for (auto [care, ones] : primes) {
      uint32_t gain = 0;
      for (auto x : uncovered) {
        if (!budget.spend()) {
          return result;
        }
        gain += covers({care, ones}, x);
      }
      if (gain > best_gain || (gain == best_gain && std::popcount(care) < std::popcount(best.care))) {
        best      = {care, ones};
        best_gain = gain;
      }
    }
    if (best_gain == 0) {
      return result;
    }
    result.cubes.push_back(best);
    result.literals += std::popcount(best.care);
    result.series    = std::max(result.series, static_cast<uint32_t>(std::popcount(best.care)));
    result.positive |= best.ones;
    result.negative |= best.care & ~best.ones;
    if (result.literals > max_literals) {
      return result;
    }
    std::erase_if(uncovered, [&](uint32_t x) { return covers(best, x); });
  }
  if (check_form(table, care_mask, result, budget)) {
    result.status = Status::feasible;
  } else if (!budget.exhausted) {
    result.status = Status::invalid;
  }
  return result;
}

namespace {
bool contains(const Cube& big, const Cube& small) {  // small's literals are a subset of big's
  return (big.care & small.care) == small.care && (big.ones & small.care) == small.ones;
}
Cube strip(const Cube& c, const Cube& d) { return {c.care & ~d.care, c.ones & ~d.care}; }
bool same(const Cube& a, const Cube& b) { return a.care == b.care && a.ones == b.ones; }

// The literal (var, polarity) in the most cubes; count 0 when none repeats.
std::tuple<uint32_t, uint32_t, bool> frequent_literal(const std::vector<Cube>& cubes) {
  uint32_t best_count = 0, best_var = 0;
  bool     best_one   = false;
  for (uint32_t j = 0; j < 32; ++j) {
    uint32_t pos = 0, neg = 0;
    for (const auto& c : cubes) {
      if ((c.care >> j) & 1) {
        ((c.ones >> j) & 1 ? pos : neg) += 1;
      }
    }
    if (pos > best_count) {
      best_count = pos, best_var = j, best_one = true;
    }
    if (neg > best_count) {
      best_count = neg, best_var = j, best_one = false;
    }
  }
  return {best_count > 1 ? best_count : 0, best_var, best_one};
}

uint32_t sop_literals(const std::vector<Cube>& cubes) {
  uint32_t sum = 0;
  for (const auto& c : cubes) {
    sum += std::popcount(c.care);
  }
  return sum;
}

// Algebraic division F / D: the cubes q with q*d in F for every d in D.
std::vector<Cube> divide(const std::vector<Cube>& f, const std::vector<Cube>& d) {
  std::vector<Cube> quotient;
  bool              first = true;
  for (const auto& dc : d) {
    std::vector<Cube> part;
    for (const auto& fc : f) {
      if (contains(fc, dc)) {
        part.push_back(strip(fc, dc));
      }
    }
    if (first) {
      quotient = std::move(part);
      first    = false;
    } else {
      std::erase_if(quotient, [&](const Cube& q) {
        return std::none_of(part.begin(), part.end(), [&](const Cube& p) { return same(p, q); });
      });
    }
  }
  return quotient;
}

uint32_t literal_factor(const std::vector<Cube>& cubes);
uint32_t kernel_factor(const std::vector<Cube>& cubes);
uint32_t literal_factor_bound(const std::vector<Cube>& cubes) { return literal_factor(cubes); }

// f = l * Q + R on the most frequent literal.
uint32_t literal_factor(const std::vector<Cube>& cubes) {
  if (cubes.size() <= 1) {
    return cubes.empty() ? 0 : static_cast<uint32_t>(std::popcount(cubes.front().care));
  }
  const auto [count, var, one] = frequent_literal(cubes);
  if (count == 0) {
    return sop_literals(cubes);
  }
  std::vector<Cube> quotient, rest;
  const Cube        lit{uint32_t{1} << var, one ? uint32_t{1} << var : 0};
  for (const auto& c : cubes) {
    (contains(c, lit) ? quotient : rest).push_back(contains(c, lit) ? strip(c, lit) : c);
  }
  return 1 + kernel_factor(quotient) + kernel_factor(rest);
}

// f = Q * K + R with K a kernel of f (divide by frequent literals until no
// literal repeats), recursively -- the better of that and literal_factor.
uint32_t kernel_factor(const std::vector<Cube>& cubes) {
  if (cubes.empty()) {
    return 0;
  }
  if (std::any_of(cubes.begin(), cubes.end(), [](const Cube& c) { return c.care == 0; })) {
    return 0;  // contains the constant-1 term
  }
  if (cubes.size() == 1) {
    return static_cast<uint32_t>(std::popcount(cubes.front().care));
  }
  auto kernel = cubes;
  while (true) {
    const auto [count, var, one] = frequent_literal(kernel);
    if (count == 0) {
      break;
    }
    const Cube        lit{uint32_t{1} << var, one ? uint32_t{1} << var : 0};
    std::vector<Cube> next;
    for (const auto& c : kernel) {
      if (contains(c, lit)) {
        next.push_back(strip(c, lit));
      }
    }
    kernel = std::move(next);
  }
  uint32_t best = literal_factor_bound(cubes);
  if (kernel.size() >= 2 && kernel.size() < cubes.size()) {
    const auto quotient = divide(cubes, kernel);
    if (!quotient.empty()) {
      std::vector<Cube> rest;
      for (const auto& fc : cubes) {
        const bool covered = std::any_of(quotient.begin(), quotient.end(), [&](const Cube& q) {
          return std::any_of(kernel.begin(), kernel.end(), [&](const Cube& k) {
            const Cube prod{q.care | k.care, q.ones | k.ones};
            return same(prod, fc);
          });
        });
        if (!covered) {
          rest.push_back(fc);
        }
      }
      best = std::min(best, kernel_factor(quotient) + kernel_factor(kernel) + kernel_factor(rest));
    }
  }
  return best;
}
}  // namespace

uint32_t factor_literals(const std::vector<Cube>& cubes) { return kernel_factor(cubes); }

namespace {
// Minterm sets of an at most 8-input function.
struct Minterms {
  std::array<uint64_t, 4> w{};
  Minterms operator&(const Minterms& o) const {
    Minterms r;
    for (size_t i = 0; i < 4; ++i) {
      r.w[i] = w[i] & o.w[i];
    }
    return r;
  }
  Minterms operator|(const Minterms& o) const {
    Minterms r;
    for (size_t i = 0; i < 4; ++i) {
      r.w[i] = w[i] | o.w[i];
    }
    return r;
  }
  Minterms operator~() const {
    Minterms r;
    for (size_t i = 0; i < 4; ++i) {
      r.w[i] = ~w[i];
    }
    return r;
  }
  bool     operator==(const Minterms&) const = default;
  bool     any() const { return (w[0] | w[1] | w[2] | w[3]) != 0; }
  uint32_t count() const {
    return static_cast<uint32_t>(std::popcount(w[0]) + std::popcount(w[1]) + std::popcount(w[2]) + std::popcount(w[3]));
  }
  bool get(uint32_t x) const { return ((w[x / 64] >> (x % 64)) & 1) != 0; }
  void set(uint32_t x) { w[x / 64] |= uint64_t{1} << (x % 64); }
};
}  // namespace

Form exact_form(const Truth_table& table, uint32_t max_literals, uint32_t max_series, bool factored, Budget& budget) {
  if (!valid_table(table) || table.inputs > 8) {
    auto form = make_form(table, factored ? 4096 : max_literals, max_series, budget);
    if (form.status == Status::feasible) {
      form.factored = factor_literals(form.cubes);
      if ((factored ? form.factored : form.literals) > max_literals) {
        form.status = Status::search_exhausted;
      }
    }
    return form;
  }
  Form           result;
  const uint32_t n     = table.inputs;
  const uint32_t count = uint32_t{1} << n;
  Minterms       full, on;
  for (uint32_t x = 0; x < count; ++x) {
    full.set(x);
    if (table.get(x)) {
      on.set(x);
    }
  }
  std::array<Minterms, 8> var{};
  for (uint32_t j = 0; j < n; ++j) {
    for (uint32_t x = 0; x < count; ++x) {
      if ((x >> j) & 1) {
        var[j].set(x);
      }
    }
  }
  const auto cube_mask = [&](uint32_t care, uint32_t ones) {
    Minterms m = full;
    for (uint32_t j = 0; j < n; ++j) {
      if ((care >> j) & 1) {
        m = m & (((ones >> j) & 1) ? var[j] : ~var[j]);
      }
    }
    return m & full;
  };
  const auto finish = [&](std::vector<Cube> cubes) {
    result.cubes = std::move(cubes);
    for (const auto& c : result.cubes) {
      result.literals += std::popcount(c.care);
      result.series    = std::max(result.series, static_cast<uint32_t>(std::popcount(c.care)));
      result.positive |= c.ones;
      result.negative |= c.care & ~c.ones;
    }
    result.factored = factor_literals(result.cubes);
    const bool fits = (factored ? result.factored : result.literals) <= max_literals && result.series <= max_series;
    result.status   = fits && check_form(table, result, budget) ? Status::feasible : Status::search_exhausted;
    return result;
  };
  if (!on.any()) {
    return finish({});
  }
  if (on == full) {
    return finish({Cube{0, 0}});
  }
  // Every implicant of at most max_series literals, then the primes among them.
  struct Prime {
    uint32_t care, ones, literals;
    Minterms mask;
  };
  std::vector<Prime> primes;
  const Minterms     off = ~on & full;
  for (uint32_t care = 1; care < count; ++care) {
    const auto lits = static_cast<uint32_t>(std::popcount(care));
    if (lits > max_series || !budget.spend(uint64_t{1} << lits)) {
      if (budget.exhausted) {
        return result;
      }
      continue;
    }
    for (uint32_t ones = care;; ones = (ones - 1) & care) {
      const auto mask = cube_mask(care, ones);
      if (mask.any() && !(mask & off).any()) {
        bool prime = true;
        for (uint32_t j = 0; j < n && prime; ++j) {
          if ((care >> j) & 1) {
            const auto bigger = cube_mask(care & ~(uint32_t{1} << j), ones & ~(uint32_t{1} << j));
            prime             = (bigger & off).any();
          }
        }
        if (prime) {
          primes.push_back({care, ones, lits, mask});
        }
      }
      if (ones == 0) {
        break;
      }
    }
  }
  Minterms reachable;
  for (const auto& p : primes) {
    reachable = reachable | p.mask;
  }
  if (!((reachable & on) == on)) {
    return result;  // some minterm needs a product longer than max_series
  }
  std::sort(primes.begin(), primes.end(), [](const Prime& a, const Prime& b) {
    return a.literals != b.literals ? a.literals < b.literals : a.mask.count() > b.mask.count();
  });
  // Branch and bound on the uncovered minterm with the fewest covering primes.
  std::vector<std::vector<uint32_t>> covering(count);
  for (uint32_t i = 0; i < primes.size(); ++i) {
    for (uint32_t x = 0; x < count; ++x) {
      if (primes[i].mask.get(x)) {
        covering[x].push_back(i);
      }
    }
  }
  std::vector<uint32_t> chosen, best;
  uint64_t              best_cost = std::numeric_limits<uint64_t>::max();
  uint64_t              nodes     = 0;
  const auto            score     = [](uint64_t literals, uint64_t cubes) { return (literals << 16) | cubes; };
  {
    // Greedy seed (most new minterms, then fewest literals): an upper bound,
    // and the answer if the node limit stops the exact search.
    Minterms covered;
    uint64_t literals = 0;
    while (!((covered & on) == on)) {
      uint32_t pick = 0, gain = 0;
      for (uint32_t i = 0; i < primes.size(); ++i) {
        const auto g = (primes[i].mask & on & ~covered).count();
        if (g > gain) {
          pick = i, gain = g;
        }
      }
      best.push_back(pick);
      covered   = covered | primes[pick].mask;
      literals += primes[pick].literals;
    }
    best_cost = score(literals, best.size());
  }
  std::function<void(const Minterms&, uint64_t)> search = [&](const Minterms& covered, uint64_t literals) {
    if (++nodes > 20000 || !budget.spend()) {
      return;
    }
    if ((covered & on) == on) {
      if (score(literals, chosen.size()) < best_cost) {
        best_cost = score(literals, chosen.size());
        best      = chosen;
      }
      return;
    }
    const auto open = on & ~covered;
    uint32_t   pick = count;
    for (uint32_t x = 0; x < count; ++x) {
      if (open.get(x) && (pick == count || covering[x].size() < covering[pick].size())) {
        pick = x;
      }
    }
    for (auto i : covering[pick]) {
      if (score(literals + primes[i].literals, chosen.size() + 1) >= best_cost) {
        continue;
      }
      chosen.push_back(i);
      search(covered | primes[i].mask, literals + primes[i].literals);
      chosen.pop_back();
    }
  };
  search(Minterms{}, 0);
  if (best.empty()) {
    return result;
  }
  std::vector<Cube> cubes;
  for (auto i : best) {
    cubes.push_back({primes[i].care, primes[i].ones});
  }
  return finish(std::move(cubes));
}

bool check_form(const Truth_table& table, const Form& form, Budget& budget) {
  return valid_table(table) && check_form(table, Truth_table(table.inputs, true), form, budget);
}

bool check_form(const Truth_table& table, const Truth_table& care_mask, const Form& form, Budget& budget) {
  if (!valid_table(table) || !valid_table(care_mask) || table.inputs != care_mask.inputs) {
    return false;
  }
  const auto count    = uint32_t{1} << table.inputs;
  uint32_t   literals = 0, series = 0, positive = 0, negative = 0;
  for (const auto& c : form.cubes) {
    if ((c.ones & ~c.care) || c.care >= count) {
      return false;
    }
    literals += std::popcount(c.care);
    series    = std::max(series, static_cast<uint32_t>(std::popcount(c.care)));
    positive |= c.ones;
    negative |= c.care & ~c.ones;
  }
  if (literals != form.literals || series != form.series || positive != form.positive || negative != form.negative) {
    return false;
  }
  for (uint32_t x = 0; x < count; ++x) {
    bool y = false;
    for (const auto& c : form.cubes) {
      if (!budget.spend()) {
        return false;
      }
      y |= covers(c, x);
    }
    if (care_mask.get(x) && y != table.get(x)) {
      return false;
    }
  }
  return true;
}

}  // namespace livehd::usyn
