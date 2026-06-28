//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_sim.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
};

// Parse a generated <unit>.hpp: the top struct name, its In{} and Out{} fields.
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
  for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
    dump_node(src, ts_node_named_child(n, i), depth + 1);
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
    for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
      TSNode c = ts_node_named_child(n, i);
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
// is required, and the driver prints its usage and exits non-zero if it is
// not supplied. Values are bound at RUN time (argv), never baked in, so one
// built driver can be re-run with different parameters.
struct Param {
  std::string name;
  bool        has_default{false};
  std::string default_expr;  // C++ expression for the default (valid iff has_default)
};

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
  // does not catch them): `main`'s parameters, the reserved CLI flags, the libc
  // macros from <cerrno> (a param named `errno` would expand `long errno = …`
  // to `long (*__error()) = …` — a hard error), and the free functions the
  // driver calls unqualified. The driver's own `_`-prefixed locals need no entry.
  static const std::set<std::string_view> driver_reserved = {
      "argc", "argv", "main", "seed", "help", "h", "errno", "ERANGE", "hlop_set_random_seed",
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
  Driver_gen(const std::string& src, const std::map<std::string, Dut>& duts, const std::string& vcd_dir)
      : src_(src), duts_(duts), vcd_dir_(vcd_dir) {}

  // Emit a complete driver main() for one test_statement. `name` is the dotted
  // selector. Returns the C++ source; throws Gen_error on an unsupported form.
  std::string emit(TSNode test, const std::string& name) {
    TSNode code = field(test, "code");
    if (ts_node_is_null(code)) {
      fail("test '" + name + "' has no body");
    }
    // collect body statements
    std::vector<TSNode> stmts;
    for (uint32_t i = 0; i < ts_node_named_child_count(code); ++i) {
      stmts.push_back(ts_node_named_child(code, i));
    }
    // pass 1: discover scalar locals (every assigned lvalue identifier) and the
    // DUT instances used (a function_call whose callee is a known mod).
    discover(stmts);

    const auto params = read_params(test);
    // A parameter name is emitted verbatim as a driver C++ identifier + CLI flag
    // (see is_valid_param_name). Reject anything unsafe — a backtick/`$`/Unicode
    // identifier, a C++ keyword (`default`, `class`, …), a reserved flag
    // (`seed`/`help`/`h`), `main`'s `argc`/`argv`, or a leading-underscore name
    // that could shadow a driver-internal local — with a clear message rather
    // than silently miscompiling the generated driver.
    for (const auto& p : params) {
      if (!is_valid_param_name(p.name)) {
        fail("test parameter '" + p.name
             + "' is not a usable simulation parameter name: it must be a plain identifier "
               "(a letter followed by letters/digits/underscores), not a leading-underscore name, "
               "a C++ keyword, or a reserved driver flag (seed/help/h/argc/argv) — rename it");
      }
    }

    std::ostringstream o;
    o << "// Generated by lhd sim (prp_sim) for test `" << name << "`. Do not edit.\n";
    for (const auto& [m, inst] : insts_) {
      o << "#include \"" << duts_.at(m).hpp << "\"\n";
    }
    // slop.hpp brings in hlop_set_random_seed() even for a test with no DUT
    // include (it is transitively included by every DUT header otherwise).
    o << "#include \"slop.hpp\"\n";
    o << "#include <cstdio>\n#include <cstdint>\n#include <cstdlib>\n#include <cerrno>\n#include <string>\n\n";
    o << "int main(int argc, char** argv) {\n";
    o << "  long _fails = 0;\n";

    // ---- Runtime arguments. Every driver accepts `--seed N` (hlop PRNG seed)
    // and `--help`; each test parameter becomes a `--<name> N` flag. Values are
    // bound here from argv, never baked in, so one built binary re-runs with any
    // parameters / seed. A parameter defaults to its signature default; one with
    // no default is required (missing -> usage + nonzero exit).
    o << "\n  unsigned long long _seed = " << kDefaultSeed << ";  // hlop PRNG seed (--seed N)\n";
    for (const auto& p : params) {
      o << "  long " << p.name << " = " << (p.has_default ? p.default_expr : "0") << ";  bool _set_" << p.name
        << " = " << (p.has_default ? "true" : "false") << ";\n";
      locals_.erase(p.name);  // bound as an argument, not a zero-init body local
    }
    // --help / usage text, columns aligned to the widest flag.
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("--seed N", std::string("hlop PRNG seed for random/unknown bits (default ") + kDefaultSeedShown + ")");
    for (const auto& p : params) {
      rows.emplace_back("--" + p.name + " N",
                        p.has_default ? ("test parameter (default " + p.default_expr + ")")
                                      : std::string("test parameter (required)"));
    }
    rows.emplace_back("--help, -h", "show this message and exit");
    size_t flag_w = 0;
    for (const auto& [flag, desc] : rows) {
      flag_w = std::max(flag_w, flag.size());
    }
    o << "\n  auto _usage = [&]() {\n";
    o << "    std::printf(\"usage: %s [options]   (test `" << c_str_lit(name) << "`)\\n\", argv[0]);\n";
    o << "    std::printf(\"options:\\n\");\n";
    for (const auto& [flag, desc] : rows) {
      const std::string pad(flag_w - flag.size() + 2, ' ');
      o << "    std::printf(\"  " << c_str_lit(flag) << pad << c_str_lit(desc) << "\\n\");\n";
    }
    o << "  };\n";
    // Argument loop: accepts `--key value` and `--key=value`.
    o << "  for (int _i = 1; _i < argc; ++_i) {\n";
    o << "    std::string _arg = argv[_i], _key = _arg, _val;\n";
    o << "    bool _has_val = false;\n";
    o << "    if (auto _eq = _arg.find('='); _eq != std::string::npos) { _key = _arg.substr(0, _eq); _val = "
         "_arg.substr(_eq + 1); _has_val = true; }\n";
    o << "    auto _need = [&]() -> std::string { if (_has_val) return _val; if (_i + 1 < argc) return argv[++_i]; "
         "std::fprintf(stderr, \"lhd sim: missing value for %s\\n\", _key.c_str()); _usage(); std::exit(2); };\n";
    // Numeric parse with full validation: the whole token must convert (base 0,
    // so 0x.. / decimal) and stay in range. A typo or out-of-range value exits
    // with a clear message instead of silently becoming 0 / clamped. `_to_i64`
    // is emitted only when there is a parameter to use it (every driver still
    // needs `_to_u64` for `--seed`).
    if (!params.empty()) {
      o << "    auto _to_i64 = [&](const std::string& _s) -> long { errno = 0; char* _e = nullptr; long _r = "
           "std::strtol(_s.c_str(), &_e, 0); if (_e == _s.c_str() || *_e != '\\0' || errno == ERANGE) { "
           "std::fprintf(stderr, \"lhd sim: %s expects an integer, got '%s'\\n\", _key.c_str(), _s.c_str()); _usage(); "
           "std::exit(2); } return _r; };\n";
    }
    // `--seed` is non-negative: reject a leading `-` (strtoull would wrap it).
    o << "    auto _to_u64 = [&](const std::string& _s) -> unsigned long long { errno = 0; char* _e = nullptr; "
         "unsigned long long _r = std::strtoull(_s.c_str(), &_e, 0); if (_s.empty() || _s[0] == '-' || _e == "
         "_s.c_str() || *_e != '\\0' || errno == ERANGE) { std::fprintf(stderr, \"lhd sim: %s expects a non-negative "
         "integer, got '%s'\\n\", _key.c_str(), _s.c_str()); _usage(); std::exit(2); } return _r; };\n";
    o << "    if (_key == \"--help\" || _key == \"-h\") { _usage(); return 0; }\n";
    o << "    else if (_key == \"--seed\") { _seed = _to_u64(_need()); }\n";
    for (const auto& p : params) {
      o << "    else if (_key == \"--" << c_str_lit(p.name) << "\") { " << p.name << " = _to_i64(_need()); _set_"
        << p.name << " = true; }\n";
    }
    o << "    else { std::fprintf(stderr, \"lhd sim: unknown argument '%s'\\n\", _key.c_str()); _usage(); return 2; }\n";
    o << "  }\n";
    for (const auto& p : params) {
      if (!p.has_default) {
        o << "  if (!_set_" << p.name << ") { std::fprintf(stderr, \"lhd sim: test `" << c_str_lit(name) << "` requires --"
          << c_str_lit(p.name) << " <value>\\n\"); _usage(); return 2; }\n";
      }
    }
    // Seed BEFORE any Slop/Dlop randomness — reset_cycle() below may draw it.
    o << "  hlop_set_random_seed(_seed);\n\n";

    // ---- DUT instances (after the seed so reset uses the seeded PRNG).
    for (const auto& [m, inst] : insts_) {
      o << "  " << duts_.at(m).cls << " " << inst << "; " << inst << ".reset_cycle();\n";
      if (!vcd_dir_.empty()) {
        // one VCD per test: <vcd_dir>/<test>.vcd (suffixed by DUT when >1 instance)
        std::string vf = vcd_dir_ + "/" + name + (insts_.size() > 1 ? ("." + m) : std::string{}) + ".vcd";
        o << "  " << inst << ".__vcd_path = \"" << vf << "\";\n";
      }
    }
    for (const auto& v : locals_) {
      o << "  long " << v << " = 0;\n";
    }
    for (const auto& [aname, elems] : arrays_) {
      o << "  long " << aname << "[] = {";
      for (size_t i = 0; i < elems.size(); ++i) {
        o << (i != 0 ? ", " : "") << elems[i];
      }
      o << "};\n";
    }
    for (const auto& [sv, m] : struct_vars_) {
      o << "  " << duts_.at(m).cls << "::Out " << sv << ";\n";
    }
    for (auto s : stmts) {
      gen_stmt(o, s, 1);
    }
    // The driver's stdout is the test's runtime output (puts + any ASSERT
    // FAILED lines); the exit code is the verdict. The `lhd sim` runner renders
    // the per-test pass/fail + overall status (consistent with --diag-fmt).
    o << "  return _fails ? 1 : 0;\n}\n";
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

  // The test's `(params)` in declaration order. Values are bound at run time
  // (the generated `--<name>` flags), so this only records each parameter's
  // name and its default (a `param:T` / `param:T=nil` form has no default and
  // is required).
  std::vector<Param> read_params(TSNode test) {
    std::vector<Param> out;
    TSNode             inp = field(test, "input");
    if (ts_node_is_null(inp)) {
      return out;
    }
    std::string pname;
    TSNode      pdef{};
    bool        have = false;
    auto        flush = [&]() {
      if (!have) {
        return;
      }
      // `param:T=nil` declares an explicit NO default — same as omitting `=...`
      // (05b-statements.md): the value MUST be supplied at run time, a `nil`
      // reaching the body is an error, never a silent 0.
      Param p;
      p.name        = pname;
      p.has_default = !ts_node_is_null(pdef) && text_of(src_, pdef) != "nil";
      if (p.has_default) {
        p.default_expr = expr(pdef);  // the parameter's default expression
      }
      out.push_back(std::move(p));
      have = false;
      pdef = TSNode{};
    };
    // arg_list named children are: typed_identifier [, default-expr], repeated.
    // A parameter with no default is immediately followed by the next param.
    for (uint32_t i = 0; i < ts_node_named_child_count(inp); ++i) {
      TSNode ci = ts_node_named_child(inp, i);
      if (ntype(ci) == "typed_identifier") {
        flush();
        TSNode id = field(ci, "identifier");
        pname     = ts_node_is_null(id) ? std::string{} : std::string(text_of(src_, id));
        have      = true;
      } else if (have) {
        pdef = ci;  // the parameter's default expression (next named child)
      }
    }
    flush();
    return out;
  }

  const std::string&                src_;
  const std::map<std::string, Dut>& duts_;
  std::string                       vcd_dir_;  // empty = no VCD; else <vcd_dir>/<test>.vcd per test
  std::set<std::string>                           locals_;       // scalar driver vars
  std::map<std::string, std::string>              insts_;        // mod name -> instance var
  std::map<std::string, std::vector<std::string>> arrays_;       // array name -> element exprs
  std::map<std::string, std::string>              struct_vars_;  // multi-output DUT capture var -> mod

  bool is_dut_call(TSNode n, std::string& mod) const {
    if (ntype(n) != "function_call_expression") {
      return false;
    }
    TSNode fn = field(n, "function");
    if (ts_node_is_null(fn)) {
      return false;
    }
    std::string nm = text_of(src_, fn);
    if (duts_.count(nm) != 0) {
      mod = nm;
      return true;
    }
    return false;
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
      // Only the count and the body carry test locals / DUT calls. The
      // `clocks=(name=ratio)` / `resets=(name=ticks)` clauses hold signal-name
      // labels, NOT test variables -- don't collect them as driver locals.
      discover_node(field(n, "value"));
      discover_node(field(n, "code"));
      return;
    }
    if (t == "assignment") {
      std::string ln = lvalue_name(src_, field(n, "lvalue"));
      TSNode      rv = field(n, "rvalue");
      std::string cmod;
      if (!ln.empty()) {
        if (!ts_node_is_null(rv) && ntype(rv) == "tuple_sq") {
          std::vector<std::string> elems;
          for (uint32_t i = 0; i < ts_node_named_child_count(rv); ++i) {
            elems.push_back(expr(ts_node_named_child(rv, i)));
          }
          arrays_[ln] = elems;
        } else if (!ts_node_is_null(rv) && is_dut_call(rv, cmod) && duts_.at(cmod).outputs.size() > 1) {
          struct_vars_[ln] = cmod;  // captures the whole Out struct (multi-output DUT)
        } else {
          locals_.insert(ln);
        }
      }
    }
    std::string mod;
    if (is_dut_call(n, mod) && insts_.count(mod) == 0) {
      insts_[mod] = mod + "_inst";
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
      discover_node(ts_node_named_child(n, i));
    }
  }

  // ---- expression -> C++ (int64-valued) -------------------------------------
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
      for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
        TSNode c  = ts_node_named_child(n, i);
        auto   ct = ntype(c);
        if (ct == "integer_literal") {
          return text_of(src_, c);
        }
        if (ct == "bool_literal") {
          return text_of(src_, c) == "true" ? "1" : "0";
        }
      }
      return text_of(src_, n);
    }
    if (t == "integer_literal") {
      return text_of(src_, n);
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
      for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
        TSNode c  = ts_node_named_child(n, i);
        auto   ct = ntype(c);
        if (ct == "op_log_not" || ct == "op_not" || ct == "op_sub" || ct == "unary_operator") {
          op = map_op(text_of(src_, c));
        } else {
          opnd = expr(c);
        }
      }
      return "(" + op + opnd + ")";
    }
    if (t == "dot_expression") {
      TSNode base = ts_node_named_child(n, 0);
      std::string bname = text_of(src_, base);
      if (ts_node_named_child_count(n) >= 2 && struct_vars_.count(bname) != 0) {
        std::string fld = text_of(src_, ts_node_named_child(n, 1));
        return "(" + bname + "." + fld + ".to_just_i64())";
      }
      fail("unsupported dot expression: " + text_of(src_, n).substr(0, 40));
    }
    if (t == "member_selection") {
      // base[index]: base identifier + `select` ([idx])
      TSNode base = ts_node_named_child(n, 0);
      std::string idx;
      for (uint32_t i = 1; i < ts_node_named_child_count(n); ++i) {
        TSNode c = ts_node_named_child(n, i);
        if (ntype(c) == "select") {
          for (uint32_t j = 0; j < ts_node_named_child_count(c); ++j) {
            idx = expr(ts_node_named_child(c, j));
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
    std::string out = "(";
    for (uint32_t i = 0; i < nc; ++i) {
      TSNode c = ts_node_named_child(n, i);
      auto   t = ntype(c);
      if (t == "binary_compare_op" || t == "binary_other_op" || t == "binary_times_op" || t == "binary_logical_op"
          || t == "binary_step_op") {
        out += " " + map_op(text_of(src_, c)) + " ";
      } else if (t == "unary_operator" || t == "op_not") {
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
    if (t == "if_expression") {
      gen_if(o, n, depth);
      return;
    }
    if (t == "control_statement") {
      for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
        auto ct = ntype(ts_node_named_child(n, i));
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
      std::string mod;
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
      if (is_dut_call(n, mod)) {
        // bare DUT call (result unused) — still advance the clock
        o << ind;
        gen_dut_cycle(o, n, mod, /*capture=*/"", depth);
        return;
      }
    }
    fail(std::string("unsupported statement in test: `") + std::string(t) + "` -> "
         + text_of(src_, n).substr(0, 40));
  }

  void gen_assignment(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    std::string lname = lvalue_name(src_, field(n, "lvalue"));
    TSNode      rv    = field(n, "rvalue");
    if (lname.empty()) {
      fail("unsupported assignment lvalue: " + text_of(src_, n).substr(0, 40));
    }
    if (!ts_node_is_null(rv) && ntype(rv) == "tuple_sq") {
      return;  // array literal: declared once at function scope (see discover)
    }
    std::string mod;
    if (!ts_node_is_null(rv) && is_dut_call(rv, mod)) {
      gen_dut_cycle(o, rv, mod, lname, depth);
      return;
    }
    if (ts_node_is_null(rv)) {
      return;  // e.g. `mut v = nil` (declared as 0 already)
    }
    o << ind << lname << " = " << expr(rv) << ";\n";
  }

  void gen_if(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    TSNode      cond;
    std::vector<TSNode> scopes;
    for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) {
      TSNode c = ts_node_named_child(n, i);
      if (ntype(c) == "scope_statement") {
        scopes.push_back(c);
      } else if (ts_node_is_null(cond)) {
        cond = c;
      }
    }
    if (ts_node_is_null(cond) || scopes.empty()) {
      fail("unsupported if form in test");
    }
    o << ind << "if (" << expr(cond) << ") {\n";
    for (uint32_t i = 0; i < ts_node_named_child_count(scopes[0]); ++i) {
      gen_stmt(o, ts_node_named_child(scopes[0], i), depth + 1);
    }
    o << ind << "}";
    if (scopes.size() >= 2) {
      o << " else {\n";
      for (uint32_t i = 0; i < ts_node_named_child_count(scopes[1]); ++i) {
        gen_stmt(o, ts_node_named_child(scopes[1], i), depth + 1);
      }
      o << ind << "}";
    }
    o << "\n";
  }

  // Emit a DUT cycle() call: set all inputs (0 default, override named args),
  // advance one clock, and (optionally) capture the single output into `capture`.
  void gen_dut_cycle(std::ostringstream& o, TSNode call, const std::string& mod, const std::string& capture, int depth) {
    std::string ind(depth * 2, ' ');
    const Dut&  d    = duts_.at(mod);
    std::string inst = insts_.at(mod);
    o << ind << "{ " << d.cls << "::In _in;\n";
    for (const auto& f : d.inputs) {
      o << ind << "  _in." << f << " = decltype(_in." << f << ")::create_integer(0);\n";
    }
    // override provided args
    TSNode args = field(call, "argument");
    if (!ts_node_is_null(args)) {
      for (uint32_t i = 0; i < ts_node_named_child_count(args); ++i) {
        TSNode a = ts_node_named_child(args, i);
        if (ntype(a) == "arg_assignment") {
          TSNode an = field(a, "lvalue");
          TSNode av = field(a, "rvalue");
          if (!ts_node_is_null(an) && !ts_node_is_null(av)) {
            std::string fn = text_of(src_, an);
            o << ind << "  _in." << fn << " = decltype(_in." << fn << ")::create_integer(" << expr(av) << ");\n";
          }
        } else {
          fail("unsupported DUT call argument (use name=value)");
        }
      }
    }
    o << ind << "  " << inst << ".cycle(_in);  // advance one clock\n";
    if (!capture.empty()) {
      // observe the POST-edge output (peek = current-state outputs, no advance)
      if (struct_vars_.count(capture) != 0) {
        o << ind << "  " << capture << " = " << inst << ".peek(_in);\n";  // whole Out struct
      } else if (d.outputs.size() == 1) {
        o << ind << "  auto _o = " << inst << ".peek(_in);\n";
        o << ind << "  " << capture << " = _o." << d.outputs[0] << ".to_just_i64();\n";
      } else {
        fail("DUT '" + mod + "' has " + std::to_string(d.outputs.size())
             + " outputs; capture into a single var requires a `const o = ...` struct binding");
      }
    }
    o << ind << "}\n";
  }

  // Pull the single `name=value` entry out of a `clocks=(...)` / `resets=(...)`
  // tuple. A single clock and a single reset are supported for now (mixing
  // clocks/reset polarities is a future "split the lgraph per clock" feature),
  // so >1 entry is a hard error. Returns false when the clause is absent.
  bool tick_one_entry(TSNode tup, const char* what, std::string& name, std::string& val) {
    if (ts_node_is_null(tup)) {
      return false;
    }
    TSNode   entry = {};
    unsigned seen  = 0;
    for (uint32_t i = 0; i < ts_node_named_child_count(tup); ++i) {
      TSNode a = ts_node_named_child(tup, i);
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
      fail(std::string("only a single ") + what + " is supported for now (got " + std::to_string(seen) + ")");
    }
    TSNode an = field(entry, "lvalue");
    TSNode av = field(entry, "rvalue");
    if (ts_node_is_null(an) || ts_node_is_null(av)) {
      fail(std::string("tick ") + what + " entry must be `name=value`");
    }
    name = text_of(src_, an);
    val  = expr(av);
    return true;
  }

  void gen_tick(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    TSNode      cnt  = field(n, "value");
    TSNode      code = field(n, "code");
    if (ts_node_is_null(cnt) || ts_node_is_null(code)) {
      fail("tick requires a count and a body (unbounded `tick {}` not yet supported)");
    }
    // Optional clocks=(name=ratio) / resets=(name=ticks): configure the clock
    // ratio + reset window on every DUT instance before the loop. These members
    // live on every top DUT whether or not a VCD is dumped (the reset must
    // assert identically in both cases), so the setup is NOT guarded by vcd_dir_.
    std::string clk_name, clk_ratio, rst_name, rst_ticks;
    bool        have_clk = tick_one_entry(field(n, "clocks"), "clock", clk_name, clk_ratio);
    bool        have_rst = tick_one_entry(field(n, "resets"), "reset", rst_name, rst_ticks);
    if (have_clk || have_rst) {
      for (const auto& [m, inst] : insts_) {
        if (have_clk) {
          o << ind << inst << ".__clk_ratio = (unsigned)(" << clk_ratio << ");\n";
          o << ind << inst << ".__clk_name = \"" << clk_name << "\";\n";
        }
        if (have_rst) {
          o << ind << inst << ".__rst_ticks = (unsigned)(" << rst_ticks << ");\n";
          o << ind << inst << ".__rst_name = \"" << rst_name << "\";\n";
          o << ind << inst << ".__rst_base = " << inst << ".__vcd_tick;\n";
        }
      }
    }
    std::string count = expr(cnt);
    o << ind << "for (long _t = 0; _t < (long)(" << count << "); ++_t) {\n";
    std::vector<TSNode> body;
    for (uint32_t i = 0; i < ts_node_named_child_count(code); ++i) {
      body.push_back(ts_node_named_child(code, i));
    }
    for (auto s : body) {
      gen_stmt(o, s, depth + 1);
    }
    o << ind << "}\n";
  }

  void gen_assert(std::ostringstream& o, TSNode n, int depth) {
    std::string ind(depth * 2, ' ');
    TSNode      args = field(n, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      fail("assert needs a condition");
    }
    TSNode cond = ts_node_named_child(args, 0);
    std::string msg = "assertion failed";
    if (ts_node_named_child_count(args) >= 2) {
      msg = text_of(src_, ts_node_named_child(args, 1));
      // strip surrounding quotes for the format string
      if (msg.size() >= 2 && msg.front() == '"') {
        msg = msg.substr(1, msg.size() - 2);
      }
    }
    o << ind << "if (!(" << expr(cond) << ")) { std::printf(\"  ASSERT FAILED: %s\\n\", \"" << msg
      << "\"); ++_fails; }\n";
  }

  // `puts("text {var} {}", expr)` — runtime print. The interpolated string is
  // lowered to a printf: `{name}` -> the in-scope value `name`, `{}` -> the next
  // positional arg, all as %ld (driver values are C++ long). `puts` appends a
  // newline; `print` does not.
  void gen_puts(std::ostringstream& o, TSNode n, int depth, bool newline) {
    std::string ind(depth * 2, ' ');
    TSNode      args = field(n, "argument");
    if (ts_node_is_null(args) || ts_node_named_child_count(args) < 1) {
      o << ind << "std::printf(\"" << (newline ? "\\n" : "") << "\");\n";
      return;
    }
    // format string = first arg (a constant wrapping a [interpolated_]string_literal)
    std::string lit = text_of(src_, ts_node_named_child(args, 0));
    if (lit.size() >= 2 && lit.front() == '"' && lit.back() == '"') {
      lit = lit.substr(1, lit.size() - 2);
    }
    // positional args after the format string
    std::vector<std::string> pos;
    for (uint32_t i = 1; i < ts_node_named_child_count(args); ++i) {
      pos.push_back(expr(ts_node_named_child(args, i)));
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
        fmt += "%ld";
        argv.push_back(name.empty() ? (pi < pos.size() ? pos[pi++] : std::string("0"))
                                    : ("(long)(" + name + ")"));
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
    o << ind << "std::printf(\"" << fmt << (newline ? "\\n" : "") << "\"";
    for (const auto& a : argv) {
      o << ", " << a;
    }
    o << ");\n";
  }
};

}  // namespace

int generate(const std::string& file, const std::string& simdir, const std::string& test_sel, const std::string& vcd_dir,
             std::vector<Driver>& out, std::string& err) {
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
    if (fn.rfind("drv_", 0) == 0) {
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

  // 2. parse the source for its test blocks
  std::string src = slurp(file);
  if (src.empty()) {
    err = "could not read source file: " + file;
    return 1;
  }
  prpparse::Source_buffer buf(file, src);
  prpparse::Parser        parser(buf);
  prpparse::Ast*          root = nullptr;
  try {
    root = parser.parse_ast();
  } catch (...) {
    err = "parse error in " + file;
    return 1;
  }
  TSNode root_node{root, &buf};

  Driver_gen gen(src, duts, vcd_dir);
  int        matched = 0;
  for (uint32_t i = 0; i < ts_node_named_child_count(root_node); ++i) {
    TSNode c = ts_node_named_child(root_node, i);
    if (ntype(c) != "test_statement") {
      continue;
    }
    TSNode name_node = field(c, "name");
    if (ts_node_is_null(name_node)) {
      continue;
    }
    std::string name = text_of(src, name_node);
    if (!test_sel.empty() && name != test_sel) {
      continue;
    }
    if (std::getenv("PRP_SIM_DUMP") != nullptr) {
      dump_node(src, c, 0);
    }
    ++matched;
    std::string code;
    try {
      code = gen.emit(c, name);
    } catch (const Gen_error& ge) {
      err = "test '" + name + "': " + ge.msg;
      return 1;
    }
    std::string base = "drv_" + sanitize(name);
    std::ofstream ofs(simdir + "/" + base + ".cpp");
    ofs << code;
    ofs.close();
    out.push_back(Driver{name, base});
  }
  if (matched == 0) {
    err = test_sel.empty() ? ("no test blocks found in " + file) : ("no test named '" + test_sel + "' in " + file);
    return 1;
  }
  return 0;
}

}  // namespace prp_sim
