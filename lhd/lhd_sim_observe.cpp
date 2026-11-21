//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lhd_sim_observe.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <istream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd::sim_observe {
namespace {

namespace rj      = rapidjson;
using Json_writer = rj::Writer<rj::StringBuffer>;

[[noreturn]] void query_usage(const std::string& msg, const std::string& hint = "") {
  throw std::invalid_argument("--query: " + msg + (hint.empty() ? "" : ". " + hint));
}

struct Q_fail {
  std::string              cls;
  std::string              msg;
  std::string              hint;
  std::vector<std::string> suggestions;
};

// `*` (any run, dots included) and `?` (one char). Deliberately NOT a full
// fnmatch: character classes would collide with the `[index]` memory spelling.
bool glob_match(std::string_view pat, std::string_view s) {
  size_t pi = 0, si = 0, ss = 0;
  size_t star = std::string_view::npos;
  while (si < s.size()) {
    if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == s[si])) {
      ++pi;
      ++si;
    } else if (pi < pat.size() && pat[pi] == '*') {
      star = pi++;
      ss   = si;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      si = ++ss;
    } else {
      return false;
    }
  }
  while (pi < pat.size() && pat[pi] == '*') {
    ++pi;
  }
  return pi == pat.size();
}

size_t edit_distance(std::string_view a, std::string_view b) {
  std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) {
    prev[j] = j;
  }
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    }
    prev = cur;
  }
  return prev[b.size()];
}

// Up to 5 nearest catalog names. An unknown name that merely disappears leaves
// an agent guessing; a name plus its neighbours is actionable.
std::vector<std::string> nearest_names(const Catalog& cat, std::string_view want) {
  std::vector<std::pair<size_t, std::string>> scored;
  scored.reserve(cat.sigs.size());
  for (const auto& s : cat.sigs) {
    scored.emplace_back(edit_distance(want, s.name), s.name);
  }
  std::sort(scored.begin(), scored.end());
  std::vector<std::string> out;
  for (const auto& [d, n] : scored) {
    if (out.size() >= 5 || d > 8) {
      break;
    }
    out.push_back(n);
  }
  return out;
}

// A comparison literal -> exactly ceil(bits/4) lowercase hex digits of the
// two's-complement value at `bits`. ARBITRARY PRECISION on purpose: the plan is
// the wire format for `find`, and --break-when's 64-bit strtoull hole must not
// be recreated one layer down. Accepts decimal (optionally signed) or 0x-hex.
std::string literal_to_hex(std::string_view lit, long bits, std::string& err) {
  while (!lit.empty() && (lit.front() == ' ' || lit.front() == '\t')) {
    lit.remove_prefix(1);
  }
  while (!lit.empty() && (lit.back() == ' ' || lit.back() == '\t')) {
    lit.remove_suffix(1);
  }
  bool neg = false;
  if (!lit.empty() && (lit.front() == '+' || lit.front() == '-')) {
    neg = lit.front() == '-';
    lit.remove_prefix(1);
  }
  std::vector<uint8_t> mag;  // little-endian magnitude
  if (lit.size() > 2 && lit[0] == '0' && (lit[1] == 'x' || lit[1] == 'X')) {
    auto hx = lit.substr(2);
    for (size_t k = 0; k < hx.size(); ++k) {
      const char c = hx[hx.size() - 1 - k];
      int        d = 0;
      if (c >= '0' && c <= '9') {
        d = c - '0';
      } else if (c >= 'a' && c <= 'f') {
        d = c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
        d = c - 'A' + 10;
      } else {
        err = std::format("'{}' is not a hex literal", lit);
        return {};
      }
      if (k / 2 >= mag.size()) {
        mag.push_back(0);
      }
      mag[k / 2] |= static_cast<uint8_t>(d << (4 * (k % 2)));
    }
  } else if (!lit.empty()) {
    for (char c : lit) {
      if (c < '0' || c > '9') {
        err = std::format("'{}' is not a decimal or 0x-hex literal", lit);
        return {};
      }
      unsigned carry = static_cast<unsigned>(c - '0');
      for (auto& b : mag) {
        const unsigned t = static_cast<unsigned>(b) * 10U + carry;
        b                = static_cast<uint8_t>(t & 0xffU);
        carry            = t >> 8;
      }
      while (carry != 0) {
        mag.push_back(static_cast<uint8_t>(carry & 0xffU));
        carry >>= 8;
      }
    }
  } else {
    err = "empty comparison literal";
    return {};
  }
  // Reject a literal the signal cannot hold rather than truncating it: a silent
  // truncation turns a typo into a comparison that never (or always) fires.
  size_t bitlen = 0;
  for (size_t i = mag.size(); i-- > 0;) {
    if (mag[i] != 0) {
      unsigned v = mag[i];
      bitlen     = i * 8;
      while (v != 0) {
        ++bitlen;
        v >>= 1;
      }
      break;
    }
  }
  if (static_cast<long>(bitlen) > bits) {
    err = std::format("literal needs {} bits but the signal is {} bits wide", bitlen, bits);
    return {};
  }
  const size_t nbytes = static_cast<size_t>((bits + 7) / 8);
  mag.resize(nbytes, 0);
  if (neg) {
    unsigned carry = 1;
    for (auto& b : mag) {
      const unsigned t = static_cast<unsigned>(static_cast<uint8_t>(~b)) + carry;
      b                = static_cast<uint8_t>(t & 0xffU);
      carry            = t >> 8;
    }
  }
  if (const long rem = bits % 8; rem != 0 && nbytes != 0) {
    mag[nbytes - 1] &= static_cast<uint8_t>((1U << rem) - 1U);
  }
  const size_t ndig = static_cast<size_t>((bits + 3) / 4);
  std::string  out;
  out.reserve(ndig);
  for (size_t k = ndig; k-- > 0;) {
    const unsigned nib  = (mag[k / 2] >> (4 * (k % 2))) & 0xfU;
    out                += "0123456789abcdef"[nib];
  }
  return out;
}

struct Q_time {
  long cycle    = -1;  // -1 = unbounded on this side
  bool is_event = false;
  long offset   = 0;
};

void reject_unknown_members(const rj::Value& obj, std::initializer_list<std::string_view> known, std::string_view where) {
  for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it) {
    const std::string_view k = it->name.GetString();
    if (std::find(known.begin(), known.end(), k) == known.end()) {
      query_usage(std::format("unknown field '{}' in {}", k, where),
                  "v1 rejects unknown request fields rather than ignoring them (a typo must not silently change the answer)");
    }
  }
}

