// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "domino_gate.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <format>
#include <iterator>
#include <utility>

namespace livehd::domino {

namespace {

[[nodiscard]] inline int points_of(int n) { return 1 << n; }

[[nodiscard]] Truth masked(Truth f, int n) {
  // Clear every bit at or above 2^n so tables over the same n compare equal.
  const int pts = points_of(n);
  for (int x = pts; x < kMaxPoints; ++x) {
    f.set(x, false);
  }
  return f;
}

// Union-find over variable indices, used for the co-occurrence components of a
// prime set: two variables are joined when one prime mentions both.
struct Var_uf {
  std::array<int, kMaxVars> parent{};
  Var_uf() {
    for (int i = 0; i < kMaxVars; ++i) {
      parent[static_cast<size_t>(i)] = i;
    }
  }
  int find(int a) {
    while (parent[static_cast<size_t>(a)] != a) {
      a = parent[static_cast<size_t>(a)] = parent[static_cast<size_t>(parent[static_cast<size_t>(a)])];
    }
    return a;
  }
  void unite(int a, int b) { parent[static_cast<size_t>(find(a))] = find(b); }
};

// Disjoint-support parts of a prime set: one mask per connected component of
// the co-occurrence graph, in increasing lowest-variable order.
[[nodiscard]] std::vector<uint32_t> components(const std::vector<uint32_t>& primes, uint32_t support) {
  Var_uf uf;
  for (auto p : primes) {
    const int first = std::countr_zero(p);
    for (int j = 0; j < kMaxVars; ++j) {
      if ((p >> j) & 1u) {
        uf.unite(first, j);
      }
    }
  }
  std::array<uint32_t, kMaxVars> by_root{};
  for (int j = 0; j < kMaxVars; ++j) {
    if ((support >> j) & 1u) {
      by_root[static_cast<size_t>(uf.find(j))] |= (1u << j);
    }
  }
  std::vector<uint32_t> out;
  for (auto m : by_root) {
    if (m != 0) {
      out.push_back(m);
    }
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Truth-table primitives

Truth truth_const(bool v, int n) {
  Truth t;
  if (v) {
    for (int x = 0; x < points_of(n); ++x) {
      t.set(x, true);
    }
  }
  return t;
}

Truth truth_var(int j, int n) {
  Truth t;
  for (int x = 0; x < points_of(n); ++x) {
    t.set(x, ((x >> j) & 1) != 0);
  }
  return t;
}

Truth truth_not(const Truth& f, int n) {
  Truth t;
  for (size_t i = 0; i < t.w.size(); ++i) {
    t.w[i] = ~f.w[i];
  }
  return masked(t, n);
}

Truth truth_and(const Truth& a, const Truth& b) {
  Truth t;
  for (size_t i = 0; i < t.w.size(); ++i) {
    t.w[i] = a.w[i] & b.w[i];
  }
  return t;
}

Truth truth_or(const Truth& a, const Truth& b) {
  Truth t;
  for (size_t i = 0; i < t.w.size(); ++i) {
    t.w[i] = a.w[i] | b.w[i];
  }
  return t;
}

Truth truth_xor(const Truth& a, const Truth& b) {
  Truth t;
  for (size_t i = 0; i < t.w.size(); ++i) {
    t.w[i] = a.w[i] ^ b.w[i];
  }
  return t;
}

Truth cofactor(const Truth& f, int n, int j, bool val) {
  Truth     t;
  const int bit = 1 << j;
  for (int x = 0; x < points_of(n); ++x) {
    const int src = val ? (x | bit) : (x & ~bit);
    t.set(x, f.get(src));
  }
  return t;
}

Truth flip_var(const Truth& f, int n, int j) {
  Truth t;
  for (int x = 0; x < points_of(n); ++x) {
    t.set(x, f.get(x ^ (1 << j)));
  }
  return t;
}

Truth dual(const Truth& f, int n) {
  Truth     t;
  const int all = points_of(n) - 1;
  for (int x = 0; x <= all; ++x) {
    t.set(x, !f.get(x ^ all));
  }
  return t;
}

bool is_const(const Truth& f, int n) {
  const bool v = f.get(0);
  for (int x = 1; x < points_of(n); ++x) {
    if (f.get(x) != v) {
      return false;
    }
  }
  return true;
}

bool depends_on(const Truth& f, int n, int j) {
  const int bit = 1 << j;
  for (int x = 0; x < points_of(n); ++x) {
    if (((x & bit) == 0) && f.get(x) != f.get(x | bit)) {
      return true;
    }
  }
  return false;
}

uint32_t support_mask(const Truth& f, int n) {
  uint32_t m = 0;
  for (int j = 0; j < n; ++j) {
    if (depends_on(f, n, j)) {
      m |= (1u << j);
    }
  }
  return m;
}

Truth truth_from_bits(std::string_view bits, int n) {
  Truth     t;
  const int pts = points_of(n);
  assert(static_cast<int>(bits.size()) == pts);
  for (int x = 0; x < pts; ++x) {
    t.set(x, bits[static_cast<size_t>(pts - 1 - x)] == '1');
  }
  return t;
}

std::string to_bits(const Truth& f, int n) {
  std::string s;
  for (int x = points_of(n) - 1; x >= 0; --x) {
    s.push_back(f.get(x) ? '1' : '0');
  }
  return s;
}

std::array<Unate, kMaxVars> unateness(const Truth& f, int n) {
  std::array<Unate, kMaxVars> out{};
  out.fill(Unate::independent);
  for (int j = 0; j < n; ++j) {
    const int bit = 1 << j;
    bool      up = false, down = false;
    for (int x = 0; x < points_of(n); ++x) {
      if ((x & bit) != 0) {
        continue;
      }
      const bool lo = f.get(x), hi = f.get(x | bit);
      up   |= (!lo && hi);
      down |= (lo && !hi);
    }
    out[static_cast<size_t>(j)] = (up && down) ? Unate::binate : up ? Unate::positive : down ? Unate::negative : Unate::independent;
  }
  return out;
}

std::string_view to_string(Unate u) {
  switch (u) {
    case Unate::independent: return "independent";
    case Unate::positive   : return "positive";
    case Unate::negative   : return "negative";
    case Unate::binate     : return "binate";
  }
  return "?";
}

std::vector<uint32_t> prime_implicants(const Truth& f, int n) {
  // For a positive unate f the primes are the minimal true points.
  std::vector<uint32_t> out;
  for (int x = 0; x < points_of(n); ++x) {
    if (!f.get(x)) {
      continue;
    }
    bool minimal = true;
    for (int j = 0; j < n && minimal; ++j) {
      if (((x >> j) & 1) != 0 && f.get(x & ~(1 << j))) {
        minimal = false;
      }
    }
    if (minimal) {
      out.push_back(static_cast<uint32_t>(x));
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Sp_formula

bool Sp_formula::evaluate(uint32_t x) const {
  auto rec = [&](auto& self, int idx) -> bool {
    const Node& nd = nodes[static_cast<size_t>(idx)];
    switch (nd.kind) {
      case Kind::lit: return ((x >> nd.var) & 1u) != 0;
      case Kind::series:
        for (int k : nd.kids) {
          if (!self(self, k)) {
            return false;
          }
        }
        return true;
      case Kind::parallel:
        for (int k : nd.kids) {
          if (self(self, k)) {
            return true;
          }
        }
        return false;
    }
    return false;
  };
  return root >= 0 && rec(rec, root);
}

std::string Sp_formula::to_string(std::span<const std::string> names) const {
  auto name = [&](int v) -> std::string {
    if (v >= 0 && static_cast<size_t>(v) < names.size()) {
      return names[static_cast<size_t>(v)];
    }
    return std::string(1, static_cast<char>('a' + v));
  };
  auto rec = [&](auto& self, int idx) -> std::string {
    const Node& nd = nodes[static_cast<size_t>(idx)];
    switch (nd.kind) {
      case Kind::lit   : return name(nd.var);
      case Kind::series: {
        std::string s;
        for (int k : nd.kids) {
          const Node& kid = nodes[static_cast<size_t>(k)];
          if (kid.kind == Kind::parallel) {
            s += "(" + self(self, k) + ")";
          } else {
            s += self(self, k);
          }
        }
        return s;
      }
      case Kind::parallel: {
        std::string s;
        for (size_t i = 0; i < nd.kids.size(); ++i) {
          if (i != 0) {
            s += "+";
          }
          s += self(self, nd.kids[i]);
        }
        return s;
      }
    }
    return "";
  };
  return root >= 0 ? rec(rec, root) : std::string{};
}

void Sp_formula::finalize() {
  transistors = 0;
  stack       = 0;
  width       = 0;
  if (root < 0) {
    return;
  }
  // Canonical child order so equal networks print identically: series parts
  // smallest first (a(b+c)), parallel parts largest first (ab+c), ties by text.
  auto leaves = [&](auto& self, int idx) -> int {
    const Node& nd = nodes[static_cast<size_t>(idx)];
    if (nd.kind == Kind::lit) {
      return 1;
    }
    int c = 0;
    for (int k : nd.kids) {
      c += self(self, k);
    }
    return c;
  };
  auto text = [&](int idx) {
    Sp_formula sub;
    sub.nodes = nodes;
    sub.root  = idx;
    return sub.to_string();
  };
  for (Node& nd : nodes) {
    if (nd.kind == Kind::lit) {
      continue;
    }
    const bool series = nd.kind == Kind::series;
    std::sort(nd.kids.begin(), nd.kids.end(), [&](int x, int y) {
      const int lx = leaves(leaves, x), ly = leaves(leaves, y);
      if (lx != ly) {
        return series ? lx < ly : lx > ly;
      }
      return text(x) < text(y);
    });
  }
  // Returns {series depth, parallel depth} of a subtree; counts leaves as it goes.
  auto rec = [&](auto& self, int idx) -> std::pair<int, int> {
    const Node& nd = nodes[static_cast<size_t>(idx)];
    if (nd.kind == Kind::lit) {
      ++transistors;
      return {1, 1};
    }
    int st = 0, wd = 0;
    for (int k : nd.kids) {
      auto [ks, kp] = self(self, k);
      if (nd.kind == Kind::series) {
        st += ks;
        wd  = std::max(wd, kp);
      } else {
        st  = std::max(st, ks);
        wd += kp;
      }
    }
    return {st, wd};
  };
  auto [st, wd] = rec(rec, root);
  stack         = st;
  width         = wd;
}

std::string_view to_string(Fit_reason r) {
  switch (r) {
    case Fit_reason::ok      : return "ok";
    case Fit_reason::constant: return "constant";
    case Fit_reason::binate  : return "binate";
    case Fit_reason::fanin   : return "fanin";
    case Fit_reason::stack   : return "stack";
    case Fit_reason::budget  : return "budget";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Sp_factorer

namespace {

using Cubes = std::vector<uint32_t>;  // positive cubes as variable masks

[[nodiscard]] Truth sop_truth(const Cubes& cubes, int m) {
  Truth t;
  for (int x = 0; x < points_of(m); ++x) {
    for (auto c : cubes) {
      if ((static_cast<uint32_t>(x) & c) == c) {
        t.set(x, true);
        break;
      }
    }
  }
  return t;
}

// Brayton's kernel enumeration over a positive cube set. A kernel is a
// cube-free quotient F/C for a cube C (its co-kernel); the recursion visits
// each once by forbidding co-kernel variables below the branching variable.
// Search caps. Without them the symmetric 8-variable functions (T4(8) has 70
// primes) spawn thousands of divisors per state and minutes of search; with
// them the worst case is well under a second and every <= 4-variable function
// is still solved exactly (the brute-force test checks that).
constexpr size_t kMaxKernels       = 64;  // kernels enumerated per function
constexpr size_t kMaxIntersectBase = 12;  // best kernels whose pairwise intersections are also tried
constexpr size_t kMaxDivisors      = 10;  // divisors kept per function, by algebraic literal saving

void kernels_rec(const Cubes& F, int j0, int m, std::vector<Cubes>& out) {
  if (out.size() >= kMaxKernels) {
    return;
  }
  for (int j = j0; j < m; ++j) {
    Cubes    S;
    uint32_t common = ~0u;
    for (auto c : F) {
      if (((c >> j) & 1u) != 0) {
        S.push_back(c);
        common &= c;
      }
    }
    if (S.size() < 2) {
      continue;
    }
    if ((common & ((1u << j) - 1)) != 0) {
      continue;  // a lower co-kernel variable: this kernel was reached already
    }
    Cubes K;
    for (auto c : S) {
      K.push_back(c & ~common);
    }
    std::sort(K.begin(), K.end());
    out.push_back(K);
    kernels_rec(K, j + 1, m, out);
  }
}

[[nodiscard]] std::vector<Cubes> kernels(const Cubes& F, int m) {
  std::vector<Cubes> out;
  kernels_rec(F, 0, m, out);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// Algebraic division F / K over positive cubes: the cubes q such that q|k is
// a cube of F for every k in K (q and k variable-disjoint).
[[nodiscard]] Cubes divide(const Cubes& F, const Cubes& K) {
  Cubes Q;
  bool  first = true;
  for (auto k : K) {
    Cubes part;
    for (auto c : F) {
      if ((c & k) == k) {
        part.push_back(c & ~k);
      }
    }
    std::sort(part.begin(), part.end());
    if (first) {
      Q     = part;
      first = false;
    } else {
      Cubes both;
      std::set_intersection(Q.begin(), Q.end(), part.begin(), part.end(), std::back_inserter(both));
      Q = both;
    }
    if (Q.empty()) {
      break;
    }
  }
  return Q;
}

// Exact minimum-literal monotone formulas for every monotone function of four
// variables under every stack bound, by closure of the literals under AND
// (stacks add) and OR (stacks max). 166 functions x 4 bounds; built once.
class Exact4 {
public:
  static constexpr int kVars = 4;
  static constexpr int kPts  = 1 << kVars;
  struct Entry {
    int      cost = kInfCost;
    uint8_t  op   = 0;  // 0 literal, 1 and, 2 or
    int8_t   var  = -1;
    uint16_t a = 0, b = 0;
    int8_t   da = 0, db = 0;
  };

  Exact4() {
    // Closure of the literals under &,| = every monotone non-constant function.
    std::vector<uint16_t> funcs;
    for (int j = 0; j < kVars; ++j) {
      uint16_t t = 0;
      for (int x = 0; x < kPts; ++x) {
        if ((x >> j) & 1) {
          t = static_cast<uint16_t>(t | (1u << x));
        }
      }
      funcs.push_back(t);
    }
    for (size_t i = 0; i < funcs.size(); ++i) {
      for (size_t k = 0; k <= i; ++k) {
        for (uint16_t t : {static_cast<uint16_t>(funcs[i] & funcs[k]), static_cast<uint16_t>(funcs[i] | funcs[k])}) {
          if (t != 0 && t != 0xffff && std::find(funcs.begin(), funcs.end(), t) == funcs.end()) {
            funcs.push_back(t);
          }
        }
      }
    }
    index_.assign(1u << kPts, -1);
    for (size_t i = 0; i < funcs.size(); ++i) {
      index_[funcs[i]] = static_cast<int>(i);
    }
    tab_.assign(funcs.size() * (kVars + 1), Entry{});
    for (int j = 0; j < kVars; ++j) {
      for (int d = 1; d <= kVars; ++d) {
        at(funcs[static_cast<size_t>(j)], d) = Entry{.cost = 1, .op = 0, .var = static_cast<int8_t>(j)};
      }
    }
    for (bool changed = true; changed;) {
      changed = false;
      for (uint16_t ta : funcs) {
        for (int da = 1; da <= kVars; ++da) {
          const int ca = at(ta, da).cost;
          if (ca >= kInfCost) {
            continue;
          }
          for (uint16_t tb : funcs) {
            for (int db = 1; db <= kVars; ++db) {
              const int cb = at(tb, db).cost;
              if (cb >= kInfCost) {
                continue;
              }
              const uint16_t tand = static_cast<uint16_t>(ta & tb), tor = static_cast<uint16_t>(ta | tb);
              if (da + db <= kVars) {
                changed |= relax(tand,
                                 da + db,
                                 Entry{.cost = ca + cb,
                                       .op   = 1,
                                       .a    = ta,
                                       .b    = tb,
                                       .da   = static_cast<int8_t>(da),
                                       .db   = static_cast<int8_t>(db)});
              }
              changed |= relax(
                  tor,
                  std::max(da, db),
                  Entry{.cost = ca + cb, .op = 2, .a = ta, .b = tb, .da = static_cast<int8_t>(da), .db = static_cast<int8_t>(db)});
            }
          }
        }
      }
    }
  }

  [[nodiscard]] bool         has(uint16_t t) const { return index_[t] >= 0; }
  [[nodiscard]] const Entry& get(uint16_t t, int d) const {
    return tab_[static_cast<size_t>(index_[t]) * (kVars + 1) + static_cast<size_t>(std::min(d, kVars))];
  }

private:
  std::vector<int>   index_;
  std::vector<Entry> tab_;

  Entry& at(uint16_t t, int d) { return tab_[static_cast<size_t>(index_[t]) * (kVars + 1) + static_cast<size_t>(d)]; }
  bool   relax(uint16_t t, int d_from, const Entry& e) {
    if (t == 0 || t == 0xffff || index_[t] < 0) {
      return false;
    }
    bool changed = false;
    for (int d = d_from; d <= kVars; ++d) {
      if (e.cost < at(t, d).cost) {
        at(t, d) = e;
        changed  = true;
      }
    }
    return changed;
  }
};

[[nodiscard]] const Exact4& exact4() {
  static const Exact4 table;
  return table;
}

// g over m <= 4 variables as a 4-variable table (extra variables independent).
[[nodiscard]] uint16_t embed4(const Truth& g, int m) {
  uint16_t t = 0;
  for (int x = 0; x < Exact4::kPts; ++x) {
    if (g.get(x & ((1 << m) - 1))) {
      t = static_cast<uint16_t>(t | (1u << x));
    }
  }
  return t;
}

}  // namespace

Sp_factorer::Compact Sp_factorer::compact(const Truth& f, int n) {
  Compact        c;
  const uint32_t support = support_mask(f, n);
  for (int j = 0; j < n; ++j) {
    if (((support >> j) & 1u) != 0) {
      c.var[static_cast<size_t>(c.m++)] = j;
    }
  }
  for (int y = 0; y < points_of(c.m); ++y) {
    int x = 0;
    for (int i = 0; i < c.m; ++i) {
      if ((y >> i) & 1) {
        x |= 1 << c.var[static_cast<size_t>(i)];
      }
    }
    c.g.set(y, f.get(x));
  }
  return c;
}

std::vector<Sp_factorer::Cand> Sp_factorer::candidates(const Truth& g, int m) {
  std::vector<Cand> out;
  const uint32_t    support  = support_mask(g, m);
  const Cubes       primes   = prime_implicants(g, m);
  const Truth       gd       = dual(g, m);
  const Cubes       primes_d = prime_implicants(gd, m);

  // Disjoint parallel parts (primes) and series parts (prime implicates).
  if (auto comps = components(primes, support); comps.size() > 1) {
    Cand c;
    for (auto comp : comps) {
      Truth part = g;
      for (int j = 0; j < m; ++j) {
        if (((support >> j) & 1u) != 0 && ((comp >> j) & 1u) == 0) {
          part = cofactor(part, m, j, false);
        }
      }
      c.parallel.push_back(part);
    }
    out.push_back(std::move(c));
  }
  if (auto comps = components(primes_d, support); comps.size() > 1) {
    Cand c;
    for (auto comp : comps) {
      Truth part = g;
      for (int j = 0; j < m; ++j) {
        if (((support >> j) & 1u) != 0 && ((comp >> j) & 1u) == 0) {
          part = cofactor(part, m, j, true);
        }
      }
      c.series.push_back(part);
    }
    out.push_back(std::move(c));
  }

  // Divisions g = K*Q + R by the best-scoring kernels and pairwise kernel
  // intersections (the divisor c+d of (a+b)(c+d)+ab+cd is the intersection of
  // the kernels b+c+d and a+c+d, not a kernel itself). Score = algebraic
  // literals saved; only the top kMaxDivisors are tried.
  auto lits = [](const Cubes& F) {
    int l = 0;
    for (auto c : F) {
      l += std::popcount(c);
    }
    return l;
  };
  struct Division {
    int   saving = 0;
    Cubes K, Q, R;
  };
  auto divisions = [&](const Cubes& F, bool pos_form) {
    const int             lf = lits(F);
    std::vector<Division> divs;
    auto                  consider = [&](const Cubes& K) {
      if (K.size() < 2 || K == F) {
        return;
      }
      const Cubes Q = divide(F, K);
      if (Q.empty()) {
        return;
      }
      Cubes prod;
      for (auto q : Q) {
        for (auto k : K) {
          prod.push_back(q | k);
        }
      }
      std::sort(prod.begin(), prod.end());
      Cubes R;
      for (auto c : F) {
        if (!std::binary_search(prod.begin(), prod.end(), c)) {
          R.push_back(c);
        }
      }
      divs.push_back(Division{.saving = lf - lits(K) - lits(Q) - lits(R), .K = K, .Q = std::move(Q), .R = std::move(R)});
    };
    auto rank = [&] {
      std::stable_sort(divs.begin(), divs.end(), [](const Division& a, const Division& b) {
        if (a.saving != b.saving) {
          return a.saving > b.saving;
        }
        return a.K < b.K;
      });
      divs.erase(std::unique(divs.begin(), divs.end(), [](const Division& a, const Division& b) { return a.K == b.K; }),
                 divs.end());
    };
    const std::vector<Cubes> ks = kernels(F, m);
    for (const Cubes& K : ks) {
      consider(K);
    }
    rank();
    const size_t base = std::min(divs.size(), kMaxIntersectBase);
    for (size_t i = 0; i < base; ++i) {
      for (size_t j = i + 1; j < base; ++j) {
        Cubes both;
        std::set_intersection(divs[i].K.begin(), divs[i].K.end(), divs[j].K.begin(), divs[j].K.end(), std::back_inserter(both));
        consider(both);
      }
    }
    rank();
    if (divs.size() > kMaxDivisors) {
      divs.resize(kMaxDivisors);
    }
    for (const Division& dv : divs) {
      const Truth tk = sop_truth(dv.K, m), tq = sop_truth(dv.Q, m);
      if (pos_form) {
        Cand c{
            .series   = {tk, tq},
            .parallel = {}
        };
        if (!dv.R.empty()) {
          c.parallel.push_back(sop_truth(dv.R, m));
        }
        out.push_back(std::move(c));
      } else {
        // F is the dual's SOP: g = (K' + Q') * R' with ' the dual.
        Cand c{.series = {truth_or(dual(tk, m), dual(tq, m))}, .parallel = {}};
        if (!dv.R.empty()) {
          c.series.push_back(dual(sop_truth(dv.R, m), m));
        }
        if (c.series.size() > 1) {
          out.push_back(std::move(c));
        }
      }
    }
  };
  divisions(primes, true);
  divisions(primes_d, false);
  // Shannon shapes and the algebraic single-variable division.
  for (int j = 0; j < m; ++j) {
    const Truth g1 = cofactor(g, m, j, true);
    const Truth g0 = cofactor(g, m, j, false);
    if (is_const(g1, m) || is_const(g0, m)) {
      continue;  // x + g0 and x * g1 are the disjoint cases above
    }
    const Truth x = truth_var(j, m);
    out.push_back(Cand{
        .series   = {x, g1},
        .parallel = {g0}
    });  // x*g1 + g0
    out.push_back(Cand{
        .series   = {g1, truth_or(x, g0)},
        .parallel = {}
    });  // g1*(x + g0)
    Cubes quot;
    for (auto c : primes) {
      if (((c >> j) & 1u) != 0) {
        quot.push_back(c & ~(1u << j));
      }
    }
    if (quot.size() >= 2) {
      const Truth q = sop_truth(quot, m);
      if (q != g1) {
        out.push_back(Cand{
            .series   = {x, q},
            .parallel = {g0}
        });  // x*(g/x) + g0
      }
    }
  }

  return out;
}

namespace {

// Every support variable appears at least once: the cheap lower bound that
// lets a sibling part shrink the budget handed to the next one.
[[nodiscard]] int lower_bound_cost(const Truth& g, int m) { return std::popcount(support_mask(g, m)); }

}  // namespace

int Sp_factorer::cand_cost(const Cand& c, int m, int d, int bound, std::vector<int>* alloc) {
  if (bound <= 0) {
    return kInfCost;
  }
  // Lower bounds of every part, so each part is searched under the budget
  // left once its siblings have taken at least their minimum.
  std::vector<int> lb_par, lb_ser;
  int              lb_total = 0;
  for (const Truth& p : c.parallel) {
    lb_par.push_back(lower_bound_cost(p, m));
    lb_total += lb_par.back();
  }
  for (const Truth& p : c.series) {
    lb_ser.push_back(lower_bound_cost(p, m));
    lb_total += lb_ser.back();
  }
  if (lb_total > bound) {
    return kInfCost;
  }
  int total = 0;         // exact cost of the parts settled so far
  int rest  = lb_total;  // lower bound of the parts not settled yet
  for (size_t i = 0; i < c.parallel.size(); ++i) {
    rest         -= lb_par[i];
    const int cp  = best_c(c.parallel[i], m, d, bound - total - rest);
    if (cp >= kInfCost || total + cp + rest > bound) {
      return kInfCost;
    }
    total += cp;
  }
  if (c.series.empty()) {
    return total;
  }
  // Knapsack over the series parts: distribute the stack budget, each part
  // needing at least one level. K[b] = cheapest cost of the parts seen so far
  // within b levels.
  const size_t                  k = c.series.size();
  std::vector<int>              K(static_cast<size_t>(d) + 1, kInfCost);
  std::vector<std::vector<int>> choice(k, std::vector<int>(static_cast<size_t>(d) + 1, 0));
  K[0] = 0;
  for (size_t i = 0; i < k; ++i) {
    rest -= lb_ser[i];
    std::vector<int> next(static_cast<size_t>(d) + 1, kInfCost);
    int              k_min = kInfCost;  // cheapest prefix so far, any depth
    for (int b = 0; b <= d; ++b) {
      k_min = std::min(k_min, K[static_cast<size_t>(b)]);
    }
    if (k_min >= kInfCost) {
      return kInfCost;
    }
    for (int t = 1; t <= d; ++t) {
      // The part's budget: what is left after the cheapest prefix and the
      // siblings still to come.
      const int ci = best_c(c.series[i], m, t, bound - total - k_min - rest);
      if (ci >= kInfCost) {
        continue;
      }
      for (int b = t; b <= d; ++b) {
        if (K[static_cast<size_t>(b - t)] >= kInfCost) {
          continue;
        }
        const int v = K[static_cast<size_t>(b - t)] + ci;
        if (v < next[static_cast<size_t>(b)]) {
          next[static_cast<size_t>(b)]      = v;
          choice[i][static_cast<size_t>(b)] = t;
        }
      }
    }
    K = std::move(next);
  }
  int best_b = -1;
  for (int b = 1; b <= d; ++b) {
    if (K[static_cast<size_t>(b)] < kInfCost && (best_b < 0 || K[static_cast<size_t>(b)] < K[static_cast<size_t>(best_b)])) {
      best_b = b;
    }
  }
  if (best_b < 0 || total + K[static_cast<size_t>(best_b)] > bound) {
    return kInfCost;
  }
  if (alloc != nullptr) {
    alloc->assign(k, 0);
    int b = best_b;
    for (size_t i = k; i-- > 0;) {
      (*alloc)[i]  = choice[i][static_cast<size_t>(b)];
      b           -= (*alloc)[i];
    }
  }
  return total + K[static_cast<size_t>(best_b)];
}

int Sp_factorer::best_c(const Truth& g_in, int m_in, int d, int bound) {
  if (d <= 0 || bound <= 0) {
    return kInfCost;
  }
  // Re-compact: parts handed in by candidates may have lost variables.
  const Compact cc = compact(g_in, m_in);
  const Truth&  g  = cc.g;
  const int     m  = cc.m;
  if (m == 0) {
    return kInfCost;  // constant: not a gate
  }
  if (m == 1) {
    return 1;
  }
  if (m > bound) {
    return kInfCost;  // every variable appears at least once
  }
  const int dd = std::min(d, m);
  if (exact_small_ && m <= Exact4::kVars) {
    const int c = exact4().get(embed4(g, m), dd).cost;
    return c <= bound ? c : kInfCost;
  }
  const Key key{g, static_cast<int8_t>(m), static_cast<int8_t>(dd)};
  if (auto it = memo_.find(key); it != memo_.end()) {
    const Memo& e = it->second;
    if (e.cost <= e.bound) {
      return e.cost <= bound ? e.cost : kInfCost;  // exact
    }
    if (bound <= e.bound) {
      return kInfCost;  // already known: nothing within this budget
    }
  }
  for (auto p : prime_implicants(g, m)) {
    if (std::popcount(p) > dd) {
      memo_[key] = Memo{.cost = kInfCost, .bound = kInfCost};  // no tree at this stack, for any budget
      return kInfCost;
    }
  }
  memo_[key] = Memo{.cost = kInfCost, .bound = bound};  // re-entry guard while computing
  int result = kInfCost;
  int limit  = bound;  // a better solution must beat the best so far
  for (const Cand& c : candidates(g, m)) {
    const int cc_cost = cand_cost(c, m, dd, limit, nullptr);
    if (cc_cost < result) {
      result = cc_cost;
      limit  = result - 1;
    }
  }
  memo_[key] = Memo{.cost = result <= bound ? result : kInfCost, .bound = bound};
  return result <= bound ? result : kInfCost;
}

// Smallest stack bound at or below d that still reaches best_c(g, d): among
// minimum-transistor networks the shallower stack is the faster gate.
int Sp_factorer::tighten(const Truth& g, int m, int d, int bound) {
  const int c = best_c(g, m, d, bound);
  int       t = d;
  while (t > 1 && best_c(g, m, t - 1, bound) == c) {
    --t;
  }
  return t;
}

int Sp_factorer::build_c(const Truth& g_in, int m_in, int d_in, int bound, Sp_formula& out,
                         const std::array<int, kMaxVars>& relabel_in) {
  const int                 d  = tighten(g_in, m_in, d_in, bound);
  const Compact             cc = compact(g_in, m_in);
  const Truth&              g  = cc.g;
  const int                 m  = cc.m;
  std::array<int, kMaxVars> relabel{};
  for (int i = 0; i < m; ++i) {
    relabel[static_cast<size_t>(i)] = relabel_in[static_cast<size_t>(cc.var[static_cast<size_t>(i)])];
  }
  auto lit = [&](int v) {
    out.nodes.push_back({Sp_formula::Kind::lit, relabel[static_cast<size_t>(v)], {}});
    return static_cast<int>(out.nodes.size()) - 1;
  };
  auto node = [&](Sp_formula::Kind kind, std::vector<int> kids) {
    if (kids.size() == 1) {
      return kids[0];
    }
    std::vector<int> flat;  // a(b(c)) prints as abc
    for (int k : kids) {
      if (out.nodes[static_cast<size_t>(k)].kind == kind) {
        for (int kk : out.nodes[static_cast<size_t>(k)].kids) {
          flat.push_back(kk);
        }
      } else {
        flat.push_back(k);
      }
    }
    out.nodes.push_back({kind, -1, std::move(flat)});
    return static_cast<int>(out.nodes.size()) - 1;
  };
  assert(m >= 1);
  if (m == 1) {
    return lit(0);
  }
  const int dd     = std::min(d, m);
  const int target = best_c(g, m, dd, bound);
  assert(target < kInfCost);
  if (exact_small_ && m <= Exact4::kVars) {
    // Replay the exact table's decomposition, relabelling its four variables.
    const auto& tab     = exact4();
    auto        shallow = [&](uint16_t t, int dep) {  // same cost, smallest stack bound
      const int c = tab.get(t, dep).cost;
      while (dep > 1 && tab.get(t, dep - 1).cost == c) {
        --dep;
      }
      return dep;
    };
    auto rec = [&](auto& self, uint16_t t, int dep) -> int {
      const auto& e = tab.get(t, shallow(t, dep));
      if (e.op == 0) {
        return lit(e.var);
      }
      const int a = self(self, e.a, e.da);
      const int b = self(self, e.b, e.db);
      return node(e.op == 1 ? Sp_formula::Kind::series : Sp_formula::Kind::parallel, {a, b});
    };
    return rec(rec, embed4(g, m), dd);
  }
  for (const Cand& c : candidates(g, m)) {
    std::vector<int> alloc;
    if (cand_cost(c, m, dd, target, &alloc) != target) {
      continue;
    }
    std::vector<int> kids;
    if (!c.series.empty()) {
      std::vector<int> ser;
      for (size_t i = 0; i < c.series.size(); ++i) {
        // Each part gets the whole remaining budget: its exact cost is memoized.
        ser.push_back(build_c(c.series[i], m, alloc[i], target, out, relabel));
      }
      kids.push_back(node(Sp_formula::Kind::series, std::move(ser)));
    }
    for (const Truth& p : c.parallel) {
      kids.push_back(build_c(p, m, dd, target, out, relabel));
    }
    return node(Sp_formula::Kind::parallel, std::move(kids));
  }
  assert(false && "best_c() found a cost no candidate reproduces");
  return lit(0);
}

int Sp_factorer::cost(const Truth& f, int n, int max_stack, int max_transistors) {
  return best_c(f, n, std::min(max_stack, n), max_transistors);
}

std::optional<Sp_formula> Sp_factorer::factor(const Truth& f, int n, int max_stack, int max_transistors) {
  const int d = std::min(max_stack, n);  // a minimal tree never repeats a variable along a path
  const int c = best_c(f, n, d, max_transistors);
  if (c >= kInfCost) {
    return std::nullopt;
  }
  Sp_formula                out;
  std::array<int, kMaxVars> identity{};
  for (int j = 0; j < kMaxVars; ++j) {
    identity[static_cast<size_t>(j)] = j;
  }
  out.root = build_c(f, n, d, c, out, identity);
  out.finalize();
  return out;
}

Gate_fit Sp_factorer::fit(const Truth& f_in, int n, const Gate_limits& lim, Rail out_rail) {
  Gate_fit    fit;
  const Truth f = out_rail == Rail::neg ? truth_not(f_in, n) : f_in;
  fit.leaf_rail.fill(Rail::pos);
  if (is_const(f, n)) {
    fit.reason = Fit_reason::constant;
    return fit;
  }
  const auto un = unateness(f, n);
  Truth      g  = f;
  for (int j = 0; j < n; ++j) {
    switch (un[static_cast<size_t>(j)]) {
      case Unate::independent: break;
      case Unate::positive   : fit.support |= (1u << j); break;
      case Unate::negative:
        fit.support                           |= (1u << j);
        fit.leaf_rail[static_cast<size_t>(j)]  = Rail::neg;
        g                                      = flip_var(g, n, j);
        break;
      case Unate::binate:
        fit.support     |= (1u << j);
        fit.binate_mask |= (1u << j);
        break;
    }
  }
  if (fit.binate_mask != 0) {
    fit.reason = Fit_reason::binate;
    return fit;
  }
  for (auto p : prime_implicants(g, n)) {
    fit.max_prime = std::max(fit.max_prime, std::popcount(p));
  }
  if (std::popcount(fit.support) > lim.k) {
    fit.reason = Fit_reason::fanin;
    return fit;
  }
  if (fit.max_prime > lim.stack) {
    fit.reason = Fit_reason::stack;
    return fit;
  }
  fit.formula = factor(g, n, lim.stack, lim.budget);
  fit.reason  = fit.formula.has_value() ? Fit_reason::ok : Fit_reason::budget;
  return fit;
}

}  // namespace livehd::domino
