// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lut_cover.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <span>
#include <unordered_map>

namespace livehd::usyn {
namespace {
using livehd::synth::Lnet;

// Tables are 256-bit words over 8 variables (bit x of word x/64 is f(x)); a
// function of its first n variables is stored REPLICATED, so it ignores
// variables n..7.
using Table = std::array<uint64_t, 4>;
constexpr std::array<uint64_t, 6> kVar{0xAAAAAAAAAAAAAAAAULL,
                                       0xCCCCCCCCCCCCCCCCULL,
                                       0xF0F0F0F0F0F0F0F0ULL,
                                       0xFF00FF00FF00FF00ULL,
                                       0xFFFF0000FFFF0000ULL,
                                       0xFFFFFFFF00000000ULL};
constexpr Table                   kOnes{~uint64_t{0}, ~uint64_t{0}, ~uint64_t{0}, ~uint64_t{0}};
constexpr uint8_t                 kDeep = std::numeric_limits<uint8_t>::max();  // not a pure domino chain

Table var_table(uint32_t j) {
  Table t{};
  for (uint32_t w = 0; w < 4; ++w) {
    t[w] = j < 6 ? kVar[j] : (((w >> (j - 6)) & 1) ? ~uint64_t{0} : 0);
  }
  return t;
}
uint64_t swap64(uint64_t t, uint32_t i, uint32_t j) {  // i < j < 6
  const uint32_t shift = (1U << j) - (1U << i);
  const uint64_t m     = kVar[i] & ~kVar[j];  // var i = 1, var j = 0
  return (t & ~(m | (m << shift))) | ((t & m) << shift) | ((t >> shift) & m);
}
Table swap_vars(Table t, uint32_t i, uint32_t j) {
  if (i == j) {
    return t;
  }
  if (i > j) {
    std::swap(i, j);
  }
  if (j < 6) {
    for (auto& w : t) {
      w = swap64(w, i, j);
    }
  } else if (i < 6) {
    // var j selects words: exchange (var i = 1, var j = 0) with (var i = 0, var j = 1).
    const uint32_t b = j - 6, s = 1U << i;
    for (uint32_t w = 0; w < 4; ++w) {
      if ((w >> b) & 1) {
        continue;
      }
      const uint64_t lo = t[w], hi = t[w | (1U << b)];
      t[w]              = (lo & ~kVar[i]) | ((hi & ~kVar[i]) << s);
      t[w | (1U << b)]  = (hi & kVar[i]) | ((lo & kVar[i]) >> s);
    }
  } else {
    std::swap(t[1], t[2]);  // vars 6 and 7
  }
  return t;
}
bool depends(const Table& t, uint32_t j) {
  if (j < 6) {
    const uint32_t s = 1U << j;
    return std::any_of(t.begin(), t.end(), [&](uint64_t w) { return ((w >> s) & ~kVar[j]) != (w & ~kVar[j]); });
  }
  const uint32_t b = j - 6;
  for (uint32_t w = 0; w < 4; ++w) {
    if (!((w >> b) & 1) && t[w] != t[w | (1U << b)]) {
      return true;
    }
  }
  return false;
}
uint64_t low_bits(uint64_t t, uint32_t n) { return n < 6 ? t & ((uint64_t{1} << (1U << n)) - 1) : t; }
Truth_table to_truth(const Table& t, uint32_t n) {
  Truth_table r(n);
  if (n <= 6) {
    r.words[0] = low_bits(t[0], n);
  } else {
    for (size_t i = 0; i < r.words.size(); ++i) {
      r.words[i] = t[i];
    }
  }
  return r;
}

struct Cut {
  std::array<Id, 8> leaves{};
  uint8_t           size  = 0;
  uint8_t           depth = 0;  // over the minimum leaf depths (enumeration ranking only)
  uint32_t          fn    = 0;
  Table             table{};  // replicated, over `leaves` in order
  float             flow = 0;
  bool              trivial  = false;  // the node itself as a leaf (a fan-in choice only)
  bool              tree     = true;   // every absorbed node has one reader (no reconvergence to check)
  bool              dup_free = true;   // every absorbed node's readers are inside the gate
};
bool same_leaves(const Cut& a, const Cut& b) {
  return a.size == b.size && std::equal(a.leaves.begin(), a.leaves.begin() + a.size, b.leaves.begin());
}
bool subset(const Cut& a, const Cut& b) {
  return a.size <= b.size && std::includes(b.leaves.begin(), b.leaves.begin() + b.size, a.leaves.begin(), a.leaves.begin() + a.size);
}
// The cut's table over the sorted union `u` of leaves (a superset of its own).
Table expand(const Cut& c, const Id* u, uint32_t us) {
  Table t = c.table;
  for (uint32_t i = c.size; i-- > 0;) {
    t = swap_vars(t, i, static_cast<uint32_t>(std::lower_bound(u, u + us, c.leaves[i]) - u));
  }
  return t;
}
// Drop the leaves outside the support.
void reduce(Cut& c) {
  for (uint32_t j = c.size; j-- > 0;) {
    if (depends(c.table, j)) {
      continue;
    }
    for (uint32_t q = j; q + 1 < c.size; ++q) {
      c.table     = swap_vars(c.table, q, q + 1);
      c.leaves[q] = c.leaves[q + 1];
    }
    --c.size;
  }
}
// A table over `size` inputs on 64 parallel patterns (Shannon, last input first).
uint64_t eval_table(const uint64_t* table, uint32_t size, const uint64_t* in) {
  std::array<uint64_t, 256> w;
  for (uint32_t m = 0; m < (1U << size); ++m) {
    w[m] = ((table[m / 64] >> (m % 64)) & 1) ? ~uint64_t{0} : 0;
  }
  for (uint32_t j = size; j-- > 0;) {
    const uint32_t half = 1U << j;
    for (uint32_t m = 0; m < half; ++m) {
      w[m] = (in[j] & w[m + half]) | (~in[j] & w[m]);
    }
  }
  return w[0];
}

struct Table_hash {
  size_t operator()(const Table& t) const {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (auto w : t) {
      h = (h ^ w) * 0x100000001b3ULL;
      h ^= h >> 29;
    }
    return static_cast<size_t>(h);
  }
};
// Costs by support-reduced table (which determines its input count).
class Store {
public:
  Store(const Recipe& recipe, const Cover_cost& cost) : recipe(recipe), cost(cost) {}
  const Recipe&                                  recipe;
  const Cover_cost&                              cost;
  Budget                                         budget{std::numeric_limits<uint64_t>::max() / 4};
  std::unordered_map<Table, uint32_t, Table_hash> index;
  std::vector<Function_cost>                     fns;
  uint32_t                                       get(const Table& table, uint32_t inputs) {
    auto [it, fresh] = index.try_emplace(table, static_cast<uint32_t>(fns.size()));
    if (fresh) {
      fns.push_back(function_cost(to_truth(table, inputs), recipe, cost, budget));
    }
    return it->second;
  }
};
}  // namespace

Function_cost function_cost(const Truth_table& t, const Recipe& recipe, const Cover_cost& cost, Budget& budget) {
  Function_cost  result;
  const uint32_t inputs = t.inputs;
  result.constant       = inputs == 0;
  result.alias          = inputs == 1;
  if (inputs <= 1) {
    result.form = exact_form(t, recipe.literals, recipe.series, false, budget);
    return result;
  }
  const auto comp = t.complement();
  if (inputs <= recipe.support) {
    for (uint32_t sign = 0; sign < 2; ++sign) {
      auto form = exact_form(sign ? comp : t, recipe.literals, recipe.series, true, budget);
      if (form.status != Status::feasible) {
        continue;
      }
      const auto c = cost.domino_overhead + form.factored;
      if (!result.domino || c < result.cost) {
        result.domino       = true;
        result.complemented = sign != 0;
        result.cost         = c;
        result.form         = std::move(form);
      }
    }
  }
  if (result.domino) {
    return result;
  }
  bool found = false;
  for (uint32_t sign = 0; sign < 2; ++sign) {
    auto form = exact_form(sign ? comp : t, 4096, inputs, true, budget);
    if (form.status != Status::feasible) {
      continue;
    }
    const auto c = cost.nonunate_penalty * (cost.cmos_factor * form.factored + cost.static_overhead);
    if (!found || c < result.cost) {
      found               = true;
      result.complemented = sign != 0;
      result.cost         = c;
      result.form         = std::move(form);
    }
  }
  return result;
}

Cover_result lut_cover(const Lnet& source, const Search_options& options) {
  Cover_result result;
  const auto   outputs = livehd::synth::combinational_outputs(source);
  if (std::ranges::find(outputs, Lnet::kNone) != outputs.end()) {
    result.status = Status::invalid;
    result.reason = "invalid source network (a latch input is unset)";
    return result;
  }
  const auto&    recipe = options.recipe;
  const uint32_t k      = recipe.support;
  const auto     n      = static_cast<Id>(source.size());
  if (k < 2 || k > 8 || options.cover_cuts == 0 || options.cover_cuts > 64 || n > options.max_nodes) {
    result.status = Status::unsupported;
    result.reason = "unsupported cover options (support 2..8, cover_cuts 1..64)";
    return result;
  }
  enum : uint8_t { kSource, kConstant, kInternal };
  std::vector<uint8_t> kind(n);
  std::vector<float>   est(n, 0);  // fanout estimate for area flow
  std::vector<uint32_t> fanout(n, 0);
  for (Id v = 0; v < n; ++v) {
    kind[v] = source.kind(v) == Lnet::Kind::source ? kSource : source.fanin_count(v) == 0 ? kConstant : kInternal;
    if (source.fanin_count(v) > k) {
      result.status = Status::unsupported;
      result.reason = "a source node has more inputs than the LUT support";
      return result;
    }
    for (auto in : source.fanins(v)) {
      ++fanout[in];
    }
  }
  for (auto o : outputs) {
    ++fanout[o];
  }
  for (Id v = 0; v < n; ++v) {
    est[v] = std::max(1.0f, static_cast<float>(fanout[v]));
  }
  const auto internal = [&](Id v) { return kind[v] == kInternal; };
  // No duplication: readers of every node (CSR) and the network outputs, for
  // the check that a gate absorbs a multi-reader node only with all its readers.
  std::vector<uint8_t>  is_output(n, 0);
  std::vector<uint32_t> reader_begin(n + 1, 0);
  std::vector<Id>       readers;
  if (!options.duplicate) {
    for (auto o : outputs) {
      is_output[o] = 1;
    }
    for (Id v = 0; v < n; ++v) {
      for (auto in : source.fanins(v)) {
        ++reader_begin[in + 1];
      }
    }
    for (Id v = 0; v < n; ++v) {
      reader_begin[v + 1] += reader_begin[v];
    }
    readers.resize(reader_begin[n]);
    auto fill = reader_begin;
    for (Id v = 0; v < n; ++v) {
      for (auto in : source.fanins(v)) {
        readers[fill[in]++] = v;
      }
    }
  }
  std::vector<uint32_t> cone_stamp(n, 0);
  uint32_t              cone_epoch = 0;
  std::vector<Id>       cone, cone_stack;
  // The gate rooted at v over the structural cut `u` computes each absorbed
  // node only for itself: every absorbed node's readers are inside the gate.
  const auto duplication_free = [&](Id v, const Id* u, uint32_t us) {
    ++cone_epoch;
    cone.clear();
    cone_stamp[v] = cone_epoch;
    cone_stack.assign(source.fanins(v).begin(), source.fanins(v).end());
    while (!cone_stack.empty()) {
      const auto w = cone_stack.back();
      cone_stack.pop_back();
      if (cone_stamp[w] == cone_epoch || std::binary_search(u, u + us, w)) {
        continue;
      }
      cone_stamp[w] = cone_epoch;
      cone.push_back(w);
      cone_stack.insert(cone_stack.end(), source.fanins(w).begin(), source.fanins(w).end());
    }
    for (auto w : cone) {
      if (kind[w] == kConstant) {
        continue;  // folded, never built
      }
      if (is_output[w]) {
        return false;
      }
      for (auto r = reader_begin[w]; r < reader_begin[w + 1]; ++r) {
        if (cone_stamp[readers[r]] != cone_epoch) {
          return false;
        }
      }
    }
    return true;
  };
  const auto selectable = [&](const Cut& c) { return options.duplicate || c.dup_free; };

  Store      store(recipe, options.cover_cost);
  const auto fcost = [&](const Cut& c) { return store.fns[c.fn].cost; };
  // Chained domino gates a cut adds: 0 for a wire or a constant, -1 for a
  // static LUT (it ends every pure domino chain).
  const auto step = [&](const Cut& c) {
    const auto& f = store.fns[c.fn];
    return f.constant || f.alias ? 0 : f.domino ? 1 : -1;
  };
  const auto depth_over = [&](const Cut& c, const std::vector<uint8_t>& at) -> uint8_t {
    const int s = step(c);
    if (s < 0) {
      return kDeep;
    }
    int d = 0;
    for (uint32_t j = 0; j < c.size; ++j) {
      d = std::max<int>(d, at[c.leaves[j]]);
    }
    return static_cast<uint8_t>(std::min<int>(d + s, kDeep));
  };

  // Priority cuts: every combination of the fan-ins' cuts (and their trivial
  // cuts) within the support, tables composed from the fan-ins' tables.
  std::vector<std::vector<Cut>> cuts(n);
  std::vector<float>            flow(n, 0);
  std::vector<uint8_t>          mind(n, 0);  // fewest chained domino gates over the kept cuts
  std::vector<Cut>              cand;
  std::array<std::vector<Cut>, 8> sets;
  for (Id v = 0; v < n; ++v) {
    if (v % 256 == 0 && options.admission && !options.admission()) {
      result.status = Status::search_exhausted;
      result.reason = "resource budget exhausted during cut enumeration";
      return result;
    }
    if (kind[v] == kSource) {
      continue;
    }
    if (kind[v] == kConstant) {
      Cut c;
      c.table = source.eval(v, 0) ? kOnes : Table{};
      c.fn    = store.get(c.table, 0);
      cuts[v] = {c};
      continue;
    }
    const auto m = source.fanin_count(v);
    for (uint32_t j = 0; j < m; ++j) {
      const auto in = source.fanin(v, j);
      if (options.fanout_boundary && kind[in] == kInternal && fanout[in] >= options.fanout_boundary) {
        sets[j].clear();  // a boundary: only its trivial cut
      } else {
        sets[j] = cuts[in];
      }
      if (kind[in] != kConstant) {
        Cut t;
        t.leaves[0] = in;
        t.size      = 1;
        t.table     = var_table(0);
        t.trivial   = true;
        sets[j].push_back(t);
      }
    }
    cand.clear();
    std::array<size_t, 8> pick{};
    while (true) {
      std::array<Id, 64> u;
      uint32_t           us = 0;
      for (uint32_t j = 0; j < m; ++j) {
        const auto& c = sets[j][pick[j]];
        for (uint32_t q = 0; q < c.size; ++q) {
          u[us++] = c.leaves[q];
        }
      }
      std::sort(u.begin(), u.begin() + us);
      us = static_cast<uint32_t>(std::unique(u.begin(), u.begin() + us) - u.begin());
      if (us <= k) {
        std::array<Table, 8> e;
        for (uint32_t j = 0; j < m; ++j) {
          e[j] = expand(sets[j][pick[j]], u.data(), us);
        }
        Table t{};
        for (uint32_t y = 0; y < (1U << m); ++y) {
          if (!source.eval(v, y)) {
            continue;
          }
          for (uint32_t w = 0; w < 4; ++w) {
            uint64_t term = ~uint64_t{0};
            for (uint32_t j = 0; j < m; ++j) {
              term &= ((y >> j) & 1) ? e[j][w] : ~e[j][w];
            }
            t[w] |= term;
          }
        }
        Cut c;
        std::copy(u.begin(), u.begin() + us, c.leaves.begin());
        c.size  = static_cast<uint8_t>(us);
        c.table = t;
        if (!options.duplicate) {
          for (uint32_t q = 0; q < m; ++q) {
            const auto& f  = sets[q][pick[q]];
            const auto  in = source.fanin(v, q);
            if (!f.trivial && kind[in] == kInternal) {
              c.tree = c.tree && f.tree && fanout[in] == 1 && !is_output[in];
            }
          }
          c.dup_free = c.tree || duplication_free(v, u.data(), us);
        }
        reduce(c);
        if (std::none_of(cand.begin(), cand.end(), [&](const Cut& o) { return same_leaves(o, c); })) {
          cand.push_back(c);
        }
      }
      uint32_t j = 0;
      while (j < m && ++pick[j] == sets[j].size()) {
        pick[j++] = 0;
      }
      if (j == m) {
        break;
      }
    }
    for (auto& c : cand) {
      c.fn   = store.get(c.table, c.size);
      c.flow = static_cast<float>(fcost(c));
      for (uint32_t j = 0; j < c.size; ++j) {
        c.flow += flow[c.leaves[j]] / est[c.leaves[j]];
      }
      c.depth = depth_over(c, mind);
    }
    // A cut whose leaves include another cut's leaves is dominated.
    std::stable_sort(cand.begin(), cand.end(), [](const Cut& a, const Cut& b) {
      return a.size != b.size ? a.size < b.size : a.flow < b.flow;
    });
    std::vector<Cut> kept;
    for (const auto& c : cand) {
      // A cut that duplicates logic never removes one that does not.
      if (std::none_of(kept.begin(), kept.end(), [&](const Cut& o) { return subset(o, c) && (selectable(o) || !selectable(c)); })) {
        kept.push_back(c);
      }
    }
    // Selectable cuts first: the cheapest by area flow, and always the
    // shallowest. Without duplication, up to half as many non-selectable cuts
    // stay for merging: a reconvergent node becomes absorbable higher up.
    std::stable_sort(kept.begin(), kept.end(), [&](const Cut& a, const Cut& b) {
      if (selectable(a) != selectable(b)) {
        return selectable(a);
      }
      return a.flow != b.flow ? a.flow < b.flow : a.size < b.size;
    });
    const auto valid   = static_cast<size_t>(std::count_if(kept.begin(), kept.end(), selectable));
    size_t     shallow = 0;
    for (size_t i = 1; i < valid; ++i) {
      if (kept[i].depth < kept[shallow].depth) {
        shallow = i;
      }
    }
    if (valid > options.cover_cuts && shallow >= options.cover_cuts) {
      std::swap(kept[options.cover_cuts - 1], kept[shallow]);
    }
    const size_t keep_valid   = std::min<size_t>(valid, options.cover_cuts);
    const size_t keep_invalid = std::min<size_t>(kept.size() - valid, options.duplicate ? 0 : options.cover_cuts / 2);
    if (keep_valid < valid) {
      kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(keep_valid), kept.begin() + static_cast<std::ptrdiff_t>(valid));
    }
    kept.resize(keep_valid + keep_invalid);
    if (keep_valid == 0) {
      result.status = Status::invalid;
      result.reason = "a node has no selectable cut";
      return result;
    }
    flow[v] = kept.front().flow;
    mind[v] = kDeep;
    for (size_t i = 0; i < keep_valid; ++i) {
      flow[v] = std::min(flow[v], kept[i].flow);
      mind[v] = std::min(mind[v], kept[i].depth);
    }
    result.cuts += kept.size();
    cuts[v]      = std::move(kept);
  }
  result.functions = store.fns.size();

