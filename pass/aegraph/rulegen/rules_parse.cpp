//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>

#include "cost_model.hpp"
#include "rule_expr.hpp"
#include "rules.hpp"
#include "sexp.hpp"

namespace livehd::aegraph::rulegen {

namespace {

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
      // 0x hex
      if (j == i + 1 && t[i] == '0' && (t[j] == 'x' || t[j] == 'X') && j + 1 < t.size()) {
        char* end = nullptr;
        v         = std::strtoll(t.c_str(), &end, 16);
        return end == t.c_str() + t.size();
      }
      return false;
    }
  }
  char* end = nullptr;
  v         = std::strtoll(t.c_str(), &end, 10);
  return end == t.c_str() + t.size();
}

bool is_ident(std::string_view s) {
  if (s.empty() || (std::isalpha(static_cast<unsigned char>(s[0])) == 0 && s[0] != '_')) {
    return false;
  }
  return std::ranges::all_of(s, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
}

bool is_rule_name(std::string_view s) {
  if (s.empty() || std::islower(static_cast<unsigned char>(s[0])) == 0) {
    return false;
  }
  return std::ranges::all_of(s, [](unsigned char c) { return std::islower(c) != 0 || std::isdigit(c) != 0 || c == '_'; });
}

}  // namespace

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

const Analysis* Analyses::find(std::string_view name) const {
  for (const auto& a : list) {
    if (a.name == name) {
      return &a;
    }
  }
  return nullptr;
}

bool is_rule_op(Ntype_op op) {
  switch (op) {
    case Ntype_op::Sum:
    case Ntype_op::Mult:
    case Ntype_op::Div:
    case Ntype_op::Rem:
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Ror:
    case Ntype_op::Rxor:
    case Ntype_op::Popcount:
    case Ntype_op::Not:
    case Ntype_op::Get_mask:
    case Ntype_op::Set_mask:
    case Ntype_op::Sext:
    case Ntype_op::Concat:
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::EQ:
    case Ntype_op::SHL:
    case Ntype_op::SRA:
    case Ntype_op::Mux:
    case Ntype_op::Hotmux: return true;
    default: return false;
  }
}

std::string op_spelling(Ntype_op op) {
  std::string name(Ntype::get_name(op));
  if (name == "shl" || name == "sra" || name == "eq" || name == "lt" || name == "gt") {
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return name;
  }
  if (!name.empty()) {
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  }
  return name;
}

Op_shape shape_of(Ntype_op op) {
  Op_shape s;
  s.banked     = Ntype::is_banked_sink_op(op);
  s.bank_count = Ntype::sink_bank_count(op);
  s.unlimited  = Ntype::is_unlimited_sink(op);
  if (!s.banked && !s.unlimited) {
    std::vector<std::pair<uint32_t, std::string>> pins;
    for (const auto& name : Ntype::sink_names(op)) {
      pins.emplace_back(Ntype::get_sink_pid(op, name), name);
    }
    std::ranges::sort(pins);
    for (auto& [pid, name] : pins) {
      s.pids.push_back(pid);
      s.names.push_back(std::move(name));
    }
  }
  return s;
}

std::string expr_text(const Expr& e) {
  switch (e.kind) {
    case Expr::Kind::num: return std::to_string(e.num);
    case Expr::Kind::var: return e.name;
    default: {
      std::string t = "(" + e.name;
      for (const auto& a : e.args) {
        t += " " + expr_text(a);
      }
      return t + ")";
    }
  }
}

std::string pat_text(const Pat& p) {
  switch (p.kind) {
    case Pat::Kind::wild: return "_";
    case Pat::Kind::var: return p.name;
    case Pat::Kind::num: return std::to_string(p.num);
    case Pat::Kind::lit: return p.name;
    case Pat::Kind::konst: return "(k " + (p.expr ? expr_text(*p.expr) : (p.name.empty() ? std::string{"_"} : p.name)) + ")";
    default: break;
  }
  std::string t = "(" + op_spelling(p.op);
  if (!p.attrs.empty()) {
    t += "{";
    for (size_t i = 0; i < p.attrs.size(); ++i) {
      t += (i != 0 ? "," : "") + p.attrs[i];
    }
    t += "}";
  }
  if (p.banked) {
    for (const auto& b : p.banks) {
      t += b.id == 0 ? " (as" : " (bs";
      for (const auto& e : b.elems) {
        t += " " + pat_text(e);
      }
      if (b.has_rest) {
        t += " .." + b.rest;
      }
      t += ")";
    }
  } else {
    for (const auto& a : p.args) {
      t += " " + pat_text(a);
    }
  }
  return t + ")";
}

int op_count(const Pat& p) {
  if (p.kind != Pat::Kind::op) {
    return 0;
  }
  int n = 1;
  for (const auto& a : p.args) {
    n += op_count(a);
  }
  for (const auto& b : p.banks) {
    for (const auto& e : b.elems) {
      n += op_count(e);
    }
  }
  return n;
}

namespace {
// Variables renamed by first appearance: two left-hand sides that differ only in names compare equal.
void rename_vars(Pat& p, std::map<std::string, std::string>& names) {
  const auto fresh = [&](const std::string& n) {
    const auto it = names.find(n);
    if (it != names.end()) {
      return it->second;
    }
    return names.emplace(n, "v" + std::to_string(names.size())).first->second;
  };
  if (p.kind == Pat::Kind::var || p.kind == Pat::Kind::konst) {
    if (!p.name.empty() && p.name != "_") {
      p.name = fresh(p.name);
      p.expr.reset();
    }
  } else if (p.kind == Pat::Kind::lit) {
    p.name = "?" + fresh(p.name);
  }
  for (auto& a : p.args) {
    rename_vars(a, names);
  }
  for (auto& b : p.banks) {
    for (auto& e : b.elems) {
      rename_vars(e, names);
    }
    if (b.has_rest) {
      b.rest = fresh(b.rest);
    }
  }
}
}  // namespace

std::string canonical_text(const Pat& p) {
  Pat                                copy = p;
  std::map<std::string, std::string> names;
  rename_vars(copy, names);
  return pat_text(copy);
}

std::string proof_tier(const Rule& r) { return r.proof == "none" ? "none" : "bv-swept"; }

