//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "debug_env.hpp"
#include "rulegen/rule_test_support.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "aegraph.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "query.hpp"
#include "rulegen/rule_expr.hpp"
#include "mask_eval.hpp"
#include "runtime/analyses.hpp"
#include "runtime/match.hpp"

namespace livehd::aegraph::rulegen::testing {

namespace gu = livehd::graph_util;
using rt::ex::Num;

namespace {

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

class Rng {
public:
  explicit Rng(uint64_t seed) : s_(seed) {}
  uint64_t next() {  // splitmix64
    uint64_t z = (s_ += 0x9e3779b97f4a7c15ULL);
    z          = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z          = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }
  uint64_t below(uint64_t n) { return n == 0 ? 0 : next() % n; }

private:
  uint64_t s_;
};

uint64_t fnv(std::string_view s) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (const unsigned char c : s) {
    h = (h ^ c) * 0x100000001b3ULL;
  }
  return h;
}

Dlop D(int64_t v) {
  Dlop r;
  r = Dlop::create_integer(v);
  return r;
}
Dlop shl_(const Dlop& a, int64_t n) {
  Dlop r;
  r = a.shl_op(D(n));
  return r;
}
Dlop or_(const Dlop& a, const Dlop& b) {
  Dlop r;
  r = a.or_op(b);
  return r;
}
Dlop sub_(const Dlop& a, const Dlop& b) {
  Dlop r;
  r = a.sub_op(b);
  return r;
}

// A random integer of up to `bits` bits (built from 32-bit chunks), negative half the time.
Dlop big_value(Rng& rng, int bits) {
  Dlop v = D(0);
  for (int c = 0; c < (bits + 31) / 32; ++c) {
    v = or_(shl_(v, 32), D(static_cast<int64_t>(rng.next() & 0xffffffffULL)));
  }
  return (rng.next() & 1U) != 0 ? sub_(D(0), v) : v;
}

constexpr int64_t kSmall[] = {0, 1, -1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 31, 32, 63, 64, 100, 255, -2, -3, -8, -16, -128};

// A class value: small interesting integers, 64-bit extremes, and wide values past 64 bits.
Dlop sample_value(Rng& rng) {
  switch (rng.below(10)) {
    case 0 : return D(kSmall[rng.below(std::size(kSmall))]);
    case 1 :
    case 2 :
    case 3 : return D(static_cast<int64_t>(rng.below(65)) - 32);
    case 4 : return D(static_cast<int64_t>(rng.below(2)));
    case 5 :
    case 6 : return D(static_cast<int64_t>(rng.next()) >> rng.below(63));
    case 7 :
    case 8 : return big_value(rng, 65 + static_cast<int>(rng.below(66)));
    default: {
      const int64_t p = int64_t{1} << rng.below(62);
      return D(p + static_cast<int64_t>(rng.below(3)) - 1);
    }
  }
}

// The value a `(k c)` binds: small, a power of two (with neighbours), or a negative; magnitudes stay
// below 2^40 so a shift or Sext amount built from it never asks Dlop for a gigantic result.
int64_t sample_k(Rng& rng) {
  switch (rng.below(6)) {
    case 0 : return kSmall[rng.below(std::size(kSmall))];
    case 1 :
    case 2 : return static_cast<int64_t>(rng.below(70)) - 8;
    case 3 :
    case 4 : return int64_t{1} << rng.below(21);
    default: return (int64_t{1} << rng.below(41)) + static_cast<int64_t>(rng.below(3)) - 1;
  }
}

std::optional<Num> to_num(const Dlop& v) {
  if (v.has_unknowns() || !v.is_integer() || !v.is_just_i64()) {
    return std::nullopt;
  }
  return rt::ex::num(v.to_just_i64());
}

// ---------------------------------------------------------------------------
// Environments and expression evaluation
// ---------------------------------------------------------------------------

struct Env {
  std::map<std::string, Dlop>                                      cls;   // class variables and (k c) values
  std::map<std::string, Num>                                       num;   // (k c) constants and :let values
  std::map<std::string, Dlop>                                      lit;   // ?q values
  std::map<std::string, std::vector<std::pair<uint32_t, Dlop>>>    rest;  // ..r operands, with their banks
};

Num eval_expr(const Expr& e, const Env& env, const Analyses& analyses) {
  switch (e.kind) {
    case Expr::Kind::num: return rt::ex::num(e.num);
    case Expr::Kind::var: {
      const auto it = env.num.find(e.name);
      return it == env.num.end() ? Num{} : it->second;
    }
    default: break;
  }
  if (rt::ex::find_builtin(e.name) != nullptr) {
    std::vector<Num> args;
    for (const auto& a : e.args) {
      args.push_back(eval_expr(a, env, analyses));
    }
    return rt::ex::call(e.name, args);
  }
  // An analysis predicate: its :spec decides, over the argument values (a class argument by its value, a
  // constant argument by its expression).
  const auto* an = analyses.find(e.name);
  if (an == nullptr || !an->spec || an->params.size() != e.args.size()) {
    return Num{};
  }
  Env local;
  for (size_t i = 0; i < e.args.size(); ++i) {
    if (an->is_const[i]) {
      const auto n = eval_expr(e.args[i], env, analyses);
      if (!n.ok) {
        return rt::ex::flag(false);
      }
      local.num[an->params[i]] = n;
      continue;
    }
    const auto it = env.cls.find(e.args[i].name);
    const auto n  = it == env.cls.end() ? std::nullopt : to_num(it->second);
    if (!n) {
      return rt::ex::flag(false);
    }
    local.num[an->params[i]] = *n;
  }
  return eval_expr(*an->spec, local, Analyses{});
}

// ---------------------------------------------------------------------------
// Instantiating a pattern on random values
// ---------------------------------------------------------------------------

// The value of `op` over `args`; a bank with no operand is its identity and a lone added operand is itself
// (what Ctx::make_bank builds), so the empty-rest cases of `(Sum (as ..r))` evaluate like the engine.
std::optional<Dlop> apply(Ntype_op op, std::vector<Eval_arg> args) {
  if (Ntype::is_banked_sink_op(op) && op != Ntype_op::LT && op != Ntype_op::GT && op != Ntype_op::Ror) {
    if (args.empty()) {
      return D(op == Ntype_op::And ? -1 : (op == Ntype_op::Mult ? 1 : 0));
    }
    if (args.size() == 1 && (op != Ntype_op::Sum || args[0].bank == 0)) {
      return args[0].value;
    }
  }
  return eval_op(op, args);
}

void collect_k_names(const Pat& p, std::vector<std::string>& out);

class Instantiator {
public:
  Instantiator(Rng& rng, const Analyses& analyses, Env& env, std::map<std::string, std::vector<const Analysis*>> constraints)
      : rng_(rng), analyses_(analyses), env_(env), constraints_(std::move(constraints)) {}

  // The left-hand side on random values: binds every variable of the pattern in env.
  std::optional<Dlop> lhs(const Pat& p) {
    switch (p.kind) {
      case Pat::Kind::wild: return sample_value(rng_);
      case Pat::Kind::var: {
        if (const auto it = env_.cls.find(p.name); it != env_.cls.end()) {
          return it->second;
        }
        auto v = sample_class(p.name);
        env_.cls.emplace(p.name, v);
        return v;
      }
      case Pat::Kind::num: return D(p.num);
      case Pat::Kind::konst: {
        if (p.name.empty() || p.name == "_") {
          return D(sample_k(rng_));
        }
        if (const auto it = env_.cls.find(p.name); it != env_.cls.end()) {
          return it->second;
        }
        const auto k = sample_k(rng_);
        env_.num[p.name] = rt::ex::num(k);
        env_.cls.emplace(p.name, D(k));
        return D(k);
      }
      case Pat::Kind::lit: {
        if (const auto it = env_.lit.find(p.name); it != env_.lit.end()) {
          return it->second;
        }
        auto v = unknown_value();
        env_.lit.emplace(p.name, v);
        return v;
      }
      case Pat::Kind::op: break;
    }
    std::vector<Eval_arg> args;
    if (!p.banked) {
      for (size_t i = 0; i < p.args.size(); ++i) {
        auto v = lhs(p.args[i]);
        if (!v) {
          return std::nullopt;
        }
        args.push_back({p.pids[i], p.pids[i], *v});
      }
    } else {
      hhds::Port_id port = 0;
      for (const auto& b : p.banks) {
        for (const auto& e : b.elems) {
          auto v = lhs(e);
          if (!v) {
            return std::nullopt;
          }
          args.push_back({port++, b.id, *v});
        }
        if (b.has_rest) {
          auto& list = env_.rest[b.rest];
          list.clear();
          const auto extra = rng_.below(3);
          for (uint64_t i = 0; i < extra; ++i) {
            list.emplace_back(b.id, sample_value(rng_));
            args.push_back({port++, b.id, list.back().second});
          }
        }
      }
    }
    return apply(p.op, std::move(args));
  }

