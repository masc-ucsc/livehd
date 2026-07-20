//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_sim.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "prp_ast_facade.hpp"
#include "prpparse/lexer.hpp"
#include "prpparse/parser.hpp"
#include "prpparse/source_buffer.hpp"

namespace prp_sim {

namespace {

std::string slurp(const std::string& path) {
  std::ifstream      ifs(path, std::ios::binary);
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

// ---- DUT interface (read from the cgen_sim <unit>.hpp files in simdir) -------
struct Dut {
  std::string              hpp;      // header filename, e.g. "counter.counter.hpp"
  std::string              cls;      // struct name, e.g. "counter_counter"
  std::vector<std::string> inputs;   // In field names
  std::vector<std::string> outputs;  // Out field names
  std::vector<std::string> regs;     // struct-scope `Slop<N> name{}` members (flops/pipe stages/regs)
  std::vector<std::string> arrays;   // `std::array<Slop<N>, S> name{}` members (memories)
  std::vector<std::pair<std::string, std::string>> subs;  // sub-instance member -> struct class
  bool                     is_child = false;  // this unit's hpp is #included by another unit (a sub-instance)

  bool has_input(const std::string& f) const {
    return std::find(inputs.begin(), inputs.end(), f) != inputs.end();
  }
  bool has_output(const std::string& f) const {
    return std::find(outputs.begin(), outputs.end(), f) != outputs.end();
  }
  bool has_reg(const std::string& f) const { return std::find(regs.begin(), regs.end(), f) != regs.end(); }
};

// Parse a generated <unit>.hpp: the top struct name, its In{}/Out{} fields, and
// the struct-scope `Slop<N> name{}` members (flops/pipe stages/regs) so the
// testbench can peek/force internal state (`acc.total`).
bool parse_hpp(const std::string& path, Dut& d) {
  std::string text = slurp(path);
  if (text.empty()) {
    return false;
  }
  std::istringstream iss(text);
  std::string        line;
  int                in_block = 0;  // 0 none, 1 In, 2 Out
  bool               got_cls  = false;
  while (std::getline(iss, line)) {
    auto sw = line.find("struct ");
    if (sw != std::string::npos) {
      auto rest = line.substr(sw + 7);
      auto sp   = rest.find_first_of(" {");
      auto name = rest.substr(0, sp);
      if (name.find(';') != std::string::npos) {
        continue;  // a forward declaration (e.g. `struct Signal;`), not the DUT struct
      }
      if (name == "In") {
        in_block = 1;
        continue;
      }
      if (name == "Out") {
        in_block = 2;
        continue;
      }
      if (!got_cls) {
        d.cls   = name;
        got_cls = true;
      }
      continue;
    }
    if (in_block != 0) {
      size_t nb = line.find_first_not_of(" \t");
      if (nb != std::string::npos && line[nb] == '}') {  // a line that *starts* with '}' closes the struct
        in_block = 0;
        continue;
      }
      // a field line: `    Slop<N> field{};`
      auto br = line.find('>');
      if (br != std::string::npos) {
        auto after = line.substr(br + 1);
        // trim
        size_t b = after.find_first_not_of(" \t");
        if (b == std::string::npos) {
          continue;
        }
        size_t e = after.find_first_of("{ \t;", b);
        auto   f = after.substr(b, e - b);
        if (!f.empty()) {
          (in_block == 1 ? d.inputs : d.outputs).push_back(f);
        }
      }
    } else if (got_cls) {
      // Memory member `  std::array<Slop<N>, S> name{};  // memory` and
      // sub-instance member `  Cls name;  // sub instance` -- both feed the
      // hierarchical `acc.sub.state[idx]` read path.
      if (line.find("std::array<Slop<") != std::string::npos && line.find("// memory") != std::string::npos) {
        auto gt = line.rfind('>');
        auto b  = line.find_first_not_of(" \t", gt + 1);
        auto e  = line.find_first_of("{ \t;", b);
        if (b != std::string::npos && e != std::string::npos && e > b) {
          d.arrays.push_back(line.substr(b, e - b));
        }
        continue;
      }
      if (line.find("// sub instance") != std::string::npos) {
        std::istringstream ls(line);
        std::string        cls, nm;
        if (ls >> cls >> nm && !nm.empty()) {
          if (nm.back() == ';') {
            nm.pop_back();
          }
          d.subs.emplace_back(nm, cls);
        }
        continue;
      }
      // struct-scope scalar member, e.g. `  Slop<32> total{};  // flop`. (Memories
      // are `std::array<...>` and sub-instances/VCD state are other types, so the
      // `Slop<` prefix selects exactly the readable/forceable flop-style regs.)
      size_t nb = line.find_first_not_of(" \t");
      if (nb != std::string::npos && line.compare(nb, 5, "Slop<") == 0) {
        auto br = line.find('>');
        if (br != std::string::npos) {
          auto   after = line.substr(br + 1);
          size_t b     = after.find_first_not_of(" \t");
          if (b != std::string::npos) {
            size_t e = after.find_first_of("{ \t;", b);
            auto   f = after.substr(b, e - b);
            if (!f.empty()) {
              d.regs.push_back(f);
            }
          }
        }
      }
    }
  }
  return got_cls;
}

// DEBUG (PRP_SIM_DUMP=1): recursive s-expression dump of a test subtree.
void dump_node(const std::string& src, TSNode n, int depth) {
  if (ts_node_is_null(n)) {
    return;
  }
  std::string ind(static_cast<size_t>(depth) * 2, ' ');
  auto        s = ts_node_start_byte(n);
  auto        e = ts_node_end_byte(n);
  std::string txt = (e <= src.size() && s <= e) ? src.substr(s, e - s) : std::string{};
  auto        nl  = txt.find('\n');
  if (nl != std::string::npos) {
    txt = txt.substr(0, nl);
  }
  if (txt.size() > 36) {
    txt = txt.substr(0, 36);
  }
  std::fprintf(stderr, "%s[%s] '%s'\n", ind.c_str(), ts_node_type(n), txt.c_str());
  for (TSNode c : ts_node_named_children(n)) {
    dump_node(src, c, depth + 1);
  }
}

// mod short name from a unit header filename: "counter.counter.hpp" -> "counter".
std::string mod_of_hpp(const std::string& fname) {
  std::string base = fname.substr(0, fname.size() - 4);  // drop ".hpp"
  auto        dot  = base.rfind('.');
  return dot == std::string::npos ? base : base.substr(dot + 1);
}

// ---- AST helpers ------------------------------------------------------------
TSNode field(TSNode n, const char* name) {
  return ts_node_child_by_field_name(n, name, std::char_traits<char>::length(name));
}
std::string_view ntype(TSNode n) { return ts_node_type(n); }

std::string text_of(const std::string& src, TSNode n) {
  auto s = ts_node_start_byte(n);
  auto e = ts_node_end_byte(n);
  if (ts_node_is_null(n) || e > src.size() || s > e) {
    return {};
  }
  return src.substr(s, e - s);
}

// The bound name of an assignment lvalue: a plain `identifier`, or the inner
// identifier of a `typed_identifier` (`mut i:u8 = 0`).
std::string lvalue_name(const std::string& src, TSNode n) {
  if (ts_node_is_null(n)) {
    return {};
  }
  auto t = ntype(n);
  if (t == "identifier") {
    return text_of(src, n);
  }
  if (t == "typed_identifier") {
    for (TSNode c : ts_node_named_children(n)) {
      if (ntype(c) == "identifier") {
        return text_of(src, c);
      }
    }
  }
  return {};
}

std::string sanitize(std::string_view s) {
  std::string o;
  for (char c : s) {
    o += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
  }
  return o;
}

// Translation error (carried up via a thrown string).
struct Gen_error {
  std::string msg;
};
[[noreturn]] void fail(const std::string& m) { throw Gen_error{m}; }

// One `test name(params)` parameter. The generated driver exposes each as a
// `--<name> <value>` command-line flag, defaulting to `default_expr` (the
// signature's default) when present; a parameter with no default (or `=nil`)
// is required, and the driver reports a clear error if it is not supplied.
// Values are bound at RUN time (argv), never baked in, so one built driver can
// be re-run with different parameters.
struct Param {
  std::string name;
  bool        has_default{false};
  std::string default_expr;  // C++ expression for the default (valid iff has_default)
};

// A `test`'s parameter as it appears in the source: name, required (no default
// or `=nil`), and the default's AST node (null iff required). The single walker
// over the `test`'s arg_list; both the driver codegen (which translates the
// default to a C++ expression) and the parse-only `list_tests` JSON (which uses
// the default's source text) read it.
struct Param_raw {
  std::string name;
  bool        required{false};
  TSNode      default_node{};
};

std::vector<Param_raw> read_params_raw(const std::string& src, TSNode test) {
  std::vector<Param_raw> out;
  TSNode                 inp = field(test, "input");
  if (ts_node_is_null(inp)) {
    return out;
  }
  // arg_list named children are: typed_identifier [, default-expr], repeated.
  // A parameter with no default is immediately followed by the next param.
  Param_raw cur;
  bool      have = false;
  auto      flush = [&]() {
    if (!have) {
      return;
    }
    // `param:T=nil` declares an explicit NO default — same as omitting `=...`
    // (05b-statements.md): the value MUST be supplied at run time.
    cur.required = ts_node_is_null(cur.default_node) || text_of(src, cur.default_node) == "nil";
    out.push_back(cur);
    have = false;
  };
  for (TSNode ci : ts_node_named_children(inp)) {
    if (ntype(ci) == "typed_identifier") {
      flush();
      cur       = Param_raw{};
      TSNode id = field(ci, "identifier");
      cur.name  = ts_node_is_null(id) ? std::string{} : std::string(text_of(src, id));
      have      = true;
    } else if (have) {
      cur.default_node = ci;  // the parameter's default expression (next named child)
    }
  }
  flush();
  return out;
}

// Escape a string for a plain C++/JSON `"..."` literal (backslash, quote, and the
// control characters). Unlike c_str_lit (printf format), `%` is NOT doubled.
std::string cpp_str_lit(std::string_view s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"': o += "\\\""; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default: o += c;
    }
  }
  return o;
}

// Render the tests + parameters as a single-line JSON object — the canonical
// `--list-tests` payload. Shared by `lhd sim --list-tests` (parse-only) and the
// generated binary's embedded `--list-tests`, so both emit byte-identical JSON.
// (Defined as the public prp_sim::tests_to_json below; declared here so the
// in-namespace generate() can use it.)
std::string tests_to_json_impl(const std::string& file, const std::vector<Test_info>& tests) {
  std::string j = "{\"file\":\"";
  j += cpp_str_lit(file);
  j += "\",\"tests\":[";
  for (size_t ti = 0; ti < tests.size(); ++ti) {
    const auto& t = tests[ti];
    j += (ti != 0 ? ",{\"name\":\"" : "{\"name\":\"");
    j += cpp_str_lit(t.name);
    j += "\",\"params\":[";
    for (size_t pi = 0; pi < t.params.size(); ++pi) {
      const auto& p = t.params[pi];
      j += (pi != 0 ? ",{\"name\":\"" : "{\"name\":\"");
      j += cpp_str_lit(p.name);
      j += "\",\"required\":";
      j += (p.required ? "true" : "false");
      if (!p.required) {
        j += ",\"default\":\"";
        j += cpp_str_lit(p.default_text);
        j += "\"";
      }
      j += "}";
    }
    j += "]}";
  }
  j += "]}";
  return j;
}

// hlop's runtime PRNG default seed (hlop_random_seed()); the driver's `--seed`
// defaults here so an unseeded run matches hlop's historical behavior.
// kDefaultSeed is the C++ initializer (with the ULL suffix); kDefaultSeedShown
// is the friendlier value printed in `--help`.
constexpr const char* kDefaultSeed      = "0xC0FFEEULL";
constexpr const char* kDefaultSeedShown = "0xC0FFEE";

// A `test name(params)` parameter name is emitted VERBATIM into the generated
// driver as (a) a C++ storage variable `long <name>`, (b) the `--<name>` CLI
// flag, and (c) body references (the `tick`/call code reads it by name). So it
// must be a plain C++ identifier that collides with neither a C++ keyword nor
// any identifier the driver itself emits. Anything else (a backtick/`$`/Unicode
// Pyrope identifier, a C++ keyword like `default`, `main`'s `argc`/`argv`, or a
// driver-internal local) would silently miscompile or be shadowed — so reject
// it with a clear diagnostic at generation time. Leading-underscore names are
// rejected wholesale: every driver-internal local is `_`-prefixed, so this is a
// future-proof bar against collisions with locals added later.
bool is_reserved_param_name(std::string_view n) {
  // C++ keywords (C++23) — emitting `long <kw>` is a hard compile error.
  static const std::set<std::string_view> cpp_keywords = {
      "alignas",   "alignof",  "and",        "and_eq",    "asm",       "auto",      "bitand",    "bitor",
      "bool",      "break",    "case",       "catch",     "char",      "char8_t",   "char16_t",  "char32_t",
      "class",     "compl",    "concept",    "const",     "consteval", "constexpr", "constinit", "const_cast",
      "continue",  "co_await", "co_return",  "co_yield",  "decltype",  "default",   "delete",    "do",
      "double",    "dynamic_cast", "else",   "enum",      "explicit",  "export",    "extern",    "false",
      "float",     "for",      "friend",     "goto",      "if",        "inline",    "int",       "long",
      "mutable",   "namespace", "new",       "noexcept",  "not",       "not_eq",    "nullptr",   "operator",
      "or",        "or_eq",    "private",     "protected", "public",    "register",  "reinterpret_cast",
      "requires",  "return",   "short",      "signed",    "sizeof",    "static",    "static_assert",
      "static_cast", "struct", "switch",     "template",  "this",      "thread_local", "throw",  "true",
      "try",       "typedef",  "typeid",     "typename",  "union",     "unsigned",  "using",     "virtual",
      "void",      "volatile", "wchar_t",    "while",     "xor",       "xor_eq",
  };
  // Other identifiers the driver references at the same (function) scope but that
  // are NOT `_`-prefixed (so the leading-underscore bar in is_valid_param_name
  // does not catch them): `main`'s parameters, the reserved CLI flags
  // (`--test NAME`, `--seed`, `--help`/`-h` — a param named like one would be
  // swallowed by that flag instead of binding), the libc macros from <cerrno> (a
  // param named `errno` would expand `long errno = …` to `long (*__error()) = …`
  // — a hard error), and the free functions the driver calls unqualified. The
  // driver's own `_`-prefixed locals need no entry.
  static const std::set<std::string_view> driver_reserved = {
      "argc", "argv", "main", "seed", "help", "h", "test", "errno", "ERANGE", "hlop_set_random_seed",
  };
  return cpp_keywords.count(n) != 0 || driver_reserved.count(n) != 0;
}

// True iff `n` is safe to emit verbatim as a driver C++ identifier and CLI flag:
// a plain `[A-Za-z][A-Za-z0-9_]*` (no leading underscore — see above) that is
// not reserved.
bool is_valid_param_name(std::string_view n) {
  if (n.empty() || (std::isalpha(static_cast<unsigned char>(n[0])) == 0)) {
    return false;  // empty, leading underscore/digit, or a non-ASCII first char
  }
  for (char c : n) {
    if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
      return false;  // backtick, `$`, space, Unicode, etc.
    }
  }
  return !is_reserved_param_name(n);
}