Q_time parse_time(const rj::Value& v, std::string_view where) {
  if (!v.IsObject()) {
    query_usage(std::format("{} must be an object, e.g. {{\"cycle\": 10}}", where));
  }
  reject_unknown_members(v, {"cycle", "event", "offset", "phase"}, where);
  if (v.HasMember("phase")) {
    if (!v["phase"].IsString() || std::string_view{v["phase"].GetString()} != "post") {
      query_usage(std::format("{}.phase: only \"post\" exists in v1", where),
                  "post is the only observation point replay exposes; a `pre` phase is the first planned extension");
    }
  }
  Q_time t;
  if (v.HasMember("event")) {
    if (!v["event"].IsString() || std::string_view{v["event"].GetString()} != "fail") {
      query_usage(std::format("{}.event: only \"fail\" exists in v1", where));
    }
    t.is_event = true;
    if (v.HasMember("offset")) {
      if (!v["offset"].IsInt64()) {
        query_usage(std::format("{}.offset must be an integer", where));
      }
      t.offset = v["offset"].GetInt64();
    }
    if (v.HasMember("cycle")) {
      query_usage(std::format("{}: give either a cycle or an event, not both", where));
    }
    return t;
  }
  if (v.HasMember("offset")) {
    query_usage(std::format("{}.offset requires an event", where));
  }
  if (!v.HasMember("cycle")) {
    query_usage(std::format("{} needs a \"cycle\" (or an \"event\")", where));
  }
  if (!v["cycle"].IsInt64() || v["cycle"].GetInt64() < 0) {
    query_usage(std::format("{}.cycle must be a non-negative integer", where));
  }
  t.cycle = v["cycle"].GetInt64();
  return t;
}

// A selector: an exact name, or any AND-combination of scope/glob/regex/kind.
struct Q_sel {
  bool        has     = false;
  bool        by_name = false;
  std::string signal;  // exact name (or alias), possibly `mem[3]`
  std::string scope;
  std::string glob;
  std::string regex;
  std::string kind;
};

Q_sel parse_selector(const rj::Value& q, std::string_view where) {
  Q_sel s;
  auto  str = [&](const char* k) -> std::string {
    if (!q[k].IsString()) {
      query_usage(std::format("{}.{} must be a string", where, k));
    }
    s.has = true;
    return q[k].GetString();
  };
  if (q.HasMember("signal")) {
    s.signal  = str("signal");
    s.by_name = true;
  }
  if (q.HasMember("scope")) {
    s.scope = str("scope");
  }
  if (q.HasMember("glob")) {
    s.glob = str("glob");
  }
  if (q.HasMember("regex")) {
    s.regex = str("regex");
  }
  if (q.HasMember("kind")) {
    s.kind                                     = str("kind");
    static constexpr std::string_view kKinds[] = {"flop", "pipe", "memrd", "input", "output", "memory"};
    if (std::find(std::begin(kKinds), std::end(kKinds), std::string_view{s.kind}) == std::end(kKinds)) {
      query_usage(std::format("{}.kind '{}' is not a v1 signal kind", where, s.kind),
                  "flop | pipe | memrd | input | output | memory");
    }
  }
  return s;
}

// Selector -> catalog indices, in CATALOG ORDER (never match order): the
// expansion order has to be reproducible for the response to be diffable.
std::vector<size_t> resolve_sel(const Q_sel& sel, const Catalog& cat, std::string_view where) {
  std::optional<std::regex> re;
  if (!sel.regex.empty()) {
    try {
      re.emplace(sel.regex, std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
      query_usage(std::format("{}.regex '{}' does not compile: {}", where, sel.regex, e.what()));
    }
  }
  std::vector<size_t> out;
  for (size_t i = 0; i < cat.sigs.size(); ++i) {
    const auto& c = cat.sigs[i];
    if (sel.by_name && c.name != sel.signal && c.alias != sel.signal) {
      continue;
    }
    if (!sel.scope.empty() && c.name != sel.scope && !std::string_view{c.name}.starts_with(sel.scope + ".")) {
      continue;
    }
    if (!sel.glob.empty() && !glob_match(sel.glob, c.name)) {
      continue;
    }
    if (re.has_value() && !std::regex_match(c.name, *re)) {
      continue;
    }
    if (!sel.kind.empty() && c.kind != sel.kind) {
      continue;
    }
    out.push_back(i);
  }
  return out;
}

// Exactly-one resolution (`value`, `changes`, a find leaf, a sample entry).
// Zero and many are DIFFERENT failures with different fixes, so they carry
// different classes.
size_t resolve_one(const Q_sel& sel, const Catalog& cat, std::string_view where) {
  auto hits = resolve_sel(sel, cat, where);
  if (hits.empty()) {
    const std::string want = sel.by_name ? sel.signal : std::string{where};
    throw Q_fail{"unknown_signal", std::format("no signal matches {}", want), "", nearest_names(cat, want)};
  }
  if (hits.size() > 1) {
    std::vector<std::string> some;
    for (size_t k = 0; k < hits.size() && k < 5; ++k) {
      some.push_back(cat.sigs[hits[k]].name);
    }
    throw Q_fail{"ambiguous_selector",
                 std::format("{} matches {} signals; this operation needs exactly one", where, hits.size()),
                 "narrow the selector, or use `values`/`snapshot` for a set",
                 some};
  }
  return hits.front();
}

// A bare hierarchical name (find leaves and `sample` entries take strings, not
// selector objects).
size_t resolve_name(const std::string& name, const Catalog& cat) {
  for (size_t i = 0; i < cat.sigs.size(); ++i) {
    if (cat.sigs[i].name == name || cat.sigs[i].alias == name) {
      return i;
    }
  }
  throw Q_fail{"unknown_signal", std::format("no signal named '{}'", name), "", nearest_names(cat, name)};
}

std::string q_error_result(const std::string& id, const Q_fail& f) {
  rj::StringBuffer             sb;
  rj::Writer<rj::StringBuffer> w(sb);
  w.StartObject();
  w.Key("id");
  w.String(id.c_str());
  w.Key("ok");
  w.Bool(false);
  w.Key("error");
  w.StartObject();
  w.Key("class");
  w.String(f.cls.c_str());
  w.Key("message");
  w.String(f.msg.c_str());
  if (!f.hint.empty()) {
    w.Key("hint");
    w.String(f.hint.c_str());
  }
  w.Key("suggestions");
  w.StartArray();
  for (const auto& s : f.suggestions) {
    w.String(s.c_str());
  }
  w.EndArray();
  w.EndObject();
  w.EndObject();
  return sb.GetString();
}

void write_signal(Json_writer& w, const Signal& c) {
  w.StartObject();
  w.Key("name");
  w.String(c.name.c_str());
  w.Key("kind");
  w.String(c.kind.c_str());
  w.Key("bits");
  w.Int64(c.bits);
  w.Key("declared_bits");
  w.Int64(c.declared_bits);
  w.Key("signed");
  w.Bool(c.is_signed);
  w.Key("direction");
  if (c.kind == "input") {
    w.String("in");
  } else if (c.kind == "output") {
    w.String("out");
  } else {
    w.Null();
  }
  w.Key("size");
  if (c.size >= 0) {
    w.Int64(c.size);
  } else {
    w.Null();
  }
  w.Key("alias");
  if (c.alias.empty()) {
    w.Null();
  } else {
    w.String(c.alias.c_str());
  }
  // Absent-until-plumbed, spelled explicitly so an agent never has to guess
  // whether the engine forgot the field or the design has no such fact.
  w.Key("source");
  w.Null();
  w.Key("clock_domain");
  w.Null();
  w.Key("ops");
  w.StartArray();
  if (c.kind == "memory") {
    w.String("value");  // indexed WORD reads only — no whole-memory dump/diff in v1
  } else {
    for (const char* op : {"value", "values", "changes", "next_change", "find", "snapshot", "diff"}) {
      w.String(op);
    }
  }
  w.EndArray();
  w.EndObject();
}

// `signals` is answered ENTIRELY HERE: the catalog is static, so enumeration
// never needs a simulation. (The query still keeps its request position.)
std::string q_signals_result(const std::string& id, const std::vector<size_t>& hits, const Catalog& cat, long max_results) {
  const bool                   truncated = static_cast<long>(hits.size()) > max_results;
  rj::StringBuffer             sb;
  rj::Writer<rj::StringBuffer> w(sb);
  w.StartObject();
  w.Key("id");
  w.String(id.c_str());
  w.Key("ok");
  w.Bool(true);
  w.Key("count");
  w.Int64(static_cast<int64_t>(hits.size()));
  w.Key("complete");
  w.Bool(!truncated);
  w.Key("truncated");
  w.Bool(truncated);
  w.Key("signals");
  w.StartArray();
  for (size_t k = 0; k < hits.size() && static_cast<long>(k) < max_results; ++k) {
    write_signal(w, cat.sigs[hits[k]]);
  }
  w.EndArray();
  w.EndObject();
  return sb.GetString();
}

// `mem[7]` -> ("mem", 7). Memory words are addressed by explicit index only.
std::pair<std::string, long> split_index(const std::string& name) {
  if (name.empty() || name.back() != ']') {
    return {name, -1};
  }
  const auto ob = name.rfind('[');
  if (ob == std::string::npos || ob == 0) {
    return {name, -1};
  }
  const auto digits = name.substr(ob + 1, name.size() - ob - 2);
  if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) {
    return {name, -1};
  }
  long index = -1;
  if (std::from_chars(digits.data(), digits.data() + digits.size(), index).ec != std::errc{}) {
    throw Q_fail{"invalid_range", "memory index is outside the supported range", "", {}};
  }
  return {name.substr(0, ob), index};
}