  // A right-hand-side term under the bindings in env.
  std::optional<Dlop> term(const Pat& p) {
    switch (p.kind) {
      case Pat::Kind::var: {
        if (const auto it = env_.cls.find(p.name); it != env_.cls.end()) {
          return it->second;
        }
        if (const auto it = env_.num.find(p.name); it != env_.num.end()) {
          return D(it->second.v);
        }
        return std::nullopt;
      }
      case Pat::Kind::num: return D(p.num);
      case Pat::Kind::konst: {
        if (!p.expr) {
          return std::nullopt;
        }
        const auto n = eval_expr(*p.expr, env_, analyses_);
        return n.ok ? std::optional<Dlop>{D(n.v)} : std::nullopt;
      }
      case Pat::Kind::lit: {
        const auto it = env_.lit.find(p.name);
        return it == env_.lit.end() ? std::nullopt : std::optional<Dlop>{it->second};
      }
      case Pat::Kind::op: break;
      default: return std::nullopt;
    }
    std::vector<Eval_arg> args;
    if (!p.banked) {
      for (size_t i = 0; i < p.args.size(); ++i) {
        auto v = term(p.args[i]);
        if (!v) {
          return std::nullopt;
        }
        args.push_back({p.pids[i], p.pids[i], *v});
      }
    } else {
      hhds::Port_id port = 0;
      for (const auto& b : p.banks) {
        for (const auto& e : b.elems) {
          auto v = term(e);
          if (!v) {
            return std::nullopt;
          }
          args.push_back({port++, b.id, *v});
        }
        if (b.has_rest) {
          for (const auto& [bank, value] : env_.rest[b.rest]) {
            args.push_back({port++, b.id, value});
          }
        }
      }
    }
    return apply(p.op, std::move(args));
  }

  // Bind every named `(k c)` of the pattern before the pattern is walked, then solve the equalities a :when
  // states between them: `(= w (- h m))` sets w from h and m, so a rule that needs related constants
  // (adjacent windows and their widths) meets its guard instead of sampling it at random.
  void presample_ks(const Rule& rule) {
    std::vector<std::string> names;
    collect_k_names(rule.lhs, names);
    for (const auto& n : names) {
      if (env_.cls.contains(n)) {
        continue;
      }
      const auto k = sample_k(rng_);
      env_.num[n]  = rt::ex::num(k);
      env_.cls.emplace(n, D(k));
    }
    std::function<void(const Expr&)> solve = [&](const Expr& e) {
      if (e.kind != Expr::Kind::call) {
        return;
      }
      if (e.name == "and") {
        for (const auto& a : e.args) {
          solve(a);
        }
        return;
      }
      if (e.name != "=" || e.args.size() != 2) {
        return;
      }
      for (int side = 0; side < 2; ++side) {
        const auto& target = e.args[side];
        if (target.kind != Expr::Kind::var || std::ranges::find(names, target.name) == names.end()) {
          continue;
        }
        const auto v = eval_expr(e.args[1 - side], env_, analyses_);
        if (v.ok) {
          env_.num[target.name] = v;
          env_.cls[target.name] = D(v.v);
          return;
        }
      }
    };
    for (int pass = 0; pass < 3; ++pass) {
      for (const auto& w : rule.whens) {
        solve(w);
      }
    }
  }

  // A `?` literal value: some bits unknown.
  Dlop unknown_value() {
    const auto base = D(sample_k(rng_));
    const auto mask = static_cast<int64_t>(1 + rng_.below(255));
    Dlop       r;
    r = base.make_unknown_bits(D(mask));
    return r;
  }

private:
  // Values for a class variable a :when constrains through an analysis. A predicate over one class and no
  // constants is filtered here; every other one (constant arguments, several classes) is met by rejection in
  // the caller, so the generator only leans towards the shapes those predicates accept: small non-negative
  // values, shifted values (low zeros) and the generic mix.
  Dlop sample_class(const std::string& name) {
    const auto it = constraints_.find(name);
    for (int tries = 0; tries < 80; ++tries) {
      auto v = sample_value(rng_);
      if (it != constraints_.end() && tries % 3 == 0) {
        // A constrained variable is a bound-style fact (a range, a bit mask): keep the generic mix below 2^12
        // so a shift amount built from it never asks Dlop for a gigantic result.
        v = D(static_cast<int64_t>(rng_.below(int64_t{1} << 13)) - (int64_t{1} << 12));
      }
      if (tries >= 4) {  // the generic mix rarely hits a narrow spec: bias towards small values
        v = D(static_cast<int64_t>(rng_.below(4)) - 1);
      }
      if (it != constraints_.end() && tries % 3 == 1) {
        v = D(static_cast<int64_t>(rng_.below(16)));
      } else if (it != constraints_.end() && tries % 3 == 2) {
        v = D(static_cast<int64_t>(rng_.below(8)) << rng_.below(9));
      }
      if (it == constraints_.end()) {
        return v;
      }
      bool ok = true;
      for (const auto* an : it->second) {
        if (an->params.size() != 1) {
          continue;
        }
        Env local;
        local.cls["x"] = v;
        Expr call;
        call.kind = Expr::Kind::call;
        call.name = an->name;
        Expr arg;
        arg.kind = Expr::Kind::var;
        arg.name = "x";
        call.args.push_back(arg);
        ok = ok && rt::ex::holds(eval_expr(call, local, analyses_));
      }
      if (ok) {
        return v;
      }
    }
    return D(0);
  }

  Rng&                                                    rng_;
  const Analyses&                                         analyses_;
  Env&                                                    env_;
  std::map<std::string, std::vector<const Analysis*>>    constraints_;
};

void collect_k_names(const Pat& p, std::vector<std::string>& out) {
  if (p.kind == Pat::Kind::konst && !p.name.empty() && p.name != "_" && std::ranges::find(out, p.name) == out.end()) {
    out.push_back(p.name);
  }
  for (const auto& a : p.args) {
    collect_k_names(a, out);
  }
  for (const auto& b : p.banks) {
    for (const auto& e : b.elems) {
      collect_k_names(e, out);
    }
  }
}

// Which class variables a :when constrains through an analysis predicate.
void collect_constraints(const Expr& e, const Analyses& analyses, std::map<std::string, std::vector<const Analysis*>>& out) {
  if (e.kind != Expr::Kind::call) {
    return;
  }
  if (const auto* an = analyses.find(e.name); an != nullptr) {
    for (size_t i = 0; i < e.args.size() && i < an->is_const.size(); ++i) {
      if (!an->is_const[i] && e.args[i].kind == Expr::Kind::var) {
        out[e.args[i].name].push_back(an);
      }
    }
    return;
  }
  for (const auto& a : e.args) {
    collect_constraints(a, analyses, out);
  }
}

bool has_lit(const Pat& p) {
  if (p.kind == Pat::Kind::lit) {
    return true;
  }
  return std::ranges::any_of(p.args, has_lit)
         || std::ranges::any_of(p.banks, [](const Bank& b) { return std::ranges::any_of(b.elems, has_lit); });
}

std::string env_text(const Env& env) {
  std::string t;
  for (const auto& [n, v] : env.cls) {
    t += "  " + n + " = " + v.to_pyrope() + "\n";
  }
  for (const auto& [n, v] : env.lit) {
    t += "  " + n + " = " + v.to_pyrope() + "\n";
  }
  for (const auto& [n, list] : env.rest) {
    t += "  .." + n + " =";
    for (const auto& [bank, v] : list) {
      t += " " + std::string{bank == 0 ? "as:" : "bs:"} + v.to_pyrope();
    }
    t += "\n";
  }
  for (const auto& [n, v] : env.num) {
    if (!env.cls.contains(n)) {
      t += "  " + n + " = " + std::to_string(v.v) + "\n";
    }
  }
  return t;
}

// bit `pos` of v (sign-extended), and whether it is unknown.
bool known_bit_differs(const Dlop& l, const Dlop& r, int pos) {
  if (l.unknown_bit_test(pos)) {
    return false;  // an unknown LHS bit may become anything
  }
  return r.unknown_bit_test(pos) || r.bit_test(pos) != l.bit_test(pos);
}

}  // namespace

// ---------------------------------------------------------------------------
// Fuzz
// ---------------------------------------------------------------------------

