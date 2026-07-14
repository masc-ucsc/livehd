// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "liberty_dff.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

#include "cell.hpp"
#include "dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace livehd::liberty {

namespace {

std::string read_files(const std::string& lib_files) {
  std::string       all;
  std::stringstream ss(lib_files);
  std::string       path;
  while (ss >> path) {
    std::ifstream ifs(path, std::ios::binary);
    if (ifs) {
      all.append(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
      all.push_back('\n');
    }
  }
  return all;
}

// Drop /* */ and // comments (quotes are not comment-bearing in Liberty).
std::string strip_comments(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
        ++i;
      }
      i += 2;
    } else if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
      while (i < s.size() && s[i] != '\n') {
        ++i;
      }
    } else {
      o.push_back(s[i++]);
    }
  }
  return o;
}

// Index of the `}` matching the `{` at `open`, honoring quoted strings.
size_t match_brace(const std::string& s, size_t open) {
  int  d = 0;
  bool q = false;
  for (size_t i = open; i < s.size(); ++i) {
    char c = s[i];
    if (q) {
      if (c == '"') {
        q = false;
      }
    } else if (c == '"') {
      q = true;
    } else if (c == '{') {
      ++d;
    } else if (c == '}') {
      if (--d == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Find `keyword (args) {` as a whole word at/after `from`. On success sets
// `args` to the parenthesized text and returns the index of the opening brace;
// std::string::npos when not found.
size_t find_group(const std::string& s, std::string_view keyword, size_t from, std::string& args) {
  for (size_t p = s.find(keyword, from); p != std::string::npos; p = s.find(keyword, p + 1)) {
    if (p > 0 && ident_char(s[p - 1])) {
      continue;  // part of a longer identifier
    }
    size_t q = p + keyword.size();
    while (q < s.size() && std::isspace(static_cast<unsigned char>(s[q]))) {
      ++q;
    }
    if (q >= s.size() || s[q] != '(') {
      continue;
    }
    size_t close = s.find(')', q);
    if (close == std::string::npos) {
      continue;
    }
    size_t brace = close + 1;
    while (brace < s.size() && std::isspace(static_cast<unsigned char>(s[brace]))) {
      ++brace;
    }
    if (brace >= s.size() || s[brace] != '{') {
      continue;
    }
    args = s.substr(q + 1, close - (q + 1));
    return brace;
  }
  return std::string::npos;
}

// Value of a `key : value ;` attribute inside `body` (unquoted, trimmed). Empty
// when absent. Only scans `body` as given (pass a shallow group body).
std::string scalar_attr(const std::string& body, std::string_view key) {
  size_t from = 0;
  while (true) {
    size_t p = body.find(key, from);
    if (p == std::string::npos) {
      return {};
    }
    from                = p + 1;
    bool word_boundary  = (p == 0 || !ident_char(body[p - 1]));
    size_t after        = p + key.size();
    if (!word_boundary) {
      continue;
    }
    size_t c = after;
    while (c < body.size() && std::isspace(static_cast<unsigned char>(body[c]))) {
      ++c;
    }
    if (c >= body.size() || body[c] != ':') {
      continue;
    }
    ++c;
    size_t end = body.find(';', c);
    if (end == std::string::npos) {
      end = body.size();
    }
    std::string v = body.substr(c, end - c);
    // trim whitespace and surrounding quotes
    size_t b = 0;
    size_t e = v.size();
    while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) {
      ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) {
      --e;
    }
    v = v.substr(b, e - b);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
      v = v.substr(1, v.size() - 2);
    }
    return v;
  }
}

std::string unquote_trim(std::string_view s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (std::isspace(static_cast<unsigned char>(s[b])) || s[b] == '"')) {
    ++b;
  }
  while (e > b && (std::isspace(static_cast<unsigned char>(s[e - 1])) || s[e - 1] == '"')) {
    --e;
  }
  return std::string{s.substr(b, e - b)};
}

// A bare identifier only (a plain pin reference: no operators/parens/negation).
bool is_bare_ident(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (!ident_char(c)) {
      return false;
    }
  }
  return true;
}