// Canonical hex -> little-endian magnitude bytes.
std::vector<uint8_t> hex_to_bytes(std::string_view hx) {
  std::vector<uint8_t> v;
  for (size_t k = 0; k < hx.size(); ++k) {
    const char c = hx[hx.size() - 1 - k];
    int        d = 0;
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      d = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      d = c - 'A' + 10;
    } else {
      return {};
    }
    if (k / 2 >= v.size()) {
      v.push_back(0);
    }
    v[k / 2] |= static_cast<uint8_t>(d << (4 * (k % 2)));
  }
  return v;
}

std::string bytes_to_dec(std::vector<uint8_t> v) {
  auto nonzero = [&] { return std::any_of(v.begin(), v.end(), [](uint8_t b) { return b != 0; }); };
  if (!nonzero()) {
    return "0";
  }
  std::string s;
  while (nonzero()) {
    unsigned rem = 0;
    for (size_t i = v.size(); i-- > 0;) {
      const unsigned cur = (rem << 8) | v[i];
      v[i]               = static_cast<uint8_t>(cur / 10);
      rem                = cur % 10;
    }
    s += static_cast<char>('0' + rem);
  }
  std::reverse(s.begin(), s.end());
  return s;
}

// The signed decimal rendering of a hex value. The interpretation WIDTH is the
// source-declared one (an `s12` holding -3 is "-3", not 4093) — but only when
// the value actually fits there. An imported or conservatively widened
// internal net can exceed its declaration, and truncating a value that uses
// those bits would silently report a different number than `hex` says.
std::string hex_to_dec(std::string_view hx, long bits, long declared_bits, bool is_signed) {
  auto v = hex_to_bytes(hx);
  if (v.empty()) {
    return {};
  }
  long width = (declared_bits > 0 && declared_bits <= bits) ? declared_bits : bits;
  auto bit   = [&](long i) {
    const size_t byte = static_cast<size_t>(i) / 8;
    return byte < v.size() && ((v[byte] >> (i % 8)) & 1U) != 0;
  };
  for (long i = width; i < bits; ++i) {
    if (bit(i)) {
      width = bits;  // the value overflows its declared width — report it whole
      break;
    }
  }
  const size_t nbytes = static_cast<size_t>((width + 7) / 8);
  v.resize(nbytes, 0);
  if (const long rem = width % 8; rem != 0 && nbytes != 0) {
    v[nbytes - 1] &= static_cast<uint8_t>((1U << rem) - 1U);
  }
  const bool neg = is_signed && width > 0 && bit(width - 1);
  if (neg) {
    unsigned carry = 1;
    for (auto& b : v) {
      const unsigned t = static_cast<unsigned>(static_cast<uint8_t>(~b)) + carry;
      b                = static_cast<uint8_t>(t & 0xffU);
      carry            = t >> 8;
    }
    if (const long rem = width % 8; rem != 0 && nbytes != 0) {
      v[nbytes - 1] &= static_cast<uint8_t>((1U << rem) - 1U);
    }
  }
  return (neg ? "-" : "") + bytes_to_dec(std::move(v));
}

struct Predicate {
  std::string           op;
  size_t                arity            = 0;
  size_t                lhs              = 0;
  std::optional<size_t> rhs              = std::nullopt;
  std::string           literal          = {};
  bool                  literal_negative = false;
};

struct Query {
  std::string              id;
  std::string              op;
  std::string              answer;  // a catalog response or an in-band validation error
  int64_t                  from       = -1;
  int64_t                  to         = -1;
  bool                     count_only = false;
  bool                     backward   = false;
  std::vector<Column>      signals;
  std::vector<Predicate>   expr;
  std::vector<std::string> at_from;
  std::vector<std::string> at_to;
  int64_t                  hit   = -1;
  size_t                   count = 0;
  std::string              changes;
};

struct Query_plan {
  size_t             max_results = 1000;
  std::vector<Query> entries;
};

int64_t resolve_time(const Q_time& time, int64_t fail_cycle) {
  if (!time.is_event) {
    return time.cycle;
  }
  if (fail_cycle < 0) {
    throw Q_fail{"invalid_range", "this query is relative to the first failing assert, but the test did not fail", "", {}};
  }
  if (time.offset < -fail_cycle || time.offset > std::numeric_limits<int64_t>::max() - fail_cycle) {
    throw Q_fail{"invalid_range", "failure-relative cycle is out of range", "", {}};
  }
  return fail_cycle + time.offset;
}

size_t resolve_scalar(const Q_sel& sel, const Catalog& cat, std::string_view where) {
  const size_t signal = resolve_one(sel, cat, where);
  if (cat.sigs[signal].size >= 0) {
    throw Q_fail{"unsupported", "a memory must be read with value and a word index", "", {}};
  }
  return signal;
}