  std::vector<uint32_t> best(n, 0), refs(n, 0);
  std::vector<uint8_t>  arr(n, 0), req(n, kDeep);
  std::vector<Id>       stack;
  const auto            chosen = [&](Id v) -> const Cut& { return cuts[v][best[v]]; };
  const auto            levels = static_cast<uint8_t>(std::min<uint32_t>(options.domino_levels, kDeep - 1));
  // Reference counts of the current cover.
  const auto mark = [&] {
    std::fill(refs.begin(), refs.end(), 0);
    for (auto o : outputs) {
      refs[o] += internal(o);
    }
    for (Id v = n; v-- > 0;) {
      if (internal(v) && refs[v]) {
        const auto& c = chosen(v);
        for (uint32_t j = 0; j < c.size; ++j) {
          refs[c.leaves[j]] += internal(c.leaves[j]);
        }
      }
    }
  };
  // An output some cover builds in `levels` chained domino gates must be built
  // so; the chosen cuts pass the requirement down to their leaves.
  const auto require = [&] {
    std::fill(req.begin(), req.end(), kDeep);
    if (!levels && options.depth_slack < 0) {
      return;
    }
    for (auto o : outputs) {
      if (!internal(o)) {
        continue;
      }
      if (mind[o] <= levels) {
        req[o] = levels;
      } else if (options.depth_slack >= 0 && mind[o] != kDeep) {
        req[o] = static_cast<uint8_t>(std::min<int>(mind[o] + options.depth_slack, kDeep - 1));
      }
    }
    for (Id v = n; v-- > 0;) {
      if (!internal(v) || !refs[v] || req[v] == kDeep) {
        continue;
      }
      const auto& c = chosen(v);
      const int   s = step(c);
      for (uint32_t j = 0; s >= 0 && j < c.size; ++j) {
        auto& r = req[c.leaves[j]];
        r       = std::min<uint8_t>(r, static_cast<uint8_t>(req[v] - s));
      }
    }
  };
  const auto cover_cost = [&] {
    uint64_t sum = 0;
    for (Id v = 0; v < n; ++v) {
      sum += internal(v) && refs[v] ? fcost(chosen(v)) : 0;
    }
    return sum;
  };