std::string rule_text(const Rule& r) {
  std::string t = "(" + std::string{r.tag == Rule::Tag::simplify ? "simplify" : (r.tag == Rule::Tag::alt ? "alt" : "choice")} + " "
                  + r.name + " " + std::to_string(r.prio) + " " + pat_text(r.lhs);
  if (!r.lets.empty()) {
    t += " :let";
    for (const auto& [n, e] : r.lets) {
      t += " (" + n + " " + expr_text(e) + ")";
    }
  }
  if (!r.whens.empty()) {
    t += " :when";
    for (const auto& w : r.whens) {
      t += " " + expr_text(w);
    }
  }
  t += " =>";
  for (size_t i = 0; i < r.arms.size(); ++i) {
    t += (i != 0 ? " |" : "") + std::string{" "} + pat_text(r.arms[i].rhs);
    if (r.arms[i].has_assign) {
      const auto& arm = r.arms[i];
      t += " [" + arm.lit + " := " + (arm.nonzero ? "NONZERO" : (arm.value ? std::to_string(*arm.value) : arm.assign_text)) + "]";
    }
  }
  return t + ")";
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

class Parser {
public:
  Parser(std::string path, Diags& diags) : path_(std::move(path)), diags_(diags) {}

  void err(int line, const std::string& msg) {
    auto d = path_ + ":" + std::to_string(line) + ": error: " + msg;
    if (std::find(diags_.begin(), diags_.end(), d) == diags_.end()) {  // a pattern and its :example may repeat one complaint
      diags_.push_back(std::move(d));
    }
  }

  Expr expr(const Sexp& s) {
    Expr e;
    e.line = s.line;
    if (s.is_atom()) {
      if (parse_int(s.text, e.num)) {
        e.kind = Expr::Kind::num;
      } else {
        e.kind = Expr::Kind::var;
        e.name = s.text;
      }
      return e;
    }
    if (s.is_list() && !s.head().empty()) {
      e.kind = Expr::Kind::call;
      e.name = std::string{s.head()};
      for (size_t i = 1; i < s.items.size(); ++i) {
        e.args.push_back(expr(s.items[i]));
      }
      return e;
    }
    err(s.line, "expected an expression, got " + to_text(s));
    return e;
  }

  Pat pat(const Sexp& s) {
    Pat p;
    p.line = s.line;
    if (s.kind == Sexp::Kind::str) {
      err(s.line, "a string is not a pattern or term");
      return p;
    }
    if (s.is_atom()) {
      const auto& t = s.text;
      if (!s.attrs.empty()) {
        err(s.line, "{attr} only attaches to an operator");
      }
      int64_t v = 0;
      if (t == "_") {
        p.kind = Pat::Kind::wild;
      } else if (parse_int(t, v)) {
        p.kind = Pat::Kind::num;
        p.num  = v;
      } else if (t.starts_with("..")) {
        err(s.line, "`" + t + "` is only valid inside an (as ...) or (bs ...) bank");
      } else if (t[0] == '?') {
        p.kind = Pat::Kind::lit;
        p.name = t;
      } else {
        p.kind = Pat::Kind::var;
        p.name = t;
      }
      return p;
    }
    const auto head = s.head();
    if (head.empty()) {
      err(s.line, "a pattern list must start with an operator name: " + to_text(s));
      return p;
    }
    if (head == "k") {
      p.kind = Pat::Kind::konst;
      if (s.items.size() != 2) {
        err(s.line, "(k ...) takes exactly one argument");
        return p;
      }
      if (s.items[1].is_atom()) {
        p.name = s.items[1].text;
      }
      p.expr = std::make_shared<const Expr>(expr(s.items[1]));
      return p;
    }
    p.kind    = Pat::Kind::op;
    p.op_text = std::string{head};
    p.op      = Ntype::get_op(lower(head));
    p.attrs   = s.items[0].attrs;
    if (p.op == Ntype_op::Invalid) {
      err(s.line, "unknown op `" + std::string{head} + "`");
      return p;
    }
    if (!is_rule_op(p.op)) {
      err(s.line, "op `" + std::string{head} + "` is not a pure e-graph op a rule can name");
      p.op = Ntype_op::Invalid;
      return p;
    }
    operands(s, p);
    return p;
  }

  void operands(const Sexp& s, Pat& p) {
    const auto shape = shape_of(p.op);
    bool       any_bank = false, any_named = false;
    for (size_t i = 1; i < s.items.size(); ++i) {
      const auto& it = s.items[i];
      any_bank = any_bank || (it.is_list() && (it.head() == "as" || it.head() == "bs"));
      any_named = any_named || (it.is_atom() && it.text.size() > 1 && it.text.back() == ':');
    }
    if (any_bank && any_named) {
      err(s.line, "an operator cannot mix bank and named operands");
      return;
    }
    if (any_bank) {
      p.banked = true;
      if (!shape.banked) {
        err(s.line, "`" + op_spelling(p.op) + "` has no operand banks: (as ...)/(bs ...) do not apply");
        return;
      }
      std::set<uint32_t> seen;
      for (size_t i = 1; i < s.items.size(); ++i) {
        const auto& it = s.items[i];
        if (!it.is_list() || (it.head() != "as" && it.head() != "bs")) {
          err(it.line, "banked operands are all (as ...) / (bs ...) lists");
          continue;
        }
        Bank b;
        b.id   = it.head() == "as" ? 0 : 1;
        b.line = it.line;
        if (b.id >= static_cast<uint32_t>(shape.bank_count)) {
          err(it.line, "`" + op_spelling(p.op) + "` has no (bs ...) bank");
          continue;
        }
        if (!seen.insert(b.id).second) {
          err(it.line, "bank written twice");
          continue;
        }
        for (size_t j = 1; j < it.items.size(); ++j) {
          const auto& e = it.items[j];
          if (e.is_atom() && e.text.starts_with("..")) {
            if (b.has_rest) {
              err(e.line, "a bank has at most one ..rest");
            }
            b.has_rest = true;
            b.rest     = e.text.substr(2);
          } else {
            b.elems.push_back(pat(e));
          }
        }
        p.banks.push_back(std::move(b));
      }
      std::ranges::sort(p.banks, {}, &Bank::id);
      return;
    }
    if (shape.banked) {
      // Sugar: (EQ x 0), (Xor x -1), (Sum a b) are bank 0; (LT a b) is left/right.
      p.banked = true;
      if (any_named) {
        err(s.line, "name the banks of `" + op_spelling(p.op) + "` with (as ...)/(bs ...)");
        return;
      }
      const bool two_sided = p.op == Ntype_op::LT || p.op == Ntype_op::GT;
      if (two_sided && s.items.size() != 3) {
        err(s.line, "`" + op_spelling(p.op) + "` positional form takes exactly (left right)");
        return;
      }
      Bank b0;
      b0.id   = 0;
      b0.line = s.line;
      Bank b1;
      b1.id   = 1;
      b1.line = s.line;
      for (size_t i = 1; i < s.items.size(); ++i) {
        if (s.items[i].is_atom() && s.items[i].text.starts_with("..")) {
          err(s.items[i].line, "..rest needs an explicit (as ...) bank");
          continue;
        }
        (two_sided && i == 2 ? b1 : b0).elems.push_back(pat(s.items[i]));
      }
      p.banks.push_back(std::move(b0));
      if (two_sided) {
        p.banks.push_back(std::move(b1));
      }
      return;
    }
    // Fixed pins (or an unlimited sink list).
    if (any_named) {
      for (size_t i = 1; i < s.items.size(); i += 2) {
        const auto& key = s.items[i];
        if (!key.is_atom() || key.text.size() < 2 || key.text.back() != ':' || i + 1 >= s.items.size()) {
          err(key.line, "named operands are `pin: pattern` pairs");
          return;
        }
        const auto name = key.text.substr(0, key.text.size() - 1);
        if (!Ntype::is_sink_name(p.op, name)) {
          err(key.line, "unknown pin `" + name + "` on `" + op_spelling(p.op) + "`");
          continue;
        }
        p.pids.push_back(Ntype::get_sink_pid(p.op, name));
        p.args.push_back(pat(s.items[i + 1]));
      }
      return;
    }
    const size_t n = s.items.size() - 1;
    if (!shape.unlimited && n != shape.pids.size()) {
      err(s.line, "`" + op_spelling(p.op) + "` takes " + std::to_string(shape.pids.size()) + " operands, not " + std::to_string(n));
      return;
    }
    if (shape.unlimited && n == 0) {
      err(s.line, "`" + op_spelling(p.op) + "` needs at least one operand");
      return;
    }
    for (size_t i = 0; i < n; ++i) {
      if (s.items[i + 1].is_atom() && s.items[i + 1].text.starts_with("..")) {
        err(s.items[i + 1].line, "..rest is only valid inside a bank");
        continue;
      }
      p.pids.push_back(shape.unlimited ? static_cast<uint32_t>(i) : shape.pids[i]);
      p.args.push_back(pat(s.items[i + 1]));
    }
  }

  void rule(const Sexp& s, Rules_file& out) {
    if (!s.is_list() || s.head().empty()) {
      err(s.line, "expected (simplify|alt|choice NAME ...), got " + to_text(s));
      return;
    }
    Rule r;
    r.line = s.line;
    const auto head = s.head();
    if (head == "simplify") {
      r.tag = Rule::Tag::simplify;
    } else if (head == "alt") {
      r.tag = Rule::Tag::alt;
    } else if (head == "choice") {
      r.tag = Rule::Tag::choice;
    } else {
      err(s.line, "unknown form `" + std::string{head} + "` (simplify, alt or choice)");
      return;
    }
    size_t i = 1;
    if (i >= s.items.size() || !s.items[i].is_atom()) {
      err(s.line, "rule needs a NAME");
      return;
    }
    r.name = s.items[i++].text;
    int64_t prio = 0;
    if (i < s.items.size() && s.items[i].is_atom() && parse_int(s.items[i].text, prio)) {
      r.prio = static_cast<int>(prio);
      ++i;
    } else if (i < s.items.size() && s.items[i].is_atom()) {
      err(s.line, "rule `" + r.name + "` priority must be an integer, not `" + s.items[i].text + "`");
      return;
    }
    if (i >= s.items.size() || !s.items[i].is_list()) {
      err(s.line, "rule `" + r.name + "` needs a left-hand-side pattern");
      return;
    }
    r.lhs = pat(s.items[i++]);

    // Clauses: keyword (":let", ":when", "=>", ":example", ...) followed by its items.
    struct Clause {
      std::string               kw;
      int                       line = 0;
      std::vector<const Sexp*>  args;
    };
    std::vector<Clause> clauses;
    for (; i < s.items.size(); ++i) {
      const auto& it = s.items[i];
      if (it.is_atom() && (it.text == "=>" || (it.text.size() > 1 && it.text[0] == ':' && it.text != ":="))) {
        clauses.push_back({it.text, it.line, {}});
      } else if (clauses.empty()) {
        err(it.line, "unexpected `" + to_text(it) + "` before `=>`/option in rule `" + r.name + "`");
      } else {
        clauses.back().args.push_back(&it);
      }
    }
    bool arrow = false;
    for (const auto& c : clauses) {
      if (c.kw == ":let") {
        for (const auto* a : c.args) {
          if (!a->is_list() || a->items.size() != 2 || !a->items[0].is_atom()) {
            err(a->line, ":let takes (name EXPR) pairs");
            continue;
          }
          r.lets.emplace_back(a->items[0].text, expr(a->items[1]));
        }
      } else if (c.kw == ":when") {
        for (const auto* a : c.args) {
          r.whens.push_back(expr(*a));
        }
      } else if (c.kw == "=>") {
        arrow = true;
        arms(c.args, r, c.line);
      } else if (c.kw == ":example") {
        if (c.args.size() != 1) {
          err(c.line, ":example takes one term");
        } else {
          r.example = pat(*c.args[0]);
        }
      } else if (c.kw == ":expect") {
        for (const auto* a : c.args) {
          if (!a->is_list() || a->items.size() != 2 || !a->items[0].is_atom() || !a->items[1].is_atom()) {
            err(a->line, ":expect takes (PROFILE lhs|rhs) pairs");
            continue;
          }
          r.expects.push_back({a->items[0].text, a->items[1].text, a->line});
        }
      } else if (c.kw == ":fuel") {
        int64_t n = 0;
        if (c.args.size() != 1 || !c.args[0]->is_atom() || !parse_int(c.args[0]->text, n)) {
          err(c.line, ":fuel takes an integer");
        } else {
          r.fuel = n;
        }
      } else if (c.kw == ":proof") {
        if (c.args.size() != 1 || !c.args[0]->is_atom()) {
          err(c.line, ":proof takes bv|int|lean|none");
        } else {
          r.proof = c.args[0]->text;
        }
      } else if (c.kw == ":doc") {
        if (c.args.size() != 1 || c.args[0]->kind != Sexp::Kind::str) {
          err(c.line, ":doc takes a string");
        } else {
          r.doc = c.args[0]->text;
        }
      } else {
        err(c.line, "unknown option `" + c.kw + "` in rule `" + r.name + "`");
      }
    }
    if (!arrow) {
      err(s.line, "rule `" + r.name + "` has no `=>` right-hand side");
    }
    out.rules.push_back(std::move(r));
  }

  void arms(const std::vector<const Sexp*>& args, Rule& r, int line) {
    // Segments separated by `|`; each is a term followed by optional [?q := C] brackets.
    std::vector<std::vector<const Sexp*>> segments(1);
    for (const auto* a : args) {
      if (a->is_atom("|")) {
        segments.emplace_back();
      } else {
        segments.back().push_back(a);
      }
    }
    if (r.tag != Rule::Tag::choice && segments.size() != 1) {
      err(line, "only `choice` has several `|` arms");
    }
    for (const auto& seg : segments) {
      if (seg.empty() || (seg[0]->is_list() && seg[0]->open == '[')) {
        err(line, "empty right-hand side");
        continue;
      }
      Choice_arm arm;
      arm.line = seg[0]->line;
      arm.rhs  = pat(*seg[0]);
      for (size_t i = 1; i < seg.size(); ++i) {
        const auto& b = *seg[i];
        if (!b.is_list() || b.open != '[' || b.items.size() != 3 || !b.items[0].is_atom() || !b.items[1].is_atom(":=")
            || !b.items[2].is_atom()) {
          err(b.line, "an assignment is [?q := C]");
          continue;
        }
        if (arm.has_assign) {
          err(b.line, "one ?-assignment per arm");
          continue;
        }
        arm.has_assign = true;
        arm.lit        = b.items[0].text;
        int64_t v      = 0;
        if (b.items[2].text == "NONZERO") {
          arm.nonzero = true;
        } else if (parse_int(b.items[2].text, v)) {
          arm.value = v;
        } else {
          arm.assign_text = b.items[2].text;
        }
      }
      r.arms.push_back(std::move(arm));
    }
  }

  void analysis(const Sexp& s, Analyses& out) {
    const auto head = s.head();
    if (!s.is_list() || (head != "analysis" && head != "pred")) {
      err(s.line, "expected (analysis ...) or (pred ...), got " + to_text(s));
      return;
    }
    Analysis a;
    a.line    = s.line;
    a.is_pred = head == "pred";
    if (s.items.size() < 3 || !s.items[1].is_atom() || !s.items[2].is_list()) {
      err(s.line, "(analysis NAME (params) :type T :spec S)");
      return;
    }
    a.name = s.items[1].text;
    for (const auto& p : s.items[2].items) {
      if (p.is_list() && p.head() == "k" && p.items.size() == 2 && p.items[1].is_atom()) {
        a.params.push_back(p.items[1].text);  // (k n): a constant parameter
        a.is_const.push_back(true);
      } else if (p.is_atom()) {
        a.params.push_back(p.text);
        a.is_const.push_back(false);
      } else {
        err(p.line, "an analysis parameter is a name (a class) or (k name) (a constant)");
      }
    }
    for (size_t i = 3; i < s.items.size(); i += 2) {
      const auto& k = s.items[i];
      if (!k.is_atom() || i + 1 >= s.items.size()) {
        err(k.line, "expected :type/:uses/:spec with a value");
        return;
      }
      const auto& v = s.items[i + 1];
      if (k.text == ":type") {
        a.type = v.text;
      } else if (k.text == ":uses") {
        a.uses = v.text;
      } else if (k.text == ":spec") {
        a.spec = expr(v);
      } else {
        err(k.line, "unknown analysis option `" + k.text + "`");
      }
    }
    out.list.push_back(std::move(a));
  }

private:
  std::string path_;
  Diags&      diags_;
};

}  // namespace

