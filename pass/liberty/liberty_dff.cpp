// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "liberty_dff.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
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

// A `next_state` that is one pin or its complement: `D`, `!D`, `D'`, `(!D)`,
// `!(D)`, `(D)'`, spaces anywhere. Sets `pin` and `inverted`; false for any
// other expression (scan muxes like `(SE*SI)+(!SE*D)`, enables, constants).
// ASAP7 spells every QN flop's next state as "!D": the cell stores !D and
// exposes it on QN, i.e. QN(t+1) = !D(t) -- an ideal D flop whose one output is
// inverted -- so the complement is not a different flop, just a sign to carry.
bool parse_next_state(std::string_view expr, std::string& pin, bool& inverted) {
  std::string s;
  for (char c : expr) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      s.push_back(c);
    }
  }
  inverted = false;
  for (int guard = 0; guard < 16; ++guard) {  // guard: a hostile string of 16+ nested parens is not a pin
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
      s = s.substr(1, s.size() - 2);
      continue;
    }
    if (!s.empty() && s.front() == '!') {
      inverted = !inverted;
      s.erase(0, 1);
      continue;
    }
    if (!s.empty() && s.back() == '\'') {
      inverted = !inverted;
      s.pop_back();
      continue;
    }
    break;
  }
  if (!is_bare_ident(s)) {
    return false;
  }
  pin = s;
  return true;
}

// `body` with every nested `{ ... }` group blanked to spaces, so a scalar
// attribute lookup sees only the group's OWN attributes. Both PDKs put `area :`
// before the pin groups, but a `pin` group may carry its own `area`-like keys
// (`max_capacitance`, ...) and the order is not guaranteed by the format.
std::string top_level_only(const std::string& body) {
  std::string o = body;
  int         d = 0;
  bool        q = false;
  for (size_t i = 0; i < o.size(); ++i) {
    char c = o[i];
    if (q) {
      if (c == '"') {
        q = false;
      }
      if (d > 0) {
        o[i] = ' ';
      }
      continue;
    }
    if (c == '"') {
      q = true;
      if (d > 0) {
        o[i] = ' ';
      }
    } else if (c == '{') {
      ++d;
      o[i] = ' ';
    } else if (c == '}') {
      --d;
      o[i] = ' ';
    } else if (d > 0) {
      o[i] = ' ';
    }
  }
  return o;
}

double parse_area(const std::string& body) {
  const std::string v = scalar_attr(top_level_only(body), "area");
  double            a = 0;
  auto [p, ec]        = std::from_chars(v.data(), v.data() + v.size(), a);
  if (ec != std::errc{} || a < 0) {
    return 0;
  }
  return a;
}

// Picoseconds per Liberty time unit, from a `time_unit : "1ns"` value
// (`<number><fs|ps|ns|us>`, spaces tolerated). The Liberty default is 1ns.
// ASAP7 says "1ps", sky130 "1ns" -- and ABC normalizes every SCL library to ps
// on read, so the flop overhead has to be in the same unit as the `delay`
// target the sizing commands take.
double time_unit_ps(std::string_view value) {
  std::string v;
  for (char c : value) {
    if (!std::isspace(static_cast<unsigned char>(c)) && c != '"') {
      v.push_back(c);
    }
  }
  double       n     = 1;
  auto [p, ec]       = std::from_chars(v.data(), v.data() + v.size(), n);
  std::string_view u = v;
  if (ec == std::errc{}) {
    u = std::string_view{p, static_cast<size_t>(v.data() + v.size() - p)};
  } else {
    n = 1;
  }
  if (u == "fs") {
    return n * 0.001;
  }
  if (u == "ps") {
    return n;
  }
  if (u == "ns") {
    return n * 1000.0;
  }
  if (u == "us") {
    return n * 1e6;
  }
  return 1000.0;  // absent/unknown: the Liberty default
}

