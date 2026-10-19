//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cost_model.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

#include "sexp.hpp"

namespace livehd::aegraph::cost {

namespace ex = livehd::aegraph::rt::ex;
using livehd::aegraph::rulegen::parse_sexps;
using livehd::aegraph::rulegen::Sexp;
using livehd::aegraph::rulegen::Syntax_error;
using livehd::aegraph::rulegen::to_text;

namespace {

constexpr size_t kMaxBinds = 8;

std::string lower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool parse_int(const std::string& t, int64_t& v) {
  if (t.empty()) {
    return false;
  }
  size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
  if (i >= t.size()) {
    return false;
  }
  for (size_t j = i; j < t.size(); ++j) {
    if (std::isdigit(static_cast<unsigned char>(t[j])) == 0) {
      return false;
    }
  }
  char* end = nullptr;
  v         = std::strtoll(t.c_str(), &end, 10);
  return end == t.c_str() + t.size();
}

class Parser {
public:
  Parser(std::string path, Diags& diags) : path_(std::move(path)), diags_(diags) {}

  void err(int line, const std::string& msg) { diags_.push_back(path_ + ":" + std::to_string(line) + ": error: " + msg); }

  Code expr(const Sexp& s, const std::vector<std::string>& binds) {
    Code c;
    c.line = s.line;
    if (s.kind == Sexp::Kind::str) {
      err(s.line, "a string is not an expression");
      return c;
    }
    if (s.is_atom()) {
      int64_t v = 0;
      if (parse_int(s.text, v)) {
        c.kind = Code::Kind::num;
        c.num  = v;
      } else if (s.text == "W") {
        c.kind = Code::Kind::W;
      } else if (s.text == "A") {
        c.kind = Code::Kind::A;
      } else if (s.text == "Wmax") {
        c.kind = Code::Kind::Wmax;
      } else if (s.text == "Wsum") {
        c.kind = Code::Kind::Wsum;
      } else if (s.text == "N") {
        c.kind = Code::Kind::N;
      } else if (s.text == "Nmax") {
        c.kind = Code::Kind::Nmax;
      } else if (const auto it = std::ranges::find(binds, s.text); it != binds.end()) {
        c.kind = Code::Kind::bound;
        c.num  = it - binds.begin();
      } else {
        err(s.line, "unbound name `" + s.text + "` (W, A, Wmax, Wsum, N, Nmax or a (k name) binder)");
      }
      return c;
    }
    const auto head = s.head();
    const auto* fn  = ex::find_builtin(head);
    if (fn == nullptr) {
      err(s.line, "unknown builtin `" + std::string{head} + "`");
      return c;
    }
    const auto n = static_cast<int>(s.items.size()) - 1;
    if (n < fn->min_args || (fn->max_args >= 0 && n > fn->max_args)) {
      err(s.line, "builtin `" + std::string{head} + "` takes " + std::to_string(fn->min_args) + (fn->max_args < 0 ? "+" : "")
                      + " argument(s), got " + std::to_string(n));
      return c;
    }
    c.kind = Code::Kind::call;
    c.fn   = fn;
    for (size_t i = 1; i < s.items.size(); ++i) {
      c.args.push_back(expr(s.items[i], binds));
    }
    return c;
  }