  // Round 0: the shallowest cut everywhere (area flow breaks ties).
  for (Id v = 0; v < n; ++v) {
    if (!internal(v)) {
      continue;
    }
    for (uint32_t c = 1; c < cuts[v].size(); ++c) {
      const auto& a = cuts[v][c];
      const auto& b = cuts[v][best[v]];
      if (!selectable(a)) {
        continue;
      }
      if (a.depth < b.depth || (a.depth == b.depth && a.flow < b.flow)) {
        best[v] = c;
      }
    }
    arr[v] = depth_over(chosen(v), arr);
  }
  mark();
  require();

  // Round 1: area flow under the required levels, fanout estimates blended
  // with the round-0 cover's references.
  for (Id v = 0; v < n; ++v) {
    est[v] = std::max(1.0f, (2 * est[v] + static_cast<float>(refs[v])) / 3);
  }
  for (Id v = 0; v < n; ++v) {
    if (!internal(v)) {
      continue;
    }
    float   bf   = std::numeric_limits<float>::max();
    uint8_t bd   = kDeep;
    auto    pick = best[v];
    for (uint32_t c = 0; c < cuts[v].size(); ++c) {
      const auto& cut = cuts[v][c];
      const auto  d   = depth_over(cut, arr);
      if (d > req[v] || !selectable(cut)) {
        continue;
      }
      auto f = static_cast<float>(fcost(cut));
      for (uint32_t j = 0; j < cut.size; ++j) {
        f += flow[cut.leaves[j]] / est[cut.leaves[j]];
      }
      if (f < bf || (f == bf && d < bd)) {
        bf = f, bd = d, pick = c;
      }
    }
    best[v] = pick;
    flow[v] = bf == std::numeric_limits<float>::max() ? cuts[v][pick].flow : bf;
    arr[v]  = depth_over(chosen(v), arr);
  }
  mark();
  require();
  result.flow_cost = cover_cost();