Fuzz_report fuzz_equal(const Rule& rule, const Analyses& analyses, uint64_t seed, size_t samples) {
  Fuzz_report report;
  Rng         rng(seed ^ fnv(rule.name));
  std::map<std::string, std::vector<const Analysis*>> constraints;
  for (const auto& w : rule.whens) {
    collect_constraints(w, analyses, constraints);
  }
  const bool refine = rule.tag != Rule::Tag::choice && has_lit(rule.lhs);

  for (size_t sample = 0; sample < samples && report.failure.empty(); ++sample) {
    for (size_t a = 0; a < rule.arms.size(); ++a) {
      const auto& arm = rule.arms[a];
      bool        done = false;
      for (int attempt = 0; attempt < 200 && !done; ++attempt) {
        Env env;
        if (rule.tag == Rule::Tag::choice) {
          // The arm's assignment substituted for its literal.
          env.lit[arm.lit] = D(arm.nonzero ? static_cast<int64_t>(1 + rng.below(40)) : arm.value.value_or(0));
        }
        Instantiator inst(rng, analyses, env, constraints);
        inst.presample_ks(rule);
        const auto   lhs = inst.lhs(rule.lhs);
        if (!lhs) {
          continue;
        }
        bool ok = true;
        for (const auto& [n, e] : rule.lets) {
          const auto v = eval_expr(e, env, analyses);
          ok           = ok && v.ok;
          env.num[n]   = v;
        }
        for (const auto& w : rule.whens) {
          ok = ok && rt::ex::holds(eval_expr(w, env, analyses));
        }
        if (!ok) {
          continue;
        }
        const auto rhs = inst.term(arm.rhs);
        if (!rhs) {
          continue;
        }
        done = true;
        const auto describe = [&](const char* why) {
          return std::string{why} + " in rule `" + rule.name + "` (arm " + std::to_string(a) + ", sample " + std::to_string(sample)
                 + ")\n  rule: " + rule_text(rule) + "\n  lhs value: " + lhs->to_pyrope() + "\n  rhs value: " + rhs->to_pyrope()
                 + "\n  bindings:\n" + env_text(env);
        };
        if (refine || lhs->has_unknowns()) {
          // A left-hand side that is the all-unknown value (x / 0) or carries a `?` literal: the right-hand
          // side has to be one of its refinements -- every known left-hand bit kept, no new `?`.
          if (rhs->has_unknowns() && !lhs->has_unknowns()) {
            report.failure = describe("a new ? appears on the right-hand side");
            break;
          }
          for (int pos = 0; pos < 320; ++pos) {
            if (known_bit_differs(*lhs, *rhs, pos)) {
              report.failure = describe(("the right-hand side loses known left-hand bit " + std::to_string(pos)).c_str());
              break;
            }
          }
        } else {
          if (rhs->has_unknowns()) {
            report.failure = describe("a ? appears on the right-hand side");
            break;
          }
          if (!lhs->eq_op(*rhs)->is_known_true()) {
            report.failure = describe("the two sides differ");
          }
        }
        ++report.samples;
      }
      if (!done) {
        ++report.skipped;
      }
    }
  }
  return report;
}

// ---------------------------------------------------------------------------
// Term -> micro-LGraph (the example and the bv tier)
// ---------------------------------------------------------------------------

namespace {

struct Built {
  hhds::Pin_class pin;
  int             bits = 0;  // signed bits that hold every value; -1 = unsupported / too wide
};

constexpr int kMaxBits = 600;

int const_bits(int64_t v) {
  const uint64_t m = v < 0 ? static_cast<uint64_t>(~v) : static_cast<uint64_t>(v);
  return (m == 0 ? 1 : 64 - std::countl_zero(m)) + 1;
}

class Term_builder {
public:
  using Leaf = std::function<Built(const Pat&)>;
  using Rest = std::function<std::vector<Built>(const std::string&)>;

  // `fixed_bits`: stamp every node unsigned with that width (the engine example); else stamp each node
  // with its own computed signed width.
  Term_builder(hhds::Graph& g, Leaf leaf, Rest rest, const Env& env, const Analyses& analyses,
               std::optional<int> fixed_bits = std::nullopt)
      : g_(g), leaf_(std::move(leaf)), rest_(std::move(rest)), env_(env), analyses_(analyses), fixed_(fixed_bits) {}

  Built constant(int64_t v) const { return Built{gu::create_const(g_, D(v)), const_bits(v)}; }

  Built build(const Pat& p) {
    switch (p.kind) {
      case Pat::Kind::wild:
      case Pat::Kind::var:
      case Pat::Kind::lit: return leaf_(p);
      case Pat::Kind::num: return constant(p.num);
      case Pat::Kind::konst: {
        if (!p.expr || (p.expr->kind == Expr::Kind::var && p.expr->name == "_")) {
          return leaf_(p);  // an unnamed (k _)
        }
        const auto n = eval_expr(*p.expr, env_, analyses_);
        return n.ok ? constant(n.v) : Built{{}, -1};
      }
      case Pat::Kind::op: break;
    }
    std::vector<std::pair<uint32_t, Built>> kids;  // (pid or bank, built)
    if (!p.banked) {
      for (size_t i = 0; i < p.args.size(); ++i) {
        kids.emplace_back(p.pids[i], build(p.args[i]));
      }
    } else {
      for (const auto& b : p.banks) {
        for (const auto& e : b.elems) {
          kids.emplace_back(b.id, build(e));
        }
        if (b.has_rest) {
          for (const auto& rb : rest_(b.rest)) {
            kids.emplace_back(b.id, rb);
          }
        }
      }
    }
    for (const auto& [id, kid] : kids) {
      (void)id;
      if (kid.bits < 0) {
        return {{}, -1};
      }
    }
    const auto bits = result_bits(p, kids);
    if (bits < 0 || bits > kMaxBits) {
      return {{}, -1};
    }
    auto node = gu::create_typed_node(g_, p.op);
    for (const auto& [id, kid] : kids) {
      kid.pin.connect_sink(gu::setup_sink_pid(node, id));
    }
    Built out;
    out.pin  = node.create_driver_pin(0);
    out.bits = fixed_ ? *fixed_ : bits;
    if (fixed_) {
      gu::set_ubits(out.pin, out.bits);
    } else if (p.op == Ntype_op::EQ || p.op == Ntype_op::LT || p.op == Ntype_op::GT || p.op == Ntype_op::Ror) {
      gu::set_ubits(out.pin, 1);  // a truth value is one bit; the encoder builds it as such (a wider stamp makes it throw)
    } else {
      gu::set_sbits(out.pin, out.bits);
    }
    return out;
  }

private:
  std::optional<int64_t> const_value(const Pat& p) const {
    if (p.kind == Pat::Kind::num) {
      return p.num;
    }
    if (p.kind == Pat::Kind::konst && p.expr) {
      const auto n = eval_expr(*p.expr, env_, analyses_);
      return n.ok ? std::optional<int64_t>{n.v} : std::nullopt;
    }
    if (p.kind == Pat::Kind::var) {
      const auto it = env_.num.find(p.name);
      return it == env_.num.end() ? std::nullopt : std::optional<int64_t>{it->second.v};
    }
    return std::nullopt;
  }

  [[nodiscard]] int result_bits(const Pat& p, const std::vector<std::pair<uint32_t, Built>>& kids) const {
    int mx = 0, sum = 0;
    for (const auto& [id, kid] : kids) {
      (void)id;
      mx = std::max(mx, kid.bits);
      sum += kid.bits;
    }
    const auto n = static_cast<int>(kids.size());
    switch (p.op) {
      case Ntype_op::Sum: return mx + (n <= 1 ? 1 : 32 - std::countl_zero(static_cast<uint32_t>(n - 1))) + 1;
      case Ntype_op::Mult: return sum;
      case Ntype_op::And:
      case Ntype_op::Or:
      case Ntype_op::Xor: return mx;
      case Ntype_op::Not:
      case Ntype_op::SRA: return kids.empty() ? -1 : kids[0].second.bits;
      case Ntype_op::Sext: {
        // The encoder evaluates the cell at its own stamped width, so the stamp has to reach the sign bit (b-1).
        if (kids.size() != 2 || p.args.size() != 2) {
          return -1;
        }
        const auto b = const_value(p.args[1]);
        return (b && *b >= 1 && *b <= kMaxBits) ? std::max(kids[0].second.bits, static_cast<int>(*b)) : -1;
      }
      case Ntype_op::Ror:
      case Ntype_op::EQ:
      case Ntype_op::LT:
      case Ntype_op::GT: return 2;
      case Ntype_op::Mux: {
        int arms = 0;
        for (size_t i = 1; i < kids.size(); ++i) {
          arms = std::max(arms, kids[i].second.bits);
        }
        return arms;
      }
      case Ntype_op::Div: return kids.size() == 2 ? kids[0].second.bits + 1 : -1;
      case Ntype_op::Rem: return kids.size() == 2 ? kids[1].second.bits : -1;
      case Ntype_op::SHL: {
        if (kids.size() != 2 || p.args.size() != 2) {
          return -1;
        }
        const auto c = const_value(p.args[1]);
        if (c && *c >= 0 && *c < kMaxBits) {
          return kids[0].second.bits + static_cast<int>(*c);
        }
        const auto amount_bits = kids[1].second.bits;
        return amount_bits <= 8 ? kids[0].second.bits + (1 << (amount_bits - 1)) : -1;
      }
      case Ntype_op::Get_mask: {
        if (kids.size() != 3 || p.args.size() != 3) {
          return -1;
        }
        const auto lo = const_value(p.args[1]), hi = const_value(p.args[2]);
        return (lo && hi && *hi > *lo && *hi - *lo < kMaxBits) ? static_cast<int>(*hi - *lo + 1) : -1;
      }
      case Ntype_op::Set_mask: {
        if (kids.size() != 4 || p.args.size() != 4) {
          return -1;
        }
        const auto lo = const_value(p.args[1]), hi = const_value(p.args[2]);
        return (lo && hi && *hi > *lo && *hi < kMaxBits) ? std::max(kids[0].second.bits, static_cast<int>(*hi)) + 1 : -1;
      }
      case Ntype_op::Concat: {
        if (kids.size() % 2 != 0 || kids.empty()) {
          return -1;
        }
        int64_t total = 0;
        for (size_t i = 1; i < p.args.size(); i += 2) {
          const auto w = const_value(p.args[i]);
          if (!w || *w <= 0 || *w >= kMaxBits) {
            return -1;
          }
          total += *w;
        }
        return total < kMaxBits ? static_cast<int>(total) + 1 : -1;
      }
      default: return -1;
    }
  }