// Try to read a plain posedge D-flop out of one `cell { body }`. `name` is the
// cell name (already unquoted). Returns nullopt when the cell is not a plain
// posedge D-flop. On success `*n_out` is the cell's total output-pin count (1 for
// a single-Q dfxtp, 2 for a Q/Q_N dfxbp) so the caller can prefer the simplest.
std::optional<Dff_cell> parse_cell(const std::string& name, const std::string& body, int* n_out) {
  std::string ff_args;
  size_t      ff_open = find_group(body, "ff", 0, ff_args);
  if (ff_open == std::string::npos) {
    return std::nullopt;  // combinational (or a latch, which we do not map)
  }
  size_t ff_close = match_brace(body, ff_open);
  if (ff_close == std::string::npos) {
    return std::nullopt;
  }
  std::string ff_body = body.substr(ff_open + 1, ff_close - (ff_open + 1));
  std::string next_state = scalar_attr(ff_body, "next_state");
  std::string clocked_on = scalar_attr(ff_body, "clocked_on");
  // Plain posedge D-flop only: bare D pin, bare posedge clock, no async
  // clear/preset (those need a reset-cell mapping we do not do here).
  if (!is_bare_ident(next_state) || !is_bare_ident(clocked_on)) {
    return std::nullopt;
  }
  if (!scalar_attr(ff_body, "clear").empty() || !scalar_attr(ff_body, "preset").empty()) {
    return std::nullopt;
  }
  // ff state var (first head arg), used to pick the non-inverting Q output.
  std::string state_var;
  if (size_t comma = ff_args.find(','); comma != std::string::npos) {
    state_var = unquote_trim(ff_args.substr(0, comma));
  } else {
    state_var = unquote_trim(ff_args);
  }

  // Walk every `pin (NAME) { ... }` group; collect the output whose function is
  // the ff state var (fall back to the sole output), and confirm D/CLK exist.
  std::string q_pin;
  std::string sole_output;
  int         n_output = 0;
  bool        has_d    = false;
  bool        has_clk  = false;
  std::string pin_args;
  for (size_t p = find_group(body, "pin", 0, pin_args); p != std::string::npos; p = find_group(body, "pin", p + 1, pin_args)) {
    size_t pclose = match_brace(body, p);
    if (pclose == std::string::npos) {
      break;
    }
    std::string pin_name = unquote_trim(pin_args);
    std::string pin_body = body.substr(p + 1, pclose - (p + 1));
    std::string dir      = scalar_attr(pin_body, "direction");
    if (dir == "output") {
      ++n_output;
      sole_output = pin_name;
      if (scalar_attr(pin_body, "function") == state_var) {
        q_pin = pin_name;
      }
    } else if (dir == "input") {
      if (pin_name == next_state) {
        has_d = true;
      }
      if (pin_name == clocked_on) {
        has_clk = true;
      }
    }
  }
  if (q_pin.empty()) {
    if (n_output == 1) {
      q_pin = sole_output;  // single-output flop: unambiguous
    } else {
      return std::nullopt;
    }
  }
  if (!has_d || !has_clk || next_state == clocked_on) {
    return std::nullopt;
  }
  if (n_out != nullptr) {
    *n_out = n_output;
  }
  return Dff_cell{name, next_state, clocked_on, q_pin};
}

}  // namespace

std::optional<Dff_cell> find_dff_cell(const std::string& lib_files, std::string_view prefer) {
  std::string             text = strip_comments(read_files(lib_files));
  std::string             cell_args;
  std::optional<Dff_cell> best;
  int                     best_outputs = 0;  // fewest-output candidate wins (dfxtp over dfxbp)
  for (size_t p = find_group(text, "cell", 0, cell_args); p != std::string::npos;
       p        = find_group(text, "cell", p + 1, cell_args)) {
    size_t cclose = match_brace(text, p);
    if (cclose == std::string::npos) {
      break;
    }
    std::string name = unquote_trim(cell_args);
    if (!prefer.empty() && name != prefer) {
      continue;
    }
    int n_out = 0;
    if (auto dff = parse_cell(name, text.substr(p + 1, cclose - (p + 1)), &n_out)) {
      if (!prefer.empty()) {
        return dff;  // explicit request: take it as-is
      }
      // Prefer the simplest posedge D-flop: fewest outputs (a single-Q dfxtp,
      // not a Q/Q_N dfxbp), then first-in-file for determinism.
      if (!best.has_value() || n_out < best_outputs) {
        best         = dff;
        best_outputs = n_out;
      }
    } else if (!prefer.empty()) {
      return std::nullopt;  // the requested cell is not a plain posedge D-flop
    }
  }
  return best;
}

std::shared_ptr<hhds::GraphIO> create_dff_io(hhds::GraphLibrary& outlib, const Dff_cell& dff) {
  if (auto existing = outlib.find_io(dff.name)) {
    return existing;
  }
  auto io = outlib.create_io(dff.name);
  io->add_input(dff.d_pin, 1);
  io->set_bits(dff.d_pin, 1);
  io->set_unsign(dff.d_pin, true);
  io->add_input(dff.clk_pin, 2);
  io->set_bits(dff.clk_pin, 1);
  io->set_unsign(dff.clk_pin, true);
  io->add_output(dff.q_pin, 3);
  io->set_bits(dff.q_pin, 1);
  io->set_unsign(dff.q_pin, true);
  return io;
}

void emit_dff_model(hhds::GraphLibrary& outlib, const Dff_cell& dff) {
  if (outlib.find_io(dff.name)) {
    return;  // already modeled
  }
  auto io   = create_dff_io(outlib, dff);
  auto body = io->create_graph();

  auto F  = gu::create_typed_node(*body, Ntype_op::Flop);
  auto Fq = F.create_driver_pin(0);
  gu::set_bits(Fq, 1);
  gu::set_unsign(Fq);
  body->get_input_pin(dff.clk_pin).connect_sink(gu::setup_sink_by_name(F, "clock_pin"));
  body->get_input_pin(dff.d_pin).connect_sink(gu::setup_sink_by_name(F, "din"));
  Fq.connect_sink(body->get_output_pin(dff.q_pin));
  body->commit();
}

}  // namespace livehd::liberty