  // Exact-area rounds: a cut costs its own LUT plus every LUT it would newly
  // reference (and that nothing else references), under the required levels.
  const auto ref_cut = [&](Id v, int delta) {
    uint64_t area = 0;
    stack.assign(1, v);
    while (!stack.empty()) {
      const auto u = stack.back();
      stack.pop_back();
      const auto& c  = chosen(u);
      area          += fcost(c);
      for (uint32_t j = 0; j < c.size; ++j) {
        const auto l = c.leaves[j];
        if (!internal(l)) {
          continue;
        }
        if (delta > 0 ? refs[l]++ == 0 : --refs[l] == 0) {
          stack.push_back(l);
        }
      }
    }
    return area;
  };
  for (uint32_t round = 0; round < options.recovery_rounds; ++round) {
    if (options.admission && !options.admission()) {
      break;  // keep the cover of the last complete round
    }
    for (Id v = 0; v < n; ++v) {
      if (!internal(v)) {
        continue;
      }
      if (refs[v]) {
        ref_cut(v, -1);
      }
      const auto keep = best[v];
      uint64_t   ba   = std::numeric_limits<uint64_t>::max();
      uint8_t    bd   = kDeep;
      auto       pick = keep;
      for (uint32_t c = 0; c < cuts[v].size(); ++c) {
        const auto d = depth_over(cuts[v][c], arr);
        if (d > req[v] || !selectable(cuts[v][c])) {
          continue;
        }
        best[v]      = c;
        const auto a = ref_cut(v, +1);
        ref_cut(v, -1);
        if (a < ba || (a == ba && d < bd)) {
          ba = a, bd = d, pick = c;
        }
      }
      best[v] = pick;
      arr[v]  = depth_over(chosen(v), arr);
      if (refs[v]) {
        ref_cut(v, +1);
      }
    }
    require();
  }
  mark();
  for (Id v = 0; v < n; ++v) {
    if (internal(v)) {
      arr[v] = depth_over(chosen(v), arr);
    }
  }