void parse_expr(const rj::Value& n, const Catalog& cat, std::vector<Predicate>& out) {
  if (!n.IsObject()) {
    query_usage("find.expr nodes must be objects");
  }
  if (n.HasMember("all") || n.HasMember("any")) {
    const bool  is_all = n.HasMember("all");
    const char* key    = is_all ? "all" : "any";
    reject_unknown_members(n, {key}, "find.expr");
    if (!n[key].IsArray() || n[key].Empty()) {
      query_usage(std::format("find.expr.{} needs a non-empty array of sub-expressions", key));
    }
    for (const auto& sub : n[key].GetArray()) {
      parse_expr(sub, cat, out);
    }
    out.push_back({key, n[key].Size()});
    return;
  }
  if (n.HasMember("not")) {
    reject_unknown_members(n, {"not"}, "find.expr");
    parse_expr(n["not"], cat, out);
    out.push_back({"not", 1});
    return;
  }
  reject_unknown_members(n, {"sig", "cmp", "value", "sig2", "slice"}, "find.expr leaf");
  if (!n.HasMember("sig") || !n["sig"].IsString() || !n.HasMember("cmp") || !n["cmp"].IsString()) {
    query_usage("a find.expr leaf needs a \"sig\" and a \"cmp\"");
  }
  if (n.HasMember("slice")) {
    throw Q_fail{"unsupported", "find.expr leaf `slice` is not supported in v1", "compare the whole signal", {}};
  }
  const std::string cmp = n["cmp"].GetString();
  const size_t      lhs = resolve_name(n["sig"].GetString(), cat);
  const auto&       c   = cat.sigs[lhs];
  if (c.size >= 0) {
    throw Q_fail{"unsupported", "find predicates need scalar signals", "", {}};
  }
  if (cmp == "changed" || cmp == "rising" || cmp == "falling") {
    if (cmp != "changed" && c.bits != 1) {
      throw Q_fail{"unsupported",
                   std::format("`{}` is only defined on a 1-bit signal ({} is {} bits)", cmp, c.name, c.bits),
                   "",
                   {}};
    }
    if (n.HasMember("value") || n.HasMember("sig2")) {
      query_usage(std::format("find.expr `{}` takes no value/sig2", cmp));
    }
    out.push_back({cmp, 0, lhs});
    return;
  }
  static constexpr std::string_view kCmps[] = {"==", "!=", "<", "<=", ">", ">="};
  if (std::find(std::begin(kCmps), std::end(kCmps), std::string_view{cmp}) == std::end(kCmps)) {
    query_usage(std::format("find.expr cmp '{}' is not a v1 comparator", cmp), "== != < <= > >= changed rising falling");
  }
  if (n.HasMember("sig2")) {
    if (n.HasMember("value")) {
      query_usage("find.expr leaf takes either a value or a sig2, not both");
    }
    if (!n["sig2"].IsString()) {
      query_usage("find.expr leaf sig2 must be a string");
    }
    const size_t rhs = resolve_name(n["sig2"].GetString(), cat);
    if (cat.sigs[rhs].size >= 0) {
      throw Q_fail{"unsupported", "find predicates need scalar signals", "", {}};
    }
    out.push_back({cmp, 0, lhs, rhs});
    return;
  }
  if (!n.HasMember("value")) {
    query_usage("find.expr leaf needs a \"value\" or a \"sig2\"");
  }
  // Numbers are accepted for ergonomics, but a wide literal MUST be written as a
  // string: JSON numbers are doubles in every consumer we do not control.
  std::string lit;
  if (n["value"].IsString()) {
    lit = n["value"].GetString();
  } else if (n["value"].IsInt64()) {
    lit = std::to_string(n["value"].GetInt64());
  } else {
    query_usage("find.expr leaf value must be a decimal/0x-hex string or an integer");
  }
  if (const auto first = lit.find_first_not_of(" \t"); first != lit.npos) {
    lit = lit.substr(first, lit.find_last_not_of(" \t") - first + 1);
  }
  if (lit.starts_with("-") && !c.is_signed) {
    query_usage(std::format("find.expr leaf on '{}': a negative literal against an unsigned {}-bit signal", c.name, c.bits));
  }
  std::string err;
  const auto  hex = literal_to_hex(lit, static_cast<long>(lit.size() + 1) * 4, err);
  if (!err.empty()) {
    query_usage(std::format("find.expr leaf on '{}': {}", c.name, err));
  }
  out.push_back({cmp, 0, lhs, std::nullopt, hex, lit.starts_with("-") && hex.find_first_not_of('0') != std::string::npos});
}