class Driver_gen {
public:
  Driver_gen(const std::string& src, const std::map<std::string, Dut>& duts, const std::string& vcd_dir,
             const std::string& file)
      : src_(src), duts_(duts), vcd_dir_(vcd_dir), file_(file) {
    auto slash  = file_.find_last_of("/\\");
    file_short_ = (slash == std::string::npos) ? file_ : file_.substr(slash + 1);
    // Pre-scan file-scope import bindings (`const my_top = import("unit.lam")`):
    // the bound NAME references a DUT even when it is not a dut-map key (an lg=
    // rename makes the module/hpp name differ from the source lambda). Only such
    // a name may bind `mut acc = <name>` to the sole DUT -- scalars/params/literals
    // (e.g. `mut acc = base`, `mut v_final = nil`) must NOT.
    for (size_t p = 0; (p = src_.find("import", p)) != std::string::npos; p += 6) {
      if (p > 0 && (std::isalnum(static_cast<unsigned char>(src_[p - 1])) || src_[p - 1] == '_')) {
        continue;  // not a whole-word `import`
      }
      size_t q = p + 6;
      while (q < src_.size() && (src_[q] == ' ' || src_[q] == '\t')) {
        ++q;
      }
      if (q >= src_.size() || src_[q] != '(') {
        continue;  // not an import(...) call
      }
      long b = static_cast<long>(p) - 1;
      while (b >= 0 && (src_[b] == ' ' || src_[b] == '\t')) {
        --b;
      }
      if (b < 0 || src_[b] != '=') {
        continue;  // not `<name> = import(...)`
      }
      --b;
      while (b >= 0 && (src_[b] == ' ' || src_[b] == '\t')) {
        --b;
      }
      long e = b;
      while (b >= 0 && (std::isalnum(static_cast<unsigned char>(src_[b])) || src_[b] == '_')) {
        --b;
      }
      if (e > b) {
        const std::string bound = src_.substr(b + 1, e - b);
        import_bound_.insert(bound);
        // capture the quoted "unit.entry" path too: an lg=-renamed entry in a
        // MULTI-module closure resolves through it (dut key `<unit>_<entry>`)
        size_t qb = src_.find('"', q);
        if (qb != std::string::npos) {
          size_t qe = src_.find('"', qb + 1);
          if (qe != std::string::npos) {
            std::string path = src_.substr(qb + 1, qe - qb - 1);
            auto        dot  = path.find_last_of('.');
            if (dot != std::string::npos) {
              import_path_[bound] = {path.substr(0, dot), path.substr(dot + 1)};
            }
          }
        }
      }
    }
  }

  // Emit one test's run-function into the shared driver:
  //   static long <fn_id>(const std::map<std::string,std::string>& _args,
  //                       std::set<std::string>& _consumed, std::string& _err, _Fail& _ff);
  // It binds the test's parameters from `_args` (marking each consumed key, so
  // main() can warn about leftover `--key`s), runs the body, and returns the
  // count of failed asserts — or -1 with `_err` set when a required parameter is
  // missing. `name` is the dotted selector. `params_out` receives the test's
  // parameter list (for the registry JSON + the kernel's `--arg` forwarding);
  // `includes_out` collects the DUT header(s) this test drives. Throws Gen_error
  // on an unsupported form.
  std::string emit_run_fn(TSNode test, const std::string& name, const std::string& fn_id,
                          std::vector<Param_info>& params_out, std::set<std::string>& includes_out) {
    TSNode code = field(test, "code");
    if (ts_node_is_null(code)) {
      fail("test '" + name + "' has no body");
    }
    // collect body statements
    std::vector<TSNode> stmts;
    for (TSNode c : ts_node_named_children(code)) {
      stmts.push_back(c);
    }
    // pass 1: discover scalar locals (every assigned lvalue identifier) and the
    // DUT instances used (a `mut acc = Module` declaration).
    discover(stmts);

    // Parameters: name + default. A name is emitted verbatim as a driver C++
    // identifier (see is_valid_param_name); reject anything unsafe — a
    // backtick/`$`/Unicode identifier, a C++ keyword (`default`, `class`, …), a
    // reserved flag (`seed`/`help`/`h`), `main`'s `argc`/`argv`, or a
    // leading-underscore name that could shadow a driver-internal local — with a
    // clear message rather than silently miscompiling the generated driver.
    std::vector<Param> params;
    for (const auto& r : read_params_raw(src_, test)) {
      if (!is_valid_param_name(r.name)) {
        fail("test parameter '" + r.name
             + "' is not a usable simulation parameter name: it must be a plain identifier "
               "(a letter followed by letters/digits/underscores), not a leading-underscore name, "
               "a C++ keyword, or a reserved driver flag (seed/help/h/argc/argv) — rename it");
      }
      param_names_.insert(r.name);  // for the tick clock-name collision check
      Param p;
      p.name        = r.name;
      p.has_default = !r.required;
      if (p.has_default) {
        p.default_expr = expr(r.default_node);  // the parameter's default expression (C++)
      }
      params.push_back(p);
      Param_info pi;
      pi.name     = r.name;
      pi.required = r.required;
      if (!r.required) {
        pi.default_text = text_of(src_, r.default_node);  // source text (for --list-tests JSON)
      }
      params_out.push_back(std::move(pi));
    }

    std::ostringstream o;
    o << "// ---- test `" << name << "` ----\n";
    o << "static long " << fn_id
      << "(const std::map<std::string, std::string>& _args, std::set<std::string>& _consumed, std::string& _err, "
         "[[maybe_unused]] _Fail& _ff) {\n";
    o << "  long _fails = 0;\n";
    // `_clk` tracks the current cycle (the active tick loop variable) so a located
    // assert can report it even AFTER the tick loop (e.g. a `wait`-timeout assert),
    // where the loop variable is out of C++ scope. -1 means "before any clock edge".
    o << "  [[maybe_unused]] long _clk = -1;\n";

    // ---- Parameters bound from the shared `_args` map (`--<name> N`). A
    // parameter defaults to its signature default; one with no default (or
    // `=nil`) is required — when absent, the test reports a clear error rather
    // than running with a silent 0. Each consumed `--key` is recorded so main()
    // can warn about a `--key` that no test uses.
    for (const auto& p : params) {
      locals_.erase(p.name);  // bound as a parameter, not a zero-init body local
      o << "  long " << p.name << " = " << (p.has_default ? p.default_expr : "0") << ";\n";
      o << "  { auto _it = _args.find(\"" << cpp_str_lit(p.name) << "\");\n";
      o << "    if (_it != _args.end()) { " << p.name << " = _to_i64(\"" << cpp_str_lit(p.name)
        << "\", _it->second); _consumed.insert(\"" << cpp_str_lit(p.name) << "\"); }\n";
      if (!p.has_default) {
        o << "    else { _err = \"test `" << cpp_str_lit(name) << "` requires --" << cpp_str_lit(p.name)
          << " <value>\"; return -1; }\n";
      }
      o << "  }\n";
    }

    // ---- DUT instances. One persistent instance per `mut acc = Module`
    // declaration (the seed is set once by main() before any test runs).
    for (const auto& [var, m] : inst_of_var) {
      includes_out.insert(duts_.at(m).hpp);
      o << "  " << duts_.at(m).cls << " " << var << "; " << var << ".reset_cycle();\n";
      if (!vcd_dir_.empty()) {
        // one VCD per test: <vcd_dir>/<test>.vcd (suffixed by instance when >1).
        // Stash the path; set it immediately for a whole-run trace, but for a
        // `--vcd-from`/`--vcd-to` window the tick loop sets it at vcd_from instead.
        std::string vf = vcd_dir_ + "/" + name + (inst_of_var.size() > 1 ? ("." + var) : std::string{}) + ".vcd";
        o << "  std::string _vcdp_" << var << " = \"" << cpp_str_lit(vf) << "\";\n";
        // Whole-run trace only for a plain `sim.vcd` (no window, not on-fail mode);
        // a window sets the path at vcd_from, on-fail sets it only on the re-run.
        o << "  if (_ckpt.vcd_from < 0 && !_ckpt.vcd_on_fail) " << var << ".__vcd_path = _vcdp_" << var << ";\n";
      }
    }
    for (const auto& v : locals_) {
      o << "  long " << v << " = 0;\n";
    }
    for (const auto& [aname, elems] : arrays_) {
      // Classify: any element wider than 64 bits flips the WHOLE array onto the
      // string plane (`const char*[]`), consumed exactly by the wide
      // __prp_poke overload; otherwise stay on the long plane, with
      // above-int64 elements rendered bit-preserving ((long)0x…ULL — the poke
      // zero-extends raw low bits, so any width <= 64 drives exact).
      bool wide = false;
      for (const auto& e : elems) {
        unsigned long long v = 0;
        if (int_literal_class(e, v) == 2) {
          wide = true;
          break;
        }
      }
      if (wide) {
        o << "  static const char* " << aname << "[] = {";
        for (size_t i = 0; i < elems.size(); ++i) {
          unsigned long long v = 0;
          if (int_literal_class(elems[i], v) < 0) {
            fail("array '" + aname + "' mixes a >64-bit constant with a non-literal element: " + elems[i]);
          }
          o << (i != 0 ? ", " : "") << '"' << elems[i] << '"';
        }
        o << "};\n";
        continue;
      }
      o << "  long " << aname << "[] = {";
      for (size_t i = 0; i < elems.size(); ++i) {
        unsigned long long v  = 0;
        const int          cl = int_literal_class(elems[i], v);
        std::string        e  = elems[i];
        if (cl == 1) {
          char buf[32];
          std::snprintf(buf, sizeof buf, "(long)0x%llxULL", v);
          e = buf;
        }
        o << (i != 0 ? ", " : "") << e;
      }
      o << "};\n";
    }
    // ---- checkpoint cadence scaffolding (only meaningful with a DUT instance):
    // periodic fork-checkpoints of the DUT state + this testbench frame are
    // written under <dir>/<test>/ckp<cycle>/ from inside the tick loop.
    // --list-signals: enumerate the observable signals after reset, then return (no
    // body run) — the agent learns the --probe/--break-when vocabulary. Hoisted out
    // of the instance guard so a test with no DUT instance also short-circuits
    // (describing nothing) instead of running the whole testbench.
    o << "  if (_dbg.list_signals) {\n";
    for (const auto& [var, m] : inst_of_var) {
      o << "    " << var << ".describe_signals(\"" << var << ".\", _dbg.sigs);\n";
    }
    o << "    return _fails;\n  }\n";
    if (!inst_of_var.empty()) {
      o << "  hlop::ckpt::Cadence _cad; _cad.init(_ckpt.enabled, _ckpt.min_secs, _ckpt.max_overhead);\n";
      o << "  std::string _ckpt_base = _ckpt.dir.empty() ? std::string() : (_ckpt.dir + \"/" << sanitize(name)
        << "\");\n";
      // No upfront mkdir: the first due checkpoint's make_dirs(_cdir) creates the
      // base recursively, so a short run that never checkpoints leaves no dirs.
      // Validate the requested --probe / --break-when signal names against the real
      // signal set ONCE (a typo would otherwise read as a silent 0). Warn, don't fail.
      o << "  if (_dbg.active()) {\n";
      o << "    std::vector<hlop::ckpt::Signal> _av; std::set<std::string> _known;\n";
      for (const auto& [var, m] : inst_of_var) {
        o << "    " << var << ".describe_signals(\"" << var << ".\", _av);\n";
      }
      o << "    for (const auto& _s : _av) _known.insert(_s.name);\n";
      o << "    for (const auto& _p : _dbg.probe) if (!_known.count(_p)) std::fprintf(stderr, \"lhd sim: warning: "
           "--probe signal '%s' is not observable (see --list-signals)\\n\", _p.c_str());\n";
      o << "    if (_dbg.has_break && !_known.count(_dbg.b_lhs)) std::fprintf(stderr, \"lhd sim: warning: --break-when "
           "signal '%s' is not observable (see --list-signals)\\n\", _dbg.b_lhs.c_str());\n";
      o << "    if (_dbg.has_break && _dbg.b_rhs_is_sig && !_known.count(_dbg.b_rhs)) std::fprintf(stderr, \"lhd sim: "
           "warning: --break-when signal '%s' is not observable (see --list-signals)\\n\", _dbg.b_rhs.c_str());\n";
      o << "  }\n";
    }
    for (auto s : stmts) {
      gen_stmt(o, s, 1);
    }
    // The function's stdout is the test's runtime output (puts + any ASSERT
    // FAILED lines); the returned count is the verdict main() renders.
    o << "  return _fails;\n}\n";
    return o.str();
  }

private:
  // Escape a string for embedding inside a generated C `"..."` printf format
  // literal: backslash/quote/newline/tab plus `%` (doubled, since these strings
  // are printf format arguments).
  static std::string c_str_lit(std::string_view s) {
    std::string o;
    for (char c : s) {
      switch (c) {
        case '\\': o += "\\\\"; break;
        case '"': o += "\\\""; break;
        case '%': o += "%%"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        default: o += c;
      }
    }
    return o;
  }

  const std::string&                src_;
  const std::map<std::string, Dut>& duts_;
  std::string                       vcd_dir_;     // empty = no VCD; else <vcd_dir>/<test>.vcd per test
  std::string                       file_;        // source .prp path (for failing_assert file:line)
  std::string                       file_short_;  // basename of file_ (the inline located message)
  bool                              in_tick_ = false;  // inside a tick body (reject nested ticks)
  bool                              restart_block_emitted_ = false;  // --restart-at handled by the first tick
  std::set<std::string>                           locals_;      // scalar driver vars
  std::set<std::string>                           param_names_; // test parameter names
  std::map<std::string, std::string>              inst_of_var;  // instance var -> module name (`mut acc = M`)
  std::map<std::string, std::vector<std::string>> arrays_;      // array name -> element exprs
  std::set<std::string>                           import_bound_;  // names bound by `= import(...)` (module refs)
  std::map<std::string, std::pair<std::string, std::string>> import_path_;  // bound name -> {unit, entry}