  // The LUTs: every referenced internal node, and each constant an output reads.
  std::vector<uint8_t> is_lut(n, 0);
  for (Id v = 0; v < n; ++v) {
    is_lut[v] = internal(v) && refs[v];
  }
  for (auto o : outputs) {
    is_lut[o] |= kind[o] == kConstant;
  }
  for (Id v = 0; v < n; ++v) {
    if (!is_lut[v]) {
      continue;
    }
    const auto& c = cuts[v].front().size == 0 && kind[v] == kConstant ? cuts[v].front() : chosen(v);
    Cover_lut   lut{v, std::vector<Id>(c.leaves.begin(), c.leaves.begin() + c.size), to_truth(c.table, c.size), store.fns[c.fn], arr[v]};
    const auto& f = lut.fn;
    if (!f.constant && !f.alias) {
      for (uint32_t j = 0; j < c.size; ++j) {
        if (kind[c.leaves[j]] == kSource) {
          result.source_literals_pos += (f.form.positive >> j) & 1;
          result.source_literals_neg += (f.form.negative >> j) & 1;
        }
      }
    }
    result.cost  += f.cost;
    if (f.constant) {
      ++result.constants;
    } else if (f.alias) {
      ++result.aliases;
    } else if (f.domino) {
      ++result.domino;
      result.domino_cost     += f.cost;
      result.domino_literals += f.form.factored;
      ++result.domino_inputs[c.size];
      ++result.domino_series[std::min<uint32_t>(f.form.series, 6)];
    } else {
      ++result.nonunate;
      result.nonunate_cost += f.cost;
    }
    result.luts.push_back(std::move(lut));
  }
  // Replication: source nodes between a LUT's leaves and its root, counted
  // once per LUT that computes them.
  std::vector<uint32_t> covered(n, 0), stamp(n, 0);
  uint32_t              epoch = 0;
  for (const auto& lut : result.luts) {
    ++epoch;
    stack.assign(1, lut.root);
    while (!stack.empty()) {
      const auto u = stack.back();
      stack.pop_back();
      if (stamp[u] == epoch || !internal(u) || std::find(lut.leaves.begin(), lut.leaves.end(), u) != lut.leaves.end()) {
        continue;
      }
      stamp[u] = epoch;
      ++covered[u];
      for (auto in : source.fanins(u)) {
        stack.push_back(in);
      }
    }
  }
  for (auto c : covered) {
    result.covered_nodes    += c != 0;
    result.replicated_nodes += c > 1 ? c - 1 : 0;
  }
  // Gates in each output's cone, counted up to 4 (wires and constants are free).
  std::vector<int32_t> lut_of(n, -1);
  for (size_t i = 0; i < result.luts.size(); ++i) {
    lut_of[result.luts[i].root] = static_cast<int32_t>(i);
  }
  for (auto o : outputs) {
    if (!internal(o) || chosen(o).size == 0 || (chosen(o).size == 1 && !internal(chosen(o).leaves[0]))) {
      continue;
    }
    ++epoch;
    uint32_t gates = 0;
    stack.assign(1, o);
    while (!stack.empty() && gates < 4) {
      const auto u = stack.back();
      stack.pop_back();
      if (stamp[u] == epoch || lut_of[u] < 0) {
        continue;
      }
      stamp[u]        = epoch;
      const auto& lut = result.luts[static_cast<size_t>(lut_of[u])];
      gates += !lut.fn.constant && !lut.fn.alias;
      for (auto leaf : lut.leaves) {
        stack.push_back(leaf);
      }
    }
    ++result.cone_gates[std::min<uint32_t>(gates, 4)];
  }
  const auto shallow = levels ? levels : uint8_t{2};
  for (auto o : outputs) {
    if (!internal(o) || chosen(o).size == 0 || (chosen(o).size == 1 && !internal(chosen(o).leaves[0]))) {
      ++result.outputs_wire;
    } else {
      ++(arr[o] <= shallow ? result.outputs_shallow : result.outputs_deep);
    }
  }

