//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// prp_sim: the `test` blocks of a Pyrope file, listed (names and parameters). The simulator itself is the LLVM
// testbench flow (inou/prp/tb_llvm.cpp, lhd/lhd_sim_llvm_rt.cpp); the generated-C++ driver this file used to
// print is gone (todo/livehd/2sl-llvm-only-sim.md).

#include "prp_sim.hpp"

#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "prp_ast_facade.hpp"
#include "prpparse/lexer.hpp"
#include "prpparse/parser.hpp"
#include "prpparse/source_buffer.hpp"
#include "str_tools.hpp"

namespace prp_sim {

namespace {

TSNode field(TSNode n, const char* name) { return ts_node_child_by_field_name(n, name, static_cast<uint32_t>(std::char_traits<char>::length(name))); }
std::string_view ntype(TSNode n) { return ts_node_type(n); }

std::string text_of(const std::string& src, TSNode n) {
  const auto s = ts_node_start_byte(n);
  const auto e = ts_node_end_byte(n);
  if (ts_node_is_null(n) || e > src.size() || s > e) {
    return {};
  }
  return std::string(str_tools::canonical_escaped_ident(std::string_view(src).substr(s, e - s)));
}

std::string slurp(const std::string& path) {
  std::ifstream      ifs(path, std::ios::binary);
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

std::string json_lit(std::string_view s) {
  std::string o;
  for (const char c : s) {
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"' : o += "\\\""; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default  : o += c;
    }
  }
  return o;
}

// A `test`'s parameter as it appears in the source: name, required (no default or `=nil`), and the default's node.
struct Param_raw {
  std::string name;
  bool        required = false;
  TSNode      default_node{};
};

std::vector<Param_raw> read_params_raw(const std::string& src, TSNode test) {
  std::vector<Param_raw> out;
  TSNode                 inp = field(test, "input");
  if (ts_node_is_null(inp)) {
    return out;
  }
  Param_raw  cur;
  bool       have  = false;
  const auto flush = [&]() {
    if (!have) {
      return;
    }
    cur.required = ts_node_is_null(cur.default_node) || text_of(src, cur.default_node) == "nil";
    out.push_back(cur);
    have = false;
  };
  for (TSNode ci : ts_node_named_children(inp)) {
    if (ntype(ci) == "typed_identifier") {
      flush();
      cur       = Param_raw{};
      TSNode id = field(ci, "identifier");
      cur.name  = ts_node_is_null(id) ? std::string{} : text_of(src, id);
      have      = true;
    } else if (have) {
      cur.default_node = ci;
    }
  }
  flush();
  return out;
}

// Parse `file` and invoke `on_test(test_node, dotted_name)` for each `test_statement` whose name matches `test_sel`
// (empty = all). Returns the match count, or -1 on a read/parse error (with `err` set).
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
    // `cnt`.`basic` and cnt.basic are the same test, however either side spells it.
    const std::string name = str_tools::canonical_escaped_path(text_of(src_out, name_node));
    if (!test_sel.empty() && name != str_tools::canonical_escaped_path(test_sel)) {
      continue;
    }
    on_test(c, name);
    ++matched;
  }
  return matched;
}

}  // namespace

std::string tests_to_json(const std::string& file, const std::vector<Test_info>& tests) {
  std::string j  = "{\"file\":\"";
  j             += json_lit(file);
  j             += "\",\"tests\":[";
  for (size_t ti = 0; ti < tests.size(); ++ti) {
    const auto& t  = tests[ti];
    j             += (ti != 0 ? ",{\"name\":\"" : "{\"name\":\"");
    j             += json_lit(t.name);
    j             += "\",\"params\":[";
    for (size_t pi = 0; pi < t.params.size(); ++pi) {
      const auto& p  = t.params[pi];
      j             += (pi != 0 ? ",{\"name\":\"" : "{\"name\":\"");
      j             += json_lit(p.name);
      j             += "\",\"required\":";
      j             += (p.required ? "true" : "false");
      if (!p.required) {
        j += ",\"default\":\"";
        j += json_lit(p.default_text);
        j += "\"";
      }
      j += "}";
    }
    j += "]}";
  }
  j += "]}";
  return j;
}

int list_tests(const std::string& file, const std::string& test_sel, std::vector<Test_info>& tests, std::string& err) {
  std::string src;
  const int   matched = for_each_test(file, test_sel, src, err, [&](TSNode test, const std::string& name) {
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