// Malformed requests are usage errors. Per-query errors stay in request order.
Query_plan plan_sim_query(const std::string& request, const Catalog& cat, int64_t fail_cycle) {
  rj::Document doc;
  doc.Parse(request.c_str());
  if (doc.HasParseError()) {
    query_usage(std::format("the request is not valid JSON (offset {})", doc.GetErrorOffset()));
  }
  if (!doc.IsObject()) {
    query_usage("the request must be a JSON object");
  }
  reject_unknown_members(doc, {"schema_version", "kind", "queries", "max_results", "allow_rng_divergence"}, "the request");
  if (!doc.HasMember("schema_version") || !doc["schema_version"].IsInt() || doc["schema_version"].GetInt() != 1) {
    query_usage("the request needs \"schema_version\": 1", "an unknown schema version is rejected, never best-effort parsed");
  }
  if (!doc.HasMember("kind") || !doc["kind"].IsString() || std::string_view{doc["kind"].GetString()} != "sim_query") {
    query_usage("the request needs \"kind\": \"sim_query\"");
  }
  if (!doc.HasMember("queries") || !doc["queries"].IsArray()) {
    query_usage("the request needs a \"queries\" array");
  }
  long max_results = 1000;
  if (doc.HasMember("max_results")) {
    if (!doc["max_results"].IsInt64() || doc["max_results"].GetInt64() <= 0) {
      query_usage("max_results must be a positive integer");
    }
    max_results = doc["max_results"].GetInt64();
  }
  if (doc.HasMember("allow_rng_divergence") && !doc["allow_rng_divergence"].IsBool()) {
    query_usage("allow_rng_divergence must be a boolean");
  }
  Query_plan plan;
  plan.max_results = static_cast<size_t>(max_results);
  std::map<std::string, size_t> seen_ids;
  for (const auto& q : doc["queries"].GetArray()) {
    if (!q.IsObject()) {
      query_usage("every entry of \"queries\" must be an object");
    }
    if (!q.HasMember("id") || !q["id"].IsString()) {
      query_usage("every query needs a string \"id\" (the response echoes it)");
    }
    const std::string id = q["id"].GetString();
    if (id.empty()) {
      query_usage("query ids must be non-empty");
    }
    if (!seen_ids.emplace(id, plan.entries.size()).second) {
      query_usage(std::format("duplicate query id '{}'", id));
    }
    if (!q.HasMember("op") || !q["op"].IsString()) {
      query_usage(std::format("query '{}' needs a string \"op\"", id));
    }
    const std::string op    = q["op"].GetString();
    const std::string where = std::format("query '{}'", id);
    auto&             entry = plan.entries.emplace_back();
    entry.id                = id;
    entry.op                = op;
    try {
      if (op == "signals") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind"}, where);
        const auto sel  = parse_selector(q, where);
        auto       hits = resolve_sel(sel, cat, where);
        if (hits.empty() && sel.by_name) {
          throw Q_fail{"unknown_signal", std::format("no signal named '{}'", sel.signal), "", nearest_names(cat, sel.signal)};
        }
        entry.answer = q_signals_result(id, hits, cat, max_results);
        continue;
      }

      if (op == "value") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind", "at", "index"}, where);
        if (!q.HasMember("at")) {
          query_usage(std::format("{}: `value` needs an \"at\"", where));
        }
        auto sel = parse_selector(q, where);
        long idx = -1;
        if (sel.by_name) {
          auto [base, parsed] = split_index(sel.signal);
          if (parsed >= 0) {
            sel.signal = base;
            idx        = parsed;
          }
        }
        if (q.HasMember("index")) {
          if (!q["index"].IsInt64() || q["index"].GetInt64() < 0) {
            query_usage(std::format("{}.index must be a non-negative integer", where));
          }
          if (idx >= 0 && idx != q["index"].GetInt64()) {
            query_usage(std::format("{}: conflicting memory indices", where));
          }
          idx = q["index"].GetInt64();
        }
        const size_t signal = resolve_one(sel, cat, where);
        const auto&  c      = cat.sigs[signal];
        const auto   t      = parse_time(q["at"], std::format("{}.at", where));
        if (c.kind == "memory" && idx < 0) {
          throw Q_fail{"unsupported",
                       std::format("'{}' is a memory: read a WORD ({}[K]) — whole-memory reads are not in v1", c.name, c.name),
                       "",
                       {}};
        }
        if (c.kind != "memory" && idx >= 0) {
          throw Q_fail{"unsupported", std::format("'{}' is a {}, not a memory — it takes no index", c.name, c.kind), "", {}};
        }
        if (idx >= 0 && c.size >= 0 && idx >= c.size) {
          throw Q_fail{"invalid_range", std::format("index {} is outside '{}' ({} words)", idx, c.name, c.size), "", {}};
        }
        entry.signals = {
            {signal, idx}
        };
        entry.from = resolve_time(t, fail_cycle);
        entry.to   = entry.from;
      } else if (op == "values" || op == "snapshot") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind", "at"}, where);
        const auto sel = parse_selector(q, where);
        if (!sel.has) {
          query_usage(std::format("{}: `{}` needs a selector", where, op),
                      "signal | scope | glob | regex | kind — omitting it is an error, not an implicit select-all");
        }
        if (!q.HasMember("at")) {
          query_usage(std::format("{}: `{}` needs an \"at\"", where, op));
        }
        const auto t    = parse_time(q["at"], std::format("{}.at", where));
        auto       hits = resolve_sel(sel, cat, where);
        std::erase_if(hits, [&](size_t i) { return cat.sigs[i].kind == "memory"; });  // selectors never enumerate memory words
        if (hits.empty()) {
          throw Q_fail{"unknown_signal",
                       std::format("{} selects no signal", where),
                       "",
                       sel.by_name ? nearest_names(cat, sel.signal) : std::vector<std::string>{}};
        }
        for (size_t i : hits) {
          entry.signals.push_back({i});
        }
        entry.from = resolve_time(t, fail_cycle);
        entry.to   = entry.from;
      } else if (op == "diff") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind", "from", "to"}, where);
        const auto sel = parse_selector(q, where);
        if (!sel.has) {
          query_usage(std::format("{}: `diff` needs a selector", where));
        }
        if (!q.HasMember("from") || !q.HasMember("to")) {
          query_usage(std::format("{}: `diff` needs a \"from\" and a \"to\"", where));
        }
        const auto f = parse_time(q["from"], std::format("{}.from", where));
        const auto t = parse_time(q["to"], std::format("{}.to", where));
        if (!f.is_event && !t.is_event && f.cycle > t.cycle) {
          query_usage(std::format("{}: from {} is after to {}", where, f.cycle, t.cycle));
        }
        auto hits = resolve_sel(sel, cat, where);
        std::erase_if(hits, [&](size_t i) { return cat.sigs[i].kind == "memory"; });
        if (hits.empty()) {
          throw Q_fail{"unknown_signal", std::format("{} selects no signal", where), "", {}};
        }
        for (size_t i : hits) {
          entry.signals.push_back({i});
        }
        entry.from = resolve_time(f, fail_cycle);
        entry.to   = resolve_time(t, fail_cycle);
      } else if (op == "changes") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind", "from", "to", "count_only"}, where);
        const auto sel = parse_selector(q, where);
        Q_time     f, t;
        if (q.HasMember("from")) {
          f = parse_time(q["from"], std::format("{}.from", where));
        }
        if (q.HasMember("to")) {
          t = parse_time(q["to"], std::format("{}.to", where));
        }
        if (f.cycle >= 0 && t.cycle >= 0 && f.cycle > t.cycle) {
          query_usage(std::format("{}: from {} is after to {}", where, f.cycle, t.cycle));
        }
        bool count_only = false;
        if (q.HasMember("count_only")) {
          if (!q["count_only"].IsBool()) {
            query_usage(std::format("{}.count_only must be a boolean", where));
          }
          count_only = q["count_only"].GetBool();
        }
        // The plan carries ONE signal per `changes`, so a scope expansion has no
        // wire form yet. Say so per-query rather than silently answering about
        // whichever signal happened to sort first.
        entry.signals    = {{resolve_scalar(sel, cat, where)}};
        entry.from       = resolve_time(f, fail_cycle);
        entry.to         = resolve_time(t, fail_cycle);
        entry.count_only = count_only;
      } else if (op == "next_change") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind", "after", "to"}, where);
        const auto sel = parse_selector(q, where);
        Q_time     a, t;
        if (q.HasMember("after")) {
          a = parse_time(q["after"], std::format("{}.after", where));
        }
        if (q.HasMember("to")) {
          t = parse_time(q["to"], std::format("{}.to", where));
        }
        if (a.cycle >= 0 && t.cycle >= 0 && a.cycle > t.cycle) {
          query_usage(std::format("{}: after {} is past to {}", where, a.cycle, t.cycle));
        }
        entry.signals = {{resolve_scalar(sel, cat, where)}};
        entry.from    = resolve_time(a, fail_cycle);
        entry.to      = resolve_time(t, fail_cycle);
      } else if (op == "find") {
        reject_unknown_members(q, {"id", "op", "dir", "from", "to", "expr", "sample"}, where);

        if (q.HasMember("dir")) {
          if (!q["dir"].IsString()) {
            query_usage(std::format("{}.dir must be a string", where));
          }
          const std::string d = q["dir"].GetString();
          if (d == "forward" || d == "fwd") {
            entry.backward = false;
          } else if (d == "backward" || d == "bwd") {
            entry.backward = true;
          } else {
            query_usage(std::format("{}.dir '{}' is not forward|backward", where, d));
          }
        }
        Q_time f, t;
        if (q.HasMember("from")) {
          f = parse_time(q["from"], std::format("{}.from", where));
        }
        if (q.HasMember("to")) {
          t = parse_time(q["to"], std::format("{}.to", where));
        }
        if (f.cycle >= 0 && t.cycle >= 0 && f.cycle > t.cycle) {
          query_usage(std::format("{}: from {} is after to {}", where, f.cycle, t.cycle));
        }
        if (!q.HasMember("expr")) {
          query_usage(std::format("{}: `find` needs an \"expr\" tree", where));
        }
        parse_expr(q["expr"], cat, entry.expr);
        if (q.HasMember("sample")) {
          if (!q["sample"].IsArray()) {
            query_usage(std::format("{}.sample must be an array of signal names", where));
          }
          for (const auto& s : q["sample"].GetArray()) {
            if (!s.IsString()) {
              query_usage(std::format("{}.sample entries must be strings", where));
            }
            entry.signals.push_back({resolve_scalar(Q_sel{true, true, s.GetString()}, cat, where)});
          }
        }
        entry.from = resolve_time(f, fail_cycle);
        entry.to   = resolve_time(t, fail_cycle);
      } else {
        query_usage(std::format("{}: unknown op '{}'", where, op),
                    "signals | value | values | changes | next_change | find | snapshot | diff");
      }
      if (entry.to >= 0 && entry.from > entry.to) {
        throw Q_fail{"invalid_range", "the start cycle is after the end cycle", "", {}};
      }
    } catch (const Q_fail& f) {
      entry.answer = q_error_result(id, f);
    }
  }

  return plan;
}