  hhds::Graph&       g_;
  Leaf               leaf_;
  Rest               rest_;
  const Env&         env_;
  const Analyses&    analyses_;
  std::optional<int> fixed_;
};

void collect_inputs(const Pat& p, std::vector<std::string>& names) {
  if (p.kind == Pat::Kind::var && std::ranges::find(names, p.name) == names.end()) {
    names.push_back(p.name);
  }
  for (const auto& a : p.args) {
    collect_inputs(a, names);
  }
  for (const auto& b : p.banks) {
    for (const auto& e : b.elems) {
      collect_inputs(e, names);
    }
  }
}

// Build the rule's :example as a micro-LGraph (8-bit unsigned inputs and output) and run the engine on it with
// only that rule enabled. Returns why it could not, or empty with `stats` filled and `id` the rule's id.
std::string run_example(const Rule& rule, Options opts, Stats& stats, rt::Rule_id& id) {
  if (!rule.example) {
    return "no :example";
  }
  auto&      reg = rt::Registry::instance();
  const auto found = reg.find(rule.name);
  if (!found) {
    return "rule `" + rule.name + "` is not in the registry (is the generated file linked into pass_aegraph?)";
  }
  id = *found;
  std::vector<std::string> inputs;
  collect_inputs(*rule.example, inputs);

  hhds::GraphLibrary lib;
  auto               io  = lib.create_io("example_" + rule.name);
  int                pid = 0;
  for (const auto& in : inputs) {
    io->add_input(in.substr(1), pid++);
    io->set_bits(in.substr(1), 8);
    io->set_unsign(in.substr(1), true);
  }
  io->add_output("o", pid++);
  io->set_bits("o", 8);
  io->set_unsign("o", true);
  auto g = io->create_graph();

  const Env      env;
  const Analyses none;
  Term_builder   builder(
      *g,
      [&](const Pat& p) { return Built{g->get_input_pin(p.name.substr(1)), 8}; },
      [](const std::string&) { return std::vector<Built>{}; },
      env,
      none,
      8);
  const auto root = builder.build(*rule.example);
  if (root.bits < 0) {
    return "the example cannot be built as a micro-LGraph";
  }
  root.pin.connect_sink(g->get_output_pin("o"));

  opts.rules_only = rule.name;
  opts.choices    = rule.tag == Rule::Tag::choice;
  stats           = optimize(g, {}, opts);
  if (id >= stats.rule_counts.size()) {
    return "the engine reported no counters for the rule";
  }
  return {};
}

}  // namespace

std::string example_fires(const Rules_file& file, const Rule& rule) {
  (void)file;
  Options     opts;
  opts.check = Options::Check::full;
  Stats       stats;
  rt::Rule_id id = 0;
  if (auto why = run_example(rule, opts, stats, id); !why.empty()) {
    return why;
  }
  const auto& c     = stats.rule_counts[id];
  const auto  fired = rule.tag == Rule::Tag::choice ? c.matched : c.applied;
  if (fired == 0) {
    return "the :example never made `" + rule.name + "` fire (tried " + std::to_string(c.tried) + ", matched "
           + std::to_string(c.matched) + "): " + pat_text(*rule.example);
  }
  return {};
}

std::string expect_side(const Rules_file& file, const Rule& rule, const std::string& profile, const std::string& side) {
  (void)file;
  Options opts;
  opts.check = Options::Check::full;
  if (profile == "sim") {
    opts.cost = Options::Cost::sim;
  } else if (profile != "synth") {
    return "unknown cost profile `" + profile + "`";
  }
  Stats       stats;
  rt::Rule_id id = 0;
  if (auto why = run_example(rule, opts, stats, id); !why.empty()) {
    return why;
  }
  const auto& c = stats.rule_counts[id];
  if (c.applied == 0) {
    return "the :example never made `" + rule.name + "` fire, so there is nothing to extract: " + pat_text(*rule.example);
  }
  // `extracted` counts the e-nodes the rule built that the extraction kept and the elaborator realized.
  const bool took_rhs = c.extracted != 0;
  if (side == "rhs" && !took_rhs) {
    return "profile `" + profile + "` keeps the left-hand side of `" + rule.name + "`, :expect says it takes the right-hand side";
  }
  if (side == "lhs" && took_rhs) {
    return "profile `" + profile + "` takes the right-hand side of `" + rule.name + "`, :expect says it keeps the left-hand side";
  }
  return {};
}

// ---------------------------------------------------------------------------
// The bv proof tier
// ---------------------------------------------------------------------------

namespace {

struct Skeleton {
  std::vector<std::string>                      vars;   // class variables, first appearance
  int                                           wilds = 0;
  std::vector<std::pair<std::string, uint32_t>> rests;  // ..r with its bank
  std::vector<std::string>                      ks;     // (k c) names
  bool                                          unsupported = false;
};

void walk_skeleton(const Pat& p, Skeleton& sk) {
  switch (p.kind) {
    case Pat::Kind::wild: ++sk.wilds; break;
    case Pat::Kind::var:
      if (std::ranges::find(sk.vars, p.name) == sk.vars.end()) {
        sk.vars.push_back(p.name);
      }
      break;
    case Pat::Kind::konst:
      if (!p.name.empty() && p.name != "_" && std::ranges::find(sk.ks, p.name) == sk.ks.end()) {
        sk.ks.push_back(p.name);
      }
      break;
    case Pat::Kind::lit: sk.unsupported = true; break;
    case Pat::Kind::op:
      for (const auto& a : p.args) {
        walk_skeleton(a, sk);
      }
      for (const auto& b : p.banks) {
        for (const auto& e : b.elems) {
          walk_skeleton(e, sk);
        }
        if (b.has_rest) {
          sk.rests.emplace_back(b.rest, b.id);
        }
      }
      break;
    default: break;
  }
}

constexpr int64_t kProveK[] = {0, 1, 2, 3, 4, 5, 7, 8, -1, -2, 15, 16, 31, 32};

struct Instance {
  uint32_t                       width     = 1;
  uint32_t                       sign_mode = 0;  // 0 signed, 1 unsigned, 2 alternating
  std::map<std::string, int64_t> ks;
  std::map<std::string, int>     rest;