  // `mut acc = Module` -- the rvalue is a bare module name. Unwrap single-child
  // expression wrappers and return the module name iff it is a known DUT.
  std::string rvalue_module(TSNode rv) const {
    while (!ts_node_is_null(rv)) {
      auto t = ntype(rv);
      if (t == "identifier") {
        std::string nm = text_of(src_, rv);
        if (duts_.count(nm) != 0) {
          return nm;
        }
        // lg= rename: the imported pub lambda name (e.g. `my_top`) differs from
        // the sole generated module/hpp (`chip_top`), so `mut acc = my_top` names
        // no dut key. Bind an IMPORT-BOUND name to the sole DUT; a scalar/param/
        // literal (`mut acc = base`, `mut v_final = nil`) is never import-bound.
        if (import_bound_.count(nm)) {
          // an lg=-renamed entry emits its module as `<unit>_<entry>` (or the
          // lg value directly matching the entry) -- try the import path first
          if (auto it = import_path_.find(nm); it != import_path_.end()) {
            const auto& [unit, entry] = it->second;
            if (duts_.count(entry) != 0) {
              return entry;
            }
            if (const std::string joined = unit + "_" + entry; duts_.count(joined) != 0) {
              return joined;
            }
          }
          if (duts_.size() == 1) {
            return duts_.begin()->first;
          }
          // multi-module closure: the tb imports the TOP entry -> map the
          // bound name to the unique ROOT unit (not #included by any other)
          std::string root;
          int         nroots = 0;
          for (const auto& [k, d] : duts_) {
            if (!d.is_child) {
              root = k;
              ++nroots;
            }
          }
          if (nroots == 1) {
            return root;
          }
        }
        return {};
      }
      if ((t == "expression_item" || t == "paren_group") && ts_node_named_child_count(rv) == 1) {
        rv = ts_node_named_child(rv, 0);
        continue;
      }
      return {};
    }
    return {};
  }

  // A dotted instance reference `acc.field`: sets base/fld and returns true iff
  // `base` is a declared instance.
  bool inst_dot(TSNode n, std::string& base, std::string& fld) const {
    if (ts_node_is_null(n) || ntype(n) != "dot_expression" || ts_node_named_child_count(n) < 2) {
      return false;
    }
    base = text_of(src_, ts_node_named_child(n, 0));
    fld  = text_of(src_, ts_node_named_child(n, 1));
    return inst_of_var.count(base) != 0;
  }

  // C++ accessor for `acc.fld`: __in.fld (input), __out.fld (output), or fld
  // (struct-scope reg). `write` rejects read-only targets.
  std::string field_access(const std::string& var, const std::string& fld, bool write) {
    if (fld.find('.') != std::string::npos) {
      // Hierarchical state path `acc.sub[.sub...].leaf[idx]` (READ-only):
      // each intermediate segment names a sub-instance member of the current
      // module; the leaf resolves to a flop member or a memory array (its
      // optional [index] is emitted verbatim as C++). The generated memory
      // member often loses its RTL name (`reg regs:[32]u64` -> `memory_60`),
      // so with exactly ONE array in the leaf module any indexed name
      // aliases to it.
      if (write) {
        fail("cannot poke hierarchical path '" + var + "." + fld + "' (read-only)");
      }
      const Dut*  hd   = &duts_.at(inst_of_var.at(var));
      std::string cxx  = var;
      std::string rest = fld;
      while (true) {
        auto        dot = rest.find('.');
        std::string seg = dot == std::string::npos ? rest : rest.substr(0, dot);
        if (dot == std::string::npos) {
          std::string idx, name = seg;
          auto        lb = seg.find('[');
          if (lb != std::string::npos && !seg.empty() && seg.back() == ']') {
            name = seg.substr(0, lb);
            idx  = seg.substr(lb + 1, seg.size() - lb - 2);
          }
          if (idx.empty() && hd->has_reg(name)) {
            return cxx + "." + name;
          }
          if (std::find(hd->arrays.begin(), hd->arrays.end(), name) != hd->arrays.end()) {
            return cxx + "." + name + "[" + idx + "]";
          }
          if (!idx.empty() && hd->arrays.size() == 1) {
            return cxx + "." + hd->arrays.front() + "[" + idx + "]";  // RTL-name alias
          }
          fail("unknown state '" + name + "' in hierarchical path '" + var + "." + fld + "'");
        }
        const std::string* cls = nullptr;
        for (const auto& [m, c] : hd->subs) {
          if (m == seg) {
            cls = &c;
            break;
          }
        }
        if (cls == nullptr) {
          fail("unknown sub-instance '" + seg + "' in hierarchical path '" + var + "." + fld + "'");
        }
        const Dut* nd = nullptr;
        for (const auto& [u, dd] : duts_) {
          if (dd.cls == *cls) {
            nd = &dd;
            break;
          }
        }
        if (nd == nullptr) {
          fail("no generated unit for sub-instance '" + seg + "' (class " + *cls + ")");
        }
        cxx += "." + seg;
        hd   = nd;
        rest = rest.substr(dot + 1);
      }
    }
    const Dut& d = duts_.at(inst_of_var.at(var));
    if (d.has_input(fld)) {
      return var + ".__in." + fld;
    }
    if (d.has_output(fld)) {
      if (write) {
        fail("cannot poke output '" + var + "." + fld + "' (outputs are read-only)");
      }
      // recompute the output from the current committed state (correct before the
      // first step, and after a poke without a step, too -- peek has no net effect)
      return var + ".peek(" + var + ".__in)." + fld;
    }
    if (d.has_reg(fld)) {
      return var + "." + fld;
    }
    fail("unknown field '" + fld + "' on instance '" + var + "' of module '" + inst_of_var.at(var) + "'");
    return {};
  }

  // Resolve a peek()/poke() hierarchical string path `"<unit>/<field>"` to a
  // field_access on the matching DUT instance (09-verification.md). `<unit>`
  // matches an instance-variable name (`mut dut = cnt` -> "dut") or a module name
  // ("cnt"); one "unit/field" level is supported -- the common testbench probe.
  std::string path_target(TSNode call, bool write) {
    TSNode args = field(call, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      fail("peek/poke needs a \"unit/field\" path");
    }
    std::string path = text_of(src_, ts_node_named_child(args, 0));
    if (path.size() >= 2 && path.front() == '"') {
      path = path.substr(1, path.size() - 2);
    }
    auto slash = path.find('/');
    if (slash == std::string::npos) {
      fail("peek/poke path '" + path + "' must be \"unit/field\"");
    }
    std::string unit = path.substr(0, slash);
    std::string fld  = path.substr(slash + 1);
    std::string var;
    if (inst_of_var.count(unit)) {
      var = unit;  // path used the instance-variable name directly
    } else {
      for (const auto& [v, m] : inst_of_var) {
        if (m == unit) {
          var = v;  // path used the module name -> its (sole) instance
          break;
        }
      }
    }
    if (var.empty()) {
      fail("peek/poke: no DUT instance for path '" + path + "'");
    }
    return field_access(var, fld, write);
  }

  // C++ (long) expression for a `{name}` puts/print interpolation: a dotted
  // `acc.field` resolves to the instance-field peek; anything else is a plain
  // in-scope value (a local or the `clock` loop var).
  std::string interp_value(const std::string& name) {
    bool is_slop = false;
    auto e       = interp_expr(name, is_slop);
    return is_slop ? e + ".to_just_i64()" : "(long)" + e;
  }

  // C++ expression for a `{name}` interpolation. A dotted `acc.field` (or a
  // hierarchical `acc.sub.state[i]`) resolves to the instance-field peek — a
  // Slop of the field's OWN width (is_slop=true; render it with
  // Slop::to_decimal/to_hex/to_binary, exact at ANY width — never through a
  // 64-bit truncation). Anything else is a plain in-scope long.
  std::string interp_expr(const std::string& name, bool& is_slop) {
    is_slop  = false;
    auto dot = name.find('.');
    if (dot != std::string::npos) {
      std::string base = name.substr(0, dot), fld = name.substr(dot + 1);
      if (inst_of_var.count(base) != 0) {
        is_slop = true;
        return field_access(base, fld, /*write=*/false);
      }
    }
    return "(" + name + ")";
  }

  void discover(const std::vector<TSNode>& stmts) {
    for (auto s : stmts) {
      discover_node(s);
    }
  }
  void discover_node(TSNode n) {
    if (ts_node_is_null(n)) {
      return;
    }
    auto t = ntype(n);
    if (t == "tick_statement") {
      // Only the count and the body carry test locals / instances. The
      // `clocks=(name=ratio)` clause holds signal-name labels, not test
      // variables -- don't collect them as driver locals.
      discover_node(field(n, "value"));
      discover_node(field(n, "code"));
      return;
    }
    if (t == "assignment") {
      // A dotted lvalue (`acc.field = v`) is a poke, not a new local; lvalue_name
      // returns {} for it, so it is naturally ignored below.
      std::string ln = lvalue_name(src_, field(n, "lvalue"));
      TSNode      rv = field(n, "rvalue");
      if (!ln.empty()) {
        std::string m = ts_node_is_null(rv) ? std::string{} : rvalue_module(rv);
        if (!ts_node_is_null(rv) && ntype(rv) == "tuple_sq") {
          std::vector<std::string> elems;
          for (TSNode c : ts_node_named_children(rv)) {
            elems.push_back(array_elem(c));
          }
          arrays_[ln] = elems;
        } else if (!m.empty()) {
          inst_of_var[ln] = m;  // `mut acc = Module` -> a persistent instance
        } else {
          locals_.insert(ln);
        }
      }
    }
    for (TSNode c : ts_node_named_children(n)) {
      discover_node(c);
    }
  }

  // ---- expression -> C++ (int64-valued) -------------------------------------
  // Lower a Pyrope integer literal onto the driver's `long` value plane.
  // Pyrope integers are arbitrary precision, C++ `long` is not: a decimal/hex
  // constant above LLONG_MAX (e.g. an auto-generated formalfail testbench
  // driving a witness value like 18446744073709551712 on a u64 bus) is not a
  // valid C++ `long` literal and used to break the driver COMPILE. Values that
  // fit in uint64 are emitted as a bit-preserving `(long)0x…ULL` — exact for
  // any poke target of width <= 64 (Slop masks to the port width). Anything
  // wider than 64 bits cannot ride the long plane at all: fail LOUDLY at
  // generation instead of emitting uncompilable or silently-wrong C++.
  std::string cpp_int_literal(std::string lit) {
    unsigned long long v  = 0;
    const int          cl = int_literal_class(lit, v);
    if (cl <= 0) {
      return lit;  // fits int64 verbatim, or a spelling we leave untouched
    }
    if (cl == 2) {
      fail("testbench integer constant does not fit 64 bits in an expression (drive it via a poke, whose string "
           "plane is exact at any width): "
           + lit);
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "(long)0x%llxULL", v);
    return buf;
  }

  // An ARRAY element keeps a plain integer literal VERBATIM (no long-plane
  // rendering): the array emitter classifies the whole array afterwards — a
  // 64-bit-representable array stays `long[]`, one with any wider element is
  // emitted as decimal strings and consumed by the wide __prp_poke overload.
  std::string array_elem(TSNode n) {
    TSNode lit = n;
    if (ntype(lit) == "constant") {
      for (TSNode c : ts_node_named_children(lit)) {
        if (ntype(c) == "integer_literal") {
          lit = c;
          break;
        }
      }
    }
    if (ntype(lit) == "integer_literal") {
      return strip_sep(text_of(src_, lit));
    }
    return expr(n);
  }

  // Raw decimal/hex literal -> value class: 0 = fits int64 (verbatim is valid
  // C++), 1 = fits uint64 (needs the bit-preserving ULL rendering), 2 = wider
  // than 64 bits (only the string/from_pyrope plane can carry it), -1 = not a
  // plain parseable literal.
  static int int_literal_class(const std::string& lit, unsigned long long& v) {
    if (lit.empty()) {
      return -1;
    }
    errno     = 0;
    char* end = nullptr;
    if (lit.size() > 2 && lit[0] == '0' && (lit[1] == 'x' || lit[1] == 'X')) {
      v = std::strtoull(lit.c_str() + 2, &end, 16);
    } else if (std::isdigit(static_cast<unsigned char>(lit[0])) != 0) {
      v = std::strtoull(lit.c_str(), &end, 10);
    } else {
      return -1;
    }
    if (end == nullptr || *end != '\0') {
      return -1;
    }
    if (errno == ERANGE) {
      return 2;
    }
    return v <= static_cast<unsigned long long>(std::numeric_limits<long long>::max()) ? 0 : 1;
  }

  std::string expr(TSNode n) {
    auto t = ntype(n);
    if (t == "identifier") {
      std::string nm = text_of(src_, n);
      if (nm == "nil") {
        return "0";
      }
      return nm;
    }
    if (t == "constant") {
      // wraps integer_literal / bool_literal / string
      for (TSNode c : ts_node_named_children(n)) {
        auto ct = ntype(c);
        if (ct == "integer_literal") {
          return cpp_int_literal(strip_sep(text_of(src_, c)));
        }
        if (ct == "bool_literal") {
          return text_of(src_, c) == "true" ? "1" : "0";
        }
      }
      return text_of(src_, n);
    }
    if (t == "integer_literal") {
      return cpp_int_literal(strip_sep(text_of(src_, n)));
    }
    if (t == "bool_literal") {
      return text_of(src_, n) == "true" ? "1" : "0";
    }
    if (t == "expression_item" || t == "paren_group") {
      return expr_seq(n);
    }
    if (t == "unary_expression") {
      std::string op;
      std::string opnd;
      for (TSNode c : ts_node_named_children(n)) {
        auto ct = ntype(c);
        // prpparse unary operator node kinds (see prp2lnast.cpp): `!`/`not` ->
        // op_log_not, `~` -> op_bit_not, unary `-` -> op_unary_minus. Matching the
        // wrong names silently DROPS the operator -> a corrupted guard (e.g.
        // `assert(-x == 6)` would test `x == 6`), so this must track prpparse.
        if (ct == "op_log_not" || ct == "op_bit_not" || ct == "op_unary_minus") {
          op = map_op(text_of(src_, c));
        } else {
          opnd = expr(c);
        }
      }
      return "(" + op + opnd + ")";
    }
    if (t == "function_call_expression") {
      TSNode fn = field(n, "function");
      if (!ts_node_is_null(fn) && text_of(src_, fn) == "peek") {  // peek("unit/field")
        return "(" + path_target(n, /*write=*/false) + ".to_just_i64())";
      }
    }
    if (t == "dot_expression") {
      std::string base, fld;
      if (inst_dot(n, base, fld)) {  // peek `acc.field` (output / reg / input)
        return "(" + field_access(base, fld, /*write=*/false) + ".to_just_i64())";
      }
      fail("unsupported dot expression: " + text_of(src_, n).substr(0, 40));
    }
    if (t == "member_selection") {
      // base[index]: base identifier + `select` ([idx])
      TSNode base = ts_node_named_child(n, 0);
      std::string idx;
      uint32_t    nc = ts_node_named_child_count(n);
      for (uint32_t i = 1; i < nc; ++i) {
        TSNode c = ts_node_named_child(n, i);
        if (ntype(c) == "select") {
          for (TSNode sc : ts_node_named_children(c)) {
            idx = expr(sc);
          }
        }
      }
      return expr(base) + "[" + idx + "]";
    }
    // a binary chain rendered as a flat sequence of operands/operators
    return expr_seq(n);
  }