template <typename Emit>
std::string json(Emit emit) {
  rj::StringBuffer sb;
  Json_writer      w(sb);
  emit(w);
  return sb.GetString();
}

void write_at(Json_writer& w, int64_t cycle) {
  w.StartObject();
  w.Key("cycle");
  w.Int64(cycle);
  w.Key("phase");
  w.String("post");
  w.EndObject();
}

void write_value(Json_writer& w, const Signal& s, const std::string& hex) {
  w.StartObject();
  w.Key("bits");
  w.Int64(s.bits);
  w.Key("declared_bits");
  w.Int64(s.declared_bits);
  w.Key("signed");
  w.Bool(s.is_signed);
  w.Key("hex");
  w.String(hex.c_str());
  w.Key("dec");
  w.String(hex_to_dec(hex, s.bits, s.declared_bits, s.is_signed).c_str());
  w.Key("known_mask");
  std::string error;
  w.String(literal_to_hex("-1", s.bits, error).c_str());
  w.Key("sampled");
  w.String(s.kind == "output" ? "during_period" : "settled");
  w.EndObject();
}

unsigned nibble(char c) { return static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + 10); }

struct Number {
  const std::string& hex;
  bool               negative;
  unsigned           high;  // sign-extended high nibble

  unsigned digit(size_t i) const {
    if (i >= hex.size()) {
      return negative ? 15 : 0;
    }
    return i + 1 == hex.size() ? high : nibble(hex[hex.size() - 1 - i]);
  }
};

Number number(const Signal& signal, const std::string& hex) {
  const unsigned used     = static_cast<unsigned>((signal.bits - 1) % 4 + 1);
  const unsigned high     = nibble(hex.front());
  const bool     negative = signal.is_signed && (high & (1u << (used - 1))) != 0;
  return {hex, negative, negative ? high | (15u & (15u << used)) : high};
}

bool test_compare(const Number& lhs, const Number& rhs, std::string_view op) {
  int cmp = 0;
  if (lhs.negative != rhs.negative) {
    cmp = lhs.negative ? -1 : 1;
  } else {
    for (size_t i = std::max(lhs.hex.size(), rhs.hex.size()); i-- > 0;) {
      if (lhs.digit(i) != rhs.digit(i)) {
        cmp = lhs.digit(i) < rhs.digit(i) ? -1 : 1;
        break;
      }
    }
  }
  if (op == "==") {
    return cmp == 0;
  }
  if (op == "!=") {
    return cmp != 0;
  }
  if (op == "<") {
    return cmp < 0;
  }
  if (op == "<=") {
    return cmp <= 0;
  }
  if (op == ">") {
    return cmp > 0;
  }
  return cmp >= 0;
}

std::string trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  return first == s.npos ? std::string{} : std::string(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1));
}

std::optional<Predicate> parse_break(const Catalog& cat, const std::string& expression) {
  static const std::regex split(R"((.*?)(==|!=|<=|>=|<|>)(.*))");
  std::smatch             match;
  if (!std::regex_match(expression, match, split) || trim(match[1].str()).empty() || trim(match[3].str()).empty()) {
    throw std::invalid_argument("--break-when needs a signal, a comparator and an integer or signal");
  }
  const auto  lhs_name = trim(match[1].str());
  const auto  rhs_text = trim(match[3].str());
  const auto  op       = match[2].str();
  std::string hex;
  bool        negative = false;
  if (rhs_text.front() == '-' || rhs_text.front() == '+' || (rhs_text.front() >= '0' && rhs_text.front() <= '9')) {
    std::string error;
    // Four bits per input character, rounded up to a full nibble for a sign bit.
    hex = literal_to_hex(rhs_text, static_cast<long>(rhs_text.size() + 1) * 4, error);
    if (!error.empty()) {
      throw std::invalid_argument(std::format("--break-when: '{}' is not a valid integer", rhs_text));
    }
    negative = rhs_text.front() == '-' && hex.find_first_not_of('0') != std::string::npos;
  }
  try {
    const auto lhs = resolve_scalar(Q_sel{true, true, lhs_name}, cat, "--break-when");
    if (hex.empty()) {
      const auto rhs = resolve_scalar(Q_sel{true, true, rhs_text}, cat, "--break-when");
      return Predicate{op, 0, lhs, rhs};
    }
    return Predicate{op, 0, lhs, std::nullopt, std::move(hex), negative};
  } catch (const Q_fail&) {
    return std::nullopt;
  }
}

constexpr int64_t kLastCycle = std::numeric_limits<int64_t>::max();

}  // namespace

struct Observer::Impl {
  const Catalog&                              catalog;
  Request                                     request;
  int64_t                                     fail_cycle;
  uint64_t                                    seed;
  Query_plan                                  plan;
  std::vector<Range>                          ranges;
  std::vector<Column>                         columns;
  std::map<Column, size_t>                    slots;
  std::vector<std::pair<std::string, Column>> probes;
  std::string                                 probe_rows;
  std::optional<Predicate>                    break_predicate;
  int64_t                                     break_cycle = -1;
  std::string                                 break_state;
  int64_t                                     first = -1, last = -1, samples = 0, replay_steps = 0, checkpoint = -1;
  std::vector<int64_t>                        checkpoints;
  std::vector<std::string>                    prior;
  std::vector<bool>                           stack;