  [[nodiscard]] std::string text() const {
    std::string t = "w=" + std::to_string(width) + (sign_mode == 0 ? " signed" : (sign_mode == 1 ? " unsigned" : " mixed-sign"));
    for (const auto& [n, v] : ks) {
      t += " " + n + "=" + std::to_string(v);
    }
    for (const auto& [n, c] : rest) {
      t += " ..{" + n + "}x" + std::to_string(c);
    }
    return t;
  }
};

// The bindings of an instance; false when a :let or a (k)-value guard rejects it. Analysis guards are
// satisfied by narrowing the input, so they count as true here.
bool bind_instance(const Rule& rule, const Analyses& analyses, const Instance& inst, Env& env) {
  for (const auto& [n, v] : inst.ks) {
    env.num[n] = rt::ex::num(v);
  }
  for (const auto& [n, e] : rule.lets) {
    const auto v = eval_expr(e, env, analyses);
    if (!v.ok) {
      return false;
    }
    env.num[n] = v;
  }
  std::function<bool(const Expr&)> holds = [&](const Expr& e) -> bool {
    if (e.kind == Expr::Kind::call && analyses.find(e.name) != nullptr) {
      return true;
    }
    if (e.kind == Expr::Kind::call && (e.name == "and" || e.name == "or" || e.name == "not")) {
      std::vector<Num> args;
      for (const auto& a : e.args) {
        args.push_back(rt::ex::flag(holds(a)));
      }
      return rt::ex::holds(rt::ex::call(e.name, args));
    }
    return rt::ex::holds(eval_expr(e, env, analyses));
  };
  return std::ranges::all_of(rule.whens, holds);
}

struct In {
  std::string name;
  uint32_t    width;
  bool        unsign;
};

struct Side {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  int                          bits = -1;
};

struct Proof_result {
  enum class Kind { proven, undecided, unsupported, refuted, illegal } kind = Kind::illegal;
  std::string detail;
};

std::unique_ptr<Side> build_side(const std::vector<In>& ins, const Pat& root, const Analyses& analyses, const Instance& inst,
                                 const Env& env, const std::vector<std::pair<std::string, std::vector<std::string>>>& rest_inputs,
                                 int out_bits) {
  auto side = std::make_unique<Side>();
  auto io   = side->lib.create_io("m");
  int  pid  = 0;
  for (const auto& i : ins) {
    io->add_input(i.name, pid++);
    io->set_bits(i.name, i.width);
    io->set_unsign(i.name, i.unsign);
  }
  io->add_output("o", pid++);
  io->set_bits("o", static_cast<uint32_t>(std::max(out_bits, 2)));
  io->set_unsign("o", false);
  side->g = io->create_graph();
  auto& g = *side->g;
  // The encoder reads signedness and width from the PIN attributes, not from the IO declaration alone.
  for (const auto& i : ins) {
    if (i.unsign) {
      gu::set_ubits(g.get_input_pin(i.name), static_cast<int32_t>(i.width));
    } else {
      gu::set_sbits(g.get_input_pin(i.name), static_cast<int32_t>(i.width));
    }
  }

  const auto input = [&](const std::string& name) {
    for (const auto& i : ins) {
      if (i.name == name) {
        return Built{g.get_input_pin(name), static_cast<int>(i.width) + (i.unsign ? 1 : 0)};
      }
    }
    return Built{{}, -1};
  };
  int  wild_seen = 0;
  Term_builder builder(
      g,
      [&](const Pat& p) -> Built {
        if (p.kind == Pat::Kind::wild) {
          return input("w" + std::to_string(wild_seen++));
        }
        if (p.kind == Pat::Kind::var) {
          if (const auto k = inst.ks.find(p.name); k != inst.ks.end()) {
            return Built{gu::create_const(g, D(k->second)), const_bits(k->second)};
          }
          if (const auto l = env.num.find(p.name); l != env.num.end()) {
            return Built{gu::create_const(g, D(l->second.v)), const_bits(l->second.v)};
          }
          return input("v_" + p.name);
        }
        if (p.kind == Pat::Kind::konst) {  // an unnamed (k _)
          return Built{gu::create_const(g, D(1)), const_bits(1)};
        }
        return Built{{}, -1};
      },
      [&](const std::string& name) {
        std::vector<Built> out;
        for (const auto& [rn, names] : rest_inputs) {
          if (rn == name) {
            for (const auto& in : names) {
              out.push_back(input(in));
            }
          }
        }
        return out;
      },
      env, analyses);
  const auto root_built = builder.build(root);
  side->bits            = root_built.bits;
  if (root_built.bits >= 0) {
    root_built.pin.connect_sink(g.get_output_pin("o"));
  }
  return side;
}

// LHD_AEG_PROVE_DEBUG=1: the two micro-graphs of every instance, one line per node.
std::string dump_graph(const std::shared_ptr<hhds::Graph>& g) {
  std::string t;
  for (auto node : g->body().nodes()) {
    t += "    n" + std::to_string(node.get_debug_nid()) + " " + std::string{Ntype::get_name(gu::type_op_of(node))} + " bits=" +
         std::to_string(gu::bits_of(node.create_driver_pin(0))) + " <-";
    for (auto sink : node.inp_sorted_pins()) {
      t += " " + std::to_string(sink.get_port_id()) + "=";
      for (const auto& d : sink.get_driver_pins()) {
        if (d.is_const()) {
          t += "c" + gu::const_of(d).to_pyrope();
        } else {
          const auto nid = d.get_master_node().get_debug_nid();
          t += nid == 0 ? "in" : "n" + std::to_string(nid);
        }
      }
    }
    t += "\n";
  }
  return t;
}

// The width of an UNSIGNED input that satisfies every analysis the :when asks of it for all its values, or 0
// when there is none (an analysis over several classes, or a spec no width meets). bool01 gives 1; fits(x, n)
// gives min(n, width); nonneg any width.
uint32_t narrow_width(const std::string& var, const std::vector<const Analysis*>& list, const Rule& rule, const Analyses& analyses,
                      const Env& env, uint32_t max_width) {
  // The guards of the rule that call an analysis on `var`, with their constant arguments.
  std::vector<const Expr*> calls;
  std::function<void(const Expr&)> find = [&](const Expr& e) {
    if (e.kind != Expr::Kind::call) {
      return;
    }
    if (const auto* an = analyses.find(e.name); an != nullptr) {
      if (std::ranges::find(list, an) != list.end()) {
        for (size_t i = 0; i < e.args.size(); ++i) {
          if (!an->is_const[i] && e.args[i].kind == Expr::Kind::var && e.args[i].name == var) {
            calls.push_back(&e);
          }
        }
      }
      return;
    }
    for (const auto& a : e.args) {
      find(a);
    }
  };
  for (const auto& w : rule.whens) {
    find(w);
  }
  for (const auto* an : list) {
    if (std::ranges::count_if(an->is_const, [](bool c) { return !c; }) != 1) {
      return 0;
    }
  }
  for (uint32_t w = std::min<uint32_t>(max_width, 62); w >= 1; --w) {
    bool ok = true;
    for (const auto* call : calls) {
      for (const int64_t probe : {int64_t{0}, int64_t{1}, (int64_t{1} << (w - 1)), (int64_t{1} << w) - 1}) {
        Env local = env;
        local.cls[var] = D(probe);
        ok             = ok && rt::ex::holds(eval_expr(*call, local, analyses));
      }
    }
    if (ok) {
      return w;
    }
  }
  return 0;
}

Proof_result prove_instance(const Rule& rule, const Analyses& analyses, const Skeleton& sk, const Instance& inst,
                            const std::map<std::string, std::vector<const Analysis*>>& narrow, int timeout_s) {
  Env env;
  if (!bind_instance(rule, analyses, inst, env)) {
    return {Proof_result::Kind::illegal, "guard"};
  }
  std::vector<In> ins;
  size_t          idx     = 0;
  const auto      unsign = [&](size_t i) { return inst.sign_mode == 1 || (inst.sign_mode == 2 && i % 2 == 1); };
  for (const auto& v : sk.vars) {
    const auto it = narrow.find(v);
    uint32_t   nw = 0;
    if (it != narrow.end()) {
      nw = narrow_width(v, it->second, rule, analyses, env, inst.width);
      if (nw == 0) {
        return {Proof_result::Kind::illegal, "no input width meets the analysis guard"};
      }
    }
    ins.push_back({"v_" + v, it != narrow.end() ? nw : inst.width, it != narrow.end() || unsign(idx)});
    ++idx;
  }
  for (int w = 0; w < sk.wilds; ++w) {
    ins.push_back({"w" + std::to_string(w), inst.width, unsign(idx++)});
  }
  std::vector<std::pair<std::string, std::vector<std::string>>> rest_inputs;
  for (const auto& [name, bank] : sk.rests) {
    (void)bank;
    std::vector<std::string> names;
    for (int j = 0; j < inst.rest.at(name); ++j) {
      names.push_back("r_" + name + "_" + std::to_string(j));
      ins.push_back({names.back(), inst.width, unsign(idx++)});
    }
    rest_inputs.emplace_back(name, std::move(names));
  }

  // Pass 1 sizes the output, pass 2 builds with it: both sides compare at a width past every
  // intermediate, so no truncation can hide (or fake) a difference.
  const auto& rhs = rule.arms.front().rhs;
  const auto  l1  = build_side(ins, rule.lhs, analyses, inst, env, rest_inputs, 2);
  const auto  r1  = build_side(ins, rhs, analyses, inst, env, rest_inputs, 2);
  if (l1->bits < 0 || r1->bits < 0) {
    return {Proof_result::Kind::illegal, "unsupported width bound"};
  }
  const int  wide = std::max(l1->bits, r1->bits) + 2;
  const auto lg   = build_side(ins, rule.lhs, analyses, inst, env, rest_inputs, wide);
  const auto rg   = build_side(ins, rhs, analyses, inst, env, rest_inputs, wide);

  if (livehd::debug::env<"LHD_AEG_PROVE_DEBUG">() != nullptr) {
    std::printf("  [debug] instance %s\n  lhs:\n%s  rhs:\n%s", inst.text().c_str(), dump_graph(lg->g).c_str(),
                dump_graph(rg->g).c_str());
  }
  livehd::lec::Lec_options options;
  options.cones       = "false";
  options.timeout     = timeout_s;
  options.min_timeout = 1;
  livehd::lec::Query_result result;
  try {
    result = livehd::lec::prove_equal(lg->g.get(), rg->g.get(), options);
  } catch (const std::exception& e) {
    return {Proof_result::Kind::unsupported, std::string{"the encoder refused the instance: "} + e.what()};
  }
  switch (result.verdict) {
    case livehd::lec::Verdict::Proven: return {Proof_result::Kind::proven, {}};
    case livehd::lec::Verdict::Refuted:
      return {Proof_result::Kind::refuted, "counterexample: " + result.witness + "\n  " + result.detail};
    default: return {result.unsupported ? Proof_result::Kind::unsupported : Proof_result::Kind::undecided, result.detail};
  }
}

}  // namespace