  // The cover against the source network on fixed pseudo-random patterns.
  uint64_t   state = 0x9e3779b97f4a7c15ULL;
  const auto next  = [&] {
    state  += 0x9e3779b97f4a7c15ULL;
    auto z  = state;
    z       = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z       = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  };
  std::vector<uint64_t>   sv(n), cv(n);
  std::array<uint64_t, 8> in;
  for (uint32_t w = 0; w < 4; ++w) {
    for (Id v = 0; v < n; ++v) {
      if (kind[v] == kSource) {
        sv[v] = cv[v] = next();
        continue;
      }
      for (uint32_t j = 0; j < source.fanin_count(v); ++j) {
        in[j] = sv[source.fanin(v, j)];
      }
      sv[v] = eval_table(source.table(v).data(), source.fanin_count(v), in.data());
    }
    for (const auto& lut : result.luts) {
      for (size_t j = 0; j < lut.leaves.size(); ++j) {
        in[j] = cv[lut.leaves[j]];
      }
      cv[lut.root] = eval_table(lut.table.words.data(), static_cast<uint32_t>(lut.leaves.size()), in.data());
      if (cv[lut.root] != sv[lut.root]) {
        result.status = Status::invalid;
        result.reason = "cover simulation mismatch";
        return result;
      }
    }
  }
  result.status = Status::feasible;
  return result;
}