  Impl(const Catalog& c, const Request& req, int64_t failure, uint64_t run_seed)
      : catalog(c), request(req), fail_cycle(failure), seed(run_seed) {
    if (!request.query.empty()) {
      plan = plan_sim_query(request.query, catalog, fail_cycle);
    }
    for (auto& q : plan.entries) {
      if (!q.answer.empty()) {
        continue;
      }
      for (const auto column : q.signals) {
        add_column(column);
      }
      bool prior_sample = q.op == "changes";
      for (const auto& node : q.expr) {
        if (node.arity == 0) {
          add_column({node.lhs});
          if (node.rhs) {
            add_column({*node.rhs});
          }
          prior_sample |= node.op == "changed" || node.op == "rising" || node.op == "falling";
        }
      }
      if (q.op == "diff") {
        add_range(q.from, q.from);
        add_range(q.to, q.to);
      } else {
        add_range(prior_sample && q.from > 0 ? q.from - 1 : q.from, q.to);
      }
    }
    if (!request.probe.empty()) {
      std::istringstream names(request.probe);
      std::string        name;
      while (std::getline(names, name, ',')) {
        name = trim(name);
        if (name.empty()) {
          continue;
        }
        try {
          Column column{resolve_scalar(Q_sel{true, true, name}, catalog, "--probe")};
          probes.emplace_back(name, column);
          add_column(column);
        } catch (const Q_fail&) {
          throw std::invalid_argument("--probe: no observable scalar signal named '" + name + "'");
        }
      }
      if (probes.empty()) {
        throw std::invalid_argument("--probe needs a comma-separated list of signal names");
      }
      add_range(request.probe_from, request.probe_to);
    }
    if (!request.break_when.empty()) {
      break_predicate = parse_break(catalog, request.break_when);
      if (break_predicate) {
        for (size_t i = 0; i < catalog.sigs.size(); ++i) {
          if (catalog.sigs[i].size < 0) {
            add_column({i});
          }
        }
        add_range(-1, -1);
      }
    }
    std::ranges::sort(ranges, {}, &Range::from);
    std::vector<Range> merged;
    for (const auto range : ranges) {
      if (merged.empty() || (merged.back().to < kLastCycle && range.from > merged.back().to + 1)) {
        merged.push_back(range);
      } else {
        merged.back().to = std::max(merged.back().to, range.to);
      }
    }
    ranges = std::move(merged);
  }

  void add_column(Column c) {
    if (slots.try_emplace(c, columns.size()).second) {
      columns.push_back(c);
    }
  }

  void add_range(int64_t from, int64_t to) { ranges.push_back({std::max<int64_t>(0, from), to < 0 ? kLastCycle : to}); }

  const std::string& value(const std::vector<std::string>& row, Column c) const { return row[slots.at(c)]; }

  std::vector<std::string> select(const std::vector<std::string>& row, const std::vector<Column>& cols) const {
    std::vector<std::string> selected;
    selected.reserve(cols.size());
    for (const auto c : cols) {
      selected.push_back(value(row, c));
    }
    return selected;
  }

  bool leaf(const Predicate& node, const std::vector<std::string>& row, bool adjacent) const {
    const auto& cur = value(row, {node.lhs});
    if (node.op == "changed" || node.op == "rising" || node.op == "falling") {
      if (!adjacent) {
        return false;
      }
      const auto& prev = value(prior, {node.lhs});
      return cur != prev && (node.op == "changed" || (cur == "1" ? node.op == "rising" : node.op == "falling"));
    }
    const auto lhs = number(catalog.sigs[node.lhs], cur);
    if (node.rhs) {
      return test_compare(lhs, number(catalog.sigs[*node.rhs], value(row, {*node.rhs})), node.op);
    }
    return test_compare(lhs, {node.literal, node.literal_negative, nibble(node.literal.front())}, node.op);
  }

  bool matches(const std::vector<Predicate>& expr, const std::vector<std::string>& row, bool adjacent) {
    stack.clear();
    for (const auto& node : expr) {
      if (node.arity == 0) {
        stack.push_back(leaf(node, row, adjacent));
      } else if (node.op == "not") {
        stack.back() = !stack.back();
      } else {
        const auto start  = stack.end() - static_cast<std::ptrdiff_t>(node.arity);
        const bool result = node.op == "all" ? std::all_of(start, stack.end(), [](bool b) { return b; })
                                             : std::any_of(start, stack.end(), [](bool b) { return b; });
        stack.resize(stack.size() - node.arity);
        stack.push_back(result);
      }
    }
    return stack.back();
  }

  void decimal(Json_writer& w, Column c, const std::string& hex) const {
    const auto& s   = catalog.sigs[c.signal];
    const auto  dec = hex_to_dec(hex, s.bits, s.declared_bits, s.is_signed);
    w.RawValue(dec.data(), dec.size(), rj::kNumberType);
  }

  void sample(int64_t cycle, std::vector<std::string>& row) {
    const bool adjacent = last >= 0 && cycle - last == 1;
    if (first < 0) {
      first = cycle;
    }
    for (auto& q : plan.entries) {
      if (!q.answer.empty() || cycle < q.from || (q.to >= 0 && cycle > q.to)) {
        continue;
      }
      if (q.op == "find") {
        if ((q.hit < 0 || q.backward) && matches(q.expr, row, adjacent)) {
          q.hit     = cycle;
          q.at_from = select(row, q.signals);
        }
      } else if (q.op == "changes" || q.op == "next_change") {
        const bool next = q.op == "next_change";
        if (!adjacent || (next && (cycle == q.from || q.count != 0))) {
          continue;
        }
        const Column c         = q.signals.front();
        const auto&  old_value = value(prior, c);
        const auto&  new_value = value(row, c);
        if (old_value == new_value) {
          continue;
        }
        ++q.count;
        if (!q.count_only && q.count <= plan.max_results) {
          if (!q.changes.empty()) {
            q.changes += ',';
          }
          q.changes += json([&](Json_writer& w) {
            w.StartObject();
            w.Key("cycle");
            w.Int64(cycle);
            w.Key("old");
            write_value(w, catalog.sigs[c.signal], old_value);
            w.Key("new");
            write_value(w, catalog.sigs[c.signal], new_value);
            w.EndObject();
          });
        }
      } else {
        if (cycle == q.from) {
          q.at_from = select(row, q.signals);
        }
        if (q.op == "diff" && cycle == q.to) {
          q.at_to = select(row, q.signals);
        }
      }
    }
    if (!probes.empty() && cycle >= request.probe_from && (request.probe_to < 0 || cycle <= request.probe_to)) {
      if (!probe_rows.empty()) {
        probe_rows += ',';
      }
      probe_rows += json([&](Json_writer& w) {
        w.StartObject();
        w.Key("cycle");
        w.Int64(cycle);
        for (const auto& [name, col] : probes) {
          w.Key(name.c_str());
          decimal(w, col, value(row, col));
        }
        w.EndObject();
      });
    }
    if (break_predicate && break_cycle < 0 && leaf(*break_predicate, row, adjacent)) {
      break_cycle = cycle;
      break_state = json([&](Json_writer& w) {
        w.StartObject();
        for (const Column c : columns) {
          if (c.index < 0) {
            w.Key(catalog.sigs[c.signal].name.c_str());
            decimal(w, c, value(row, c));
          }
        }
        w.EndObject();
      });
    }
    last = cycle;
    ++samples;
    prior.swap(row);
  }

