//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Unified LNAST/LGraph inspection tools: cat, grep, diff, and tree.

#include "lhd_kernel_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <limits>
#include <regex>
#include <set>
#include <sstream>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "hhds/graph.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "node_util.hpp"

namespace lhd {

// ===========================================================================
// `lhd tool`: unified ln/lg inspector — cat / grep / diff / tree.
// Replaces the former ln.cat / ln.diff. Verbs are polymorphic over ln:/lg:.
// ===========================================================================

enum class Tool_target { node, pin, edge, all };

Tool_target parse_tool_target(const std::string& s) {
  if (s.empty() || s == "all") {
    return Tool_target::all;
  }
  if (s == "node") {
    return Tool_target::node;
  }
  if (s == "pin") {
    return Tool_target::pin;
  }
  if (s == "edge") {
    return Tool_target::edge;
  }
  throw Lhd_error{"usage", std::format("--target expects node|pin|edge|all, got '{}'", s), ""};
}

// One AND-combined filter term, `field:value` (or a bare token: numeric=>id,
// text=>name). Numeric fields support comparisons/ranges + `nil`; string
// fields default to substring, `~`=regex, `=`=exact.
struct Tool_filter {
  enum class Kind { sub, re, eq, num_eq, num_gt, num_lt, num_ge, num_le, num_range, is_nil, any_sub };
  std::string field;
  Kind        kind = Kind::sub;
  std::string sval;
  std::regex  re;
  long        n1 = 0;
  long        n2 = 0;
};

bool tool_is_numeric_field(std::string_view f) {
  return f == "id" || f == "nid" || f == "color" || f == "bits" || f == "delay" || f == "hier_color" || f == "match";
}

// The columns a `<field><sep><value>` filter may target. A token whose head is
// not one of these is treated as a bare match-everything term instead, so a
// value that merely contains ':'/'=' (e.g. a src path `x.prp:5`) is not
// mis-split into a bogus field.
bool tool_is_known_field(std::string_view f) {
  return f == "nid" || f == "id" || f == "kind" || f == "name" || f == "color" || f == "src" || f == "partitionable" || f == "bits"
         || f == "signed" || f == "from" || f == "to" || f == "delay" || f == "hier_color" || f == "match";
}

long tool_parse_long(std::string_view v, std::string_view ctx) {
  size_t consumed = 0;
  long   n        = 0;
  try {
    n = std::stol(std::string{v}, &consumed);
  } catch (const std::exception&) {
    consumed = 0;
  }
  if (v.empty() || consumed != v.size()) {
    throw Lhd_error{"usage", std::format("filter '{}': expected an integer, got '{}'", ctx, v), ""};
  }
  return n;
}

Tool_filter parse_tool_filter(const std::string& tok) {
  Tool_filter f;
  // A field filter is `<field><op><value>`, where <op> starts at the first of
  // ':' '=' '>' '<' '~' and <field> (the text before it) is a known column.
  // Pyrope reads `a:b` as "a has type b", so '=' is the preferred separator
  // (`name=get_mask`, `kind=get_mask`, `color=nil`); '>'/'<' may lead directly
  // (`bits>8`) and ':' stays accepted out of habit. Anything else — a token
  // with no operator, or whose head is not a known field (e.g. the src path
  // `x.prp:5`) — is a bare match-everything term: a substring tested against
  // every column and the node/pin identity, so `lhd tool grep get_mask lg:dir`
  // lights up the get_mask cells exactly as `cat` shows them.
  auto sep = tok.find_first_of(":=<>~");
  if (sep == std::string::npos || !tool_is_known_field(std::string_view{tok}.substr(0, sep))) {
    f.kind = Tool_filter::Kind::any_sub;
    f.sval = tok;
    return f;
  }
  std::string field = tok.substr(0, sep);
  std::string val   = tok.substr(sep);  // operator(s) + value; e.g. ">8", "=nil", ":Mult"
  f.field           = field;
  if (!val.empty() && (val.front() == ':' || val.front() == '=')) {
    val.erase(0, 1);  // strip one equality separator; a relational op may remain (`=>8`, `:>8`)
  }
  if (val == "nil") {
    f.kind = Tool_filter::Kind::is_nil;
    return f;
  }
  if (tool_is_numeric_field(field)) {
    if (val.starts_with(">=")) {
      f.kind = Tool_filter::Kind::num_ge;
      f.n1   = tool_parse_long(val.substr(2), tok);
    } else if (val.starts_with("<=")) {
      f.kind = Tool_filter::Kind::num_le;
      f.n1   = tool_parse_long(val.substr(2), tok);
    } else if (val.starts_with(">")) {
      f.kind = Tool_filter::Kind::num_gt;
      f.n1   = tool_parse_long(val.substr(1), tok);
    } else if (val.starts_with("<")) {
      f.kind = Tool_filter::Kind::num_lt;
      f.n1   = tool_parse_long(val.substr(1), tok);
    } else if (auto rp = val.find(".."); rp != std::string::npos) {
      f.kind = Tool_filter::Kind::num_range;
      f.n1   = tool_parse_long(val.substr(0, rp), tok);
      f.n2   = tool_parse_long(val.substr(rp + 2), tok);
    } else {
      f.kind = Tool_filter::Kind::num_eq;
      f.n1   = tool_parse_long(val, tok);
    }
  } else if (val.starts_with("~")) {
    f.kind = Tool_filter::Kind::re;
    try {
      f.re = std::regex(val.substr(1));
    } catch (const std::regex_error& e) {
      throw Lhd_error{"usage", std::format("filter '{}': bad regex: {}", tok, e.what()), ""};
    }
  } else if (val.starts_with("=")) {
    f.kind = Tool_filter::Kind::eq;
    f.sval = val.substr(1);
  } else {
    f.kind = Tool_filter::Kind::sub;
    f.sval = val;
  }
  return f;
}

// A flattened inspector record. `cols` carries every filterable+displayable
// field (value "nil" = unset); `ident` is the always-shown identity.
struct Tool_record {
  char                                             type = 'n';  // n|p|e
  std::string                                      ident;
  std::vector<std::pair<std::string, std::string>> cols;
};

const std::string* tool_col(const Tool_record& r, std::string_view key) {
  for (const auto& [k, v] : r.cols) {
    if (k == key) {
      return &v;
    }
  }
  return nullptr;
}

bool tool_match(const Tool_record& r, const Tool_filter& f) {
  if (f.kind == Tool_filter::Kind::any_sub) {  // bare term: substring vs identity + every set column
    if (r.ident.find(f.sval) != std::string::npos) {
      return true;
    }
    for (const auto& [k, val] : r.cols) {
      if (val != "nil" && val.find(f.sval) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
  const std::string* v = tool_col(r, f.field);
  if (v == nullptr && f.field == "id") {
    v = tool_col(r, "nid");  // 'id' aliases 'nid'
  }
  if (v == nullptr) {
    return false;  // field not applicable to this entity
  }
  if (f.kind == Tool_filter::Kind::is_nil) {
    return *v == "nil";
  }
  if (*v == "nil") {
    return false;
  }
  switch (f.kind) {
    case Tool_filter::Kind::sub: return v->find(f.sval) != std::string::npos;
    case Tool_filter::Kind::re: return std::regex_search(*v, f.re);
    case Tool_filter::Kind::eq: return *v == f.sval;
    default: break;
  }
  long n = tool_parse_long(*v, f.field);
  switch (f.kind) {
    case Tool_filter::Kind::num_eq: return n == f.n1;
    case Tool_filter::Kind::num_gt: return n > f.n1;
    case Tool_filter::Kind::num_lt: return n < f.n1;
    case Tool_filter::Kind::num_ge: return n >= f.n1;
    case Tool_filter::Kind::num_le: return n <= f.n1;
    case Tool_filter::Kind::num_range: return n >= f.n1 && n <= f.n2;
    default: return false;
  }
}

bool tool_match_all(const Tool_record& r, const std::vector<Tool_filter>& filters) {
  for (const auto& f : filters) {
    if (!tool_match(r, f)) {
      return false;
    }
  }
  return true;
}

std::string tool_endpoint_name(const hhds::Pin_class& pin) {
  namespace gu = livehd::graph_util;
  if (gu::is_graph_input_pin(pin) || gu::is_graph_output_pin(pin)) {
    return std::format("${}", pin.get_pin_name());
  }
  auto node = pin.get_master_node();
  auto pn   = gu::pin_name_of(pin);
  if (pn.empty()) {
    return std::format("{}.p{}", gu::debug_name(node), pin.get_port_id());
  }
  return std::format("{}.{}", gu::debug_name(node), pn);
}

std::string tool_node_src(hhds::Graph* g, const hhds::Node_class& node) {
  auto ref = node.attr(hhds::attrs::srcid);
  if (!ref.has()) {
    return "nil";
  }
  auto sp = g->source_locator().resolve_span(ref.get());
  if (sp.start_line) {
    return std::format("{}:{}", sp.file.empty() ? std::string{"?"} : sp.file, *sp.start_line);
  }
  return sp.file.empty() ? std::string{"nil"} : sp.file;
}

std::string tool_color_str(const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  return gu::has_node_color(node) ? std::to_string(gu::node_color_of(node)) : std::string{"nil"};
}

// The pass/semdiff structural-correspondence id. "nil" until semdiff has run
// (the attribute is absent); 0 is a real value meaning "no counterpart".
std::string tool_match_str(const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  return gu::has_match(node) ? std::to_string(gu::match_of(node)) : std::string{"nil"};
}
std::string tool_match_str(const hhds::Pin_class& pin) {
  namespace gu = livehd::graph_util;
  return gu::has_match(pin) ? std::to_string(gu::match_of(pin)) : std::string{"nil"};
}

std::string tool_pin_label(const hhds::Pin_class& pin) {
  auto pn = livehd::graph_util::pin_name_of(pin);
  return pn.empty() ? std::format("p{}", pin.get_port_id()) : std::string{pn};
}

Tool_record tool_node_record(hhds::Graph* g, const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  Tool_record r;
  r.type  = 'n';
  r.ident = gu::debug_name(node);
  r.cols.emplace_back("nid", std::to_string(static_cast<uint64_t>(node.get_debug_nid())));
  r.cols.emplace_back("kind", std::string{Ntype::get_name(gu::type_op_of(node))});
  auto nm = gu::node_name_of(node);
  r.cols.emplace_back("name", nm.empty() ? std::string{"nil"} : std::string{nm});
  r.cols.emplace_back("color", tool_color_str(node));
  r.cols.emplace_back("match", tool_match_str(node));
  r.cols.emplace_back("src", tool_node_src(g, node));
  r.cols.emplace_back("partitionable", livehd::color::is_partitionable(node) ? "1" : "0");
  return r;
}

// Output (driver) pins of `node`, deduped — the "what is this signal's width"
// view. An input pin is some other node's output, so it is not lost.
void tool_pin_records(const hhds::Node_class& node, std::vector<Tool_record>& out) {
  namespace gu = livehd::graph_util;
  std::set<std::pair<int, std::string>> seen;
  for (const auto& e : node.out_edges()) {
    const auto& pin = e.driver;
    auto        pn  = gu::pin_name_of(pin);
    if (!seen.insert({pin.get_port_id(), std::string{pn}}).second) {
      continue;
    }
    Tool_record r;
    r.type  = 'p';
    r.ident = tool_endpoint_name(pin);
    r.cols.emplace_back("nid", std::to_string(static_cast<uint64_t>(node.get_debug_nid())));
    r.cols.emplace_back("name", pn.empty() ? std::string{"nil"} : std::string{pn});
    int32_t b = gu::bits_of(pin);
    r.cols.emplace_back("bits", b != 0 ? std::to_string(b) : std::string{"nil"});
    r.cols.emplace_back("signed", gu::is_unsign(pin) ? "0" : "1");
    r.cols.emplace_back("match", tool_match_str(pin));
    out.push_back(std::move(r));
  }
}

void tool_edge_records(const hhds::Node_class& node, std::vector<Tool_record>& out) {
  namespace gu = livehd::graph_util;
  for (const auto& e : node.out_edges()) {
    std::string from = tool_endpoint_name(e.driver);
    std::string to   = tool_endpoint_name(e.sink);
    int32_t     b    = gu::bits_of(e.driver);
    Tool_record r;
    r.type  = 'e';
    r.ident = std::format("{} -> {}  ({}b)", from, to, b);
    r.cols.emplace_back("from", from);
    r.cols.emplace_back("to", to);
    r.cols.emplace_back("bits", b != 0 ? std::to_string(b) : std::string{"nil"});
    out.push_back(std::move(r));
  }
}

void tool_flat_records(hhds::Graph* g, Tool_target tgt, std::vector<Tool_record>& recs) {
  for (auto node : g->forward_class()) {
    if (tgt == Tool_target::node || tgt == Tool_target::all) {
      recs.push_back(tool_node_record(g, node));
    }
    if (tgt == Tool_target::pin || tgt == Tool_target::all) {
      tool_pin_records(node, recs);
    }
    if (tgt == Tool_target::edge || tgt == Tool_target::all) {
      tool_edge_records(node, recs);
    }
  }
}

std::vector<std::string> tool_split_csv(const std::string& s) {
  std::vector<std::string> out;
  size_t                   start = 0;
  while (start <= s.size()) {
    auto end = s.find(',', start);
    out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return out;
}

std::vector<std::string> tool_display_cols(const Options& opts, Tool_target tgt) {
  if (!opts.tool_attr.empty()) {
    return tool_split_csv(opts.tool_attr);
  }
  switch (tgt) {
    case Tool_target::node: return {"color", "match", "src"};
    case Tool_target::pin: return {"bits", "signed", "match"};
    case Tool_target::edge: return {"bits"};
    default: return {"color", "match", "src", "bits", "signed"};  // target=all flat (grep)
  }
}

std::string tool_render_pretty(const Tool_record& r, const std::vector<std::string>& cols) {
  std::string line = r.ident;
  for (const auto& key : cols) {
    const std::string* v = tool_col(r, key);
    if (v == nullptr) {
      continue;
    }
    if (key == "signed") {  // cosmetic: bare `signed` when set, omit otherwise
      if (*v == "1") {
        line += "  signed";
      }
      continue;
    }
    if (*v == "nil") {  // omit unset/absent attributes to cut verbosity
      continue;
    }
    line += std::format("  {}={}", key, *v);
  }
  return line;
}

std::string tool_render_jsonl(const Tool_record& r, std::string_view mod) {
  std::string out = "{";
  out += std::format("\"t\":\"{}\"", r.type == 'n' ? "node" : (r.type == 'p' ? "pin" : "edge"));
  out += std::format(",\"mod\":\"{}\"", json_escape_min(mod));
  for (const auto& [k, v] : r.cols) {
    if (v == "nil") {
      out += std::format(",\"{}\":null", k);
    } else if (k == "signed" || k == "partitionable") {
      out += std::format(",\"{}\":{}", k, v == "1" ? "true" : "false");
    } else if (tool_is_numeric_field(k) || k == "bits") {
      out += std::format(",\"{}\":{}", k, v);
    } else {
      out += std::format(",\"{}\":\"{}\"", k, json_escape_min(v));
    }
  }
  out += "}";
  return out;
}

// Load one lg: library and return the graphs selected by --top (all when
// --top is empty), sorted by name. The graphs outlive `var` (owned by the
// Hhds_graph_library singleton keyed on `dir`).
std::vector<std::shared_ptr<hhds::Graph>> tool_select_graphs(const std::string& dir, const Options& opts) {
  if (!fs::is_directory(dir)) {
    throw Lhd_error{"missing_file", std::format("lg: input not found: {}", dir), "an lg: input is a GraphLibrary directory"};
  }
  // With --top, resolve just that one graph by name and materialize only it: the
  // GraphLibrary loads bodies lazily, so its sub-instances load on demand as the
  // caller descends (e.g. tool_tree_children -> get_subnode_graph). Iterating the
  // whole library via load_lg_into_var would instead materialize every graph — on
  // a large design (XiangShan: 1630 graphs, top needs ~79) that is the difference
  // between reading a handful of bodies and reading all of them.
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  std::vector<std::shared_ptr<hhds::Graph>> sel;
  if (!opts.top.empty()) {
    if (auto gio = lib.find_io(opts.top)) {
      if (auto g = gio->get_graph()) {
        sel.push_back(g);
      }
    }
    return sel;
  }
  Eprp_var var;
  load_lg_into_var(dir, var);
  for (auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    sel.push_back(g);
  }
  std::sort(sel.begin(), sel.end(), [](const auto& a, const auto& b) { return a->get_name() < b->get_name(); });
  return sel;
}

// Nested per-node block for `cat --target all` (pretty): node header + its
// input pins (with driver) + output pins (with sink).
void tool_cat_all_pretty(hhds::Graph* g, const std::vector<Tool_filter>& filters, std::string& out, size_t& budget,
                         bool& truncated) {
  namespace gu = livehd::graph_util;
  auto take    = [&](const std::string& line) {
    if (budget == 0) {
      truncated = true;
      return false;
    }
    out += line;
    out += '\n';
    --budget;
    return true;
  };
  for (auto node : g->forward_class()) {
    auto nr = tool_node_record(g, node);
    if (!filters.empty() && !tool_match_all(nr, filters)) {
      continue;
    }
    if (!take(std::format("  {}", tool_render_pretty(nr, {"color", "src"})))) {
      return;
    }
    for (const auto& e : node.inp_edges()) {
      // A sink pin has no width of its own — its width is the DRIVER's (`bits` is a
      // driver-pin property; see graph/node_util.hpp set_bits). Read the driver.
      int32_t b = gu::bits_of(e.driver);
      if (!take(std::format("    .{}  bits={}{}  <- {}",
                            tool_pin_label(e.sink),
                            b != 0 ? std::to_string(b) : std::string{"nil"},
                            gu::is_unsign(e.driver) ? "" : " signed",
                            tool_endpoint_name(e.driver)))) {
        return;
      }
    }
    for (const auto& e : node.out_edges()) {
      int32_t b = gu::bits_of(e.driver);
      if (!take(std::format("    .{}  bits={}{}  -> {}",
                            tool_pin_label(e.driver),
                            b != 0 ? std::to_string(b) : std::string{"nil"},
                            gu::is_unsign(e.driver) ? "" : " signed",
                            tool_endpoint_name(e.sink)))) {
        return;
      }
    }
  }
}

size_t tool_budget(const Options& opts) {
  return opts.tool_max <= 0 ? std::numeric_limits<size_t>::max() : static_cast<size_t>(opts.tool_max);
}

// `tool cat lg:…` — single-library structured dump (module headers preserved).
void tool_cat_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (lg_dirs.size() > 1) {
    throw Lhd_error{"usage", "tool cat takes one lg: input (use tool grep for multi-library search)", ""};
  }
  Tool_target tgt   = parse_tool_target(opts.tool_target);
  auto        dcols = tool_display_cols(opts, tgt);
  bool        jsonl = opts.diag_fmt == Diag_fmt::jsonl;
  auto        graphs = tool_select_graphs(lg_dirs.front(), opts);
  if (graphs.empty()) {
    throw Lhd_error{"config", std::format("lg: input {} holds no matching graphs", lg_dirs.front()), "check --top"};
  }
  std::string out;
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  for (const auto& gp : graphs) {
    hhds::Graph* g = gp.get();
    if (!jsonl) {
      out += std::format("module {}\n", g->get_name());
    }
    if (!jsonl && tgt == Tool_target::all) {
      tool_cat_all_pretty(g, filters, out, budget, truncated);
    } else {
      std::vector<Tool_record> recs;
      tool_flat_records(g, tgt, recs);
      for (const auto& r : recs) {
        if (!filters.empty() && !tool_match_all(r, filters)) {
          continue;
        }
        if (budget == 0) {
          truncated = true;
          break;
        }
        out += jsonl ? tool_render_jsonl(r, g->get_name()) : std::format("  {}", tool_render_pretty(r, dcols));
        out += '\n';
        --budget;
      }
    }
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; narrow with --top/filters or raise --max (0 = unlimited)\n",
                       opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// `tool grep lg:… [lg:…]` — flat matches across one or more libraries, each
// line prefixed `lib/`. A filter is required.
void tool_grep_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (filters.empty()) {
    throw Lhd_error{"usage", "tool grep requires at least one filter (e.g. color:nil, name:Mult, bits:>8)", ""};
  }
  Tool_target tgt    = parse_tool_target(opts.tool_target);
  auto        dcols  = tool_display_cols(opts, tgt);
  bool        jsonl  = opts.diag_fmt == Diag_fmt::jsonl;
  std::string out;
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  for (const auto& dir : lg_dirs) {
    std::string lib = fs::path(dir).filename().string();
    if (lib.empty()) {
      lib = dir;
    }
    for (const auto& gp : tool_select_graphs(dir, opts)) {
      hhds::Graph*             g = gp.get();
      std::vector<Tool_record> recs;
      tool_flat_records(g, tgt, recs);
      for (const auto& r : recs) {
        if (tool_match_all(r, filters) == opts.tool_invert) {  // -v keeps the non-matching records
          continue;
        }
        if (budget == 0) {
          truncated = true;
          break;
        }
        if (jsonl) {
          out += tool_render_jsonl(r, std::format("{}/{}", lib, g->get_name()));
        } else {
          out += std::format("{}/{}", lib, g->get_name());
          out += "  ";
          out += tool_render_pretty(r, dcols);
        }
        out += '\n';
        --budget;
      }
      if (truncated) {
        break;
      }
    }
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; tighten filters or raise --max (0 = unlimited)\n", opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// Deterministic per-line listing of one library for `tool diff` (module-prefixed
// so the unified diff stays aligned across modules).
std::vector<std::string> tool_diff_lines(const std::string& dir, Options& opts, Tool_target tgt,
                                         const std::vector<Tool_filter>& filters, const std::vector<std::string>& dcols) {
  std::vector<std::string> lines;
  for (const auto& gp : tool_select_graphs(dir, opts)) {
    hhds::Graph*             g = gp.get();
    std::vector<Tool_record> recs;
    tool_flat_records(g, tgt, recs);
    for (const auto& r : recs) {
      if (!filters.empty() && !tool_match_all(r, filters)) {
        continue;
      }
      lines.push_back(std::format("{}: {}", g->get_name(), tool_render_pretty(r, dcols)));
    }
  }
  return lines;
}

// One node as seen by the match-aware diff: its correspondence id + a label.
struct Match_node {
  uint32_t    id;
  std::string line;  // "<kind>  <ident>  src=…"
};

std::vector<Match_node> tool_match_nodes(hhds::Graph* g) {
  namespace gu = livehd::graph_util;
  std::vector<Match_node> v;
  for (auto node : g->forward_class()) {
    std::string src  = tool_node_src(g, node);
    std::string line = std::format("{:<10}  {}", Ntype::get_name(gu::type_op_of(node)), gu::debug_name(node));
    if (src != "nil") {
      line += std::format("  src={}", src);
    }
    v.push_back({gu::match_of(node), std::move(line)});
  }
  return v;
}

// `tool diff lg:A lg:B --match` — semantic diff driven by the semdiff `match`
// attribute: matched regions (shared id) are the common part (summarized);
// unmatched nodes (match=0) are the actual differences, printed `-` for ref-only
// and `+` for impl-only. Falls back with a hint when neither side was marked.
void tool_diff_match_lg(Options& opts, const std::vector<std::string>& lg_dirs) {
  auto   ga    = tool_select_graphs(lg_dirs[0], opts);
  auto   gb    = tool_select_graphs(lg_dirs[1], opts);
  size_t pairs = std::min(ga.size(), gb.size());

  bool saw_match = false;
  for (size_t i = 0; i < pairs; ++i) {
    if (ga[i]->has_attr(livehd::attrs::match)) {  // a side was marked by semdiff
      saw_match = true;
    }
  }
  if (!saw_match) {
    std::string hint
        = "-- no `match` attribute found; run `lhd pass semdiff --ref lg:… --impl lg:…` first to mark correspondences, "
        "then `lhd tool diff … --match`\n";
    std::fwrite(hint.data(), 1, hint.size(), stdout);
    std::fflush(stdout);
    return;
  }

  std::string out;
  for (size_t i = 0; i < pairs; ++i) {
    hhds::Graph* a = ga[i].get();
    hhds::Graph* b = gb[i].get();
    auto         ra = tool_match_nodes(a);
    auto         rb = tool_match_nodes(b);

    // Matched ids on each side (id != 0); a node is part of the common region.
    std::set<uint32_t> ids_a;
    std::set<uint32_t> ids_b;
    uint32_t           ma = 0;
    uint32_t           mb = 0;
    for (const auto& r : ra) {
      if (r.id != 0) {
        ids_a.insert(r.id);
        ++ma;
      }
    }
    for (const auto& r : rb) {
      if (r.id != 0) {
        ids_b.insert(r.id);
        ++mb;
      }
    }
    size_t shared = 0;
    for (uint32_t id : ids_a) {
      if (ids_b.contains(id)) {
        ++shared;
      }
    }

    out += std::format("//---- tool diff (match) {} vs {}\n", a->get_name(), b->get_name());
    out += std::format("  matched: {} regions ({} ref nodes, {} impl nodes)\n", shared, ma, mb);
    for (const auto& r : ra) {
      if (r.id == 0) {
        out += std::format("  - {}\n", r.line);
      }
    }
    for (const auto& r : rb) {
      if (r.id == 0) {
        out += std::format("  + {}\n", r.line);
      }
    }
    uint32_t ta = static_cast<uint32_t>(ra.size());
    uint32_t tb = static_cast<uint32_t>(rb.size());
    uint32_t tot = ta + tb;
    double   sim = tot == 0 ? 1.0 : static_cast<double>(ma + mb) / static_cast<double>(tot);
    out += std::format("  {}/{} ref matched, {}/{} impl matched, similarity {:.3f}\n", ma, ta, mb, tb, sim);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void tool_diff_lg(Options& opts, const std::vector<std::string>& lg_dirs, const std::vector<Tool_filter>& filters) {
  if (lg_dirs.size() != 2) {
    throw Lhd_error{"usage", "tool diff takes exactly two lg: inputs", "e.g. `lhd tool diff lg:before lg:after --attr color`"};
  }
  if (opts.tool_match) {  // semdiff `match`-attribute visualization
    tool_diff_match_lg(opts, lg_dirs);
    return;
  }
  Tool_target tgt   = parse_tool_target(opts.tool_target);
  auto        dcols = tool_display_cols(opts, tgt);
  auto        a     = tool_diff_lines(lg_dirs[0], opts, tgt, filters, dcols);
  auto        b     = tool_diff_lines(lg_dirs[1], opts, tgt, filters, dcols);
  std::string out;
  if (a == b) {
    out += "identical\n";
  } else {
    print_line_diff(out, a, b, static_cast<size_t>(opts.tool_context));
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

size_t tool_node_count(hhds::Graph* g) {
  // fast_class (raw node_table walk), NOT forward_class: a count is
  // order-independent, and forward_class would build a topological order that
  // reads every edge — which for a persisted graph forces its overflow
  // (edge-adjacency) sets to be loaded. fast_class touches only node_table, so a
  // pure node/instance tree never pays to read edges it will not print.
  size_t n = 0;
  for ([[maybe_unused]] auto node : g->fast_class()) {
    ++n;
  }
  return n;
}

// Does a node's cell kind match one of the `tool tree --target kind:<X>`
// selectors? `register`/`reg` aliases the sequential state cells (flop, fflop,
// latch) but NOT memory; `memory`/`mem` aliases memory; any other token matches
// an Ntype name exactly (flop, mux, sub, …), so the tree can spotlight any kind.
bool tool_tree_kind_match(Ntype_op op, const std::vector<std::string>& kinds) {
  std::string_view name = Ntype::get_name(op);
  for (const auto& k : kinds) {
    if (k == "register" || k == "reg" || k == "registers") {
      if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch) {
        return true;
      }
    } else if (k == "memory" || k == "mem" || k == "memories") {
      if (op == Ntype_op::Memory) {
        return true;
      }
    } else if (k == name) {
      return true;
    }
  }
  return false;
}

// Width of a node's first non-zero output pin (the "register/memory holds N
// bits" view). 0 = unknown (no sized driver pin, e.g. a dead flop).
int32_t tool_tree_node_bits(const hhds::Node_class& node) {
  namespace gu = livehd::graph_util;
  for (const auto& e : node.out_edges()) {
    if (auto b = gu::bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
}

// List the nodes of `g` whose kind matches `kinds`, indented to sit beside the
// module's sub-instances (a register/memory is content of the module, not a
// child in the call tree). No-op when `kinds` is empty (the default tree).
void tool_tree_kind_nodes(hhds::Graph* g, const std::vector<std::string>& kinds, int indent, std::string& out, size_t& budget,
                          bool& truncated) {
  namespace gu = livehd::graph_util;
  if (kinds.empty()) {
    return;
  }
  for (auto node : g->forward_class()) {  // topological => deterministic order
    auto op = gu::type_op_of(node);
    if (op == Ntype_op::Sub || !tool_tree_kind_match(op, kinds)) {
      continue;  // Sub instances are the call tree itself (printed elsewhere)
    }
    if (budget == 0) {
      truncated = true;
      return;
    }
    auto bits = tool_tree_node_bits(node);
    out += std::format("{}{}  : {}{}\n",
                       std::string(static_cast<size_t>(indent), ' '),
                       gu::default_instance_name(node),
                       Ntype::get_name(op),
                       bits != 0 ? std::format("  ({}b)", bits) : std::string{});
    --budget;
  }
}

void tool_tree_children(hhds::Graph* g, const std::vector<std::string>& kinds, int maxdepth, int depth,
                        std::set<hhds::Gid>& on_path, std::string& out, size_t& budget, bool& truncated) {
  namespace gu = livehd::graph_util;
  if (depth >= maxdepth) {
    return;
  }
  for (auto node : g->fast_class()) {
    if (node.get_subnode_gid() == hhds::Gid_invalid) {
      continue;
    }
    auto sub = node.get_subnode_graph();
    if (!sub) {
      continue;
    }
    if (budget == 0) {
      truncated = true;
      return;
    }
    out += std::format("{}{}  : {}  [{} nodes]\n",
                       std::string(static_cast<size_t>(depth + 1) * 2, ' '),
                       gu::default_instance_name(node),
                       sub->get_name(),
                       tool_node_count(sub.get()));
    --budget;
    // The instance's registers/memories sit one level in, alongside its own
    // sub-instances (which the recursion below prints at the same indent).
    tool_tree_kind_nodes(sub.get(), kinds, (depth + 2) * 2, out, budget, truncated);
    if (truncated) {
      return;
    }
    hhds::Gid gid = node.get_subnode_gid();
    if (on_path.insert(gid).second) {  // cycle guard
      tool_tree_children(sub.get(), kinds, maxdepth, depth + 1, on_path, out, budget, truncated);
      on_path.erase(gid);
    }
    if (truncated) {
      return;
    }
  }
}

void tool_tree_lg(Options& opts, const std::vector<std::string>& lg_dirs) {
  if (lg_dirs.size() != 1) {
    throw Lhd_error{"usage", "tool tree takes one lg: input", ""};
  }
  int maxdepth = opts.tool_hier < 0 ? std::numeric_limits<int>::max() : opts.tool_hier;  // tree: full by default
  auto graphs   = tool_select_graphs(lg_dirs.front(), opts);
  if (graphs.empty()) {
    throw Lhd_error{"config", std::format("lg: input {} holds no matching graphs", lg_dirs.front()), "check --top"};
  }
  std::string out;
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  for (const auto& gp : graphs) {
    hhds::Graph* g = gp.get();
    out += std::format("{}  [{} nodes]\n", g->get_name(), tool_node_count(g));
    tool_tree_kind_nodes(g, opts.tool_kinds, 2, out, budget, truncated);  // top module's own regs/mems
    if (truncated) {
      break;
    }
    std::set<hhds::Gid> on_path;
    tool_tree_children(g, opts.tool_kinds, maxdepth, 0, on_path, out, budget, truncated);
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; raise --max (0 = unlimited)\n", opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// ── `tool tree ln:` — the LNAST counterpart of the lg instance tree ─────────
// `tool cat ln:` already dumps every node; `tool tree ln:` is the SUMMARY: only
// the scope / control-flow / call / def nodes that give the tree its shape, each
// annotated with the total LNAST node count of its subtree (the "[N nodes]" stat,
// like the lg tree). Everything else (declares, stores, operators, refs, consts,
// types) is collapsed. `--target kind:<verbal>` adds extra node kinds; `--hier N`
// limits depth; `--max N` caps rows. Rendered with box-drawing connectors.
struct Ln_tree_row {
  std::string              label;
  std::vector<Ln_tree_row> children;
};

bool tool_tree_ln_skeleton(Lnast_ntype::Lnast_ntype_int t) {
  using L = Lnast_ntype;
  return L::is_top(t) || L::is_stmts(t) || L::is_if(t) || L::is_unique_if(t) || L::is_for(t) || L::is_while(t) || L::is_func_def(t)
         || L::is_func_call(t) || L::is_io(t);
}

// `--target kind:<X>` for the ln tree: X names an Lnast verbal (store, declare,
// if, …), matched exactly. Additive — the skeleton set is always shown too.
bool tool_tree_ln_kind_match(Lnast_ntype::Lnast_ntype_int t, const std::vector<std::string>& kinds) {
  if (kinds.empty()) {
    return false;
  }
  std::string_view name = Lnast_ntype::to_sv(t);
  for (const auto& k : kinds) {
    if (k == name) {
      return true;
    }
  }
  return false;
}

// fcall/fdef carry their identity in the leading ref/const children (fcall:
// instance, callee, arg-stores…; fdef: name, …). Surface them the way the lg
// tree shows `instance : Module`. Other nodes append their own name if any.
std::string tool_tree_ln_label(const Lnast& ln, const Lnast_nid& nid, Lnast_ntype::Lnast_ntype_int type, size_t total) {
  std::string head{Lnast_ntype::to_sv(type)};
  if (Lnast_ntype::is_func_call(type) || Lnast_ntype::is_func_def(type)) {
    std::vector<std::string_view> names;
    for (auto c = ln.get_first_child(nid); !c.is_invalid() && names.size() < 2; c = ln.get_sibling_next(c)) {
      auto ct = ln.get_type(c);
      if (!Lnast_ntype::is_ref(ct) && !Lnast_ntype::is_const(ct)) {
        break;  // leading name run ended
      }
      names.push_back(ln.get_name(c));
    }
    if (names.size() >= 2) {
      head += std::format(" {} : {}", names[0], names[1]);
    } else if (names.size() == 1) {
      head += std::format(" {}", names[0]);
    }
  } else if (auto n = ln.get_name(nid); !n.empty()) {
    head += std::format(" {}", n);
  }
  return std::format("{}  [{} nodes]", head, total);
}

// Build the skeleton for the subtree at `nid` into `sink` (each shown node
// attaches to its nearest shown ancestor). Returns the total LNAST node count of
// the subtree — ALL nodes, shown or collapsed — so a scope's count reflects
// everything it holds. `sink == nullptr` ⇒ count only (over depth / suppressed).
size_t tool_tree_ln_collect(const Lnast& ln, const Lnast_nid& nid, int depth, int maxdepth, const std::vector<std::string>& kinds,
                            std::vector<Ln_tree_row>* sink) {
  auto type  = ln.get_type(nid);
  bool shown = tool_tree_ln_skeleton(type) || tool_tree_ln_kind_match(type, kinds);

  std::vector<Ln_tree_row>  kids;
  std::vector<Ln_tree_row>* child_sink = sink;  // non-shown nodes are transparent (pass through)
  bool                      emit       = false;
  if (shown) {
    if (sink != nullptr && depth < maxdepth) {
      emit       = true;
      child_sink = &kids;
    } else {
      child_sink = nullptr;  // over depth (or already suppressed): count only
    }
  }
  int child_depth = shown ? depth + 1 : depth;  // only shown nodes consume a level

  size_t total = 1;
  for (auto c = ln.get_first_child(nid); !c.is_invalid(); c = ln.get_sibling_next(c)) {
    total += tool_tree_ln_collect(ln, c, child_depth, maxdepth, kinds, child_sink);
  }
  if (emit) {
    sink->push_back(Ln_tree_row{tool_tree_ln_label(ln, nid, type, total), std::move(kids)});
  }
  return total;
}

void tool_tree_ln_print(const Ln_tree_row& row, const std::string& prefix, bool last, std::string& out, size_t& budget,
                        bool& truncated) {
  if (budget == 0) {
    truncated = true;
    return;
  }
  out += prefix;
  out += last ? "└── " : "├── ";
  out += row.label;
  out += '\n';
  --budget;
  std::string child_prefix = prefix + (last ? "    " : "│   ");
  for (size_t i = 0; i < row.children.size(); ++i) {
    tool_tree_ln_print(row.children[i], child_prefix, i + 1 == row.children.size(), out, budget, truncated);
    if (truncated) {
      return;
    }
  }
}

void tool_tree_ln(Options& opts, Result& res, const std::vector<std::string>& ln_tokens) {
  auto in = classify_ln_inputs(ln_tokens, "tool tree");
  for (const auto& d : opts.in_dirs) {  // --in-dir ln:DIR spelling
    if (d.kind == "ln") {
      in.ln_dirs.push_back(d.path);
    }
  }
  auto units = sorted_by_name(filter_top(ln_tool_units(opts, res, in), opts.top));
  if (units.empty()) {
    throw Lhd_error{"config", "ln: input holds no matching units", "check --top"};
  }

  int         maxdepth  = opts.tool_hier < 0 ? std::numeric_limits<int>::max() : opts.tool_hier;  // tree: full by default
  size_t      budget    = tool_budget(opts);
  bool        truncated = false;
  std::string out;
  for (const auto& ln : units) {
    // The unit's root (top/stmts/func_def) is the header; its children are the
    // box-tree body — so the count beside the header is the whole-unit total.
    std::vector<Ln_tree_row> body;
    size_t                   total = 1;  // the root node itself
    for (auto c = ln->get_first_child(ln->get_root()); !c.is_invalid(); c = ln->get_sibling_next(c)) {
      total += tool_tree_ln_collect(*ln, c, 0, maxdepth, opts.tool_kinds, &body);
    }
    std::string suffix{ln->get_lambda_kind()};
    if (ln->is_verilog_origin()) {
      suffix += suffix.empty() ? "verilog" : ", verilog";
    }
    out += std::format("{}  [{} nodes]{}\n",
                       ln->get_top_module_name(),
                       total,
                       suffix.empty() ? std::string{} : std::format("  ({})", suffix));
    for (size_t i = 0; i < body.size(); ++i) {
      tool_tree_ln_print(body[i], "", i + 1 == body.size(), out, budget, truncated);
      if (truncated) {
        break;
      }
    }
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    out += std::format("-- output truncated at --max {} rows; raise --max (0 = unlimited)\n", opts.tool_max);
  }
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// A positional token's input kind: "lg" / "ln" / "src" (.prp/.v/.sv) / "" (a
// filter term). lg: prefix strips to a directory; ln:/source keep the raw token
// for classify_ln_inputs.
std::string tool_input_kind(const std::string& t) {
  if (auto pos = t.find(':'); pos != std::string::npos && pos != 0) {
    auto k = t.substr(0, pos);
    if (k == "lg" || k == "lgraph" || k == "design") {
      return "lg";
    }
    if (k == "ln" || k == "lnast") {
      return "ln";
    }
    if (k == "verilog" || k == "pyrope") {
      return "src";  // explicit source scheme (URL-like); classify_ln_inputs strips the prefix
    }
  }
  std::string_view sv{t};
  if (sv.ends_with(".prp") || sv.ends_with(".v") || sv.ends_with(".sv")) {
    return "src";  // bare-extension shortcut for pyrope:/verilog:
  }
  return "";
}

void tool_command(Options& opts, Result& res) {
  setup_diag(opts, "tool");
  if (opts.files.empty()) {
    throw Lhd_error{"usage",
                    "tool requires a verb: cat | grep | diff | tree",
                    "e.g. `lhd tool cat lg:dir` or `lhd tool grep color:nil lg:dir`"};
  }
  const std::string verb = opts.files[0];
  if (verb != "cat" && verb != "grep" && verb != "diff" && verb != "tree") {
    throw Lhd_error{"usage", std::format("unknown tool verb '{}'", verb), "use: cat | grep | diff | tree"};
  }

  std::vector<std::string> lg_dirs;
  std::vector<std::string> ln_tokens;  // ln:/source tokens, raw for classify_ln_inputs
  std::vector<Tool_filter> filters;
  for (size_t k = 1; k < opts.files.size(); ++k) {
    const std::string& t    = opts.files[k];
    auto               kind = tool_input_kind(t);
    if (kind == "lg") {
      lg_dirs.push_back(t.substr(t.find(':') + 1));
    } else if (kind == "ln" || kind == "src") {
      ln_tokens.push_back(t);
    } else {
      filters.push_back(parse_tool_filter(t));
    }
  }
  for (const auto& d : opts.in_dirs) {  // --in-dir ln:DIR spelling
    if (d.kind == "ln") {
      ln_tokens.push_back("ln:" + d.path);
    }
  }
  for (const auto& d : opts.ins) {  // --in lg:DIR spelling
    if (d.kind == "lg") {
      lg_dirs.push_back(d.path);
    }
  }

  const bool have_lg = !lg_dirs.empty();
  const bool have_ln = !ln_tokens.empty();
  if (have_lg && have_ln) {
    throw Lhd_error{"usage", "tool takes either lg: or ln:/source inputs, not both", ""};
  }
  for (const auto& d : lg_dirs) {  // reject a corrupt/non-GraphLibrary dir cleanly before the hhds loader asserts
    check_lg_input_dir(d);
  }

  if (verb == "tree") {
    if (have_ln) {
      tool_tree_ln(opts, res, ln_tokens);  // LNAST structural skeleton
      return;
    }
    if (!have_lg) {
      throw Lhd_error{"usage",
                      "tool tree needs an lg: or ln:/source input",
                      "e.g. `lhd tool tree lg:dir --top m` or `lhd tool tree ln:dir`"};
    }
    tool_tree_lg(opts, lg_dirs);
    return;
  }
  if (verb == "grep") {
    if (!have_lg) {
      throw Lhd_error{"usage", "tool grep needs lg: input(s)", "ln: grep is not yet supported"};
    }
    tool_grep_lg(opts, lg_dirs, filters);
    return;
  }
  if (verb == "cat") {
    if (have_ln) {
      if (!filters.empty()) {
        throw Lhd_error{"usage", "tool cat ln: does not take filters (LNAST cat is whole-unit)", ""};
      }
      tool_cat_ln(opts, res, ln_tokens);
    } else if (have_lg) {
      tool_cat_lg(opts, lg_dirs, filters);
    } else {
      throw Lhd_error{"usage", "tool cat needs an lg:DIR, ln:DIR, or .prp/.v/.sv input", ""};
    }
    return;
  }
  // verb == "diff"
  if (have_ln) {
    if (!filters.empty()) {
      throw Lhd_error{"usage", "tool diff ln: does not take filters", ""};
    }
    tool_diff_ln(opts, res, ln_tokens);
  } else if (have_lg) {
    tool_diff_lg(opts, lg_dirs, filters);
  } else {
    throw Lhd_error{"usage", "tool diff needs two lg: or two ln:/source inputs", ""};
  }
}

}  // namespace lhd