// The `values(...)` matrix of a Liberty table group: rows are the quoted
// strings (`\\` line continuations sit between them and are skipped with
// everything else outside the quotes), a 1-D table is one row. Empty when the
// group has no parsable values.
std::vector<std::vector<double>> table_values(const std::string& table_body) {
  std::vector<std::vector<double>> rows;
  size_t                           v = table_body.find("values");
  while (v != std::string::npos && v > 0 && ident_char(table_body[v - 1])) {
    v = table_body.find("values", v + 1);
  }
  if (v == std::string::npos) {
    return rows;
  }
  const size_t open = table_body.find('(', v);
  if (open == std::string::npos) {
    return rows;
  }
  const size_t close = table_body.find(')', open);
  if (close == std::string::npos) {
    return rows;
  }
  for (size_t q0 = table_body.find('"', open); q0 != std::string::npos && q0 < close; q0 = table_body.find('"', q0 + 1)) {
    const size_t q1 = table_body.find('"', q0 + 1);
    if (q1 == std::string::npos || q1 > close) {
      break;
    }
    std::vector<double> row;
    std::string         text = table_body.substr(q0 + 1, q1 - (q0 + 1));
    for (size_t b = 0; b < text.size();) {
      size_t e = text.find(',', b);
      if (e == std::string::npos) {
        e = text.size();
      }
      size_t tb = b;
      size_t te = e;
      while (tb < te && std::isspace(static_cast<unsigned char>(text[tb]))) {
        ++tb;
      }
      while (te > tb && std::isspace(static_cast<unsigned char>(text[te - 1]))) {
        --te;
      }
      double x     = 0;
      auto [p, ec] = std::from_chars(text.data() + tb, text.data() + te, x);
      if (ec != std::errc{} || p != text.data() + te) {
        return {};  // a value we cannot read: the whole table is untrusted
      }
      row.push_back(x);
      b = e + 1;
    }
    if (!row.empty()) {
      rows.push_back(std::move(row));
    }
    q0 = q1;
  }
  return rows;
}

// Which entry of a timing table stands for the flop overhead. Calibrated
// against OpenSTA's full-path numbers on the lhdtrack netlists (ASAP7:
// OpenSTA minus ABC's SCL comb delay = 75-87 ps on br_arb_rr / br_counter_incr
// with DFFHQNx1; sky130 dfxtp_1: 250-440 ps):
//  - clk->Q: row 0 (index_1 = the clock's input slew; an STA run clocks the
//    launch flop off an IDEAL clock, zero slew, clamped to the first row) at
//    the middle load column (a mapped register drives a few gates, not the
//    table's minimum load). DFFHQNx1 73.0 ps, dfxtp_1 328 ps.
//  - setup: the table's center (a moderate data slew against a moderate
//    clock slew; the two PDKs disagree on which axis is which, and the center
//    is neutral to that). DFFHQNx1 10.9 ps, dfxtp_1 200 ps.
// Together 83.9 ps / 528 ps against the 75-87 / 250-440 measured: a margin
// that errs on the safe side of the period by a few percent rather than
// handing back a region that misses it by the flop it cannot see. The first
// entry (min slew, min load: 46.9 + 10.2 = 57 ps) measured 18-28 ps short on
// both sequential ASAP7 designs.
enum class Table_pick : uint8_t { zero_slew_mid_load, center };

std::optional<double> table_entry(const std::string& table_body, Table_pick pick) {
  const auto rows = table_values(table_body);
  if (rows.empty()) {
    return std::nullopt;
  }
  const size_t r = pick == Table_pick::center ? rows.size() / 2 : 0;
  const auto&  row = rows[r];
  return row[row.size() / 2];
}

// The largest `pick` entry over the `keys` tables of every `timing()` group in
// `pin_body` whose `related_pin` is `related` and whose `timing_type` is `type`
// (`rising_edge` on the output pin for clk->Q, `setup_rising` on the data pin
// for setup). Hold/min_pulse_width/recovery groups are skipped by the type. 0
// when the pin has no such table -- the caller adds nothing then.
double pin_timing_max(const std::string& pin_body, std::string_view related, std::string_view type, Table_pick pick,
                      std::initializer_list<std::string_view> keys) {
  double      best = 0;
  std::string args;
  for (size_t t = find_group(pin_body, "timing", 0, args); t != std::string::npos; t = find_group(pin_body, "timing", t + 1, args)) {
    const size_t tclose = match_brace(pin_body, t);
    if (tclose == std::string::npos) {
      break;
    }
    const std::string tbody = pin_body.substr(t + 1, tclose - (t + 1));
    const std::string top   = top_level_only(tbody);
    if (scalar_attr(top, "related_pin") != related || scalar_attr(top, "timing_type") != type) {
      continue;
    }
    for (const auto key : keys) {
      std::string  targs;
      const size_t k = find_group(tbody, key, 0, targs);
      if (k == std::string::npos) {
        continue;
      }
      const size_t kclose = match_brace(tbody, k);
      if (kclose == std::string::npos) {
        continue;
      }
      if (const auto x = table_entry(tbody.substr(k + 1, kclose - (k + 1)), pick); x.has_value()) {
        best = std::max(best, *x);
      }
    }
  }
  return best;
}