  void entry(const Sexp& s, Profile& p) {
    // (cost NAME PATTERN [:when GUARD...] PRICE [:doc "..."])
    if (s.items.size() < 4) {
      err(s.line, "(cost NAME PATTERN PRICE) takes a profile name, a pattern and a price");
      return;
    }
    if (!s.items[1].is_atom() || s.items[1].text != p.name) {
      const std::string named = s.items[1].is_atom() ? s.items[1].text : std::string{"?"};
      err(s.line, "entry names profile `" + named + "`, this file is `" + p.name + "`");
      return;
    }
    Entry e;
    e.line = s.line;
    const auto& pat = s.items[2];
    if (pat.is_atom("_")) {
      e.any_op = true;
    } else if (pat.is_atom() || pat.is_list()) {
      const Sexp* head = pat.is_atom() ? &pat : (pat.items.empty() ? nullptr : &pat.items.front());
      if (head == nullptr || !head->is_atom()) {
        err(pat.line, "a pattern is an op name or (OP item...)");
        return;
      }
      if (head->is_atom("_")) {
        e.any_op = true;
      } else {
        e.op = Ntype::get_op(lower(head->text));
        if (e.op == Ntype_op::Invalid) {
          err(pat.line, "unknown op `" + head->text + "`");
          return;
        }
      }
      if (pat.is_list()) {
        e.has_items = true;
        for (size_t i = 1; i < pat.items.size(); ++i) {
          const auto& it = pat.items[i];
          Item item;
          if (it.is_atom("_")) {
            item.kind = Item::Kind::any;
          } else if (it.is_atom("..")) {
            item.kind = Item::Kind::rest;
            if (i + 1 != pat.items.size()) {
              err(it.line, "`..` must be the last item");
            }
            e.has_rest = true;
          } else if (it.is_list() && it.head() == "k" && it.items.size() <= 2) {
            item.kind = Item::Kind::konst;
            if (it.items.size() == 2) {
              if (!it.items[1].is_atom()) {
                err(it.line, "(k name) takes a name");
                continue;
              }
              item.name = it.items[1].text;
              if (item.name != "_") {
                if (std::ranges::find(e.binds, item.name) != e.binds.end()) {
                  err(it.line, "`" + item.name + "` is bound twice");
                }
                item.bind = static_cast<int>(e.binds.size());
                e.binds.push_back(item.name);
              }
            }
          } else {
            err(it.line, "a pattern item is `_`, (k), (k name) or `..`");
            continue;
          }
          if (item.kind != Item::Kind::rest) {
            e.items.push_back(std::move(item));
          }
        }
        if (e.binds.size() > kMaxBinds) {
          err(pat.line, "at most " + std::to_string(kMaxBinds) + " (k name) binders");
        }
      }
    } else {
      err(pat.line, "a pattern is an op name, `_` or (OP item...)");
      return;
    }

    // The rest: strip :doc, split off :when guards, the last form is the price.
    std::vector<const Sexp*> rest;
    for (size_t i = 3; i < s.items.size(); ++i) {
      if (s.items[i].is_atom(":doc")) {
        if (i + 1 < s.items.size() && s.items[i + 1].kind == Sexp::Kind::str) {
          e.doc = s.items[i + 1].text;
          ++i;
        } else {
          err(s.items[i].line, ":doc takes a string");
        }
        continue;
      }
      rest.push_back(&s.items[i]);
    }
    if (rest.empty()) {
      err(s.line, "entry has no price");
      return;
    }
    size_t first_guard = rest.size();
    if (rest.front()->is_atom(":when")) {
      first_guard = 1;
    }
    if (first_guard < rest.size() && rest.size() < 3) {
      err(s.line, ":when needs a guard and a price");
      return;
    }
    for (size_t i = first_guard; first_guard < rest.size() && i + 1 < rest.size(); ++i) {
      e.whens.push_back(expr(*rest[i], e.binds));
    }
    if (first_guard == rest.size() && rest.size() != 1) {
      err(s.line, "expected one price expression (use :when for guards)");
      return;
    }
    e.price = expr(*rest.back(), e.binds);
    p.entries.push_back(std::move(e));
  }

private:
  std::string path_;
  Diags&      diags_;
};

struct Env {
  int64_t                     W;
  int64_t                     A;
  int64_t                     Wmax;
  int64_t                     Wsum;
  int64_t                     N;
  int64_t                     Nmax;
  std::array<int64_t, kMaxBinds> bound;
};

ex::Num eval(const Code& c, const Env& env) {
  switch (c.kind) {
    case Code::Kind::num  : return ex::num(c.num);
    case Code::Kind::W    : return ex::num(env.W);
    case Code::Kind::A    : return ex::num(env.A);
    case Code::Kind::Wmax : return ex::num(env.Wmax);
    case Code::Kind::Wsum : return ex::num(env.Wsum);
    case Code::Kind::N    : return ex::num(env.N);
    case Code::Kind::Nmax : return ex::num(env.Nmax);
    case Code::Kind::bound: return ex::num(env.bound[static_cast<size_t>(c.num)]);
    case Code::Kind::call : break;
  }
  std::array<ex::Num, 8> vals{};
  const auto             n = std::min<size_t>(c.args.size(), vals.size());
  if (c.args.size() > vals.size()) {
    // n-ary + * min max over more than 8 arguments: fold in order
    ex::Num acc = eval(c.args[0], env);
    for (size_t i = 1; i < c.args.size(); ++i) {
      const std::array<ex::Num, 2> two{acc, eval(c.args[i], env)};
      acc = ex::call(c.fn->name, two);
    }
    return acc;
  }
  for (size_t i = 0; i < n; ++i) {
    vals[i] = eval(c.args[i], env);
    if (!vals[i].ok) {
      return {};
    }
  }
  return ex::call(c.fn->name, std::span<const ex::Num>(vals.data(), n));
}

bool matches(const Entry& e, Ntype_op op, std::span<const Operand> ops, Env& env) {
  if (!e.any_op && e.op != op) {
    return false;
  }
  if (e.has_items) {
    if (Ntype::is_banked_sink_op(op)) {
      std::array<bool, 64> used{};
      size_t               n_any = 0;
      size_t               taken = 0;
      for (const auto& item : e.items) {
        if (item.kind == Item::Kind::any) {
          ++n_any;
          continue;
        }
        bool found = false;
        for (size_t i = 0; i < ops.size() && i < used.size(); ++i) {
          if (!used[i] && ops[i].is_const) {
            used[i] = true;
            found   = true;
            ++taken;
            if (item.bind >= 0) {
              env.bound[static_cast<size_t>(item.bind)] = ops[i].value;
            }
            break;
          }
        }
        if (!found) {
          return false;
        }
      }
      const size_t free_ops = ops.size() - taken;
      if (free_ops < n_any || (!e.has_rest && free_ops != n_any)) {
        return false;
      }
    } else {
      if (ops.size() < e.items.size() || (!e.has_rest && ops.size() != e.items.size())) {
        return false;
      }
      for (size_t i = 0; i < e.items.size(); ++i) {
        if (e.items[i].kind != Item::Kind::konst) {
          continue;
        }
        if (!ops[i].is_const) {
          return false;
        }
        if (e.items[i].bind >= 0) {
          env.bound[static_cast<size_t>(e.items[i].bind)] = ops[i].value;
        }
      }
    }
  }
  for (const auto& w : e.whens) {
    if (!ex::holds(eval(w, env))) {
      return false;
    }
  }
  return true;
}

Env make_env(int64_t width, std::span<const Operand> ops) {
  Env env{};
  env.W = width;
  env.A = static_cast<int64_t>(ops.size());
  for (const auto& o : ops) {
    env.Wmax = std::max(env.Wmax, o.width);
    env.Wsum += o.width;
  }
  env.N    = std::max<int64_t>(1, (width + 63) / 64);
  env.Nmax = std::max<int64_t>(1, (env.Wmax + 63) / 64);
  return env;
}

}  // namespace

Profile parse_profile(std::string_view text, std::string_view path, Diags& diags) {
  Profile p;
  p.path = std::string{path};
  Parser parser(std::string{path}, diags);
  try {
    const auto top = parse_sexps(text, path);
    // The profile header first: the entries name it.
    for (const auto& s : top) {
      if (s.head() != "profile") {
        continue;
      }
      if (!p.name.empty()) {
        parser.err(s.line, "a second (profile ...) form");
        continue;
      }
      p.line = s.line;
      if (s.items.size() < 2 || !s.items[1].is_atom()) {
        parser.err(s.line, "(profile NAME ...) takes a name");
        continue;
      }
      p.name = s.items[1].text;
      for (size_t i = 2; i < s.items.size(); ++i) {
        if (s.items[i].is_atom(":default-width") && i + 1 < s.items.size()) {
          int64_t v = 0;
          if (!s.items[i + 1].is_atom() || !parse_int(s.items[i + 1].text, v) || v < 1 || v > 4096) {
            parser.err(s.items[i].line, ":default-width takes an integer in 1..4096");
          } else {
            p.default_width = v;
          }
          ++i;
        } else if (s.items[i].is_atom(":doc") && i + 1 < s.items.size() && s.items[i + 1].kind == Sexp::Kind::str) {
          p.doc = s.items[i + 1].text;
          ++i;
        } else {
          parser.err(s.items[i].line, "unknown option in (profile ...)");
        }
      }
    }
    if (p.name.empty()) {
      parser.err(1, "no (profile NAME) form");
      return p;
    }
    for (const auto& s : top) {
      if (s.head() == "cost") {
        parser.entry(s, p);
      } else if (s.head() != "profile") {
        parser.err(s.line, "expected (profile ...) or (cost ...), got " + to_text(s));
      }
    }
  } catch (const Syntax_error& e) {
    diags.push_back(e.message);
  }
  return p;
}

void lint_profile(const Profile& p, Diags& diags) {
  const auto err = [&](int line, const std::string& msg) {
    diags.push_back(p.path + ":" + std::to_string(line) + ": error: " + msg);
  };
  if (p.entries.empty()) {
    err(p.line, "profile `" + p.name + "` has no (cost ...) entries");
    return;
  }
  const auto& last = p.entries.back();
  if (!last.any_op || last.has_items || !last.whens.empty()) {
    err(last.line, "the last entry of a profile is the catch-all `(cost " + p.name + " _ PRICE)`: every op needs a price");
  }
  // An unguarded entry with no item constraint shadows every later entry of the same op.
  for (size_t i = 0; i < p.entries.size(); ++i) {
    const auto& a = p.entries[i];
    if (a.has_items || !a.whens.empty()) {
      continue;
    }
    for (size_t j = i + 1; j < p.entries.size(); ++j) {
      const auto& b = p.entries[j];
      if (a.any_op || (!b.any_op && b.op == a.op)) {
        diags.push_back(p.path + ":" + std::to_string(b.line) + ": error: entry is unreachable, line " + std::to_string(a.line)
                        + " matches every node it could");
      }
    }
  }
  // The catch-all must have a value for a representative node.
  const std::array<Operand, 2> sample{Operand{0, 8, false, 0}, Operand{1, 8, false, 0}};
  if (last.any_op && priced_by(p, Ntype_op::Sum, 8, sample) != last.line && priced_by(p, Ntype_op::Sum, 8, sample) == 0) {
    err(last.line, "the catch-all has no value for a plain node");
  }
}

int64_t price(const Profile& p, Ntype_op op, int64_t width, std::span<const Operand> ops) {
  Env env = make_env(width, ops);
  for (const auto& e : p.entries) {
    Env local = env;
    if (!matches(e, op, ops, local)) {
      continue;
    }
    const auto r = eval(e.price, local);
    if (!r.ok) {
      continue;
    }
    return std::clamp<int64_t>(r.v, 0, kMaxPrice);
  }
  return 0;
}

int priced_by(const Profile& p, Ntype_op op, int64_t width, std::span<const Operand> ops) {
  Env env = make_env(width, ops);
  for (const auto& e : p.entries) {
    Env local = env;
    if (matches(e, op, ops, local) && eval(e.price, local).ok) {
      return e.line;
    }
  }
  return 0;
}

}  // namespace livehd::aegraph::cost