  // Render a node whose named children are [operand, op, operand, ...] or a
  // single wrapped expression.
  std::string expr_seq(TSNode n) {
    uint32_t nc = ts_node_named_child_count(n);
    if (nc == 1) {
      return expr(ts_node_named_child(n, 0));
    }
    // `a implies b` has no infix C++ spelling, so it cannot ride the
    // operand/operator loop below — it needs the LHS negated, which means
    // knowing the operator BEFORE emitting the operands. Rewrite the whole
    // sequence as `(!(a) || (b))`.
    //
    // It used to fall through map_op() unchanged and be emitted VERBATIM into
    // the driver, so a test block using the implies form (the spelling the
    // dual sim+BMC fixture convention uses on the formal side) produced
    // uncompilable C++ and a bare "expected ')'" from the host compiler
    // (2f-latch M6). The two planes should not need different spellings of the
    // same property.
    {
      std::vector<TSNode> parts;
      int                 implies_at = -1;
      for (TSNode c : ts_node_named_children(n)) {
        auto t = ntype(c);
        const bool is_op = t == "binary_compare_op" || t == "binary_other_op" || t == "binary_times_op"
                           || t == "binary_logical_op" || t == "binary_step_op";
        if (is_op && text_of(src_, c) == "implies") {
          implies_at = static_cast<int>(parts.size());
          continue;
        }
        if (is_op) {
          parts.clear();       // a mixed chain: not a simple `a implies b`
          implies_at = -1;
          break;
        }
        parts.push_back(c);
      }
      if (implies_at == 1 && parts.size() == 2) {
        return "(!(" + expr(parts[0]) + ") || (" + expr(parts[1]) + "))";
      }
    }
    std::string out = "(";
    for (TSNode c : ts_node_named_children(n)) {
      auto t = ntype(c);
      if (t == "binary_compare_op" || t == "binary_other_op" || t == "binary_times_op" || t == "binary_logical_op"
          || t == "binary_step_op") {
        out += " " + map_op(text_of(src_, c)) + " ";
      } else if (t == "op_log_not" || t == "op_bit_not" || t == "op_unary_minus") {
        out += map_op(text_of(src_, c));
      } else {
        out += expr(c);
      }
    }
    out += ")";
    return out;
  }

  static std::string map_op(std::string_view s) {
    if (s == "and") {
      return "&&";
    }
    if (s == "or") {
      return "||";
    }
    if (s == "not") {
      return "!";
    }
    return std::string{s};  // == != < > <= >= + - * & | ^
  }