Rules_file parse_rules(std::string_view text, std::string_view path, std::string_view group, Diags& diags) {
  Rules_file f;
  f.group = std::string{group};
  f.path  = std::string{path};
  f.text  = std::string{text};
  try {
    const auto top = parse_sexps(text, path);
    Parser     parser(std::string{path}, diags);
    for (const auto& s : top) {
      parser.rule(s, f);
    }
  } catch (const Syntax_error& e) {
    diags.push_back(e.message);
  }
  return f;
}

Analyses parse_analyses(std::string_view text, std::string_view path, Diags& diags) {
  Analyses a;
  a.path = std::string{path};
  a.text = std::string{text};
  try {
    const auto top = parse_sexps(text, path);
    Parser     parser(std::string{path}, diags);
    for (const auto& s : top) {
      parser.analysis(s, a);
    }
  } catch (const Syntax_error& e) {
    diags.push_back(e.message);
  }
  return a;
}

// ---------------------------------------------------------------------------
// Hand-written rules (so RULES.md lists them and the lints see their names)
// ---------------------------------------------------------------------------

std::vector<std::string> scan_analysis_impls(std::string_view cpp) {
  std::vector<std::string> out;
  for (size_t pos = 0; pos < cpp.size();) {
    const auto nl   = cpp.find('\n', pos);
    auto       line = cpp.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos             = nl == std::string_view::npos ? cpp.size() : nl + 1;
    if (!line.starts_with("AEG_ANALYSIS(")) {
      continue;
    }
    const auto comma = line.find(',');
    if (comma != std::string_view::npos) {
      out.emplace_back(line.substr(13, comma - 13));
    }
  }
  return out;
}