Prove_report prove_bv(const Rule& rule, const Analyses& analyses, bool full, double limit_seconds) {
  Prove_report report;
  if (rule.arms.size() != 1 || rule.tag == Rule::Tag::choice) {
    return report;
  }
  Skeleton sk;
  walk_skeleton(rule.lhs, sk);
  if (sk.unsupported) {
    return report;  // a ?-literal rule: covered by the refinement fuzz
  }
  std::map<std::string, std::vector<const Analysis*>> constraints;
  for (const auto& w : rule.whens) {
    collect_constraints(w, analyses, constraints);
  }
  std::map<std::string, std::vector<const Analysis*>> narrow;
  for (const auto& [name, list] : constraints) {
    if (std::ranges::find(sk.vars, name) != sk.vars.end()) {
      narrow.emplace(name, list);
    }
  }

  // Every legal instance, in a fixed order.
  std::vector<Instance> all;
  const uint32_t        widths[] = {1, 2, 3, 8, 16, 65};
  std::vector<std::map<std::string, int64_t>> k_assign(1);
  for (const auto& k : sk.ks) {
    std::vector<std::map<std::string, int64_t>> next;
    for (const auto& base : k_assign) {
      for (const auto v : kProveK) {
        auto m = base;
        m[k]   = v;
        next.push_back(std::move(m));
      }
    }
    k_assign = std::move(next);
  }
  std::vector<std::map<std::string, int>> rest_assign(1);
  for (const auto& [name, bank] : sk.rests) {
    (void)bank;
    std::vector<std::map<std::string, int>> next;
    for (const auto& base : rest_assign) {
      for (const int c : {0, 1}) {
        auto m = base;
        m[name] = c;
        next.push_back(std::move(m));
      }
    }
    rest_assign = std::move(next);
  }
  for (const auto& ks : k_assign) {
    Instance probe;
    probe.ks = ks;
    Env env;
    if (!bind_instance(rule, analyses, probe, env)) {
      continue;
    }
    for (const auto& rest : rest_assign) {
      for (const auto w : widths) {
        for (uint32_t sign = 0; sign < 3; ++sign) {
          Instance inst = probe;
          inst.width     = w;
          inst.sign_mode = sign;
          inst.rest      = rest;
          all.push_back(std::move(inst));
        }
      }
    }
  }
  if (all.empty()) {
    return report;
  }
  std::vector<size_t> order(all.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::ranges::stable_sort(order, [&](size_t a, size_t b) {
    return fnv(rule.name + "#" + std::to_string(a)) < fnv(rule.name + "#" + std::to_string(b));
  });
  if (!full && order.size() > 8) {
    order.resize(8);
  }

  const auto start = std::chrono::steady_clock::now();
  for (const auto i : order) {
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > limit_seconds || report.undecided >= 2) {
      ++report.skipped;  // out of time, or the solver already gave up twice on this rule (division, wide multiplies)
      continue;
    }
    ++report.instances;
    const auto r = prove_instance(rule, analyses, sk, all[i], narrow, 2);
    switch (r.kind) {
      case Proof_result::Kind::proven: ++report.proven; break;
      case Proof_result::Kind::undecided:
        ++report.undecided;
        report.note = report.note.empty() ? r.detail : report.note;
        break;
      case Proof_result::Kind::unsupported:
        ++report.unsupported;
        report.note = report.note.empty() ? r.detail : report.note;
        break;
      case Proof_result::Kind::illegal: ++report.skipped; break;
      case Proof_result::Kind::refuted:
        report.refuted = "rule `" + rule.name + "` is REFUTED at instance [" + all[i].text() + "]\n  " + rule_text(rule)
                         + "\n  " + r.detail;
        return report;
    }
  }
  return report;
}

// ---------------------------------------------------------------------------
// gtest drivers
// ---------------------------------------------------------------------------

Parsed parse_all(const char* path, const char* rules, const char* analyses) {
  Parsed p;
  p.analyses = parse_analyses(analyses, "analyses.decl", p.diags);
  const std::string name = path;
  const auto        slash = name.find_last_of('/');
  const auto        stem  = name.substr(slash == std::string::npos ? 0 : slash + 1);
  p.file                  = parse_rules(rules, path, stem.substr(0, stem.find('.')), p.diags);
  return p;
}

namespace {

uint64_t env_u64(const char* name, uint64_t dflt) {
  const char* v = std::getenv(name);
  return (v != nullptr && *v != '\0') ? std::strtoull(v, nullptr, 0) : dflt;
}

std::chrono::steady_clock::time_point group_start() {
  static const auto start = std::chrono::steady_clock::now();
  return start;
}

#ifdef NDEBUG
constexpr double kWallBudget = 25.0;
#else
constexpr double kWallBudget = 90.0;
#endif

const Rule* find_rule(const Parsed& p, const char* name) {
  for (const auto& r : p.file.rules) {
    if (r.name == name) {
      return &r;
    }
  }
  ADD_FAILURE() << "no rule named " << name << " in " << p.file.path;
  return nullptr;
}

}  // namespace

void Rule_check::parses(const char* path, const char* rules, const char* analyses) {
  (void)group_start();
  const auto p = parse_all(path, rules, analyses);
  Diags diags = p.diags;
  if (diags.empty()) {
    lint({p.file}, p.analyses, {}, diags);
  }
  for (const auto& d : diags) {
    if (d.find(": error: ") != std::string::npos) {
      ADD_FAILURE() << d;
    }
  }
}

void Rule_check::fuzz_equal(const char* path, const char* rules, const char* analyses, const char* rule) {
  (void)group_start();
  const auto p = parse_all(path, rules, analyses);
  const auto* r = find_rule(p, rule);
  if (r == nullptr) {
    return;
  }
  const auto seed    = env_u64("LHD_AEG_FUZZ_SEED", 0x5eed0e01ULL);
  const auto samples = env_u64("LHD_AEG_FUZZ_SAMPLES", 1000);
  const auto report  = livehd::aegraph::rulegen::testing::fuzz_equal(*r, p.analyses, seed, samples);
  EXPECT_TRUE(report.failure.empty()) << report.failure << "\n  reproduce with LHD_AEG_FUZZ_SEED=" << seed;
  EXPECT_GE(report.samples + report.skipped, samples) << "every sample is compared or accounted for";
  EXPECT_GE(report.samples * 10, samples * r->arms.size() * 9)
      << "the sampler found a legal assignment for fewer than 90% of the samples (" << report.skipped
      << " skipped): a guard it cannot satisfy makes the check vacuous";
}

void Rule_check::example_fires(const char* path, const char* rules, const char* analyses, const char* rule) {
  (void)group_start();
  const auto p = parse_all(path, rules, analyses);
  const auto* r = find_rule(p, rule);
  if (r == nullptr) {
    return;
  }
  const auto why = livehd::aegraph::rulegen::testing::example_fires(p.file, *r);
  EXPECT_TRUE(why.empty()) << why;
}

void Rule_check::expect(const char* path, const char* rules, const char* analyses, const char* rule, const char* profile,
                        const char* side) {
  (void)group_start();
  const auto p = parse_all(path, rules, analyses);
  const auto* r = find_rule(p, rule);
  if (r == nullptr) {
    return;
  }
  const auto why = livehd::aegraph::rulegen::testing::expect_side(p.file, *r, profile, side);
  EXPECT_TRUE(why.empty()) << why;
}

void Rule_check::prove_bv(const char* path, const char* rules, const char* analyses, const char* rule) {
  (void)group_start();
  const auto p = parse_all(path, rules, analyses);
  const auto* r = find_rule(p, rule);
  if (r == nullptr) {
    return;
  }
  const bool full  = env_u64("LHD_AEG_PROVE_FULL", 0) != 0;
  const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - group_start()).count();
  const double left = std::max(1.0, (full ? 3600.0 : kWallBudget * 0.8) - used);
  const auto report = livehd::aegraph::rulegen::testing::prove_bv(*r, p.analyses, full, left);
  EXPECT_TRUE(report.refuted.empty()) << report.refuted;
  std::printf("[ prove ] %s: %zu instances, %zu proven, %zu undecided, %zu unsupported, %zu skipped%s%s\n", rule, report.instances,
              report.proven, report.undecided, report.unsupported, report.skipped,
              report.note.empty() ? "" : "\n           first: ", report.note.c_str());
}

void Rule_check::wall_time(const char* what) {
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - group_start()).count();
  if (env_u64("LHD_AEG_PROVE_FULL", 0) == 0) {
    EXPECT_LT(elapsed, kWallBudget) << what << " took " << elapsed << " s (budget " << kWallBudget << " s)";
  }
}

// ---------------------------------------------------------------------------
// The analysis soundness fuzz
// ---------------------------------------------------------------------------

namespace {

struct Stub_builder final : rt::Builder {
  Class_id make(Ntype_op, std::span<const Operand>) override { return kNoClass; }
  Class_id konst(const Dlop&) override { return kNoClass; }
  [[nodiscard]] bool single_use(Class_id) const override { return false; }
};

// A random DAG of raw e-nodes (no engine, no rules): analyses see exactly the shapes it builds.
struct Random_graph {
  Egraph                eg;
  std::vector<Class_id> classes;
  struct Spec {
    Ntype_op             op = Ntype_op::Invalid;  // Invalid: a leaf or a constant
    std::vector<Operand> ops;
    Class_id             alt_a = kNoClass, alt_b = kNoClass;  // a union class (op Invalid): its two equal sides
  };
  std::vector<Spec> specs;  // indexed by class id
  struct Declared {
    Class_id cls;
    int      bits;
    bool     is_unsigned;
  };
  std::vector<Declared> declared;  // leaves seeded with rt::seed_leaf