  // ---- located-assert helpers -----------------------------------------------
  // 1-based source line of a node (counted from byte offset, so it needs no
  // tree-sitter point API — keeps this independent of the facade shim).
  int line_of(TSNode n) const {
    if (ts_node_is_null(n)) {
      return 0;
    }
    uint32_t off  = ts_node_start_byte(n);
    int      line = 1;
    for (uint32_t i = 0; i < off && i < src_.size(); ++i) {
      if (src_[i] == '\n') {
        ++line;
      }
    }
    return line;
  }
  static std::string trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) {
      return {};
    }
    size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
  }
  // A bare literal operand (`30`, `0xff`, `true`) prints its own value, so the
  // located message shows just the source (`v_final=10 == 30`, not `30=30`).
  static bool is_literal_operand(const std::string& s) {
    if (s == "true" || s == "false") {
      return true;
    }
    size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
      ++i;
    }
    if (i >= s.size()) {
      return false;
    }
    if (i + 2 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
      for (size_t j = i + 2; j < s.size(); ++j) {
        if (s[j] != '_' && std::isxdigit(static_cast<unsigned char>(s[j])) == 0) {  // allow digit separators
          return false;
        }
      }
      return true;
    }
    for (; i < s.size(); ++i) {
      if (s[i] != '_' && std::isdigit(static_cast<unsigned char>(s[i])) == 0) {  // allow digit separators
        return false;
      }
    }
    return true;
  }
  // Pyrope integer literals allow `_` digit separators (`1_000`, `0xFF_00`), which
  // are NOT valid C++ — strip them before emitting the literal into the driver.
  static std::string strip_sep(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
    return s;
  }
  // Unwrap single-child `expression_item`/`paren_group` wrappers to the operative node.
  TSNode unwrap(TSNode n) const {
    while (!ts_node_is_null(n)) {
      auto t = ntype(n);
      if ((t == "expression_item" || t == "paren_group") && ts_node_named_child_count(n) == 1) {
        n = ts_node_named_child(n, 0);
        continue;
      }
      break;
    }
    return n;
  }
  // Render named children [b,e) of `parent` as a C++ (long) sub-expression — the
  // operand-range half of expr_seq, so `acc.total` / `cycles - 2` each render on
  // their own side of a top-level comparison.
  std::string expr_range(TSNode parent, uint32_t b, uint32_t e) {
    std::vector<TSNode> kids;  // collected once: per-index ts_node_named_child rescans would be O(kids^2)
    for (TSNode k : ts_node_named_children(parent)) {
      kids.push_back(k);
    }
    if (e - b == 1) {
      return expr(kids[b]);
    }
    std::string out = "(";
    for (uint32_t i = b; i < e; ++i) {
      TSNode c = kids[i];
      auto   t = ntype(c);
      if (t == "binary_compare_op" || t == "binary_other_op" || t == "binary_times_op" || t == "binary_logical_op"
          || t == "binary_step_op") {
        out += " " + map_op(text_of(src_, c)) + " ";
      } else if (t == "op_log_not" || t == "op_bit_not" || t == "op_unary_minus") {
        out += map_op(text_of(src_, c));
      } else {
        out += expr(c);
      }
    }
    out += ")";
    return out;
  }
  // Source text spanning named children [b,e).
  std::string src_range(TSNode parent, uint32_t b, uint32_t e) const {
    TSNode first = ts_node_named_child(parent, b);
    TSNode last  = ts_node_named_child(parent, e - 1);
    auto   s     = ts_node_start_byte(first);
    auto   en    = ts_node_end_byte(last);
    if (en > src_.size() || s > en) {
      return {};
    }
    return src_.substr(s, en - s);
  }
  // If `cond` is a single top-level comparison (`lhs <cmp> rhs`), split it so the
  // assert can print both operand values. Returns false for a bare boolean, a
  // logical-combined condition (`a and b`), or a chained compare — those fall
  // back to printing the whole condition source.
  bool decompose_compare(TSNode cond, std::string& lhs_cpp, std::string& lhs_src, std::string& op,
                         std::string& rhs_cpp, std::string& rhs_src) {
    TSNode   u  = unwrap(cond);
    uint32_t nc = ts_node_named_child_count(u);
    if (nc < 3) {
      return false;
    }
    int      k = -1, cnt = 0;
    uint32_t i = 0;  // ordinal of the current named child (k records the compare-op position)
    for (TSNode c : ts_node_named_children(u)) {
      if (ntype(c) == "binary_compare_op") {
        k = static_cast<int>(i);
        ++cnt;
      }
      ++i;
    }
    if (cnt != 1 || k <= 0 || k >= static_cast<int>(nc) - 1) {
      return false;
    }
    op      = text_of(src_, ts_node_named_child(u, static_cast<uint32_t>(k)));
    lhs_cpp = expr_range(u, 0, static_cast<uint32_t>(k));
    lhs_src = trim(src_range(u, 0, static_cast<uint32_t>(k)));
    rhs_cpp = expr_range(u, static_cast<uint32_t>(k) + 1, nc);
    rhs_src = trim(src_range(u, static_cast<uint32_t>(k) + 1, nc));
    return true;
  }

  // ---- statements -----------------------------------------------------------
  void gen_stmt(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    auto        t = ntype(n);
    if (t == "comment") {
      return;
    }
    if (t == "assignment") {
      gen_assignment(o, n, depth);
      return;
    }
    if (t == "tick_statement") {
      gen_tick(o, n, depth);
      return;
    }
    if (t == "step_statement") {
      // advance every declared instance one clock; `step N` advances N edges.
      if (inst_of_var.empty()) {
        fail("`step` with no instance declared (use `mut acc = Module` first)");
      }
      TSNode cnt = field(n, "value");
      if (ts_node_is_null(cnt)) {
        for (const auto& [var, m] : inst_of_var) {
          o << ind << var << ".step();\n";
        }
      } else {
        o << ind << "for (long _s = 0; _s < (long)(" << expr(cnt) << "); ++_s) {\n";
        for (const auto& [var, m] : inst_of_var) {
          o << ind << "  " << var << ".step();\n";
        }
        o << ind << "}\n";
      }
      return;
    }
    if (t == "if_expression") {
      gen_if(o, n, depth);
      return;
    }
    if (t == "control_statement") {
      for (TSNode c : ts_node_named_children(n)) {
        auto ct = ntype(c);
        if (ct == "break_statement") {
          o << ind << "break;\n";
          return;
        }
        if (ct == "continue_statement") {
          o << ind << "continue;\n";
          return;
        }
      }
      return;
    }
    if (t == "function_call_expression") {
      TSNode      fn   = field(n, "function");
      std::string fnnm = ts_node_is_null(fn) ? "" : text_of(src_, fn);
      if (fnnm == "assert" || fnnm == "cassert" || fnnm == "assume") {
        gen_assert(o, n, depth);
        return;
      }
      if (fnnm == "puts" || fnnm == "print") {
        gen_puts(o, n, depth, /*newline=*/fnnm == "puts");
        return;
      }
      if (fnnm == "poke") {  // poke("unit/field", value) -> force an internal reg/input
        TSNode args = field(n, "argument");
        if (ts_node_is_null(args) || ts_node_named_child_count(args) < 2) {
          fail("poke needs a \"unit/field\" path and a value");
        }
        o << ind << "__prp_poke(" << path_target(n, /*write=*/true) << ", " << expr(ts_node_named_child(args, 1))
          << ");\n";
        return;
      }
    }
    fail(std::string("unsupported statement in test: `") + std::string(t) + "` -> "
         + text_of(src_, n).substr(0, 40));
  }

  void gen_assignment(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    TSNode      lv = field(n, "lvalue");
    TSNode      rv = field(n, "rvalue");

    // poke `acc.field = v` -> set an input latch (or force an internal reg).
    std::string base, fld;
    if (inst_dot(lv, base, fld)) {
      std::string tgt = field_access(base, fld, /*write=*/true);
      // A >64-bit constant poke rides the string plane (exact at any width).
      std::string        raw = array_elem(rv);
      unsigned long long v   = 0;
      if (int_literal_class(raw, v) == 2) {
        o << ind << "__prp_poke(" << tgt << ", \"" << raw << "\");\n";
      } else {
        o << ind << "__prp_poke(" << tgt << ", " << expr(rv) << ");\n";
      }
      return;
    }

    std::string lname = lvalue_name(src_, lv);
    if (lname.empty()) {
      fail("unsupported assignment lvalue: " + text_of(src_, n).substr(0, 40));
    }
    if (!ts_node_is_null(rv) && ntype(rv) == "tuple_sq") {
      return;  // array literal: declared once at function scope (see discover)
    }
    if (!ts_node_is_null(rv) && !rvalue_module(rv).empty()) {
      return;  // `mut acc = Module`: the instance is declared once upfront (see main)
    }
    if (ts_node_is_null(rv)) {
      return;  // e.g. `mut v = nil` (declared as 0 already)
    }
    o << ind << lname << " = " << expr(rv) << ";\n";
  }

  void gen_if(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    // The named children interleave (condition, scope) pairs — one per
    // `if`/`elif` arm — with an optional trailing condition-less scope for
    // `else`. The old code kept only the FIRST condition and at most two
    // scopes, silently collapsing an elif CHAIN into a plain if/else: a
    // testbench ROM selecting over 5 address ranges served the second arm's
    // word for every address past the first range.
    struct Arm {
      TSNode cond{};
      TSNode scope{};
      bool   has_cond = false;
    };
    std::vector<Arm> arms;
    TSNode           pend{};
    bool             have_pend = false;
    for (TSNode c : ts_node_named_children(n)) {
      if (ntype(c) == "scope_statement") {
        arms.push_back({have_pend ? pend : TSNode{}, c, have_pend});
        have_pend = false;
      } else {
        pend      = c;
        have_pend = true;
      }
    }
    if (arms.empty() || !arms.front().has_cond) {
      fail("unsupported if form in test");
    }
    for (size_t i = 0; i < arms.size(); ++i) {
      if (i == 0) {
        o << ind << "if (" << expr(arms[i].cond) << ") {\n";
      } else if (arms[i].has_cond) {
        o << " else if (" << expr(arms[i].cond) << ") {\n";
      } else {
        o << " else {\n";
      }
      for (TSNode c : ts_node_named_children(arms[i].scope)) {
        gen_stmt(o, c, depth + 1);
      }
      o << ind << "}";
    }
    o << "\n";
  }

  // Does the subtree contain a `step` (clock edge)? Used to reject a `tick` body
  // that never advances the clock.
  bool subtree_has_step(TSNode n) const {
    if (ts_node_is_null(n)) {
      return false;
    }
    if (ntype(n) == "step_statement") {
      return true;
    }
    for (TSNode c : ts_node_named_children(n)) {
      if (subtree_has_step(c)) {
        return true;
      }
    }
    return false;
  }

  // Pull the single `name=value` entry out of a `clocks=(...)` tuple. A single
  // clock is supported for now (multiclock needs a LiveHD pass), so >1 entry is a
  // hard error. Returns false when the clause is absent.
  bool tick_one_entry(TSNode tup, const char* what, std::string& name, std::string& val) {
    if (ts_node_is_null(tup)) {
      return false;
    }
    TSNode   entry = {};
    unsigned seen  = 0;
    for (TSNode a : ts_node_named_children(tup)) {
      if (ntype(a) != "assignment") {
        continue;  // skip the operator wrapper / stray nodes
      }
      ++seen;
      entry = a;
    }
    if (seen == 0) {
      fail(std::string("tick ") + what + " clause needs a `name=value` entry");
    }
    if (seen > 1) {
      // The message used to read "only a single clock is supported for now",
      // which is now MISLEADING: multi-clock designs ARE simulated as of
      // todo/livehd/2f-latch M6 — a second clock is driven as an ordinary data
      // input and its state commits on a detected edge of that net (see
      // tests/sim/multiclock_two_domain.prp). What this clause controls is the
      // VCD waveform plus the tick LOOP VARIABLE, and a tick loop has exactly
      // one counter, so a second entry has no meaning here rather than being
      // unimplemented. Say that, so nobody reads the old text as "LiveHD cannot
      // do multiple clocks" and works around a limitation that is gone.
      if (std::string_view(what) == "clock") {
        fail("a `tick` has ONE loop counter, so `clocks=(...)` takes ONE entry (got " + std::to_string(seen)
             + "). This is NOT a multi-clock limitation: drive the second clock as an ordinary input "
               "(`acc.clkb = ...`) and give its registers `clock_pin=ref clkb` — they commit on that net's edges. "
               "See inou/prp/tests/sim/multiclock_two_domain.prp");
      }
      fail(std::string("only a single ") + what + " is supported for now (got " + std::to_string(seen) + ")");
    }
    TSNode an = field(entry, "lvalue");
    TSNode av = field(entry, "rvalue");
    if (ts_node_is_null(an) || ts_node_is_null(av)) {
      fail(std::string("tick ") + what + " entry must be `name=value`");
    }
    name = lvalue_name(src_, an);  // strip any type annotation: `clock:u4` -> `clock`
    val  = expr(av);
    return true;
  }

  // Emit the periodic-checkpoint block at the top of a tick body: when the
  // cadence is due (or every N cycles, `--checkpoint-every`), fork a child that
  // writes ckp<cycle>/{regs.json, <mem>.hex, tb.json, meta.json}; the parent
  // creates the dir first (so prune sees this cycle) and prunes to max after.
  void emit_checkpoint_block(std::ostringstream& o, const std::string& ind, const std::string& clk) {
    if (inst_of_var.empty()) {
      return;
    }
    o << ind << "if (_ckpt.enabled && !_ckpt_base.empty() && " << clk << " > 0 && (_ckpt.every > 0 ? (" << clk
      << " % _ckpt.every == 0) : _cad.due())) {\n";
    o << ind << "  std::string _cdir = hlop::ckpt::ckpt_path(_ckpt_base, " << clk << ");\n";
    o << ind << "  auto _t0 = std::chrono::steady_clock::now();\n";
    o << ind << "  hlop::ckpt::fork_checkpoint([&]() {\n";
    o << ind << "    hlop::ckpt::make_dirs(_cdir);\n";
    o << ind << "    std::map<std::string, std::string> _regs;\n";
    for (const auto& [var, m] : inst_of_var) {
      o << ind << "    " << var << ".dump_state(\"" << var << ".\", _regs, _cdir);\n";
    }
    o << ind << "    hlop::ckpt::write_str_map(_cdir + \"/regs.json\", _regs);\n";
    o << ind << "    std::map<std::string, std::string> _tb;\n";
    for (const auto& v : locals_) {
      o << ind << "    _tb[\"" << v << "\"] = std::to_string(" << v << ");\n";
    }
    o << ind << "    hlop::ckpt::write_str_map(_cdir + \"/tb.json\", _tb);\n";
    o << ind << "    std::map<std::string, std::string> _meta;\n";
    o << ind << "    _meta[\"cycle\"] = std::to_string(" << clk << ");\n";
    o << ind << "    unsigned long long _dh = hlop::ckpt::kFnvOffset;\n";
    for (const auto& [var, m] : inst_of_var) {
      o << ind << "    _dh = hlop::ckpt::fnv1a_u64(_dh, " << var << ".design_hash());\n";
    }
    o << ind << "    _meta[\"design_hash\"] = std::to_string(_dh);\n";
    o << ind << "    _meta[\"seed\"] = std::to_string(_seed_used);\n";
    o << ind << "    _meta[\"rng_draws\"] = std::to_string(hlop_random_draws());\n";
    o << ind << "    _meta[\"clock\"] = \"" << clk << "\";\n";
    o << ind << "    hlop::ckpt::write_str_map(_cdir + \"/meta.json\", _meta);\n";
    o << ind << "    hlop::ckpt::mark_complete(_cdir);  // LAST: makes the checkpoint visible to prune/restart\n";
    o << ind << "  });\n";
    o << ind << "  hlop::ckpt::prune_checkpoints(_ckpt_base, _ckpt.max);\n";
    o << ind << "  _cad.taken(std::chrono::duration<double>(std::chrono::steady_clock::now() - _t0).count());\n";
    o << ind << "}\n";
  }

  // Emit the restart prologue before the FIRST tick loop: if `--restart-at X`,
  // load the nearest checkpoint <= X (DUT state + this testbench frame), warn on a
  // design_hash mismatch (cross-version reload), and resume the loop at that cycle
  // instead of 0. Only the first tick restarts (the single-clock model has one
  // primary loop); later ticks run normally. No-op when no checkpoint is <= X.
  void emit_restart_block(std::ostringstream& o, const std::string& ind, const std::string& clk) {
    if (inst_of_var.empty() || restart_block_emitted_) {
      return;
    }
    restart_block_emitted_ = true;
    // Effective restart target: --restart-at, else the --vcd-from window start.
    o << ind << "long _rt = _ckpt.restart_at >= 0 ? _ckpt.restart_at : _ckpt.vcd_from;\n";
    o << ind << "if (_rt >= 0 && !_ckpt_base.empty()) {\n";
    o << ind << "  long _nc = hlop::ckpt::nearest_checkpoint_cycle(_ckpt_base, _rt);\n";
    o << ind << "  if (_nc >= 0) {\n";
    o << ind << "    std::string _cdir = hlop::ckpt::ckpt_path(_ckpt_base, _nc);\n";
    o << ind << "    auto _rregs = hlop::ckpt::read_str_map(_cdir + \"/regs.json\");\n";
    for (const auto& [var, m] : inst_of_var) {
      o << ind << "    " << var << ".load_state(\"" << var << ".\", _rregs, _cdir);\n";
    }
    o << ind << "    auto _rtb = hlop::ckpt::read_str_map(_cdir + \"/tb.json\");\n";
    for (const auto& v : locals_) {
      o << ind << "    if (auto _it = _rtb.find(\"" << v << "\"); _it != _rtb.end()) " << v
        << " = std::strtol(_it->second.c_str(), nullptr, 0);\n";
    }
    o << ind << "    { unsigned long long _dh = hlop::ckpt::kFnvOffset;\n";
    for (const auto& [var, m] : inst_of_var) {
      o << ind << "      _dh = hlop::ckpt::fnv1a_u64(_dh, " << var << ".design_hash());\n";
    }
    o << ind << "      auto _rmeta = hlop::ckpt::read_str_map(_cdir + \"/meta.json\");\n";
    o << ind << "      auto _mh = _rmeta.find(\"design_hash\");\n";
    o << ind << "      if (_mh != _rmeta.end() && _mh->second != std::to_string(_dh)) std::fprintf(stderr, \"lhd sim: "
                "warning: checkpoint design_hash mismatch at cycle %ld (cross-version reload; missing regs keep their "
                "reset value)\\n\", _nc);\n";
    // The PRNG stream position is NOT checkpointed (the thread_local engine resumes
    // at position 0). If the checkpointed run used randomness, warn that randomized
    // stimulus will diverge after restart (deterministic runs are unaffected).
    o << ind << "      auto _rd = _rmeta.find(\"rng_draws\");\n";
    o << ind << "      if (_rd != _rmeta.end() && _rd->second != \"0\" && !_rd->second.empty()) std::fprintf(stderr, "
                "\"lhd sim: warning: checkpoint used randomness (%s draws); PRNG stream is not restored, so randomized "
                "stimulus will diverge after restart\\n\", _rd->second.c_str()); }\n";
    o << ind << "    " << clk << " = _nc;\n";
    if (!vcd_dir_.empty()) {
      // Align the VCD time axis with the absolute cycle (the period counter is not
      // checkpointed; reset_cycle zeroed it), so a windowed trace timestamps at ~cycle*10.
      for (const auto& [var, m] : inst_of_var) {
        o << ind << "    " << var << ".__vcd_tick = (unsigned)_nc;\n";
      }
    }
    o << ind << "    std::fprintf(stderr, \"lhd sim: restarted from checkpoint cycle %ld (target %ld)\\n\", _nc, _rt);\n";
    o << ind << "  } else if (_rt > 0) {\n";
    o << ind << "    std::fprintf(stderr, \"lhd sim: no checkpoint <= %ld; replaying from cycle 0\\n\", _rt);\n";
    o << ind << "  }\n";
    o << ind << "}\n";
  }

  // Emit the windowed-VCD enable/disable at the top of a tick body (only when VCD
  // is on): start tracing at cycle vcd_from, stop after vcd_to. Combined with the
  // restart prologue (which jumps to ~vcd_from), this yields a VCD covering just
  // [vcd_from, vcd_to] of a long run.
  void emit_vcd_window_block(std::ostringstream& o, const std::string& ind, const std::string& clk) {
    if (vcd_dir_.empty() || inst_of_var.empty()) {
      return;
    }
    o << ind << "if (_ckpt.vcd_from >= 0) {\n";
    for (const auto& [var, m] : inst_of_var) {
      o << ind << "  if (" << clk << " == _ckpt.vcd_from) " << var << ".__vcd_path = _vcdp_" << var << ";\n";
      o << ind << "  if (_ckpt.vcd_to >= 0 && " << clk << " == _ckpt.vcd_to + 1) { " << var << ".__vcd.reset(); " << var
        << ".__vcd_path.clear(); }\n";
    }
    o << ind << "}\n";
  }

  // Emit the per-cycle observability block at the END of a tick body (so it sees
  // the post-step state, matching the located-assert / puts semantics): snapshot
  // every scalar signal once, then record a --probe trajectory row (within the
  // window) and/or test the --break-when condition (first hit only).
  void emit_debug_block(std::ostringstream& o, const std::string& ind, const std::string& clk) {
    if (inst_of_var.empty()) {
      return;
    }
    // `window_only` marks the verdict-discarded --vcd-on-fail re-run — skip probing
    // there so the trajectory is not double-appended / the break not re-evaluated.
    o << ind << "if (_dbg.active() && !_ckpt.window_only) {\n";
    o << ind << "  std::map<std::string, long> _sn;\n";
    for (const auto& [var, m] : inst_of_var) {
      o << ind << "  " << var << ".probe_signals(\"" << var << ".\", _sn);\n";
    }
    o << ind << "  if (!_dbg.probe.empty() && " << clk << " >= (_dbg.probe_from < 0 ? 0 : _dbg.probe_from) && "
                "(_dbg.probe_to < 0 || " << clk << " <= _dbg.probe_to)) {\n";
    o << ind << "    _DbgRow _r; _r.cycle = " << clk
      << "; for (const auto& _s : _dbg.probe) { auto _it = _sn.find(_s); if (_it != _sn.end()) _r.vals[_s] = "
         "_it->second; } _dbg.rows.push_back(std::move(_r));\n";
    o << ind << "  }\n";
    // Only evaluate the break when BOTH operands resolve to real signals — a
    // missing name must NOT default to 0 (that would fire a spurious cycle-0 hit).
    o << ind << "  if (_dbg.has_break && !_dbg.break_hit && _sn.count(_dbg.b_lhs) && (!_dbg.b_rhs_is_sig || "
                "_sn.count(_dbg.b_rhs))) {\n";
    o << ind << "    long _lv = _sn[_dbg.b_lhs];\n";
    o << ind << "    long _rv = _dbg.b_rhs_is_sig ? _sn[_dbg.b_rhs] : _dbg.b_rhs_val;\n";
    o << ind << "    if (_dbg_cmp(_lv, _dbg.b_op, _rv)) { _dbg.break_hit = true; _dbg.break_cycle = " << clk
      << "; _dbg.break_state = _sn; }\n";
    o << ind << "  }\n";
    o << ind << "}\n";
  }

  void gen_tick(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    TSNode      cnt  = field(n, "value");
    TSNode      code = field(n, "code");
    if (ts_node_is_null(cnt) || ts_node_is_null(code)) {
      fail("tick requires a count and a body (unbounded `tick {}` not yet supported)");
    }
    // A nested tick would emit a second `for (long clock ...)` that shadows the
    // outer clock and clobber the single function-scope `_clk`, so an outer-body
    // assert after the inner tick would report the inner cycle. The single-clock
    // model gives nesting no meaning -- reject it (flatten into one tick loop).
    if (in_tick_) {
      fail("a `tick` cannot be nested inside another `tick` (single-clock model) -- flatten into one tick loop");
    }
    in_tick_ = true;
    // Optional clocks=(name=ratio): name the clock + set its VCD time ratio on
    // each instance. (Multiclock is parsed but not lowered here; reset is no
    // longer a tick clause -- it is poked as an ordinary input.) The clock's name
    // is ALSO the loop variable, so the body can read it (`acc.reset = clock < 2`,
    // `puts("clock={clock}")`); it defaults to `clock`.
    std::string clk_name = "clock", clk_ratio;
    if (tick_one_entry(field(n, "clocks"), "clock", clk_name, clk_ratio)) {
      for (const auto& [var, m] : inst_of_var) {
        o << ind << var << ".__clk_ratio = (unsigned)(" << clk_ratio << ");\n";
        o << ind << var << ".__clk_name = \"" << clk_name << "\";\n";
      }
    }
    // The clock name is emitted as the C++ loop variable, so it must be a usable
    // identifier and must not collide with a test param / local / instance / array
    // (it would silently shadow it -> wrong value, 0 iterations, or a confusing
    // C++ error). Mirrors the parameter-name guard.
    if (!is_valid_param_name(clk_name)) {
      fail("tick clock name '" + clk_name + "' is not a usable identifier (C++ keyword / reserved / not a plain name) -- rename it");
    }
    if (param_names_.count(clk_name) != 0 || locals_.count(clk_name) != 0 || inst_of_var.count(clk_name) != 0
        || arrays_.count(clk_name) != 0) {
      fail("tick clock variable '" + clk_name + "' collides with a test parameter/local/instance of the same name -- rename one");
    }
    if (!inst_of_var.empty() && !subtree_has_step(code)) {
      fail("a `tick` body must advance the clock with `step` (none found)");
    }
    // `clock` (the clock's name) is the 0-based cycle index, stable across the
    // whole iteration (see newtick.md).
    std::string count = expr(cnt);
    // Block-scope the clock loop variable so SEQUENTIAL ticks in one test each get
    // their own `clock` (it is declared outside the for now, for the restart jump).
    o << ind << "{\n";
    o << ind << "long " << clk_name << " = 0;\n";
    emit_restart_block(o, ind, clk_name);  // --restart-at: load nearest ckpt, resume at its cycle
    o << ind << "for (; " << clk_name << " < (long)(" << count << "); ++" << clk_name << ") {\n";
    o << ind << "  _clk = " << clk_name << ";\n";  // current cycle, for located asserts (survives the loop)
    emit_vcd_window_block(o, ind + "  ", clk_name);  // --vcd-from/--vcd-to: enable/disable trace
    emit_checkpoint_block(o, ind + "  ", clk_name);  // periodic DUT+tb checkpoint
    std::vector<TSNode> body;
    for (TSNode c : ts_node_named_children(code)) {
      body.push_back(c);
    }
    for (auto s : body) {
      gen_stmt(o, s, depth + 1);
    }
    emit_debug_block(o, ind + "  ", clk_name);  // --probe / --break-when (post-step state)
    // Only the verdict-discarded --vcd-on-fail re-run (window_only) stops at vcd_to;
    // an explicit --vcd-from/--vcd-to window runs full-length so the test verdict +
    // result-json reflect the WHOLE run (the VCD is still just the window).
    if (!vcd_dir_.empty()) {
      o << ind << "  if (_ckpt.window_only && _ckpt.vcd_to >= 0 && " << clk_name << " >= _ckpt.vcd_to) break;\n";
    }
    o << ind << "}\n";
    o << ind << "}\n";
    in_tick_ = false;
  }

  void gen_assert(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    TSNode      args = field(n, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      fail("assert needs a condition");
    }
    TSNode      cond = ts_node_named_child(args, 0);
    std::string msg;
    if (ts_node_named_child_count(args) >= 2) {
      msg = text_of(src_, ts_node_named_child(args, 1));
      // strip surrounding quotes for the format string
      if (msg.size() >= 2 && msg.front() == '"') {
        msg = msg.substr(1, msg.size() - 2);
      }
    }
    int         line     = line_of(n);
    std::string loc      = file_short_ + ":" + std::to_string(line);
    std::string cond_src = trim(text_of(src_, cond));

    // The guard always uses the full condition (correctness); the message
    // decomposes a top-level comparison into both operand values so a failure is
    // self-explanatory: `acc.total=27 != 30` instead of just the message.
    std::string lhs_cpp, lhs_src, op, rhs_cpp, rhs_src;
    bool        cmp = decompose_compare(cond, lhs_cpp, lhs_src, op, rhs_cpp, rhs_src);
    o << ind << "if (!(" << expr(cond) << ")) {\n";
    if (cmp) {
      bool        lhs_lit = is_literal_operand(lhs_src);
      bool        rhs_lit = is_literal_operand(rhs_src);
      std::string fmt     = c_str_lit(loc) + ":assert fail: clock=%ld: " + c_str_lit(lhs_src)
                        + (lhs_lit ? "" : "=%ld") + " " + c_str_lit(op) + " " + c_str_lit(rhs_src)
                        + (rhs_lit ? "" : "=%ld");
      if (!msg.empty()) {
        fmt += "  [" + c_str_lit(msg) + "]";
      }
      o << ind << "  std::printf(\"" << fmt << "\\n\", _clk";
      if (!lhs_lit) {
        o << ", (long)(" << lhs_cpp << ")";
      }
      if (!rhs_lit) {
        o << ", (long)(" << rhs_cpp << ")";
      }
      o << ");\n";
    } else {
      std::string fmt = c_str_lit(loc) + ":assert fail: clock=%ld: condition `" + c_str_lit(cond_src)
                        + "` is false";
      if (!msg.empty()) {
        fmt += "  [" + c_str_lit(msg) + "]";
      }
      o << ind << "  std::printf(\"" << fmt << "\\n\", _clk);\n";
    }
    // First failing assert -> structured result (--result-json {test,status,cycle,failing_assert,prp_file,line,msg}).
    o << ind << "  if (!_ff.has) { _ff.has = true; _ff.cycle = _clk; _ff.assertion = \"" << cpp_str_lit(cond_src)
      << "\"; _ff.file = \"" << cpp_str_lit(file_) << "\"; _ff.line = " << line << "; _ff.msg = \"" << cpp_str_lit(msg)
      << "\"; _ff.loc = \"" << cpp_str_lit(loc) << "\"; }\n";
    o << ind << "  ++_fails;\n";
    o << ind << "}\n";
  }

  // `puts("text {var} {}", expr)` — runtime print. The interpolated string is
  // lowered to a printf: `{name}` -> the in-scope value `name`, `{}` -> the next
  // positional arg, all as %ld (driver values are C++ long). `puts` appends a
  // newline; `print` does not.
  void gen_puts(std::ostringstream& o, TSNode n, int depth, bool newline) {
    std::string ind(depth * 2, ' ');
    // `file:line:cmd:` origin prefix on every printed message (cmd = puts/print).
    std::string prefix = c_str_lit(file_short_ + ":" + std::to_string(line_of(n)) + ":" + (newline ? "puts" : "print") + ":");
    TSNode      args   = field(n, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      o << ind << "std::printf(\"" << prefix << (newline ? "\\n" : "") << "\");\n";
      return;
    }
    // format string = first arg (a constant wrapping a [interpolated_]string_literal)
    std::string lit = text_of(src_, ts_node_named_child(args, 0));
    if (lit.size() >= 2 && lit.front() == '"' && lit.back() == '"') {
      lit = lit.substr(1, lit.size() - 2);
    }
    // positional args after the format string
    std::vector<std::string> pos;
    uint32_t                 ai = 0;  // skips the format string (the first named child)
    for (TSNode a : ts_node_named_children(args)) {
      if (ai++ != 0) {
        pos.push_back(expr(a));
      }
    }
    std::string              fmt;
    std::vector<std::string> argv;
    size_t                   pi = 0;
    for (size_t i = 0; i < lit.size(); ++i) {
      char c = lit[i];
      if (c == '{') {
        if (i + 1 < lit.size() && lit[i + 1] == '{') {
          fmt += '{';
          ++i;
          continue;
        }
        size_t      j    = lit.find('}', i);
        std::string name = (j == std::string::npos) ? "" : lit.substr(i + 1, j - i - 1);
        // `{val:spec}` — split the format spec off; a bracketed index
        // (`{acc.registers.regs[2]:x}`) keeps its brackets in `name`.
        std::string spec;
        if (auto c2 = name.rfind(':'); c2 != std::string::npos && name.find(']', c2) == std::string::npos) {
          spec = name.substr(c2 + 1);
          name = name.substr(0, c2);
        }
        bool        is_slop = false;
        std::string ve =
            name.empty() ? (pi < pos.size() ? pos[pi++] : std::string("0")) : interp_expr(name, is_slop);
        if (is_slop) {
          // Render through the Slop formatting entry points — exact at any
          // width, no 64-bit truncation. Spec `[width][b|x|X|d][s]`: base
          // picks the Slop method; digits/sep ride as its arguments.
          // (`:o` has no Slop renderer — 64-bit fallback.)
          size_t w = 0, si = 0;
          while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9') {
            w = w * 10 + static_cast<size_t>(spec[si++] - '0');
          }
          char base = (si < spec.size() && spec[si] != 's') ? spec[si++] : 'd';
          bool sepf = si < spec.size() && spec[si] == 's';
          const std::string dg = std::to_string(w);
          const std::string sp = sepf ? "true" : "false";
          std::string call;
          switch (base) {
            case 'd': call = ve + ".to_decimal(" + dg + ", " + sp + ")"; break;
            case 'b': call = ve + ".to_binary(" + dg + ", " + sp + ")"; break;
            case 'x': call = ve + ".to_hex(" + dg + ", " + sp + ", false)"; break;
            case 'X': call = ve + ".to_hex(" + dg + ", " + sp + ", true)"; break;
            default : call = "__fmt_i64(" + ve + ".to_just_i64(), \"" + spec + "\")"; break;
          }
          fmt += "%s";
          argv.push_back(call + ".c_str()");
        } else if (spec.empty()) {
          fmt += "%ld";
          argv.push_back("(long)" + ve);
        } else {
          fmt += "%s";
          argv.push_back("__fmt_i64((long long)" + ve + ", \"" + spec + "\").c_str()");
        }
        i = (j == std::string::npos) ? lit.size() : j;
      } else if (c == '}') {
        if (i + 1 < lit.size() && lit[i + 1] == '}') {
          ++i;
        }
        fmt += '}';
      } else if (c == '%') {
        fmt += "%%";
      } else if (c == '"') {
        fmt += "\\\"";
      } else if (c == '\\') {
        fmt += "\\\\";
      } else {
        fmt += c;
      }
    }
    o << ind << "std::printf(\"" << prefix << fmt << (newline ? "\\n" : "") << "\"";
    for (const auto& a : argv) {
      o << ", " << a;
    }
    o << ");\n";
  }
};

