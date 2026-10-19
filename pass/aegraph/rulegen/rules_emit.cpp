//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// The emitters of rulegen: rule C++ (with #line back into the .rules file), the
// per-group fuzz and prove tests, and RULES.md. Every output is a pure function
// of its inputs: no timestamps, no addresses, no hash-map iteration order.

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "rule_expr.hpp"
#include "rules.hpp"

namespace livehd::aegraph::rulegen {

namespace {

// ---------------------------------------------------------------------------
// One rule -> C++
// ---------------------------------------------------------------------------

class Rule_emitter {
public:
  Rule_emitter(const Rules_file& f, const Rule& r, const Analyses& a) : file_(f), rule_(r), analyses_(a) {}

  std::string run() {
    collect(rule_.lhs);
    for (const auto& [n, e] : rule_.lets) {
      kinds_[n] = 'l';
    }

    line_dir(rule_.line);
    out_ += "AEG_RULE(" + rule_.name + ", " + tag_name() + ", " + std::to_string(rule_.prio) + ", Ntype_op::"
            + op_spelling(rule_.lhs.op) + ") {\n";
    ind_ = 1;
    emit("[[maybe_unused]] bool done = false;");
    for (const auto& [name, kind] : kinds_) {
      switch (kind) {
        case 'c': emit("[[maybe_unused]] Class_id v_" + name + " = kNoClass;"); break;
        case 'k':
          emit("[[maybe_unused]] ex::Num k_" + name + "{};");
          emit("[[maybe_unused]] Class_id kc_" + name + " = kNoClass;");
          break;
        case 'q': emit("[[maybe_unused]] Class_id q_" + name.substr(1) + " = kNoClass;"); break;
        case 'r': emit("[[maybe_unused]] std::vector<Operand> r_" + name + ";"); break;
        default: break;
      }
    }
    std::vector<Task> tasks;
    tasks.push_back(Task::of(&rule_.lhs, "", true));
    tasks_(std::move(tasks), "return;");
    out_ += "}\n";
    return out_;
  }

private:
  struct Task {
    enum class Kind { pat, bank_elem, bank_rest };
    Kind        kind = Kind::pat;
    const Pat*  pat  = nullptr;
    std::string cls;
    bool        root = false;
    const Bank* bank = nullptr;
    std::string ops;
    std::string used;

    static Task of(const Pat* p, std::string cls, bool root = false) {
      Task t;
      t.pat  = p;
      t.cls  = std::move(cls);
      t.root = root;
      return t;
    }
  };

  [[nodiscard]] std::string tag_name() const {
    return rule_.tag == Rule::Tag::simplify ? "simplify" : (rule_.tag == Rule::Tag::alt ? "alt" : "choice");
  }

  void collect(const Pat& p) {
    switch (p.kind) {
      case Pat::Kind::var: kinds_[p.name] = 'c'; break;
      case Pat::Kind::konst:
        if (!p.name.empty() && p.name != "_") {
          kinds_[p.name] = 'k';
        }
        break;
      case Pat::Kind::lit: kinds_[p.name] = 'q'; break;
      case Pat::Kind::op:
        for (const auto& a : p.args) {
          collect(a);
        }
        for (const auto& b : p.banks) {
          for (const auto& e : b.elems) {
            collect(e);
          }
          if (b.has_rest) {
            kinds_[b.rest] = 'r';
          }
        }
        break;
      default: break;
    }
  }

  void emit(const std::string& s) { out_ += std::string(static_cast<size_t>(ind_) * 2, ' ') + s + "\n"; }
  void line_dir(int line) { out_ += "#line " + std::to_string(line) + " \"" + file_.path + "\"\n"; }
  std::string next() { return std::to_string(uid_++); }

  // ---- matching ----

  void tasks_(std::vector<Task> tasks, const std::string& fail) {
    if (tasks.empty()) {
      finish(fail);
      return;
    }
    Task t = std::move(tasks.front());
    tasks.erase(tasks.begin());
    switch (t.kind) {
      case Task::Kind::pat: match_pat(t, std::move(tasks), fail); break;
      case Task::Kind::bank_elem: bank_elem(t, std::move(tasks)); break;
      default: bank_rest(t, std::move(tasks), fail); break;
    }
  }