  Class_id add_spec(Class_id cls, Spec spec) {
    if (specs.size() <= cls) {
      specs.resize(cls + 1);
    }
    if (specs[cls].op == Ntype_op::Invalid && spec.op != Ntype_op::Invalid) {
      specs[cls] = std::move(spec);
    }
    if (std::ranges::find(classes, cls) == classes.end()) {
      classes.push_back(cls);
    }
    return cls;
  }
  Class_id leaf() { return add_spec(eg.add_leaf(), {}); }
  // A class equal to both `a` and `b` (the caller guarantees it): the meet path of the analyses.
  Class_id alt(Class_id a, Class_id b) {
    const auto cls = eg.add_union(a, b);
    if (specs.size() <= cls) {
      specs.resize(cls + 1);
    }
    specs[cls].alt_a = a;
    specs[cls].alt_b = b;
    if (std::ranges::find(classes, cls) == classes.end()) {
      classes.push_back(cls);
    }
    return cls;
  }
  Class_id konst(int64_t v) { return add_spec(eg.add_const(D(v)), {}); }
  Class_id node(Ntype_op op, std::vector<Operand> ops) {
    std::ranges::sort(ops);
    bool created = false;
    const auto cls = eg.intern(op, Enode_attrs{}, ops, created);
    return add_spec(cls, {op, std::move(ops)});
  }
  [[nodiscard]] std::string dump() const {
    std::string t;
    for (const auto c : classes) {
      t += "  c" + std::to_string(c) + " = ";
      if (specs[c].alt_a != kNoClass) {
        t += "union(c" + std::to_string(specs[c].alt_a) + " c" + std::to_string(specs[c].alt_b) + ")";
      } else if (specs[c].op == Ntype_op::Invalid) {
        const auto* v = eg.value_of(c);
        t += v != nullptr ? "const " + v->to_pyrope() : std::string{"leaf"};
      } else {
        t += std::string{Ntype::get_name(specs[c].op)} + "(";
        for (const auto& o : specs[c].ops) {
          t += " b" + std::to_string(o.bank) + ":c" + std::to_string(o.cls);
        }
        t += " )";
      }
      t += "\n";
    }
    return t;
  }
};

void random_graph(Rng& rng, Random_graph& g) {
  const int    leaves = 2 + static_cast<int>(rng.below(3));
  for (int i = 0; i < leaves; ++i) {
    const auto l = g.leaf();
    if (rng.below(2) != 0) {  // a declared width: the leaf's value domain is a fact about the class
      const int  bits = 1 + static_cast<int>(rng.below(40));
      const bool uns  = rng.below(2) != 0;
      rt::seed_leaf(g.eg, l, bits, uns);
      g.declared.push_back({l, bits, uns});
    }
  }
  for (const int64_t k : {0, 1, 2, 3, 4, 7, 8, 15, 16, 255, -1}) {
    g.konst(k);
  }
  const auto pick = [&]() { return g.classes[rng.below(g.classes.size())]; };
  const auto small_k = [&](uint64_t n) { return g.konst(static_cast<int64_t>(rng.below(n))); };
  const int  nodes = 14 + static_cast<int>(rng.below(18));
  for (int i = 0; i < nodes; ++i) {
    switch (rng.below(23)) {
      case 0: {
        std::vector<Operand> ops{{0, pick()}};
        for (uint64_t j = rng.below(3); j > 0; --j) {
          ops.push_back({static_cast<uint32_t>(rng.below(2)), pick()});
        }
        g.node(Ntype_op::Sum, ops);
        break;
      }
      case 1: g.node(Ntype_op::Mult, {{0, pick()}, {0, pick()}}); break;
      case 2:
      case 3:
      case 4: {
        std::vector<Operand> ops{{0, pick()}, {0, pick()}};
        if (rng.below(3) == 0) {
          ops.push_back({0, pick()});
        }
        g.node(rng.below(3) == 0 ? Ntype_op::And : (rng.below(2) != 0 ? Ntype_op::Or : Ntype_op::Xor), ops);
        break;
      }
      case 5: g.node(Ntype_op::Not, {{0, pick()}}); break;
      case 6: g.node(Ntype_op::SHL, {{0, pick()}, {1, small_k(70)}}); break;
      case 7: g.node(Ntype_op::SRA, {{0, pick()}, {1, small_k(70)}}); break;
      case 8: g.node(Ntype_op::Sext, {{0, pick()}, {1, g.konst(1 + static_cast<int64_t>(rng.below(70)))}}); break;
      case 9:
      case 10: {
        const auto lo = static_cast<int64_t>(rng.below(40));
        g.node(Ntype_op::Get_mask, {{0, pick()}, {2, g.konst(lo)}, {3, g.konst(lo + 1 + static_cast<int64_t>(rng.below(40)))}});
        break;
      }
      case 11: {
        const auto lo = static_cast<int64_t>(rng.below(40));
        g.node(Ntype_op::Set_mask,
               {{0, pick()}, {2, g.konst(lo)}, {3, g.konst(lo + 1 + static_cast<int64_t>(rng.below(24)))}, {4, pick()}});
        break;
      }
      case 12: {
        std::vector<Operand> ops;
        const auto           lanes = 1 + rng.below(3);
        for (uint64_t j = 0; j < lanes; ++j) {
          const auto w    = 1 + static_cast<int64_t>(rng.below(24));
          const auto lane = g.node(Ntype_op::Get_mask, {{0, pick()}, {2, g.konst(0)}, {3, g.konst(w)}});
          ops.push_back({static_cast<uint32_t>(2 * j), lane});
          ops.push_back({static_cast<uint32_t>(2 * j + 1), g.konst(w)});
        }
        g.node(Ntype_op::Concat, ops);
        break;
      }
      case 13: {
        // 1 to 4 arms; the selector is any class, so it is often outside the arm range (value '?', skipped)
        const auto           arms = 1 + rng.below(4);
        std::vector<Operand> ops{{0, rng.below(2) != 0 ? g.node(Ntype_op::Get_mask, {{0, pick()}, {2, g.konst(0)}, {3, g.konst(2)}}) : pick()}};
        for (uint64_t j = 1; j <= arms; ++j) {
          ops.push_back({static_cast<uint32_t>(j), pick()});
        }
        g.node(Ntype_op::Mux, ops);
        break;
      }
      case 14: g.node(Ntype_op::EQ, {{0, pick()}, {0, rng.below(2) != 0 ? g.konst(static_cast<int64_t>(rng.below(2))) : pick()}}); break;
      case 15: g.node(rng.below(2) != 0 ? Ntype_op::LT : Ntype_op::GT, {{0, pick()}, {1, pick()}}); break;
      case 16: g.node(Ntype_op::Ror, {{0, pick()}}); break;
      case 17: g.node(rng.below(2) != 0 ? Ntype_op::Div : Ntype_op::Rem, {{0, pick()}, {1, g.konst(static_cast<int64_t>(rng.below(20)) - 2)}}); break;
      case 19: g.node(Ntype_op::Popcount, {{0, pick()}, {1, g.konst(1 + static_cast<int64_t>(rng.below(40)))}}); break;
      case 20:
      case 21: {  // a variable, non-negative shift amount
        const auto amount = g.node(Ntype_op::Get_mask, {{0, pick()}, {2, g.konst(0)}, {3, g.konst(1 + static_cast<int64_t>(rng.below(6)))}});
        g.node(rng.below(2) != 0 ? Ntype_op::SHL : Ntype_op::SRA, {{0, pick()}, {1, amount}});
        break;
      }
      case 22: {  // a union class: two forms of the same value, so the facts of both sides are met
        const auto x = pick();
        switch (rng.below(5)) {
          case 0: g.alt(x, g.node(Ntype_op::Sum, {{0, x}, {0, g.konst(0)}})); break;
          case 1: g.alt(x, g.node(Ntype_op::Not, {{0, g.node(Ntype_op::Not, {{0, x}})}})); break;
          case 2: g.alt(g.node(Ntype_op::Get_mask, {{0, x}, {2, g.konst(0)}, {3, g.konst(8)}}), g.node(Ntype_op::And, {{0, x}, {0, g.konst(255)}})); break;
          case 3: g.alt(g.node(Ntype_op::SHL, {{0, x}, {1, g.konst(1)}}), g.node(Ntype_op::Mult, {{0, x}, {0, g.konst(2)}})); break;
          default: g.alt(g.node(Ntype_op::And, {{0, x}, {0, x}}), g.node(Ntype_op::Or, {{0, x}, {0, x}})); break;
        }
        break;
      }
      default: {
        // The packed-word shapes the lane layout reads: Or / Sum of a low field and a shifted high one.
        const auto k   = 1 + static_cast<int64_t>(rng.below(20));
        const auto low = g.node(Ntype_op::Get_mask, {{0, pick()}, {2, g.konst(0)}, {3, g.konst(k)}});
        const auto high = g.node(Ntype_op::SHL, {{0, pick()}, {1, g.konst(k)}});
        g.node(rng.below(2) != 0 ? Ntype_op::Or : Ntype_op::Sum, {{0, low}, {0, high}});
        break;
      }
    }
  }
}

// The concrete value of every class under one leaf assignment.
std::vector<std::optional<Dlop>> evaluate(const Random_graph& g, Rng& rng) {
  std::vector<std::optional<Dlop>> value(g.specs.size());
  for (const auto c : g.classes) {
    const auto& spec = g.specs[c];
    if (spec.alt_a != kNoClass) {
      value[c] = value[spec.alt_a];  // both sides are the same value by construction
      continue;
    }
    if (spec.op == Ntype_op::Invalid) {
      if (const auto* k = g.eg.value_of(c); k != nullptr) {
        value[c] = *k;
      } else if (const auto d = std::ranges::find(g.declared, c, &Random_graph::Declared::cls); d != g.declared.end()) {
        // inside the declared range, biased to its edges
        const auto m = (int64_t{1} << d->bits) - 1;
        auto       v = static_cast<int64_t>(rng.next()) & m;
        if (rng.below(4) == 0) {
          v = rng.below(2) != 0 ? m : 0;
        }
        value[c] = D(d->is_unsigned ? v : (v << (64 - d->bits)) >> (64 - d->bits));  // sign-extend from `bits`
      } else {
        value[c] = sample_value(rng);
      }
      continue;
    }
    std::vector<Eval_arg> args;
    bool                  ok     = true;
    const bool            banked = Ntype::is_banked_sink_op(spec.op);
    for (size_t i = 0; i < spec.ops.size(); ++i) {
      const auto& v = value[spec.ops[i].cls];
      if (!v) {
        ok = false;
        break;
      }
      args.push_back({banked ? static_cast<hhds::Port_id>(i) : static_cast<hhds::Port_id>(spec.ops[i].bank), spec.ops[i].bank, *v});
    }
    if (!ok) {
      continue;
    }
    // Concat asserts that a lane fits its window; the generator wraps lanes in Get_mask, so they do.
    const auto r = eval_op(spec.op, args);
    if (r && !r->has_unknowns() && r->is_integer()) {
      value[c] = *r;
    }
  }
  return value;
}

}  // namespace