// Parse `file` and invoke `on_test(test_node, dotted_name)` for each
// `test_statement` whose name matches `test_sel` (empty = all). Returns the match
// count, or -1 on a read/parse error (with `err` set). The parsed source text is
// stored in `src_out`; the TSNodes handed to the callback reference it, so it
// must outlive the callback (the caller owns it).
int for_each_test(const std::string& file, const std::string& test_sel, std::string& src_out, std::string& err,
                  const std::function<void(TSNode, const std::string&)>& on_test) {
  src_out = slurp(file);
  if (src_out.empty()) {
    err = "could not read source file: " + file;
    return -1;
  }
  prpparse::Source_buffer buf(file, src_out);
  prpparse::Parser        parser(buf);
  prpparse::Ast*          root = nullptr;
  try {
    root = parser.parse_ast();
  } catch (...) {
    err = "parse error in " + file;
    return -1;
  }
  TSNode root_node{root, &buf};
  int    matched = 0;
  for (TSNode c : ts_node_named_children(root_node)) {
    if (ntype(c) != "test_statement") {
      continue;
    }
    TSNode name_node = field(c, "name");
    if (ts_node_is_null(name_node)) {
      continue;
    }
    std::string name = text_of(src_out, name_node);
    if (!test_sel.empty() && name != test_sel) {
      continue;
    }
    if (std::getenv("PRP_SIM_DUMP") != nullptr) {
      dump_node(src_out, c, 0);
    }
    on_test(c, name);
    ++matched;
  }
  return matched;
}

}  // namespace