  std::string answer(const Query& q) const {
    if (!q.answer.empty()) {
      return q.answer;
    }
    const bool change = q.op == "changes" || q.op == "next_change";
    const bool find   = q.op == "find";
    const bool diff   = q.op == "diff";
    const bool one    = q.op == "value";
    if (!change && !find && (q.at_from.empty() || (diff && q.at_to.empty()))) {
      return q_error_result(q.id, {"invalid_range", "a requested cycle was not sampled", "", {}});
    }
    return json([&](Json_writer& w) {
      w.StartObject();
      w.Key("id");
      w.String(q.id.c_str());
      w.Key("ok");
      w.Bool(true);
      bool truncated = false;
      if (change) {
        w.Key("signal");
        w.String(catalog.sigs[q.signals.front().signal].name.c_str());
        w.Key("count");
        w.Uint64(q.count);
        const auto rows = "[" + q.changes + "]";
        w.Key("changes");
        w.RawValue(rows.data(), rows.size(), rj::kArrayType);
        w.Key("searched");
        w.StartObject();
        w.Key("from");
        w.Int64(q.from);
        w.Key("to");
        w.Int64(q.to);
        w.EndObject();
        truncated = !q.count_only && q.count > plan.max_results;
      } else if (one) {
        const Column col = q.signals.front();
        const auto&  s   = catalog.sigs[col.signal];
        w.Key("signal");
        w.String(s.name.c_str());
        if (col.index >= 0) {
          w.Key("index");
          w.Int64(col.index);
        }
        w.Key("at");
        write_at(w, q.from);
        w.Key("value");
        write_value(w, s, q.at_from.front());
      } else {
        if (find) {
          w.Key("found");
          w.Bool(q.hit >= 0);
          if (q.hit < 0) {
            w.EndObject();
            return;
          }
        }
        if (diff) {
          w.Key("from");
          write_at(w, q.from);
          w.Key("to");
          write_at(w, q.to);
        } else {
          w.Key("at");
          write_at(w, find ? q.hit : q.from);
        }
        w.Key(find ? "sample" : diff ? "diff" : "values");
        w.StartArray();
        size_t emitted = 0;
        for (size_t i = 0; i < q.signals.size(); ++i) {
          if (diff && q.at_from[i] == q.at_to[i]) {
            continue;
          }
          if (emitted == plan.max_results) {
            truncated = true;
            break;
          }
          const auto& s = catalog.sigs[q.signals[i].signal];
          w.StartObject();
          w.Key("signal");
          w.String(s.name.c_str());
          w.Key(diff ? "from_value" : "value");
          write_value(w, s, q.at_from[i]);
          if (diff) {
            w.Key("to_value");
            write_value(w, s, q.at_to[i]);
          }
          w.EndObject();
          ++emitted;
        }
        w.EndArray();
      }
      if (!one && !find) {
        w.Key("complete");
        w.Bool(!truncated);
        w.Key("truncated");
        w.Bool(truncated);
      }
      w.EndObject();
    });
  }
};

Observer::Observer(const Catalog& catalog, const Request& request, int64_t fail_cycle, uint64_t seed)
    : impl_(std::make_unique<Impl>(catalog, request, fail_cycle, seed)) {}
Observer::~Observer() = default;

const std::vector<Column>& Observer::columns() const { return impl_->columns; }
const std::vector<Range>&  Observer::ranges() const { return impl_->ranges; }

bool Observer::wants_sample(int64_t cycle) const {
  const auto it = std::ranges::lower_bound(impl_->ranges, cycle, {}, &Range::to);
  return it != impl_->ranges.end() && it->from <= cycle;
}

void Observer::sample(int64_t cycle, std::vector<std::string>& row) {
  if (cycle <= impl_->last) {
    throw std::runtime_error("replay samples are out of cycle order");
  }
  impl_->sample(cycle, row);
  row.resize(impl_->columns.size());
}

void Observer::read_samples(std::istream& in) {
  std::vector<std::string> row(impl_->columns.size());
  for (int64_t cycle; in >> cycle;) {
    for (auto& hex : row) {
      if (!(in >> hex)) {
        throw std::runtime_error("truncated replay sample");
      }
    }
    sample(cycle, row);
  }
  if (!in.eof()) {
    throw std::runtime_error("could not read replay samples");
  }
}

void Observer::replayed(int64_t first, int64_t count) {
  if (impl_->replay_steps == 0) {
    impl_->checkpoint = first;
  }
  if (first >= 0) {
    impl_->checkpoints.push_back(first);
  }
  impl_->replay_steps += count;
}

std::string Observer::debug_json() const {
  const auto& o = *impl_;
  if (!o.request.list_signals && o.request.probe.empty() && o.request.break_when.empty()) {
    return {};
  }
  return json([&](Json_writer& w) {
    w.StartObject();
    if (o.request.list_signals) {
      w.Key("signals");
      w.StartArray();
      for (const auto& s : o.catalog.sigs) {
        auto legacy = s;
        if (!legacy.alias.empty()) {
          std::swap(legacy.name, legacy.alias);
        }
        write_signal(w, legacy);
      }
      w.EndArray();
    }
    if (!o.request.probe.empty()) {
      w.Key("probe");
      w.StartObject();
      w.Key("from");
      w.Int64(std::max<int64_t>(0, o.request.probe_from));
      w.Key("to");
      w.Int64(o.request.probe_to >= 0 ? o.request.probe_to : o.last);
      w.Key("rows");
      const auto rows = "[" + o.probe_rows + "]";
      w.RawValue(rows.data(), rows.size(), rj::kArrayType);
      w.EndObject();
    }
    if (!o.request.break_when.empty()) {
      w.Key("break");
      w.StartObject();
      w.Key("hit");
      w.Bool(o.break_cycle >= 0);
      if (o.break_cycle >= 0) {
        w.Key("cycle");
        w.Int64(o.break_cycle);
        w.Key("state");
        w.RawValue(o.break_state.data(), o.break_state.size(), rj::kObjectType);
      }
      w.EndObject();
    }
    w.EndObject();
  });
}

std::string Observer::query_json() const {
  const auto& o = *impl_;
  if (o.request.query.empty()) {
    return {};
  }
  return json([&](Json_writer& w) {
    w.StartObject();
    w.Key("schema_version");
    w.Int(1);
    w.Key("kind");
    w.String("sim_query_result");
    w.Key("run");
    w.StartObject();
    w.Key("test");
    w.String(o.catalog.test.c_str());
    w.Key("clock");
    w.String(o.catalog.clock.c_str());
    w.Key("phase");
    w.String("post");
    w.Key("samples");
    w.Int64(o.samples);
    w.Key("first_cycle");
    w.Int64(o.first);
    w.Key("last_cycle");
    w.Int64(o.last);
    w.Key("cycles_replayed");
    w.Int64(o.replay_steps);
    w.Key("checkpoint_used");
    if (o.checkpoint >= 0) {
      w.Int64(o.checkpoint);
    } else {
      w.Null();
    }
    w.Key("checkpoints_used");
    w.StartArray();
    for (const auto cycle : o.checkpoints) {
      w.Int64(cycle);
    }
    w.EndArray();
    w.Key("fail_cycle");
    w.Int64(o.fail_cycle);
    w.Key("seed");
    w.Uint64(o.seed);
    w.EndObject();
    w.Key("results");
    w.StartArray();
    for (const auto& q : o.plan.entries) {
      const auto answer = o.answer(q);
      w.RawValue(answer.data(), answer.size(), rj::kObjectType);
    }
    w.EndArray();
    w.EndObject();
  });
}

std::string Observer::break_summary() const {
  return impl_->break_cycle < 0
             ? std::string{}
             : std::format("break-when '{}' first held at clock {}", impl_->request.break_when, impl_->break_cycle);
}

}  // namespace lhd::sim_observe