std::string analyses_sound(const Analyses& analyses, uint64_t seed, size_t graphs, size_t trials, size_t* checked) {
  auto&  registry = rt::Analysis_registry::instance();
  size_t count    = 0;
  std::map<std::string, size_t> per;
  // The declaration and the runtime agree: every declared analysis is implemented with the same arity.
  for (const auto& an : analyses.list) {
    const auto* info = registry.find(an.name);
    if (info == nullptr) {
      return "analysis `" + an.name + "` is declared in analyses.decl but has no AEG_ANALYSIS";
    }
    size_t n_const = 0;
    for (const bool c : an.is_const) {
      n_const += c ? 1 : 0;
    }
    if (info->n_const != n_const || info->n_class != an.params.size() - n_const) {
      return "analysis `" + an.name + "` takes " + std::to_string(an.params.size() - n_const) + " classes and " + std::to_string(n_const)
             + " constants in analyses.decl but " + std::to_string(info->n_class) + " + " + std::to_string(info->n_const)
             + " in AEG_ANALYSIS";
    }
  }
  for (const auto& info : registry.all()) {
    if (analyses.find(info.name) == nullptr) {
      return "AEG_ANALYSIS `" + std::string{info.name} + "` has no declaration in analyses.decl";
    }
  }

  Rng rng(seed);
  Stub_builder stub;
  for (size_t gi = 0; gi < graphs; ++gi) {
    Random_graph g;
    random_graph(rng, g);
    rt::Ctx ctx(g.eg, stub, 1U << 20);
    for (size_t trial = 0; trial < trials; ++trial) {
      const auto value = evaluate(g, rng);
      std::vector<Class_id> with_value;
      for (const auto c : g.classes) {
        if (value[c]) {
          with_value.push_back(c);
        }
      }
      if (with_value.empty()) {
        continue;
      }
      const auto fail = [&](const std::string& what) {
        return what + "\n  seed " + std::to_string(seed) + " graph " + std::to_string(gi) + " trial " + std::to_string(trial) + "\n"
               + g.dump();
      };
      // ---- the predicates ----
      for (const auto& info : registry.all()) {
        const auto* an = analyses.find(info.name);
        for (const auto c : with_value) {
          for (int combo = 0; combo < 12; ++combo) {
            std::vector<Class_id> cls{c};
            if (info.n_class == 2) {
              cls.push_back(with_value[rng.below(with_value.size())]);
            }
            std::vector<int64_t> consts;
            const auto           v = to_num(*value[c]);
            for (size_t i = 0; i < info.n_const; ++i) {
              switch (rng.below(5)) {
                case 0: consts.push_back(static_cast<int64_t>(rng.below(70))); break;
                case 1: consts.push_back(v ? v->v + static_cast<int64_t>(rng.below(5)) - 2 : 0); break;
                case 2: consts.push_back(static_cast<int64_t>(rng.below(1 << 16))); break;
                case 3: consts.push_back((int64_t{1} << rng.below(40)) - 1); break;
                default: consts.push_back(static_cast<int64_t>(rng.below(16)) - 3); break;
              }
            }
            if (!info.fn(ctx, cls, consts)) {
              continue;
            }
            // The runtime claims the fact; the spec must hold on the concrete values.
            Env local;
            bool decidable = true;
            size_t ci = 0, ki = 0;
            for (size_t i = 0; i < an->params.size(); ++i) {
              if (an->is_const[i]) {
                local.num[an->params[i]] = rt::ex::num(consts[ki++]);
              } else {
                const auto n = to_num(*value[cls[ci++]]);
                if (!n) {
                  decidable = false;
                  break;
                }
                local.num[an->params[i]] = *n;
              }
            }
            if (!decidable) {
              continue;
            }
            const auto verdict = eval_expr(*an->spec, local, Analyses{});
            if (!verdict.ok) {
              continue;  // the spec itself is undefined here (a mask above 62 bits)
            }
            ++count;
            ++per[info.name];
            if (!rt::ex::holds(verdict)) {
              std::string args;
              for (const auto cl : cls) {
                args += " c" + std::to_string(cl) + "=" + value[cl]->to_pyrope();
              }
              for (const auto k : consts) {
                args += " k=" + std::to_string(k);
              }
              return fail("UNSOUND analysis `" + std::string{info.name} + "`: the runtime says it holds but the spec " + expr_text(*an->spec)
                          + " is false for" + args);
            }
          }
        }
      }
      // ---- the lane layout: a lane's source window equals the class's window ----
      for (const auto c : with_value) {
        const auto layout = rt::layout_of(g.eg, c);
        for (int w = 0; w < 6; ++w) {
          const int64_t lo = static_cast<int64_t>(rng.below(60));
          const int64_t hi = lo + 1 + static_cast<int64_t>(rng.below(24));
          const auto*   lane = rt::find_lane(*layout, lo, hi);
          if (lane == nullptr) {
            continue;
          }
          const auto want = livehd::eval_get_mask(*value[c], static_cast<int>(lo), static_cast<int>(hi));
          Dlop got;
          if (lane->zero) {
            got = D(0);
          } else {
            if (!value[lane->src]) {
              continue;
            }
            const auto s = lane->off + (lo - lane->lo);
            got          = livehd::eval_get_mask(*value[lane->src], static_cast<int>(s), static_cast<int>(s + (hi - lo)));
          }
          ++count;
          ++per["(lane layout)"];
          if (!want.eq_op(got)->is_known_true()) {
            return fail("UNSOUND lane layout of c" + std::to_string(c) + " window [" + std::to_string(lo) + "," + std::to_string(hi)
                        + "): the class reads " + want.to_pyrope() + " but its lane (src c" + std::to_string(lane->src) + " off "
                        + std::to_string(lane->off) + (lane->zero ? " zero" : "") + ") reads " + got.to_pyrope());
          }
        }
      }
      // ---- (base, polarity) ----
      for (const auto c : with_value) {
        const auto pol = rt::polarity_of(g.eg, c);
        if (!pol.valid || !value[pol.base]) {
          continue;
        }
        const auto want = pol.inverted ? D(1) : D(0);
        Dlop       expect;
        if (pol.inverted) {
          expect = D(1).sub_op(*value[pol.base]);
        } else {
          expect = *value[pol.base];
        }
        (void)want;
        ++count;
        ++per["(polarity)"];
        if (!value[c]->eq_op(expect)->is_known_true()) {
          return fail("UNSOUND polarity of c" + std::to_string(c) + ": base c" + std::to_string(pol.base) + (pol.inverted ? " inverted" : "")
                      + " but the values are " + value[c]->to_pyrope() + " and " + value[pol.base]->to_pyrope());
        }
      }
    }
  }
  if (checked != nullptr) {
    *checked = count;
  }
  if (livehd::debug::env<"LHD_AEG_ANALYSES_VERBOSE">() != nullptr) {
    for (const auto& [name, n] : per) {
      std::printf("[ analyses ]   %-14s %zu positive claims\n", name.c_str(), n);
    }
  }
  return {};
}

}  // namespace livehd::aegraph::rulegen::testing