// Try to read a plain posedge D-flop out of one `cell { body }`. `name` is the
// cell name (already unquoted). Returns nullopt when the cell is not a plain
// posedge D-flop.
std::optional<Dff_cell> parse_cell(const std::string& name, const std::string& body, double unit_ps) {
  std::string ff_args;
  size_t      ff_open = find_group(body, "ff", 0, ff_args);
  if (ff_open == std::string::npos) {
    return std::nullopt;  // combinational (or a latch, which we do not map)
  }
  size_t ff_close = match_brace(body, ff_open);
  if (ff_close == std::string::npos) {
    return std::nullopt;
  }
  std::string ff_body    = body.substr(ff_open + 1, ff_close - (ff_open + 1));
  std::string next_state = scalar_attr(ff_body, "next_state");
  std::string clocked_on = scalar_attr(ff_body, "clocked_on");
  // Plain posedge D-flop only: one D pin (either phase), a bare POSEDGE clock
  // -- `!CLK` is a negedge flop (ASAP7 DFFLQNx1 is otherwise identical to
  // DFFHQNx1, and mapping a posedge register onto it would be a miscompile) --
  // and no async clear/preset (those need a reset-cell mapping we do not do).
  std::string next_pin;
  bool        next_inv = false;
  if (!parse_next_state(next_state, next_pin, next_inv) || !is_bare_ident(clocked_on)) {
    return std::nullopt;
  }
  if (!scalar_attr(ff_body, "clear").empty() || !scalar_attr(ff_body, "preset").empty()) {
    return std::nullopt;
  }
  // ff state vars: the first head arg is the stored state, the second its
  // complement. An output whose function is the state var is a Q candidate,
  // one that reads the complement is a QN candidate.
  std::string state_var;
  std::string nstate_var;
  if (size_t comma = ff_args.find(','); comma != std::string::npos) {
    state_var  = unquote_trim(ff_args.substr(0, comma));
    nstate_var = unquote_trim(ff_args.substr(comma + 1));
  } else {
    state_var = unquote_trim(ff_args);
  }

  // Walk every `pin (NAME) { ... }` group; collect the Q / QN candidates (fall
  // back to the sole output), and confirm D/CLK exist as inputs.
  std::string q_pin;
  std::string qn_pin;
  std::string sole_output;
  int         n_output = 0;
  bool        has_d    = false;
  bool        has_clk  = false;
  double      setup    = 0;
  // Per output pin: its clk->Q first-entry delay, resolved once the Q/QN choice
  // below is made (the overhead is the pin the netlist actually uses).
  std::vector<std::pair<std::string, double>> out_clk_to_q;
  std::string                                 pin_args;
  for (size_t p = find_group(body, "pin", 0, pin_args); p != std::string::npos; p = find_group(body, "pin", p + 1, pin_args)) {
    size_t pclose = match_brace(body, p);
    if (pclose == std::string::npos) {
      break;
    }
    std::string pin_name = unquote_trim(pin_args);
    std::string pin_body = body.substr(p + 1, pclose - (p + 1));
    std::string dir      = scalar_attr(top_level_only(pin_body), "direction");
    if (dir == "output") {
      ++n_output;
      sole_output           = pin_name;
      const std::string fun = scalar_attr(top_level_only(pin_body), "function");
      if (fun == state_var && q_pin.empty()) {
        q_pin = pin_name;
      } else if (!nstate_var.empty() && fun == nstate_var && qn_pin.empty()) {
        qn_pin = pin_name;
      }
      out_clk_to_q.emplace_back(
          pin_name,
          pin_timing_max(pin_body, clocked_on, "rising_edge", Table_pick::zero_slew_mid_load, {"cell_rise", "cell_fall"}));
    } else if (dir == "input") {
      if (pin_name == next_pin) {
        has_d = true;
        setup = pin_timing_max(pin_body, clocked_on, "setup_rising", Table_pick::center, {"rise_constraint", "fall_constraint"});
      }
      if (pin_name == clocked_on) {
        has_clk = true;
      }
    }
  }
  // Prefer the non-inverted output (a dfxbp exposes both; Q keeps the netlist
  // free of a read-back inversion), else the inverted one, else the sole
  // output of a cell whose function names neither state var.
  bool        out_inv = false;
  std::string out_pin = q_pin;
  if (out_pin.empty() && !qn_pin.empty()) {
    out_pin = qn_pin;
    out_inv = true;
  }
  if (out_pin.empty()) {
    if (n_output == 1) {
      out_pin = sole_output;  // single-output flop: unambiguous
    } else {
      return std::nullopt;
    }
  }
  if (!has_d || !has_clk || next_pin == clocked_on) {
    return std::nullopt;
  }
  Dff_cell c;
  c.name    = name;
  c.d_pin   = next_pin;
  c.clk_pin = clocked_on;
  c.q_pin   = out_pin;
  // The two signs compose: ASAP7 DFFHQNx1 stores !D and shows the state
  // (inverted once); a hypothetical `ff(IQ,IQN){next_state:"D"}` with only a
  // QN=IQN output is inverted once too; dfxtp/dfxbp (D, Q=IQ) not at all.
  c.q_inverted = next_inv != out_inv;
  c.area       = parse_area(body);
  c.n_out      = n_output;
  for (const auto& [pin, d] : out_clk_to_q) {
    if (pin == out_pin) {
      c.clk_to_q_ps = d * unit_ps;
    }
  }
  c.setup_ps = setup * unit_ps;
  return c;
}