int generate(const std::string& file, const std::string& simdir, const std::string& test_sel, const std::string& vcd_dir,
             std::vector<Test_info>& tests, std::string& err) {
  // 1. read DUT interfaces from the cgen_sim headers in simdir
  std::map<std::string, Dut> duts;
  std::error_code            ec;
  for (auto& de : std::filesystem::directory_iterator(simdir, ec)) {
    if (!de.is_regular_file()) {
      continue;
    }
    std::string fn = de.path().filename().string();
    if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".hpp") {
      continue;
    }
    // Skip ONLY the generated driver itself, by exact name (no driver `.hpp` is
    // ever emitted, but stay precise) — a prefix skip would also drop a real DUT
    // unit from a `drv*.prp` source (cgen names units `<file_stem>.<module>.hpp`).
    if (fn == std::string(kDriverBasename) + ".hpp" || fn == std::string(kDriverBasename) + ".cpp") {
      continue;
    }
    Dut d;
    d.hpp = fn;
    if (parse_hpp(de.path().string(), d)) {
      duts[mod_of_hpp(fn)] = d;
    }
  }
  if (duts.empty()) {
    err = "no DUT modules found in " + simdir + " (the design produced no Slop units)";
    return 1;
  }
  // Mark CHILD units (their hpp is #included by another unit's hpp, i.e. they
  // are sub-instances). rvalue_module's import-bound fallback then resolves an
  // lg=-renamed top entry (`const top = import("u.top")` where the emitted
  // module is `u_top`) to the unique ROOT of a multi-module closure.
  for (auto& [k, d] : duts) {
    std::ifstream     f(simdir + "/" + d.hpp);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string txt = ss.str();
    for (auto& [k2, d2] : duts) {
      if (k2 != k && txt.find("\"" + d2.hpp + "\"") != std::string::npos) {
        d2.is_child = true;
      }
    }
  }

  // 2. parse the source and emit one run-function per `test`, accumulating the
  // sources, the registry order, and the union of DUT headers used. State is
  // per-test (a fresh Driver_gen each time), so tests never contaminate one
  // another's locals/instances.
  std::ostringstream       fns;          // run-function bodies, registry order
  std::vector<std::string> fn_ids;       // run-function names, registry order
  std::set<std::string>    includes;     // DUT headers any test drives
  std::set<std::string>    used_ids;     // for fn_id uniqueness
  std::string              src;          // owns the text the TSNodes reference
  bool                     gen_failed = false;
  std::string              gen_err;

  int matched = for_each_test(file, test_sel, src, err, [&](TSNode test, const std::string& name) {
    if (gen_failed) {
      return;  // stop emitting after the first error (err already captured)
    }
    // Unique C++ function id per test (two dotted names can sanitize alike).
    std::string fn_id = "_run_" + sanitize(name);
    for (int suffix = 2; used_ids.count(fn_id) != 0; ++suffix) {
      fn_id = "_run_" + sanitize(name) + "_" + std::to_string(suffix);
    }
    used_ids.insert(fn_id);

    Driver_gen              gen(src, duts, vcd_dir, file);  // fresh per-test state
    std::vector<Param_info> pinfo;
    try {
      fns << gen.emit_run_fn(test, name, fn_id, pinfo, includes) << "\n";
    } catch (const Gen_error& ge) {
      gen_failed = true;
      gen_err    = "test '" + name + "': " + ge.msg;
      return;
    }
    fn_ids.push_back(fn_id);
    tests.push_back(Test_info{name, std::move(pinfo)});
  });
  if (matched < 0) {
    return 1;  // err set by for_each_test
  }
  if (gen_failed) {
    err = gen_err;
    return 1;
  }
  if (matched == 0) {
    err = test_sel.empty() ? ("no test blocks found in " + file) : ("no test named '" + test_sel + "' in " + file);
    return 1;
  }

  bool any_param = false;
  for (const auto& t : tests) {
    any_param |= !t.params.empty();
  }

  // 3. assemble the single driver. Includes -> shared validating parsers -> the
  // run-functions -> the registry + embedded `--list-tests` JSON -> main().
  std::ostringstream o;
  o << "// Generated by lhd sim (prp_sim). Do not edit.\n";
  o << "// One driver for every `test` block of " << file << ":\n";
  o << "//   --list-tests          print the tests + parameters as JSON, then exit\n";
  o << "//   --test NAME           run only test NAME (repeatable; default = all)\n";
  o << "//   --seed N              hlop PRNG seed for random / unknown bits\n";
  o << "//   --<param> N           bind a `test name(params)` parameter\n";
  o << "//   --help, -h            usage\n";
  for (const auto& h : includes) {
    o << "#include \"" << h << "\"\n";
  }
  // slop.hpp brings in hlop_set_random_seed() even for a test with no DUT include
  // (it is transitively included by every DUT header otherwise).
  o << "#include \"slop.hpp\"\n";
  o << "#include \"checkpoint.hpp\"  // periodic DUT-state checkpoint cadence/fork/prune\n";
  // Width-deducing input poke: a testbench value is a 64-bit two's-complement
  // long; drive it into a Slop<N> port as the RAW low bits, ZERO-extended above
  // bit 63 (matching Verilog `port = val & mask`). create_integer() alone would
  // SIGN-fill a wide (N>64) port from a negative value; zext_to<N> keeps the low
  // min(64,N) bits and zeroes the rest (identical to create_integer for N<=64).
  o << "template<int N> static inline void __prp_poke(Slop<N>& d, long long v){ "
       "d = Slop<64>::create_integer(v).template zext_to<N>(); }\n";
  // Wide poke: a testbench constant that does not fit 64 bits (e.g. a formalfail
  // witness driving a u65 CSR bus) rides as a decimal STRING and converts at the
  // port's own width via the constexpr pyrope codec — exact at any width.
  o << "template<int N> static inline void __prp_poke(Slop<N>& d, std::string_view v){ "
       "d = Slop<N>::from_pyrope(v); }\n";  // string_view: a literal 0 must not be ambiguous with a null pointer
  // `{val:spec}` puts interpolation for PLAIN locals: grammar
  // `[width][b/o/x/X/d][s]` — width zero-pads, `s` groups digits `_`-separated
  // every 4 (mirrors the comptime __fmt fold in upass/constprop).
  o << "static std::string __fmt_i64(long long v, const char* spec){\n"
       "  size_t w=0; const char* p=spec; while(*p>='0'&&*p<='9') w=w*10+(size_t)(*p++-'0');\n"
       "  char base = (*p && *p!='s') ? *p++ : 'd';\n"
       "  bool sep = (*p=='s'); unsigned long long m = v<0 ? -(unsigned long long)v : (unsigned long long)v;\n"
       "  const char* dig = (base=='X') ? \"0123456789ABCDEF\" : \"0123456789abcdef\";\n"
       "  unsigned b = base=='b'?1u:base=='o'?3u:(base=='x'||base=='X')?4u:0u;\n"
       "  std::string s;\n"
       "  if (b==0u){ s = std::to_string(m); } else { do { s.push_back(dig[m&((1u<<b)-1u)]); m >>= b; } while(m); }\n"
       "  if (b!=0u) std::reverse(s.begin(), s.end());\n"
       "  if (w>s.size()) s.insert(0, w-s.size(), '0');\n"
       "  if (sep){ std::string g; size_t c=0; for(size_t k=s.size(); k>0; --k){ if(c&&c%4==0) g.push_back('_'); g.push_back(s[k-1]); ++c; } std::reverse(g.begin(), g.end()); s=std::move(g); }\n"
       "  if (v<0) s.insert(0,1,'-');\n"
       "  return s;\n"
       "}\n";
  const bool vcd_on = !vcd_dir.empty();
  if (vcd_on) {
    // For vcd::global_timestamp — reset between tests (see main()).
    o << "#include \"vcd_writer.hpp\"\n";
  }
  o << "#include <cstdio>\n#include <cstdint>\n#include <cstdlib>\n#include <cerrno>\n#include <cctype>\n"
       "#include <string>\n#include <string_view>\n#include <map>\n#include <set>\n#include <vector>\n#include <fstream>\n#include <chrono>\n\n";

  // Checkpoint configuration (sim.checkpoint*): periodic fork-checkpoints of the
  // DUT state + testbench frame, written under <dir>/<test>/ckp<cycle>/, pruned to
  // `max` and spaced ~min_secs apart (or every `every` cycles when set). main()
  // fills this from the CLI; the run-functions read it. seed is mirrored for meta.
  o << "struct _CkptCfg { bool enabled = true; double min_secs = 10.0; long max = 10; double max_overhead = 0.10; "
       "long every = 0; std::string dir; long restart_at = -1; long vcd_from = -1; long vcd_to = -1; "
       "bool vcd_on_fail = false; long vcd_fail_window = 20; bool window_only = false; };\n";
  o << "static _CkptCfg _ckpt;\n";
  o << "static unsigned long long _seed_used = " << kDefaultSeed << ";\n";

  // ---- observability (--list-signals / --probe / --break-when) ----
  // The driver snapshots scalar signals (probe_signals) by hierarchical name each
  // active cycle; --probe records a per-cycle trajectory, --break-when finds the
  // first cycle a `SIG OP VALUE|SIG` condition holds, --list-signals enumerates
  // the observable signals. Results go to the --debug-json sidecar.
  o << "struct _DbgRow { long cycle; std::map<std::string, long> vals; };\n";
  o << "struct _DbgState {\n"
       "  bool list_signals = false;\n"
       "  std::vector<std::string> probe; long probe_from = -1, probe_to = -1;\n"
       "  std::string out;\n"
       "  std::vector<hlop::ckpt::Signal> sigs;\n"
       "  std::vector<_DbgRow> rows;\n"
       "  bool break_hit = false; long break_cycle = -1; std::map<std::string, long> break_state;\n"
       "  bool has_break = false; std::string b_lhs, b_op, b_rhs; bool b_rhs_is_sig = false; long b_rhs_val = 0;\n"
       "  bool active() const { return !probe.empty() || has_break; }\n"
       "};\n";
  o << "static _DbgState _dbg;\n";
  o << "static bool _dbg_cmp(long _a, const std::string& _op, long _b) {\n"
       "  if (_op == \">\") return _a > _b; if (_op == \"<\") return _a < _b;\n"
       "  if (_op == \">=\") return _a >= _b; if (_op == \"<=\") return _a <= _b;\n"
       "  if (_op == \"==\") return _a == _b; if (_op == \"!=\") return _a != _b;\n"
       "  return false;\n}\n";
  // Parse `SIG OP VALUE|SIG` into _dbg.{b_lhs,b_op,b_rhs,...}. A 2-char operator is
  // checked before its 1-char prefix; the RHS is a number iff it starts 0-9/-/+.
  o << "static void _dbg_parse_break(const std::string& _s) {\n"
       "  static const char* _ops2[] = {\">=\", \"<=\", \"==\", \"!=\"};\n"
       "  static const char* _ops1[] = {\">\", \"<\"};\n"
       "  size_t _pos = std::string::npos; std::string _op;\n"
       "  for (auto _o : _ops2) { auto _p = _s.find(_o); if (_p != std::string::npos) { _pos = _p; _op = _o; break; } }\n"
       "  if (_pos == std::string::npos) for (auto _o : _ops1) { auto _p = _s.find(_o); if (_p != std::string::npos) { "
       "_pos = _p; _op = _o; break; } }\n"
       "  if (_pos == std::string::npos) { std::fprintf(stderr, \"lhd sim: --break-when needs a comparison "
       "(SIG >|<|>=|<=|==|!= VALUE), got '%s'\\n\", _s.c_str()); std::exit(2); }\n"
       "  auto _trim = [](std::string _t) { size_t _b = _t.find_first_not_of(\" \\t\"); size_t _e = "
       "_t.find_last_not_of(\" \\t\"); return _b == std::string::npos ? std::string() : _t.substr(_b, _e - _b + 1); };\n"
       "  _dbg.b_lhs = _trim(_s.substr(0, _pos)); _dbg.b_op = _op; _dbg.b_rhs = _trim(_s.substr(_pos + _op.size()));\n"
       "  if (_dbg.b_lhs.empty()) { std::fprintf(stderr, \"lhd sim: --break-when needs a signal on the left of '%s', "
       "got '%s'\\n\", _op.c_str(), _s.c_str()); std::exit(2); }\n"
       // A numeric RHS is parsed (and VALIDATED) as 64-bit; strtoull also negates a
       // leading '-' mod 2^64, so `sig == -1` / `sig == 0xffffffffffffffff` both match
       // a high-bit-set probed value (to_just_i64's bit pattern). A non-numeric RHS
       // is another signal name.
       "  if (!_dbg.b_rhs.empty() && (std::isdigit((unsigned char)_dbg.b_rhs[0]) || _dbg.b_rhs[0] == '-' || _dbg.b_rhs[0] "
       "== '+')) {\n"
       "    errno = 0; char* _e = nullptr; unsigned long long _u = std::strtoull(_dbg.b_rhs.c_str(), &_e, 0);\n"
       "    if (_e == _dbg.b_rhs.c_str() || *_e != '\\0' || errno == ERANGE) { std::fprintf(stderr, \"lhd sim: "
       "--break-when value '%s' is not a valid integer\\n\", _dbg.b_rhs.c_str()); std::exit(2); }\n"
       "    _dbg.b_rhs_val = (long)_u; _dbg.b_rhs_is_sig = false;\n"
       "  } else { _dbg.b_rhs_is_sig = true; }\n"
       "  _dbg.has_break = true;\n}\n";

  // First-failing-assert record per test run (located message + structured
  // --result-json). Shared by every run-function (passed by reference) and main().
  o << "struct _Fail { bool has = false; long cycle = -1; std::string assertion; std::string file; int line = 0; "
       "std::string msg; std::string loc; };\n";
  o << "static std::string _json_esc(const std::string& _s) {\n"
       "  static const char _hex[] = \"0123456789abcdef\";\n"
       "  std::string _o;\n"
       "  for (char _c : _s) { unsigned char _u = (unsigned char)_c; switch (_c) {\n"
       "    case '\\\\': _o += \"\\\\\\\\\"; break; case '\"': _o += \"\\\\\\\"\"; break;\n"
       "    case '\\n': _o += \"\\\\n\"; break; case '\\r': _o += \"\\\\r\"; break; case '\\t': _o += \"\\\\t\"; break;\n"
       "    default: if (_u < 0x20) { _o += \"\\\\u00\"; _o += _hex[_u >> 4]; _o += _hex[_u & 0xf]; } else _o += _c; } }\n"
       "  return _o;\n}\n\n";

  // Validating numeric parsers, shared by every run-function (params) + main
  // (`--seed`). The whole token must convert (base 0: 0x.. / decimal) and stay in
  // range; a typo / out-of-range value exits with a clear message rather than
  // silently becoming 0 / clamped. `_to_i64` is always emitted now (the
  // `--checkpoint-*` flags use it too). `--seed`/params are non-negative for _to_u64.
  (void)any_param;
  o << "static long _to_i64(const std::string& _key, const std::string& _s) {\n"
       "  errno = 0; char* _e = nullptr; long _r = std::strtol(_s.c_str(), &_e, 0);\n"
       "  if (_e == _s.c_str() || *_e != '\\0' || errno == ERANGE) {\n"
       "    std::fprintf(stderr, \"lhd sim: --%s expects an integer, got '%s'\\n\", _key.c_str(), _s.c_str());\n"
       "    std::exit(2);\n  }\n  return _r;\n}\n";
  o << "static unsigned long long _to_u64(const std::string& _key, const std::string& _s) {\n"
       "  errno = 0; char* _e = nullptr; unsigned long long _r = std::strtoull(_s.c_str(), &_e, 0);\n"
       "  if (_s.empty() || _s[0] == '-' || _e == _s.c_str() || *_e != '\\0' || errno == ERANGE) {\n"
       "    std::fprintf(stderr, \"lhd sim: --%s expects a non-negative integer, got '%s'\\n\", _key.c_str(), "
       "_s.c_str());\n    std::exit(2);\n  }\n  return _r;\n}\n\n";

  o << fns.str();

  // Registry + the canonical `--list-tests` JSON (identical to what
  // `lhd sim --list-tests` prints, since both render tests_to_json()).
  o << "\nstruct _Test { const char* name; long (*run)(const std::map<std::string, std::string>&, "
       "std::set<std::string>&, std::string&, _Fail&); };\n";
  o << "static const _Test _tests[] = {\n";
  for (size_t i = 0; i < fn_ids.size(); ++i) {
    o << "  {\"" << cpp_str_lit(tests[i].name) << "\", &" << fn_ids[i] << "},\n";
  }
  o << "};\n";
  // Every test parameter declared in this binary (union over all tests). The
  // end-of-run check warns about an unknown `--key` against THIS set (not the
  // per-run `_consumed`), so a declared-but-unbound parameter — e.g. one a test
  // fail-fasts past on an earlier required parameter — is not falsely flagged.
  {
    std::set<std::string> decl;
    for (const auto& t : tests) {
      for (const auto& p : t.params) {
        decl.insert(p.name);
      }
    }
    o << "static const std::set<std::string> _declared_params = {";
    bool first = true;
    for (const auto& p : decl) {
      o << (first ? "" : ", ") << "\"" << cpp_str_lit(p) << "\"";
      first = false;
    }
    o << "};\n";
  }
  o << "static const char* _tests_json = \"" << cpp_str_lit(tests_to_json_impl(file, tests)) << "\";\n\n";

  o << "static void _usage(const char* _argv0) {\n"
       "  std::printf(\"usage: %s [--list-tests] [--test NAME]... [--seed N] [--result-json PATH] [--<param> "
       "N]...\\n\", _argv0);\n"
       "  std::printf(\"  Runs the lhd-sim test(s) compiled into this binary (default: all).\\n\");\n"
       "  std::printf(\"options:\\n\");\n"
       "  std::printf(\"  --list-tests       print the tests + parameters as JSON and exit\\n\");\n"
       "  std::printf(\"  --test NAME        run only test NAME (repeatable; default: all)\\n\");\n"
       "  std::printf(\"  --seed N           hlop PRNG seed for random/unknown bits (default "
    << kDefaultSeedShown
    << ")\\n\");\n"
       "  std::printf(\"  --result-json PATH write a JSON {test,status,cycle,failing_assert,prp_file,line} report\\n\");\n"
       "  std::printf(\"  --<param> N        bind a test parameter (see --list-tests)\\n\");\n"
       "  std::printf(\"  --help, -h         show this message and exit\\n\");\n"
       "  std::printf(\"tests:\\n\");\n"
       "  for (const auto& _t : _tests) std::printf(\"  %s\\n\", _t.name);\n"
       "}\n\n";

  // main(): central argument parsing (--list-tests / --test / --seed / --help and
  // the generic `--<param> value` test flags), then run the selected test(s). The
  // seed is set once (hlop's PRNG latches its thread_local engine on first use, so
  // it is process-wide, not re-applied per test). A required-parameter-missing
  // test reports FAIL and the run continues; an unknown `--key` that no run test
  // consumes is warned about at the end.
  o << "int main(int argc, char** argv) {\n"
       "  unsigned long long _seed = "
    << kDefaultSeed
    << ";\n"
       "  bool _list = false;\n"
       "  std::string _result_json;\n"
       "  std::vector<std::string> _selected;\n"
       "  std::map<std::string, std::string> _args;\n"
       "  for (int _i = 1; _i < argc; ++_i) {\n"
       "    std::string _arg = argv[_i], _key = _arg, _val; bool _has_val = false;\n"
       "    if (auto _eq = _arg.find('='); _eq != std::string::npos) { _key = _arg.substr(0, _eq); _val = "
       "_arg.substr(_eq + 1); _has_val = true; }\n"
       "    auto _need = [&]() -> std::string { if (_has_val) return _val; if (_i + 1 < argc) return argv[++_i]; "
       "std::fprintf(stderr, \"lhd sim: missing value for %s\\n\", _key.c_str()); _usage(argv[0]); std::exit(2); };\n"
       "    if (_key == \"--help\" || _key == \"-h\") { _usage(argv[0]); return 0; }\n"
       "    else if (_key == \"--list-tests\") { _list = true; }\n"
       "    else if (_key == \"--seed\") { _seed = _to_u64(\"seed\", _need()); }\n"
       "    else if (_key == \"--result-json\") { _result_json = _need(); }\n"
       "    else if (_key == \"--ckpt-dir\") { _ckpt.dir = _need(); }\n"
       "    else if (_key == \"--no-checkpoint\") { _ckpt.enabled = false; }\n"
       "    else if (_key == \"--checkpoint-min-secs\") { _ckpt.min_secs = std::strtod(_need().c_str(), nullptr); }\n"
       "    else if (_key == \"--checkpoint-max\") { _ckpt.max = _to_i64(\"checkpoint-max\", _need()); }\n"
       "    else if (_key == \"--checkpoint-max-overhead\") { _ckpt.max_overhead = std::strtod(_need().c_str(), nullptr); }\n"
       "    else if (_key == \"--checkpoint-every\") { _ckpt.every = _to_i64(\"checkpoint-every\", _need()); }\n"
       "    else if (_key == \"--restart-at\" || _key == \"--restart-cycle\") { _ckpt.restart_at = _to_i64(\"restart-at\", _need()); }\n"
       "    else if (_key == \"--vcd-from\") { _ckpt.vcd_from = _to_i64(\"vcd-from\", _need()); }\n"
       "    else if (_key == \"--vcd-to\") { _ckpt.vcd_to = _to_i64(\"vcd-to\", _need()); }\n"
       "    else if (_key == \"--vcd-on-fail\") { _ckpt.vcd_on_fail = true; }\n"
       "    else if (_key == \"--vcd-fail-window\") { _ckpt.vcd_fail_window = _to_i64(\"vcd-fail-window\", _need()); }\n"
       "    else if (_key == \"--list-signals\") { _dbg.list_signals = true; }\n"
       "    else if (_key == \"--probe\") { std::string _v = _need(), _t; auto _push = [&]() { size_t _b = "
       "_t.find_first_not_of(\" \\t\"); if (_b != std::string::npos) { size_t _e = _t.find_last_not_of(\" \\t\"); "
       "_dbg.probe.push_back(_t.substr(_b, _e - _b + 1)); } _t.clear(); }; for (char _c : _v) { if (_c == ',') _push(); "
       "else _t += _c; } _push(); }\n"
       "    else if (_key == \"--probe-from\") { _dbg.probe_from = _to_i64(\"probe-from\", _need()); }\n"
       "    else if (_key == \"--probe-to\") { _dbg.probe_to = _to_i64(\"probe-to\", _need()); }\n"
       "    else if (_key == \"--break-when\") { _dbg_parse_break(_need()); }\n"
       "    else if (_key == \"--debug-json\") { _dbg.out = _need(); }\n"
       "    else if (_key == \"--test\") { _selected.push_back(_need()); }\n"
       "    else if (_key.size() > 2 && _key[0] == '-' && _key[1] == '-') { _args[_key.substr(2)] = _need(); }\n"
       "    else { std::fprintf(stderr, \"lhd sim: unknown argument '%s'\\n\", _key.c_str()); _usage(argv[0]); return "
       "2; }\n"
       "  }\n"
       "  if (_list) { std::printf(\"%s\\n\", _tests_json); return 0; }\n"
       "  hlop_set_random_seed(_seed);\n"
       "  _seed_used = _seed;  // mirrored into checkpoint meta.json\n"
       "  std::vector<const _Test*> _torun;\n"
       "  if (_selected.empty()) { for (const auto& _t : _tests) _torun.push_back(&_t); }\n"
       "  else { for (const auto& _nm : _selected) { const _Test* _f = nullptr; for (const auto& _t : _tests) if (_nm "
       "== _t.name) { _f = &_t; break; } if (_f == nullptr) { std::fprintf(stderr, \"lhd sim: no test named '%s'\\n\", "
       "_nm.c_str()); _usage(argv[0]); return 2; } _torun.push_back(_f); } }\n"
       // Observability (--list-signals/--probe/--break-when) is a single-run query —
       // the debug envelope holds ONE result, and the global _dbg would otherwise mix
       // signals/rows across tests. Require exactly one test.
       "  if ((_dbg.list_signals || _dbg.active()) && _torun.size() != 1) {\n"
       "    std::fprintf(stderr, \"lhd sim: --list-signals/--probe/--break-when need a single test; select one by name "
       "(`lhd sim file.prp <test>`) -- this run has %zu\\n\", _torun.size());\n"
       "    return 2;\n  }\n"
       "  long _fail_tests = 0;\n"
       "  std::set<std::string> _consumed;\n"
       // A bare JSON array of per-test results, so `lhd sim` can embed it verbatim
       // as the result envelope's "tests" member (and a direct binary run gets a
       // standalone array).
       "  std::string _rj = \"[\";\n"
       "  bool _rj_first = true;\n"
       "  for (const auto* _t : _torun) {\n"
       "    std::string _err; _Fail _ff;\n"
    << (vcd_on ? "    vcd::global_timestamp = 0;  // each test gets an independent VCD timeline (#0..)\n" : "")
    << "    long _f = _t->run(_args, _consumed, _err, _ff);\n"
       "    const char* _status = (_f < 0) ? \"error\" : (_f > 0 ? \"fail\" : \"pass\");\n"
       "    if (_f < 0) { std::printf(\"FAIL %s (%s)\\n\", _t->name, _err.c_str()); ++_fail_tests; }\n"
       "    else if (_f > 0) {\n"
       "      if (_ff.has) std::printf(\"FAIL %s (%ld assert(s) failed; first at %s clock=%ld)\\n\", _t->name, _f, "
       "_ff.loc.c_str(), _ff.cycle);\n"
       "      else std::printf(\"FAIL %s (%ld assert(s) failed)\\n\", _t->name, _f);\n"
       "      ++_fail_tests;\n"
       "    } else { std::printf(\"PASS %s\\n\", _t->name); }\n"
    // --vcd-on-fail: re-run a failed test with a VCD window around the failure
    // cycle (the restart prologue jumps to ~vcd_from, the early break stops at
    // vcd_to), suppressing the re-run's stdout. Only emitted when VCD is on.
    << (vcd_on
            ? "    if (_ckpt.vcd_on_fail && _f > 0 && _ff.has) {\n"
              "      long _vf = _ff.cycle > _ckpt.vcd_fail_window ? _ff.cycle - _ckpt.vcd_fail_window : 0;\n"
              // Save/override the relevant config: trace [_vf, fail_cycle], discard the
              // verdict (window_only -> early break), and DO NOT checkpoint during the
              // re-run (it would pollute/prune the checkpoint set).
              "      long _of = _ckpt.vcd_from, _ot = _ckpt.vcd_to; bool _oe = _ckpt.enabled, _ow = _ckpt.window_only;\n"
              "      _ckpt.vcd_from = _vf; _ckpt.vcd_to = _ff.cycle; _ckpt.enabled = false; _ckpt.window_only = true;\n"
              // Suppress the re-run's stdout (the located assert already printed once).
              // If the redirect can't be set up, run WITHOUT suppression rather than lose output.
              "      std::fflush(stdout); int _sfd = dup(fileno(stdout)); FILE* _nul = std::fopen(\"/dev/null\", \"w\");\n"
              "      bool _red = (_sfd >= 0 && _nul != nullptr && dup2(fileno(_nul), fileno(stdout)) >= 0);\n"
              "      std::string _e2; _Fail _f2; _t->run(_args, _consumed, _e2, _f2);\n"
              "      std::fflush(stdout);\n"
              "      if (_red) dup2(_sfd, fileno(stdout));\n"
              "      if (_nul != nullptr) std::fclose(_nul); if (_sfd >= 0) close(_sfd);\n"
              "      _ckpt.vcd_from = _of; _ckpt.vcd_to = _ot; _ckpt.enabled = _oe; _ckpt.window_only = _ow;\n"
              "      std::fprintf(stderr, \"lhd sim: %s failed at clock %ld -> wrote failure VCD (cycles %ld..%ld)\\n\", "
              "_t->name, _ff.cycle, _vf, _ff.cycle);\n"
              "    }\n"
            : "")
    << "    if (!_result_json.empty()) {\n"
       "      _rj += _rj_first ? \"\" : \",\"; _rj_first = false;\n"
       "      _rj += \"{\\\"test\\\":\\\"\"; _rj += _json_esc(_t->name); _rj += \"\\\",\\\"status\\\":\\\"\"; _rj += "
       "_status; _rj += \"\\\"\";\n"
       "      if (_f < 0) { _rj += \",\\\"error\\\":\\\"\"; _rj += _json_esc(_err); _rj += \"\\\"\"; }\n"
       "      else if (_f > 0 && _ff.has) {\n"
       "        _rj += \",\\\"cycle\\\":\"; _rj += std::to_string(_ff.cycle);\n"
       "        _rj += \",\\\"failing_assert\\\":\\\"\"; _rj += _json_esc(_ff.assertion); _rj += \"\\\"\";\n"
       "        _rj += \",\\\"prp_file\\\":\\\"\"; _rj += _json_esc(_ff.file); _rj += \"\\\"\";\n"
       "        _rj += \",\\\"line\\\":\"; _rj += std::to_string(_ff.line);\n"
       "        if (!_ff.msg.empty()) { _rj += \",\\\"msg\\\":\\\"\"; _rj += _json_esc(_ff.msg); _rj += \"\\\"\"; }\n"
       "      }\n"
       "      _rj += \"}\";\n"
       "    }\n"
       "  }\n"
       "  _rj += \"]\";\n"
       // A sidecar-write failure is only a bookkeeping problem -- WARN, but keep the
       // exit code the tests' real verdict (else a passing run would look like a
       // usage error, exit 2; the lhd kernel tolerates a missing result file).
       "  if (!_result_json.empty()) {\n"
       "    std::ofstream _ofs(_result_json);\n"
       "    if (!_ofs) { std::fprintf(stderr, \"lhd sim: warning: cannot write --result-json '%s'\\n\", "
       "_result_json.c_str()); }\n"
       "    else { _ofs << _rj << \"\\n\"; }\n"
       "  }\n"
       "  for (const auto& _kv : _args) if (_declared_params.find(_kv.first) == _declared_params.end()) std::fprintf(stderr, \"lhd "
       "sim: warning: --%s matches no test parameter (ignored)\\n\", _kv.first.c_str());\n"
       // ---- observability output (--list-signals / --probe / --break-when) ----
       "  if (_dbg.has_break && _dbg.break_hit) std::fprintf(stderr, \"lhd sim: break-when '%s %s %s' first held at "
       "clock %ld\\n\", _dbg.b_lhs.c_str(), _dbg.b_op.c_str(), _dbg.b_rhs.c_str(), _dbg.break_cycle);\n"
       "  else if (_dbg.has_break) std::fprintf(stderr, \"lhd sim: break-when '%s %s %s' never held\\n\", "
       "_dbg.b_lhs.c_str(), _dbg.b_op.c_str(), _dbg.b_rhs.c_str());\n"
       "  if (!_dbg.out.empty()) {\n"
       "    std::string _dj = \"{\"; bool _df = true;\n"
       "    if (_dbg.list_signals) {\n"
       "      _dj += \"\\\"signals\\\":[\";\n"
       "      for (size_t _i = 0; _i < _dbg.sigs.size(); ++_i) { if (_i) _dj += \",\"; _dj += \"{\\\"name\\\":\\\"\"; "
       "_dj += _json_esc(_dbg.sigs[_i].name); _dj += \"\\\",\\\"bits\\\":\"; _dj += std::to_string(_dbg.sigs[_i].bits); "
       "_dj += \",\\\"kind\\\":\\\"\"; _dj += _json_esc(_dbg.sigs[_i].kind); _dj += \"\\\"}\"; }\n"
       "      _dj += \"]\"; _df = false;\n"
       "    }\n"
       "    if (!_dbg.probe.empty()) {\n"
       "      if (!_df) _dj += \",\"; _df = false;\n"
       "      _dj += \"\\\"probe\\\":{\\\"signals\\\":[\";\n"
       "      for (size_t _i = 0; _i < _dbg.probe.size(); ++_i) { if (_i) _dj += \",\"; _dj += \"\\\"\"; _dj += "
       "_json_esc(_dbg.probe[_i]); _dj += \"\\\"\"; }\n"
       "      _dj += \"],\\\"from\\\":\"; _dj += std::to_string(_dbg.probe_from); _dj += \",\\\"to\\\":\"; _dj += "
       "std::to_string(_dbg.probe_to); _dj += \",\\\"rows\\\":[\";\n"
       "      for (size_t _i = 0; _i < _dbg.rows.size(); ++_i) { if (_i) _dj += \",\"; _dj += \"{\\\"cycle\\\":\"; _dj "
       "+= std::to_string(_dbg.rows[_i].cycle); for (const auto& _kv : _dbg.rows[_i].vals) { _dj += \",\\\"\"; _dj += "
       "_json_esc(_kv.first); _dj += \"\\\":\"; _dj += std::to_string(_kv.second); } _dj += \"}\"; }\n"
       "      _dj += \"]}\";\n"
       "    }\n"
       "    if (_dbg.has_break) {\n"
       "      if (!_df) _dj += \",\"; _df = false;\n"
       "      _dj += \"\\\"break\\\":{\\\"cond\\\":\\\"\"; _dj += _json_esc(_dbg.b_lhs + \" \" + _dbg.b_op + \" \" + "
       "_dbg.b_rhs); _dj += \"\\\",\\\"hit\\\":\"; _dj += (_dbg.break_hit ? \"true\" : \"false\");\n"
       "      if (_dbg.break_hit) { _dj += \",\\\"cycle\\\":\"; _dj += std::to_string(_dbg.break_cycle); _dj += "
       "\",\\\"state\\\":{\"; bool _sf = true; for (const auto& _kv : _dbg.break_state) { if (!_sf) _dj += \",\"; _sf = "
       "false; _dj += \"\\\"\"; _dj += _json_esc(_kv.first); _dj += \"\\\":\"; _dj += std::to_string(_kv.second); } _dj "
       "+= \"}\"; }\n"
       "      _dj += \"}\";\n"
       "    }\n"
       "    _dj += \"}\"; std::ofstream _dofs(_dbg.out); if (_dofs) _dofs << _dj << \"\\n\";\n"
       "  }\n"
       "  hlop::ckpt::drain_checkpoints();  // block until in-flight checkpoint children finish (write _done)\n"
       "  return _fail_tests ? 1 : 0;\n}\n";

  std::ofstream ofs(simdir + "/" + kDriverBasename + ".cpp");
  ofs << o.str();
  ofs.close();
  return 0;
}

std::string tests_to_json(const std::string& file, const std::vector<Test_info>& tests) {
  return tests_to_json_impl(file, tests);
}

int list_tests(const std::string& file, const std::string& test_sel, std::vector<Test_info>& tests, std::string& err) {
  std::string src;
  int         matched = for_each_test(file, test_sel, src, err, [&](TSNode test, const std::string& name) {
    Test_info ti;
    ti.name = name;
    for (const auto& r : read_params_raw(src, test)) {
      Param_info pi;
      pi.name     = r.name;
      pi.required = r.required;
      if (!r.required) {
        pi.default_text = text_of(src, r.default_node);
      }
      ti.params.push_back(std::move(pi));
    }
    tests.push_back(std::move(ti));
  });
  if (matched < 0) {
    return 1;  // err set
  }
  if (matched == 0) {
    err = test_sel.empty() ? ("no test blocks found in " + file) : ("no test named '" + test_sel + "' in " + file);
    return 1;
  }
  return 0;
}

}  // namespace prp_sim