std::vector<Native_rule> scan_native(std::string_view cpp, std::string_view path) {
  std::vector<Native_rule> out;
  std::string              group;
  const auto               slash = path.find_last_of('/');
  const auto               base  = std::string{path.substr(slash == std::string_view::npos ? 0 : slash + 1)};

  std::vector<std::string_view> lines;
  for (size_t pos = 0; pos <= cpp.size();) {
    const auto nl = cpp.find('\n', pos);
    lines.push_back(cpp.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
    if (nl == std::string_view::npos) {
      break;
    }
    pos = nl + 1;
  }
  const auto trim = [](std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
      s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
      s.remove_suffix(1);
    }
    return s;
  };
  for (size_t ln = 0; ln < lines.size(); ++ln) {
    const auto line = trim(lines[ln]);
    if (line.starts_with("AEG_RULE_GROUP(")) {
      const auto close = line.find(')');
      group            = std::string{trim(line.substr(15, close - 15))};
      continue;
    }
    bool native = false;
    if (line.starts_with("AEG_NATIVE_RULE(")) {
      native = true;
    } else if (!line.starts_with("AEG_RULE(")) {
      continue;
    }
    const auto open  = line.find('(');
    const auto close = line.find(')');
    if (close == std::string_view::npos) {
      continue;
    }
    std::vector<std::string> args;
    std::string              cur;
    for (const char c : line.substr(open + 1, close - open - 1)) {
      if (c == ',') {
        args.emplace_back(trim(cur));
        cur.clear();
      } else {
        cur += c;
      }
    }
    args.emplace_back(trim(cur));
    if (args.size() < 4) {
      continue;
    }
    Native_rule n;
    n.name   = args[0];
    n.tag    = args[1];
    n.group  = group;
    n.file   = base;
    n.line   = static_cast<int>(ln + 1);
    n.prio   = std::atoi(args[2].c_str());
    n.native = native;
    for (size_t i = 3; i < args.size(); ++i) {
      auto r = args[i];
      if (r.starts_with("Ntype_op::")) {
        r = r.substr(10);
      }
      n.roots.push_back(r);
    }
    // The comment block right above (a native rule's documentation).
    size_t first = ln;
    while (first > 0 && trim(lines[first - 1]).starts_with("//")) {
      --first;
    }
    for (size_t k = first; k < ln; ++k) {
      auto c = trim(lines[k]);
      c.remove_prefix(2);
      c = trim(c);
      if (c.empty()) {
        continue;
      }
      n.doc += (n.doc.empty() ? "" : " ") + std::string{c};
    }
    out.push_back(std::move(n));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Lints
// ---------------------------------------------------------------------------

namespace {

int price(Ntype_op op) {
  switch (op) {
    case Ntype_op::Get_mask:
    case Ntype_op::Sext:
    case Ntype_op::Not     : return 0;
    case Ntype_op::Sum:
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::Hotmux:
    case Ntype_op::Popcount: return 2;
    case Ntype_op::Mult    : return 3;
    case Ntype_op::Div:
    case Ntype_op::Rem     : return 4;
    default                : return 1;
  }
}

int max_price(const Pat& p) {
  if (p.kind != Pat::Kind::op) {
    return -1;
  }
  int m = price(p.op);
  for (const auto& a : p.args) {
    m = std::max(m, max_price(a));
  }
  for (const auto& b : p.banks) {
    for (const auto& e : b.elems) {
      m = std::max(m, max_price(e));
    }
  }
  return m;
}


// ---- per-profile cost of a rule (the cost lint) -------------------------------------------------------------------

using Binding = std::map<std::string, int64_t>;

// Evaluate a rule expression over sampled constants. `unknown` is set when it names something the lint cannot
// sample (an analysis predicate, a class): the caller then treats a guard as satisfied.
rt::ex::Num eval_sample(const Expr& e, const Binding& env, bool& unknown) {
  switch (e.kind) {
    case Expr::Kind::num: return rt::ex::num(e.num);
    case Expr::Kind::var: {
      const auto it = env.find(e.name);
      if (it == env.end()) {
        unknown = true;
        return rt::ex::flag(true);
      }
      return rt::ex::num(it->second);
    }
    case Expr::Kind::call: break;
  }
  const auto* b = rt::ex::find_builtin(e.name);
  if (b == nullptr) {
    unknown = true;
    return rt::ex::flag(true);
  }
  std::vector<rt::ex::Num> vals;
  for (const auto& a : e.args) {
    vals.push_back(eval_sample(a, env, unknown));
    if (!vals.back().ok) {
      return {};
    }
  }
  return rt::ex::call(e.name, vals);
}

int64_t bits_of_value(int64_t v) {
  uint64_t m = v < 0 ? ~static_cast<uint64_t>(v) : static_cast<uint64_t>(v);
  int64_t  n = 1;
  while (m > 1) {
    m >>= 1;
    ++n;
  }
  return n + (v < 0 ? 1 : 0);
}

struct Priced {
  int64_t width    = 0;
  int64_t total    = 0;
  bool    is_const = false;
  int64_t value    = 0;
  bool    ok       = true;  // false: a constant the sample could not evaluate
};

// The tree price of a pattern or term: every operator node once per occurrence, variables are leaves of width
// `w` and cost nothing (both sides of a rule share them), `(k c)` is the sampled constant.
Priced price_pat(const cost::Profile& prof, const Pat& p, int64_t w, const Binding& env) {
  Priced out;
  switch (p.kind) {
    case Pat::Kind::wild:
    case Pat::Kind::var:
    case Pat::Kind::lit: out.width = w; return out;
    case Pat::Kind::num:
      out.width = bits_of_value(p.num);
      out.is_const = true;
      out.value = p.num;
      return out;
    case Pat::Kind::konst: {
      out.is_const = true;
      if (p.expr) {
        bool       unknown = false;
        const auto v = eval_sample(*p.expr, env, unknown);
        out.ok = v.ok && !unknown;
        out.value = v.v;
      } else if (!p.name.empty() && p.name != "_") {
        const auto it = env.find(p.name);
        out.ok = it != env.end();
        out.value = out.ok ? it->second : 0;
      } else {
        out.value = 2;
      }
      out.width = bits_of_value(out.value);
      return out;
    }
    case Pat::Kind::op: break;
  }
  std::vector<cost::Operand> ops;
  const auto add = [&](const Pat& a, uint32_t bank) {
    const auto k = price_pat(prof, a, w, env);
    out.total += k.total;
    out.ok = out.ok && k.ok;
    ops.push_back(cost::Operand{bank, k.width, k.is_const, k.value});
  };
  uint32_t i = 0;
  for (const auto& a : p.args) {
    add(a, i < p.pids.size() ? p.pids[i] : i);
    ++i;
  }
  for (const auto& b : p.banks) {
    for (const auto& e : b.elems) {
      add(e, b.id);
    }
  }
  int64_t width = w;
  switch (p.op) {
    case Ntype_op::EQ:
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::Ror:
    case Ntype_op::Rxor: width = 1; break;
    case Ntype_op::Get_mask:
      if (ops.size() == 3 && ops[1].is_const && ops[2].is_const && ops[2].value > ops[1].value) {
        width = std::min<int64_t>(w, ops[2].value - ops[1].value);
      }
      break;
    default: break;
  }
  out.width = width;
  out.total += cost::price(prof, p.op, width, ops);
  return out;
}

void collect_binders(const Pat& p, std::vector<std::string>& names) {
  if (p.kind == Pat::Kind::konst && !p.name.empty() && p.name != "_" && (!p.expr || p.expr->kind == Expr::Kind::var)
      && std::ranges::find(names, p.name) == names.end()) {
    names.push_back(p.name);
  }
  for (const auto& a : p.args) {
    collect_binders(a, names);
  }
  for (const auto& b : p.banks) {
    for (const auto& e : b.elems) {
      collect_binders(e, names);
    }
  }
}

class Linter {
public:
  Linter(const Analyses& an, Diags& diags, const std::vector<cost::Profile>* profiles = nullptr)
      : analyses_(an), diags_(diags), profiles_(profiles) {}

  void err(const std::string& path, int line, const std::string& msg) {
    diags_.push_back(path + ":" + std::to_string(line) + ": error: " + msg);
  }
  void warn(const std::string& path, int line, const std::string& msg) {
    diags_.push_back(path + ":" + std::to_string(line) + ": warning: " + msg);
  }

  // A scope: name -> kind. c = class, k = (k c) constant, q = ? literal, r = rest, l = :let value.
  using Scope = std::map<std::string, char>;

  void bind(Scope& sc, const std::string& name, char kind, int line) {
    const auto [it, inserted] = sc.emplace(name, kind);
    if (!inserted && it->second != kind) {
      err(path_, line, "`" + name + "` is bound as two different kinds (class, (k) constant, ?, rest or :let)");
    }
  }

  void check_op(const Pat& p) {
    if (p.op == Ntype_op::Invalid) {
      return;
    }
    if (!is_rule_op(p.op)) {
      err(path_, p.line, "op `" + p.op_text + "` is not a pure e-graph op a rule can name");
    }
  }

  void lhs(const Pat& p, Scope& sc) {
    switch (p.kind) {
      case Pat::Kind::wild:
      case Pat::Kind::num: break;
      case Pat::Kind::var:
        if (!is_ident(p.name)) {
          err(path_, p.line, "bad variable name `" + p.name + "`");
        } else {
          bind(sc, p.name, 'c', p.line);
        }
        break;
      case Pat::Kind::konst:
        if (p.expr && p.expr->kind != Expr::Kind::var) {
          err(path_, p.line, "in a pattern (k c) binds a name; computed constants belong in :let");
        } else if (!p.name.empty() && p.name != "_") {
          if (!is_ident(p.name)) {
            err(path_, p.line, "bad constant name `" + p.name + "`");
          } else {
            bind(sc, p.name, 'k', p.line);
          }
        }
        break;
      case Pat::Kind::lit:
        if (!is_ident(p.name.substr(1))) {
          err(path_, p.line, "bad literal name `" + p.name + "`");
        } else {
          bind(sc, p.name, 'q', p.line);
        }
        break;
      case Pat::Kind::op:
        check_op(p);
        if (!p.attrs.empty()) {
          err(path_, p.line, "{attr} is a right-hand-side constructor option, not a pattern");
        }
        for (const auto& a : p.args) {
          lhs(a, sc);
        }
        for (const auto& b : p.banks) {
          for (const auto& e : b.elems) {
            lhs(e, sc);
          }
          if (b.has_rest) {
            if (!is_ident(b.rest)) {
              err(path_, b.line, "bad ..rest name");
            } else if (!sc.emplace(b.rest, 'r').second) {
              err(path_, b.line, "`" + b.rest + "` is bound twice");
            }
          }
        }
        break;
    }
  }

  [[nodiscard]] bool is_bool(const Expr& e) const {
    if (e.kind != Expr::Kind::call) {
      return false;
    }
    if (const auto* b = rt::ex::find_builtin(e.name); b != nullptr) {
      return b->boolean;
    }
    return analyses_.find(e.name) != nullptr;
  }

  void expr(const Expr& e, const Scope& sc) {
    switch (e.kind) {
      case Expr::Kind::num: break;
      case Expr::Kind::var: {
        const auto it = sc.find(e.name);
        if (it == sc.end()) {
          err(path_, e.line, "unbound variable `" + e.name + "` in an expression");
        } else if (it->second != 'k' && it->second != 'l') {
          err(path_, e.line, "`" + e.name + "` is not a constant (only (k c) and :let names are values in an expression)");
        }
        break;
      }
      case Expr::Kind::call: {
        if (const auto* b = rt::ex::find_builtin(e.name); b != nullptr) {
          const int n = static_cast<int>(e.args.size());
          if (n < b->min_args || (b->max_args >= 0 && n > b->max_args)) {
            err(path_, e.line,
                "builtin `" + e.name + "` takes " + std::to_string(b->min_args) + (b->max_args == b->min_args ? "" : "+")
                    + " arguments, not " + std::to_string(n));
          }
          for (const auto& a : e.args) {
            expr(a, sc);
          }
          break;
        }
        if (const auto* an = analyses_.find(e.name); an != nullptr) {
          if (analyses_.impl_known && !analyses_.impl.contains(an->name)) {
            err(path_, e.line, "analysis `" + e.name + "` has no runtime implementation (AEG_ANALYSIS in runtime/analyses.cpp)");
          }
          if (an->params.size() != e.args.size()) {
            err(path_, e.line, "analysis `" + e.name + "` takes " + std::to_string(an->params.size()) + " arguments");
            break;
          }
          for (size_t i = 0; i < e.args.size(); ++i) {
            const auto& a = e.args[i];
            if (an->is_const[i]) {
              expr(a, sc);  // a constant parameter takes any constant expression
              continue;
            }
            if (a.kind != Expr::Kind::var) {
              err(path_, a.line, "a class argument of an analysis is a variable");
              continue;
            }
            const auto it = sc.find(a.name);
            if (it == sc.end()) {
              err(path_, a.line, "unbound variable `" + a.name + "`");
            } else if (it->second != 'c' && it->second != 'k') {
              err(path_, a.line, "`" + a.name + "` is not a class");
            }
          }
          break;
        }
        err(path_, e.line, "unknown builtin or analysis `" + e.name + "`");
        break;
      }
    }
  }

  void term(const Pat& p, const Scope& sc, bool allow_attrs) {
    switch (p.kind) {
      case Pat::Kind::wild: err(path_, p.line, "`_` has no value on the right-hand side"); break;
      case Pat::Kind::num: break;
      case Pat::Kind::var: {
        if (p.name.find('?') != std::string::npos) {
          err(path_, p.line, "`" + p.name + "`: a ?-bearing constant cannot be created (create_const of ? is forbidden, P7)");
          break;
        }
        const auto it = sc.find(p.name);
        if (it == sc.end()) {
          err(path_, p.line, "unbound variable `" + p.name + "` on the right-hand side");
        } else if (it->second == 'r') {
          err(path_, p.line, "`" + p.name + "` is a ..rest binder, usable only as ..name inside a bank");
        }
        break;
      }
      case Pat::Kind::konst:
        if (!p.expr) {
          err(path_, p.line, "(k EXPR) needs an expression");
        } else {
          expr(*p.expr, sc);
          if (is_bool(*p.expr)) {
            err(path_, p.line, "(k EXPR) needs a number, not a truth value");
          }
        }
        break;
      case Pat::Kind::lit:
        if (!sc.contains(p.name)) {
          err(path_, p.line,
              "fresh `" + p.name + "`: a rule may not mint a ? literal (it would split the literal); bind it on the left");
        }
        break;
      case Pat::Kind::op:
        check_op(p);
        for (const auto& at : p.attrs) {
          if (!allow_attrs) {
            err(path_, p.line, "{" + at + "} is not allowed here");
          } else if (at != "proven") {
            err(path_, p.line, "unregistered contract attribute {" + at + "} (registered: proven)");
          } else {
            err(path_, p.line, "{proven} constructors need the Hotmux obligation machinery (milestone F); not available yet");
          }
        }
        for (const auto& a : p.args) {
          term(a, sc, allow_attrs);
        }
        for (const auto& b : p.banks) {
          for (const auto& e : b.elems) {
            term(e, sc, allow_attrs);
          }
          if (b.has_rest) {
            const auto it = sc.find(b.rest);
            if (it == sc.end() || it->second != 'r') {
              err(path_, b.line, "..`" + b.rest + "` is not a rest binder of the left-hand side");
            }
          }
        }
        break;
    }
  }

  void example(const Pat& p) {
    switch (p.kind) {
      case Pat::Kind::num: break;
      case Pat::Kind::var:
        if (p.name.size() < 2 || p.name[0] != '$' || !is_ident(p.name.substr(1))) {
          err(path_, p.line, "example inputs are spelled $name, not `" + p.name + "`");
        }
        break;
      case Pat::Kind::op:
        check_op(p);
        if (!p.attrs.empty()) {
          err(path_, p.line, "no {attr} in an example");
        }
        for (const auto& a : p.args) {
          example(a);
        }
        for (const auto& b : p.banks) {
          if (b.has_rest) {
            err(path_, b.line, "no ..rest in an example");
          }
          for (const auto& e : b.elems) {
            example(e);
          }
        }
        break;
      default: err(path_, p.line, "an example is a concrete term: operators, integers and $inputs"); break;
    }
  }

  [[nodiscard]] bool known_profile(const std::string& name) const {
    if (profiles_ == nullptr || profiles_->empty()) {
      return name == "synth" || name == "sim";
    }
    return std::ranges::any_of(*profiles_, [&](const cost::Profile& p) { return p.name == name; });
  }
  [[nodiscard]] std::string profile_names() const {
    if (profiles_ == nullptr || profiles_->empty()) {
      return "synth or sim";
    }
    std::string t;
    for (const auto& p : *profiles_) {
      t += (t.empty() ? "" : " or ") + p.name;
    }
    return t;
  }

  // A `simplify` replaces the class without asking the extractor, so it has to be at least as cheap in EVERY
  // profile: sampled over the constants its guards admit and a few widths, the right-hand side may not cost more
  // than the left. A rule that is cheaper in one profile and dearer in another is an `alt`.
  void cost_lint(const Rule& r) {
    std::vector<std::string> binders;
    collect_binders(r.lhs, binders);
    static constexpr std::array<int64_t, 14> kPool{0, 1, 2, 3, 4, 5, 7, 8, 16, 31, 32, 63, 64, 100};
    std::vector<Binding> instances;
    std::vector<size_t>  idx(binders.size(), 0);
    for (size_t tries = 0; tries < 20000 && instances.size() < 6; ++tries) {
      Binding env;
      bool    ok = true;
      for (size_t i = 0; i < binders.size(); ++i) {
        env[binders[i]] = kPool[idx[i]];
      }
      for (const auto& [n, e] : r.lets) {
        bool       unknown = false;
        const auto v       = eval_sample(e, env, unknown);
        if (!v.ok || unknown) {
          ok = false;
          break;
        }
        env[n] = v.v;
      }
      for (const auto& w : r.whens) {
        if (!ok) {
          break;
        }
        bool       unknown = false;
        const auto v       = eval_sample(w, env, unknown);
        ok                 = v.ok && (unknown || v.v != 0);
      }
      if (ok) {
        instances.push_back(std::move(env));
      }
      // odometer
      size_t d = 0;
      for (; d < idx.size(); ++d) {
        if (++idx[d] < kPool.size()) {
          break;
        }
        idx[d] = 0;
      }
      if (d == idx.size()) {
        break;
      }
    }
    for (const auto& prof : *profiles_) {
      for (const auto& env : instances) {
        for (const int64_t w : {int64_t{1}, int64_t{8}, int64_t{70}}) {
          const auto l = price_pat(prof, r.lhs, w, env);
          const auto h = price_pat(prof, r.arms[0].rhs, w, env);
          if (l.ok && h.ok && h.total > l.total) {
            std::string at;
            for (const auto& [n, v] : env) {
              at += (at.empty() ? " with " : ", ") + n + "=" + std::to_string(v);
            }
            err(path_, r.line,
                "simplify `" + r.name + "` costs more than it replaces under profile `" + prof.name + "` (" + std::to_string(h.total)
                    + " against " + std::to_string(l.total) + " at width " + std::to_string(w) + at + "): make it alt");
            return;
          }
        }
      }
    }
  }

  void rule(const Rules_file& file, const Rule& r) {
    path_ = file.path;
    if (!is_rule_name(r.name)) {
      err(path_, r.line, "rule name `" + r.name + "` must be lower_snake_case");
    }
    if (r.lhs.kind != Pat::Kind::op) {
      err(path_, r.line, "the left-hand side of `" + r.name + "` must be an operator pattern");
      return;
    }
    Scope sc;
    lhs(r.lhs, sc);
    for (const auto& [n, e] : r.lets) {
      expr(e, sc);
      if (is_ident(n)) {
        if (sc.contains(n)) {
          err(path_, e.line, "`" + n + "` is already bound");
        }
        sc[n] = 'l';
      } else {
        err(path_, e.line, "bad :let name `" + n + "`");
      }
    }
    for (const auto& w : r.whens) {
      expr(w, sc);
      if (!is_bool(w)) {
        err(path_, w.line, ":when takes a truth value (a comparison, `pow2?`, or an analysis predicate)");
      }
    }
    if (r.arms.empty()) {
      err(path_, r.line, "rule `" + r.name + "` has no right-hand side");
      return;
    }
    if (r.tag == Rule::Tag::choice) {
      const bool has_lit = std::ranges::any_of(sc, [](const auto& kv) { return kv.second == 'q'; });
      if (!has_lit) {
        err(path_, r.line, "choice `" + r.name + "` binds no ?literal on the left");
      }
      for (const auto& arm : r.arms) {
        if (!arm.has_assign) {
          err(path_, arm.line, "choice arm without a ?-assignment: write [?q := C]");
        } else if (const auto it = sc.find(arm.lit); it == sc.end() || it->second != 'q') {
          err(path_, arm.line, "`" + arm.lit + "` is not a ? literal bound on the left");
        } else if (!arm.nonzero && !arm.value) {
          err(path_, arm.line, "expression assignment `" + arm.lit + " := " + arm.assign_text + "` needs the choice milestone (J)");
        }
      }
    } else {
      for (const auto& arm : r.arms) {
        if (arm.has_assign) {
          err(path_, arm.line, "only `choice` arms take a [?q := C] assignment");
        }
      }
    }
    for (const auto& arm : r.arms) {
      term(arm.rhs, sc, true);
    }
    if (r.tag == Rule::Tag::simplify && r.arms.size() == 1) {
      const int lhs_nodes = op_count(r.lhs), rhs_nodes = op_count(r.arms[0].rhs);
      if (rhs_nodes > lhs_nodes) {
        err(path_, r.line,
            "simplify `" + r.name + "` grows the graph (" + std::to_string(rhs_nodes) + " nodes against "
                + std::to_string(lhs_nodes) + "): make it alt");
      } else if (profiles_ == nullptr || profiles_->empty()) {
        if (max_price(r.arms[0].rhs) > price(r.lhs.op)) {
          err(path_, r.line, "simplify `" + r.name + "` builds an op priced above its left-hand root: make it alt");
        }
      } else {
        cost_lint(r);
      }
    }
    if (!r.example) {
      err(path_, r.line, "rule `" + r.name + "` has no :example (it is what proves the rule fires)");
    } else {
      example(*r.example);
    }
    for (const auto& e : r.expects) {
      if (!known_profile(e.profile)) {
        err(path_, e.line, "unknown cost profile `" + e.profile + "` (" + profile_names() + ")");
      }
      if (e.side != "lhs" && e.side != "rhs") {
        err(path_, e.line, ":expect side is lhs or rhs, not `" + e.side + "`");
      }
    }
    if (r.proof != "bv" && r.proof != "int" && r.proof != "lean" && r.proof != "none") {
      err(path_, r.line, ":proof is bv|int|lean|none, not `" + r.proof + "`");
    }
    if (r.fuel && *r.fuel < 1) {
      err(path_, r.line, ":fuel must be positive");
    }
  }

  void run(const std::vector<Rules_file>& files, const std::vector<Native_rule>& natives) {
    std::map<std::string, std::string> names;
    for (const auto& n : natives) {
      if (!names.emplace(n.name, n.file + ":" + std::to_string(n.line)).second) {
        diags_.push_back(n.file + ":" + std::to_string(n.line) + ": error: duplicate hand-written rule name `" + n.name + "`");
      }
    }
    for (const auto& f : files) {
      if (!is_ident(f.group)) {
        diags_.push_back(f.path + ":1: error: bad group name `" + f.group + "`");
      }
      if (f.rules.empty()) {
        diags_.push_back(f.path + ":1: error: group `" + f.group + "` has no rules (empty or truncated file)");
      }
      std::map<std::string, const Rule*> canon;
      for (const auto& r : f.rules) {
        const auto [it, inserted] = names.emplace(r.name, f.path + ":" + std::to_string(r.line));
        if (!inserted) {
          err(f.path, r.line, "rule name `" + r.name + "` is already used at " + it->second);
        }
        rule(f, r);
        // Two rules with one left-hand side and one priority: the order is only the tie-break.
        if (r.lhs.kind == Pat::Kind::op && r.arms.size() == 1) {
          const auto key = std::to_string(r.prio) + "|" + canonical_text(r.lhs) + "|" + std::to_string(static_cast<int>(r.tag));
          if (const auto c = canon.find(key); c != canon.end() && r.whens.empty() && c->second->whens.empty()) {
            warn(f.path, r.line, "rule `" + r.name + "` overlaps `" + c->second->name + "` (same left-hand side and priority)");
          } else {
            canon.emplace(key, &r);
          }
        }
      }
    }
  }

private:
  const Analyses&                  analyses_;
  Diags&                           diags_;
  const std::vector<cost::Profile>* profiles_;
  std::string                      path_;
};

}  // namespace

void lint(const std::vector<Rules_file>& files, const Analyses& analyses, const std::vector<Native_rule>& natives, Diags& diags) {
  lint(files, analyses, natives, {}, diags);
}

void lint(const std::vector<Rules_file>& files, const Analyses& analyses, const std::vector<Native_rule>& natives,
          const std::vector<cost::Profile>& profiles, Diags& diags) {
  // An analysis :spec is documentation, hypothesis and sampler; it may name the fact's own parameters (LO, HI,
  // MASK) and operators, so it is not linted as a guard.
  Linter l(analyses, diags, &profiles);
  l.run(files, natives);
}

}  // namespace livehd::aegraph::rulegen