  void match_pat(const Task& t, std::vector<Task> rest, const std::string& fail) {
    const Pat& p = *t.pat;
    switch (p.kind) {
      case Pat::Kind::wild: tasks_(std::move(rest), fail); return;
      case Pat::Kind::var: {
        if (bound_.insert("c" + p.name).second) {
          emit("v_" + p.name + " = " + t.cls + ";");
        } else {
          emit("if (" + t.cls + " != v_" + p.name + ") " + fail);
        }
        tasks_(std::move(rest), fail);
        return;
      }
      case Pat::Kind::num:
        emit("if (!ctx.is_int(" + t.cls + ", INT64_C(" + std::to_string(p.num) + "))) " + fail);
        tasks_(std::move(rest), fail);
        return;
      case Pat::Kind::konst: {
        const auto id = next();
        emit("const auto kv" + id + " = ctx.small_int(" + t.cls + ");");
        if (p.name.empty() || p.name == "_") {
          emit("if (!kv" + id + ") " + fail);
        } else if (bound_.insert("k" + p.name).second) {
          emit("if (!kv" + id + ") " + fail);
          emit("k_" + p.name + " = ex::num(*kv" + id + ");");
          emit("kc_" + p.name + " = " + t.cls + ";");
        } else {
          emit("if (!kv" + id + " || *kv" + id + " != k_" + p.name + ".v) " + fail);
        }
        tasks_(std::move(rest), fail);
        return;
      }
      case Pat::Kind::lit: {
        const auto q = "q_" + p.name.substr(1);
        if (bound_.insert("q" + p.name).second) {
          emit("if (ctx.eg().cls(" + t.cls + ").kind != Egraph::Kind::literal) " + fail);
          emit(q + " = " + t.cls + ";");
        } else {
          emit("if (" + t.cls + " != " + q + ") " + fail);
        }
        tasks_(std::move(rest), fail);
        return;
      }
      case Pat::Kind::op: break;
    }
    if (t.root) {
      emit("if (root.op != Ntype_op::" + op_spelling(p.op) + ") " + fail);
      slots(p, "root.ops", std::move(rest), fail);
      return;
    }
    const auto id = next();
    emit("ctx.each_member(" + t.cls + ", [&](Class_id m" + id + ") {");
    ++ind_;
    emit("const Enode* n" + id + " = ctx.node_of(m" + id + ");");
    emit("if (n" + id + " == nullptr || n" + id + "->op != Ntype_op::" + op_spelling(p.op) + ") return false;");
    emit("const auto ops" + id + " = ctx.operands_of(*n" + id + ");");
    slots(p, "ops" + id, std::move(rest), "return false;");
    emit("return done;");
    --ind_;
    emit("});");
  }