std::optional<Lnet> cover_network(const Lnet& source, const Cover_result& cover) {
  using livehd::synth::Lid;
  Lnet             net;
  std::vector<Lid> ids(source.size(), Lnet::kNone);
  for (const auto& in : source.inputs()) {
    ids[in.node] = net.add_input(in.name);
  }
  for (const auto& l : source.latches()) {
    ids[l.q] = net.latch(net.add_latch(l.name, l.init)).q;
  }
  std::vector<Lid> fanins;
  for (const auto& lut : cover.luts) {
    const auto& f = lut.fn;
    if (f.constant) {
      ids[lut.root] = net.add_constant(!f.form.cubes.empty() != f.complemented);
      continue;
    }
    fanins.clear();
    for (const auto leaf : lut.leaves) {
      if (ids[leaf] == Lnet::kNone) {
        return std::nullopt;
      }
      fanins.push_back(ids[leaf]);
    }
    if (f.alias) {
      const bool inverted = (f.form.cubes.size() == 1 && f.form.cubes.front().ones == 0) != f.complemented;
      ids[lut.root]       = inverted ? net.add_lut({fanins.front()}, Lnet::kNot) : fanins.front();
      continue;
    }
    const auto node = net.add_lut(fanins, std::span<const uint64_t>(lut.table.words));
    Lnet::Sop  sop;
    sop.complemented = f.complemented;
    for (const auto& cube : f.form.cubes) {
      sop.cubes.push_back({cube.care, cube.ones});
    }
    net.set_sop(node, std::move(sop));
    ids[lut.root] = node;
  }
  for (const auto& o : source.outputs()) {
    if (ids[o.node] == Lnet::kNone) {
      return std::nullopt;
    }
    net.add_output(ids[o.node], o.name);
  }
  for (size_t k = 0; k < source.latches().size(); ++k) {
    const auto d = source.latch(static_cast<uint32_t>(k)).d;
    if (d == Lnet::kNone || ids[d] == Lnet::kNone) {
      return std::nullopt;
    }
    net.set_latch_input(static_cast<uint32_t>(k), ids[d]);
  }
  return net;
}

}  // namespace livehd::usyn