// Ranking key for the auto-pick: area first (missing area sorts LAST -- a cell
// the library does not size is not a bargain), then the fewest outputs, then a
// true Q over a QN (no inversion to carry), then the name for determinism.
bool rank_less(const Dff_cell& a, const Dff_cell& b) {
  const double aa = a.area > 0 ? a.area : std::numeric_limits<double>::infinity();
  const double ba = b.area > 0 ? b.area : std::numeric_limits<double>::infinity();
  if (aa != ba) {
    return aa < ba;
  }
  if (a.n_out != b.n_out) {
    return a.n_out < b.n_out;
  }
  if (a.q_inverted != b.q_inverted) {
    return !a.q_inverted;
  }
  return a.name < b.name;
}

bool same_shape(const Dff_cell& a, const Dff_cell& b) {
  return a.d_pin == b.d_pin && a.clk_pin == b.clk_pin && a.q_pin == b.q_pin && a.q_inverted == b.q_inverted;
}

}  // namespace

std::vector<Dff_cell> scan_dff_cells(const std::string& lib_files) {
  std::string text = strip_comments(read_files(lib_files));
  // `time_unit` is a library-header attribute; several files may be
  // concatenated here, so each cell takes the unit of the nearest header
  // before it (a file without one gets the Liberty default, 1ns).
  std::vector<std::pair<size_t, double>> units;
  for (size_t u = text.find("time_unit"); u != std::string::npos; u = text.find("time_unit", u + 1)) {
    if (u > 0 && ident_char(text[u - 1])) {
      continue;
    }
    size_t c = u + std::string_view{"time_unit"}.size();
    while (c < text.size() && std::isspace(static_cast<unsigned char>(text[c]))) {
      ++c;
    }
    if (c >= text.size() || text[c] != ':') {
      continue;
    }
    const size_t end = text.find(';', c);
    units.emplace_back(u, time_unit_ps(text.substr(c + 1, (end == std::string::npos ? text.size() : end) - (c + 1))));
  }
  const auto unit_at = [&](size_t pos) {
    double ps = 1000.0;
    for (const auto& [at, u] : units) {
      if (at < pos) {
        ps = u;
      }
    }
    return ps;
  };
  std::string           cell_args;
  std::vector<Dff_cell> found;
  for (size_t p = find_group(text, "cell", 0, cell_args); p != std::string::npos;
       p        = find_group(text, "cell", p + 1, cell_args)) {
    size_t cclose = match_brace(text, p);
    if (cclose == std::string::npos) {
      break;
    }
    if (auto dff = parse_cell(unquote_trim(cell_args), text.substr(p + 1, cclose - (p + 1)), unit_at(p))) {
      found.push_back(std::move(*dff));
    }
  }
  return found;
}