  void slots(const Pat& p, const std::string& ops, std::vector<Task> rest, const std::string& fail) {
    std::vector<Task> tasks;
    if (!p.banked) {
      std::string cond = ops + ".size() != " + std::to_string(p.args.size());
      for (size_t i = 0; i < p.args.size(); ++i) {
        cond += " || " + ops + "[" + std::to_string(i) + "].bank != " + std::to_string(p.pids[i]);
      }
      emit("if (" + cond + ") " + fail);
      for (size_t i = 0; i < p.args.size(); ++i) {
        tasks.push_back(Task::of(&p.args[i], ops + "[" + std::to_string(i) + "].cls"));
      }
      tasks.insert(tasks.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
      tasks_(std::move(tasks), fail);
      return;
    }
    const auto shape = shape_of(p.op);
    const auto id    = next();
    emit("if (" + ops + ".size() > 64) " + fail);
    for (int b = 0; b < shape.bank_count; ++b) {
      const Bank* spec = nullptr;
      for (const auto& bank : p.banks) {
        if (bank.id == static_cast<uint32_t>(b)) {
          spec = &bank;
        }
      }
      const auto cnt = "bank_count(" + ops + ", " + std::to_string(b) + ")";
      if (spec == nullptr) {
        emit("if (" + cnt + " != 0) " + fail);
      } else if (spec->has_rest) {
        emit("if (" + cnt + " < " + std::to_string(spec->elems.size()) + ") " + fail);
      } else {
        emit("if (" + cnt + " != " + std::to_string(spec->elems.size()) + ") " + fail);
      }
    }
    emit("uint64_t used" + id + " = 0;");
    for (const auto& bank : p.banks) {
      for (const auto& e : bank.elems) {
        Task t;
        t.kind = Task::Kind::bank_elem;
        t.pat  = &e;
        t.bank = &bank;
        t.ops  = ops;
        t.used = "used" + id;
        tasks.push_back(std::move(t));
      }
      if (bank.has_rest) {
        Task t;
        t.kind = Task::Kind::bank_rest;
        t.bank = &bank;
        t.ops  = ops;
        t.used = "used" + id;
        tasks.push_back(std::move(t));
      }
    }
    tasks.insert(tasks.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
    tasks_(std::move(tasks), fail);
  }

  void bank_elem(const Task& t, std::vector<Task> rest) {
    const auto id = next();
    const auto j  = "j" + id;
    emit("for (size_t " + j + " = 0; " + j + " < " + t.ops + ".size() && !done; ++" + j + ") {");
    ++ind_;
    emit("if (" + t.ops + "[" + j + "].bank != " + std::to_string(t.bank->id) + " || ((" + t.used + " >> " + j
         + ") & 1U) != 0) continue;");
    emit(t.used + " |= uint64_t{1} << " + j + ";");
    emit("[&] {");
    ++ind_;
    std::vector<Task> tasks;
    tasks.push_back(Task::of(t.pat, t.ops + "[" + j + "].cls"));
    tasks.insert(tasks.end(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
    tasks_(std::move(tasks), "return;");
    --ind_;
    emit("}();");
    emit(t.used + " &= ~(uint64_t{1} << " + j + ");");
    --ind_;
    emit("}");
  }

  void bank_rest(const Task& t, std::vector<Task> rest, const std::string& fail) {
    const auto id = next();
    const auto j  = "j" + id;
    emit("r_" + t.bank->rest + ".clear();");
    emit("for (size_t " + j + " = 0; " + j + " < " + t.ops + ".size(); ++" + j + ") {");
    ++ind_;
    emit("if (" + t.ops + "[" + j + "].bank == " + std::to_string(t.bank->id) + " && ((" + t.used + " >> " + j + ") & 1U) == 0) r_"
         + t.bank->rest + ".push_back(" + t.ops + "[" + j + "]);");
    --ind_;
    emit("}");
    tasks_(std::move(rest), fail);
  }

  // ---- results ----

  [[nodiscard]] std::string class_cxx(const std::string& name) const {
    const auto it = kinds_.find(name);
    if (it == kinds_.end()) {
      return "kNoClass";
    }
    switch (it->second) {
      case 'c': return "v_" + name;
      case 'k': return "kc_" + name;
      default: return "ctx.konst(l_" + name + ".v)";
    }
  }

  [[nodiscard]] std::string expr_cxx(const Expr& e) const {
    switch (e.kind) {
      case Expr::Kind::num: return "ex::num(INT64_C(" + std::to_string(e.num) + "))";
      case Expr::Kind::var: {
        const auto it = kinds_.find(e.name);
        return (it != kinds_.end() && it->second == 'l' ? "l_" : "k_") + e.name;
      }
      default: break;
    }
    if (const auto* b = rt::ex::find_builtin(e.name); b != nullptr) {
      std::vector<std::string> args;
      for (const auto& a : e.args) {
        args.push_back(expr_cxx(a));
      }
      if (args.size() == 1 && !b->cxx_unary.empty()) {
        return "ex::" + std::string{b->cxx_unary} + "(" + args[0] + ")";
      }
      if (args.size() <= 2) {
        std::string t = "ex::" + std::string{b->cxx} + "(";
        for (size_t i = 0; i < args.size(); ++i) {
          t += (i != 0 ? ", " : "") + args[i];
        }
        return t + ")";
      }
      std::string acc = args[0];
      for (size_t i = 1; i < args.size(); ++i) {
        acc = "ex::" + std::string{b->cxx} + "(" + acc + ", " + args[i] + ")";
      }
      return acc;
    }
    // An analysis predicate: class arguments by class, constant arguments through their ex::Num (a constant
    // that does not evaluate makes the guard false).
    const auto* an = analyses_.find(e.name);
    if (an == nullptr || an->params.size() != e.args.size()) {
      return "ex::flag(false)";
    }
    std::string pre, classes, consts;
    size_t      n_class = 0, n_const = 0;
    for (size_t i = 0; i < e.args.size(); ++i) {
      if (an->is_const[i]) {
        const auto id = "a" + std::to_string(i);
        pre += "const ex::Num " + id + " = " + expr_cxx(e.args[i]) + "; if (!" + id + ".ok) return false; ";
        consts += (n_const++ != 0 ? ", " : "") + id + ".v";
      } else {
        classes += (n_class++ != 0 ? ", " : "") + class_cxx(e.args[i].name);
      }
    }
    return "ex::flag([&]() -> bool { " + pre + "return an::" + e.name + "(ctx, std::array<Class_id, " + std::to_string(n_class)
           + ">{" + classes + "}, std::array<int64_t, " + std::to_string(n_const) + ">{" + consts + "}); }())";
  }

  // The (k EXPR) temporaries of a term, in evaluation order.
  void konst_temps(const Pat& p, const std::string& fail) {
    if (p.kind == Pat::Kind::konst && p.expr) {
      const auto id = "t" + next();
      emit("const ex::Num " + id + " = " + expr_cxx(*p.expr) + ";");
      emit("if (!" + id + ".ok) " + fail);
      temps_[&p] = id;
    }
    for (const auto& a : p.args) {
      konst_temps(a, fail);
    }
    for (const auto& b : p.banks) {
      for (const auto& e : b.elems) {
        konst_temps(e, fail);
      }
    }
  }

  [[nodiscard]] std::string term_cxx(const Pat& p) {
    switch (p.kind) {
      case Pat::Kind::var: return class_cxx(p.name);
      case Pat::Kind::num: return "ctx.konst(int64_t{" + std::to_string(p.num) + "})";
      case Pat::Kind::konst: return "ctx.konst(" + temps_.at(&p) + ".v)";
      case Pat::Kind::lit: return "q_" + p.name.substr(1);
      case Pat::Kind::op: break;
      default: return "kNoClass";
    }
    const auto op = "Ntype_op::" + op_spelling(p.op);
    if (!p.banked) {
      std::string t = "ctx.make(" + op + ", {";
      for (size_t i = 0; i < p.args.size(); ++i) {
        t += (i != 0 ? ", " : "") + std::string{"Operand{"} + std::to_string(p.pids[i]) + ", " + term_cxx(p.args[i]) + "}";
      }
      return t + "})";
    }
    const auto id = next();
    std::string t = "[&] {\n";
    t += std::string(static_cast<size_t>(ind_ + 1) * 2, ' ') + "std::vector<Operand> o" + id + ";\n";
    for (const auto& b : p.banks) {
      for (const auto& e : b.elems) {
        t += std::string(static_cast<size_t>(ind_ + 1) * 2, ' ') + "o" + id + ".push_back(Operand{" + std::to_string(b.id) + ", "
             + term_cxx(e) + "});\n";
      }
      if (b.has_rest) {
        t += std::string(static_cast<size_t>(ind_ + 1) * 2, ' ') + "for (const auto& x" + id + " : r_" + b.rest + ") o" + id
             + ".push_back(Operand{" + std::to_string(b.id) + ", x" + id + ".cls});\n";
      }
    }
    const bool bank_op = p.op == Ntype_op::Sum || p.op == Ntype_op::Mult || p.op == Ntype_op::And || p.op == Ntype_op::Or
                         || p.op == Ntype_op::Xor;
    t += std::string(static_cast<size_t>(ind_ + 1) * 2, ' ');
    t += bank_op ? "return ctx.make_bank(" + op + ", std::move(o" + id + "));\n"
                 : "return ctx.make(" + op + ", std::span<const Operand>(o" + id + ".data(), o" + id + ".size()));\n";
    t += std::string(static_cast<size_t>(ind_) * 2, ' ') + "}()";
    return t;
  }

  void finish(const std::string& fail) {
    line_dir(rule_.line);
    for (const auto& [n, e] : rule_.lets) {
      emit("const ex::Num l_" + n + " = " + expr_cxx(e) + ";");
      emit("if (!l_" + n + ".ok) " + fail);
    }
    for (const auto& w : rule_.whens) {
      emit("if (!ex::holds(" + expr_cxx(w) + ")) " + fail);
    }
    for (const auto& arm : rule_.arms) {
      konst_temps(arm.rhs, fail);
    }
    for (size_t i = 0; i < rule_.arms.size(); ++i) {
      const auto& arm = rule_.arms[i];
      switch (rule_.tag) {
        case Rule::Tag::simplify: emit("out.subsume(" + term_cxx(arm.rhs) + ");"); break;
        case Rule::Tag::alt: emit("out.add(" + term_cxx(arm.rhs) + ");"); break;
        case Rule::Tag::choice: {
          // The implicit (admits ?q C) guard: an arm whose value the literal cannot take is skipped.
          const auto q = "q_" + arm.lit.substr(1);
          const auto v = arm.nonzero ? std::string{"1"} : std::to_string(arm.value.value_or(0));
          const auto admits = arm.nonzero ? "ctx.admits_nonzero(" + q + ")" : "ctx.admits(" + q + ", INT64_C(" + v + "))";
          emit("if (" + admits + ") {");
          ++ind_;
          emit("out.choice(" + term_cxx(arm.rhs) + ", " + q + ", INT64_C(" + v + "));");
          --ind_;
          emit("}");
          break;
        }
      }
    }
    emit("done = true;");
  }

  const Rules_file&           file_;
  const Rule&                 rule_;
  const Analyses&             analyses_;
  std::string                 out_;
  int                         ind_ = 0;
  int                         uid_ = 0;
  std::map<std::string, char> kinds_;  // ordered: the declarations come out sorted
  std::set<std::string>       bound_;
  std::map<const Pat*, std::string> temps_;
};

std::string header(const Rules_file& f, const char* what) {
  return "//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.\n"
         "// GENERATED by rulegen from "
         + f.path + " (" + what + "). Do not edit.\n";
}

}  // namespace

std::string emit_rules_cpp(const Rules_file& f, const Analyses& analyses) {
  std::string out = header(f, "rule code");
  out += "\n#include <array>\n#include <cstdint>\n#include <span>\n#include <vector>\n\n";
  out += "#include \"rulegen/rule_expr.hpp\"\n#include \"runtime/analyses.hpp\"\n#include \"runtime/match.hpp\"\n\n";
  // The analyses this group's guards call (defined by AEG_ANALYSIS in runtime/analyses.cpp).
  std::set<std::string> used;
  std::function<void(const Expr&)> scan = [&](const Expr& e) {
    if (e.kind == Expr::Kind::call && analyses.find(e.name) != nullptr) {
      used.insert(e.name);
    }
    for (const auto& a : e.args) {
      scan(a);
    }
  };
  for (const auto& r : f.rules) {
    for (const auto& w : r.whens) {
      scan(w);
    }
  }
  if (!used.empty()) {
    out += "namespace livehd::aegraph::rt::an {\n";
    for (const auto& name : used) {
      out += "bool " + name + "(Ctx&, std::span<const Class_id>, std::span<const int64_t>);\n";
    }
    out += "}  // namespace livehd::aegraph::rt::an\n\n";
  }
  out += "using namespace livehd::aegraph;\nusing namespace livehd::aegraph::rt;\n\n";
  out += "AEG_RULE_GROUP(" + f.group + ");\n\n";
  for (const auto& r : f.rules) {
    out += Rule_emitter(f, r, analyses).run();
    out += "\n";
  }
  return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

namespace {

std::string test_prelude(const Rules_file& f, const Analyses& a, const char* what) {
  std::string out = header(f, what);
  out += "\n#include \"gtest/gtest.h\"\n#include \"rulegen/rule_test_support.hpp\"\n\nnamespace {\n";
  out += "constexpr const char* kRulesPath = \"" + f.path + "\";\n";
  out += "constexpr const char* kRules = R\"AEGRULES(" + f.text + ")AEGRULES\";\n";
  out += "constexpr const char* kAnalyses = R\"AEGRULES(" + a.text + ")AEGRULES\";\n";
  out += "}  // namespace\n\n";
  return out;
}

}  // namespace

std::string emit_cost_cpp(const cost::Profile& profile, std::string_view text) {
  std::string out = "//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.\n// GENERATED by rulegen from "
                    + profile.path + " (cost profile). Do not edit.\n\n";
  out += "#include \"runtime/cost.hpp\"\n\nnamespace {\n";
  out += "const livehd::aegraph::rt::Cost_registrar kProfile_" + profile.name + "(\"" + profile.name + "\", \"" + profile.path
         + "\", R\"AEGCOST(" + std::string{text} + ")AEGCOST\");\n";
  out += "}  // namespace\n";
  return out;
}

std::string emit_fuzz_test(const Rules_file& f, const Analyses& a) {
  std::string out = test_prelude(f, a, "fuzz and example tests");
  out += "using livehd::aegraph::rulegen::testing::Rule_check;\n\n";
  out += "TEST(AegFuzz, " + f.group + "_group_parses) { Rule_check::parses(kRulesPath, kRules, kAnalyses); }\n\n";
  for (const auto& r : f.rules) {
    out += "#line " + std::to_string(r.line) + " \"" + f.path + "\"\n";
    out += "TEST(AegFuzz, " + f.group + "_" + r.name + "_equal) { Rule_check::fuzz_equal(kRulesPath, kRules, kAnalyses, \"" + r.name
           + "\"); }\n";
    out += "TEST(AegFuzz, " + f.group + "_" + r.name + "_example_fires) {\n";
    out += "  Rule_check::example_fires(kRulesPath, kRules, kAnalyses, \"" + r.name + "\");\n}\n";
    for (const auto& e : r.expects) {
      out += "TEST(AegFuzz, " + f.group + "_" + r.name + "_expect_" + e.profile + "_" + e.side + ") {\n";
      out += "  Rule_check::expect(kRulesPath, kRules, kAnalyses, \"" + r.name + "\", \"" + e.profile + "\", \"" + e.side + "\");\n}\n";
    }
  }
  out += "\nTEST(AegFuzz, " + f.group + "_zz_wall_time) { Rule_check::wall_time(\"fuzz_" + f.group + "\"); }\n";
  return out;
}

std::string emit_prove_test(const Rules_file& f, const Analyses& a) {
  std::string out = test_prelude(f, a, "bv proof tier");
  out += "using livehd::aegraph::rulegen::testing::Rule_check;\n\n";
  out += "TEST(AegProve, " + f.group + "_group_parses) { Rule_check::parses(kRulesPath, kRules, kAnalyses); }\n\n";
  for (const auto& r : f.rules) {
    if (r.proof == "none") {
      continue;
    }
    out += "#line " + std::to_string(r.line) + " \"" + f.path + "\"\n";
    out += "TEST(AegProve, " + f.group + "_" + r.name + ") { Rule_check::prove_bv(kRulesPath, kRules, kAnalyses, \"" + r.name
           + "\"); }\n";
  }
  out += "\nTEST(AegProve, " + f.group + "_zz_wall_time) { Rule_check::wall_time(\"prove_" + f.group + "\"); }\n";
  return out;
}

// ---------------------------------------------------------------------------
// RULES.md
// ---------------------------------------------------------------------------

namespace {

std::string md_escape(std::string s) {
  for (size_t pos = s.find('|'); pos != std::string::npos; pos = s.find('|', pos + 2)) {
    s.insert(pos, "\\");
  }
  return s;
}

std::string base_name(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

struct Row {
  std::string name, tag, root, proof, source, what;
  int         prio = 0;
};

}  // namespace

std::string emit_rules_md(const std::vector<Rules_file>& files, const Analyses& analyses, const std::vector<Native_rule>& natives) {
  std::ostringstream o;
  o << "# Aegraph rules\n\n"
    << "<!-- GENERATED by rulegen (see tools/aegraph.bzl). Do not edit: `bazel test //pass/aegraph:aeg_rules_md_fresh` fails\n"
    << "     when this file is stale. Refresh it with `bazel run //pass/aegraph:aeg_rules_refresh`. -->\n\n"
    << "Every rule the engine can run, per group (a group is a `rules/<group>.rules` file; a hand-written rule joins the\n"
    << "group named by its file's `AEG_RULE_GROUP`). Inside a group the rules are listed in the order the engine tries them:\n"
    << "priority descending, then name. `simplify` replaces the class, `alt` unions an alternative that extraction may keep,\n"
    << "`choice` is an alt carrying a `?` assignment.\n\n"
    << "Proof tier: `bv-swept` = the rule's instances are LEC-checked by `aeg_prove_<group>_smoke` over widths\n"
    << "{1,2,3,8,16,65} (bounded evidence, not a proof for all widths); `native-unverified` = hand-written, covered by the\n"
    << "union-invariant fuzz and an equiv fixture only; `none` = the rule opted out.\n\n";

  std::vector<std::string> groups;
  for (const auto& f : files) {
    groups.push_back(f.group);
  }
  std::set<std::string> extra;
  for (const auto& n : natives) {
    if (std::ranges::find(groups, n.group) == groups.end()) {
      extra.insert(n.group);
    }
  }
  groups.insert(groups.end(), extra.begin(), extra.end());

  size_t total = 0;
  for (const auto& g : groups) {
    std::vector<Row> rows;
    for (const auto& f : files) {
      if (f.group != g) {
        continue;
      }
      for (const auto& r : f.rules) {
        Row row;
        row.name   = r.name;
        row.prio   = r.prio;
        row.tag    = r.tag == Rule::Tag::simplify ? "simplify" : (r.tag == Rule::Tag::alt ? "alt" : "choice");
        row.root   = op_spelling(r.lhs.op);
        row.proof  = proof_tier(r);
        row.source = base_name(f.path) + ":" + std::to_string(r.line);
        const auto rhs = r.arms.empty() ? std::string{} : pat_text(r.arms[0].rhs);
        row.what       = r.doc.empty() ? "`" + pat_text(r.lhs) + " => " + rhs + "`" : r.doc;
        if (!r.expects.empty()) {
          row.what += " Extraction on the example:";
          for (const auto& e : r.expects) {
            row.what += " " + e.profile + " keeps the " + (e.side == "lhs" ? "left" : "right") + " side;";
          }
          row.what.pop_back();
          row.what += ".";
        }
        rows.push_back(std::move(row));
      }
    }
    for (const auto& n : natives) {
      if (n.group != g) {
        continue;
      }
      Row row;
      row.name   = n.name;
      row.prio   = n.prio;
      row.tag    = n.tag;
      row.root   = "";
      for (size_t i = 0; i < n.roots.size(); ++i) {
        row.root += (i != 0 ? ", " : "") + n.roots[i];
      }
      row.proof  = n.native ? "native-unverified" : "hand-written";
      row.source = n.file + ":" + std::to_string(n.line);
      row.what   = n.doc;
      rows.push_back(std::move(row));
    }
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b) {
      if (a.prio != b.prio) {
        return a.prio > b.prio;
      }
      return a.name < b.name;
    });
    o << "## Group `" << g << "`\n\n";
    o << "| rule | tag | prio | root | proof | source | what it does |\n| --- | --- | ---: | --- | --- | --- | --- |\n";
    for (const auto& r : rows) {
      o << "| `" << r.name << "` | " << r.tag << " | " << r.prio << " | " << r.root << " | " << r.proof << " | " << r.source
        << " | " << md_escape(r.what) << " |\n";
    }
    o << "\n";
    total += rows.size();
  }
  o << "Total: " << total << " rules.\n\n";

  o << "## Analyses\n\n| name | kind | parameters | runtime | spec |\n| --- | --- | --- | --- | --- |\n";
  for (const auto& a : analyses.list) {
    std::string params;
    for (size_t i = 0; i < a.params.size(); ++i) {
      params += (i != 0 ? " " : "") + (a.is_const[i] ? "(k " + a.params[i] + ")" : a.params[i]);
    }
    o << "| `" << a.name << "` | " << (a.is_pred ? "pred" : "analysis") << " | " << params << " | "
      << (!analyses.impl_known || analyses.impl.contains(a.name) ? "implemented" : "declared") << " | `"
      << (a.spec ? md_escape(expr_text(*a.spec)) : std::string{}) << "` |\n";
  }
  o << "\n## Guard and constant-expression builtins\n\n";
  for (const auto& b : rt::ex::kBuiltins) {
    o << "`" << b.name << "` ";
  }
  o << "\n";
  return o.str();
}

}  // namespace livehd::aegraph::rulegen