Dff_selection resolve_dff_cells(const std::string& lib_files, std::string_view prefer) {
  Dff_selection sel;
  auto          cells = scan_dff_cells(lib_files);
  if (!prefer.empty()) {
    // Explicit request: take it as-is (the ladder is that one cell -- the user
    // named a drive strength, so no fanout-driven swap to a sibling).
    for (auto& c : cells) {
      if (c.name == prefer) {
        sel.base = c;
        sel.ladder.push_back(c);
        break;
      }
    }
    return sel;
  }
  auto best = std::min_element(cells.begin(), cells.end(), rank_less);
  if (best == cells.end()) {
    return sel;
  }
  sel.base = *best;
  for (const auto& c : cells) {
    if (same_shape(c, *best)) {
      sel.ladder.push_back(c);
    }
  }
  std::stable_sort(sel.ladder.begin(), sel.ladder.end(), rank_less);
  return sel;
}

std::optional<Dff_cell> find_dff_cell(const std::string& lib_files, std::string_view prefer) {
  return resolve_dff_cells(lib_files, prefer).base;
}

std::vector<Dff_cell> find_dff_ladder(const std::string& lib_files, const Dff_cell& base) {
  std::vector<Dff_cell> ladder;
  for (auto& c : scan_dff_cells(lib_files)) {
    if (same_shape(c, base)) {
      ladder.push_back(std::move(c));
    }
  }
  std::stable_sort(ladder.begin(), ladder.end(), rank_less);
  return ladder;
}

std::string dff_descriptor(const Dff_cell& dff) {
  return std::format("{}:{}:{}:{}:{}", dff.name, dff.d_pin, dff.clk_pin, dff.q_pin, dff.q_inverted ? 1 : 0);
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
  hhds::Pin_class din = body->get_input_pin(dff.d_pin);
  if (dff.q_inverted) {
    // A QN cell shows the complement of what it latched (QN = !IQN, IQN <- D)
    // and pass.abc's read-back wires that QN pin as the register's Q, feeding
    // the D pin ~f. Two models are functionally identical from the first clock
    // on -- Not(Flop(D)) (the Liberty's own wording) and Flop(Not(D)) -- but
    // they differ in WHICH bit the model's state cell holds, and pass/lec keys
    // its cut correspondence on exactly that: the impl's `<reg>_<bit>` /
    // `<mem>__mem<i>_<bit>` model flop SHARES its power-on symbol with the
    // source register or memory entry (query.cpp bit-blast + memory-bank
    // bridges), so the state must BE the value the netlist observes at the pin.
    // Not(Flop(D)) shared the complement: every resetless register read before
    // its first write refuted at cycle 0 under ASAP7's DFFHQNx1 while the same
    // netlist proved under sky130's dfxtp_1 (br_fifo_flops pyrope-vs-netlist
    // REFUTED at the staging-buffer counter cut; br_ram_flops_tile 16x32 with
    // EnableReset=0). Hence Flop(Not(D)): the state is the QN pin.
    auto N = gu::create_typed_node(*body, Ntype_op::Not);
    din.connect_sink(gu::setup_sink_by_name(N, "a"));
    din = N.create_driver_pin(0);
    gu::set_bits(din, 1);
    gu::set_unsign(din);
  }
  din.connect_sink(gu::setup_sink_by_name(F, "din"));
  Fq.connect_sink(body->get_output_pin(dff.q_pin));
  body->commit();
}

}  // namespace livehd::liberty
