//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Simulation and source-level checking commands.

#include "lhd_kernel_internal.hpp"

#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <thread>

#include "absl/strings/str_join.h"
#include "diag.hpp"
#include "file_utils.hpp"
#include "graph_library_singleton.hpp"
#include "pass.hpp"
#include "prp_sim.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace lhd {

// ---- sim --------------------------------------------------------------------

std::string shell_quote(const std::string& s);  // defined below (check section)

// ---- `sim --query`: the batched JSON query API (todo/livehd/2f-sim.html) ------
//
// The kernel/driver seam is deliberately asymmetric. The KERNEL owns JSON: it
// parses and validates the request, resolves every selector against the STATIC
// catalog the generator wrote (sim_catalog.json — names, kinds and widths are
// all known at codegen time), answers whatever needs no simulation at all,
// unions what is left into ONE time interval, and hands the driver a
// selector-free line plan. The generated driver has no JSON parser and must not
// grow one: it reads the plan, evaluates it inside its single tick loop, and
// writes sim_query.json back, which the kernel merges and embeds verbatim as the
// envelope's "query" member (exactly like the "debug" sidecar).
//
// The split is what makes a batch answerable in ONE replay, and it is also why
// the kernel is the only side that ever reports a "usage" verdict: by the time
// the driver runs, the request is already known to be well-formed.
namespace {

namespace rj = rapidjson;

// A malformed REQUEST is a process-level usage error (exit 2): the invocation
// was wrong, so there is nothing to answer. Contrast Q_fail below.
[[noreturn]] void query_usage(const std::string& msg, const std::string& hint = "") {
  throw Lhd_error{"usage", std::format("--query: {}", msg), hint};
}

// A per-QUERY failure. One bad signal name must never erase the batch's other
// answers, so these become in-band `ok:false` results with the enumerated class
// vocabulary (unknown_signal|ambiguous_selector|invalid_range|unsupported|
// timeout|limit) instead of failing the process.
struct Q_fail {
  std::string              cls;
  std::string              msg;
  std::string              hint;
  std::vector<std::string> suggestions;
};

// One sim_catalog.json record. Widths are BOTH reported on purpose: `bits` is
// the internal Slop width (9 for a `u8` register — today's pinned probe
// behavior) and `declared_bits` is what the source asked for.
struct Cat_sig {
  std::string name;
  std::string alias;  // the legacy spelling (`acc.__in.din`), empty when none
  std::string kind;   // flop|pipe|memrd|input|output|memory
  long        bits          = 0;
  long        declared_bits = 0;
  long        size          = -1;  // memory words; -1 = not a memory
  bool        is_signed     = false;
};

struct Sim_catalog {
  std::string          test;
  std::string          clock;
  std::vector<Cat_sig> sigs;  // CATALOG ORDER — the deterministic expansion order of every selector
};

// What plan_sim_query hands back to the run: the kernel-answered results (by
// REQUEST position; empty string = the driver owns it) plus the ids, so the
// sidecar can be spliced back into request order afterwards.
struct Query_plan {
  bool                     active     = false;  // --query was given
  bool                     wrote_plan = false;  // some query needs the replay
  std::string              test;
  std::string              clock;
  std::vector<std::string> ids;
  std::vector<std::string> kernel_results;  // parallel to ids
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
std::vector<std::string> nearest_names(const Sim_catalog& cat, std::string_view want) {
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

// The generator writes sim_catalog.json beside the driver. Its ABSENCE is not a
// user mistake — every generated sim dir has one — so it reads as an internal
// error naming the stale dir rather than as bad input.
Sim_catalog load_sim_catalog(const std::string& simdir, const std::string& test_sel) {
  const std::string path = std::format("{}/sim_catalog.json", simdir);
  std::ifstream     ifs(path);
  if (!ifs.is_open()) {
    throw Lhd_error{"internal",
                    std::format("--query needs the signal catalog, but {} does not exist", path),
                    "the sim dir predates the query API — re-run without --run-only (or delete --workdir) to regenerate it"};
  }
  std::stringstream ss;
  ss << ifs.rdbuf();
  rj::Document doc;
  doc.Parse(ss.str().c_str());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("kind") || !doc["kind"].IsString()
      || std::string_view{doc["kind"].GetString()} != "sim_catalog" || !doc.HasMember("tests") || !doc["tests"].IsObject()) {
    throw Lhd_error{"internal", std::format("malformed signal catalog {}", path), "regenerate the sim dir (drop --run-only)"};
  }

  // Exactly ONE selected test, matching every other observability mode: a batch
  // plans one replay, and "which run is cycle 42 in?" has no answer otherwise.
  const auto&              tmap  = doc["tests"];
  const rj::Value*         entry = nullptr;
  std::string              name;
  std::vector<std::string> keys;
  for (auto it = tmap.MemberBegin(); it != tmap.MemberEnd(); ++it) {
    keys.emplace_back(it->name.GetString());
  }
  if (!test_sel.empty()) {
    for (auto it = tmap.MemberBegin(); it != tmap.MemberEnd(); ++it) {
      const std::string k    = it->name.GetString();
      // The selector may be the dotted name or just its tail (`run` for `cnt.run`),
      // the same two spellings the driver's --test accepts.
      const auto        dot  = k.rfind('.');
      const std::string tail = dot == std::string::npos ? k : k.substr(dot + 1);
      if (k == test_sel || tail == test_sel) {
        entry = &it->value;
        name  = k;
        break;
      }
    }
    if (entry == nullptr) {
      query_usage(std::format("no test matched '{}'", test_sel), std::format("this sim holds: {}", absl::StrJoin(keys, ", ")));
    }
  } else if (keys.size() == 1) {
    entry = &tmap.MemberBegin()->value;
    name  = keys.front();
  } else {
    query_usage("a query batch needs exactly one selected test",
                std::format("name one as the second positional: {}", absl::StrJoin(keys, ", ")));
  }

  Sim_catalog cat;
  cat.test = name;
  if (entry->HasMember("clock") && (*entry)["clock"].IsString()) {
    cat.clock = (*entry)["clock"].GetString();
  }
  if (!entry->HasMember("signals") || !(*entry)["signals"].IsArray()) {
    throw Lhd_error{"internal", std::format("catalog entry '{}' has no signals array ({})", name, path), ""};
  }
  for (const auto& s : (*entry)["signals"].GetArray()) {
    if (!s.IsObject() || !s.HasMember("name") || !s["name"].IsString()) {
      continue;
    }
    Cat_sig c;
    c.name = s["name"].GetString();
    if (s.HasMember("alias") && s["alias"].IsString()) {
      c.alias = s["alias"].GetString();
    }
    c.kind          = (s.HasMember("kind") && s["kind"].IsString()) ? s["kind"].GetString() : "flop";
    c.bits          = (s.HasMember("bits") && s["bits"].IsNumber()) ? s["bits"].GetInt64() : 0;
    c.declared_bits = (s.HasMember("declared_bits") && s["declared_bits"].IsNumber()) ? s["declared_bits"].GetInt64() : c.bits;
    c.size          = (s.HasMember("size") && s["size"].IsNumber()) ? s["size"].GetInt64() : -1;
    c.is_signed     = s.HasMember("signed") && s["signed"].IsBool() && s["signed"].GetBool();
    cat.sigs.push_back(std::move(c));
  }
  return cat;
}

// --query FILE | - (stdin) | {inline JSON}. The inline form is what makes the
// agent loop one invocation with no scratch file.
std::string read_query_request(const std::string& spec) {
  if (spec.front() == '{') {
    return spec;
  }
  if (spec == "-") {
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    return oss.str();
  }
  std::ifstream ifs(spec);
  if (!ifs.is_open()) {
    throw Lhd_error{"missing_file",
                    std::format("--query file '{}' does not exist", spec),
                    "pass `-` to read the request from stdin"};
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// A request timestamp. {"cycle":N} is absolute; {"event":"fail","offset":K} is
// resolved DURING the run — the kernel cannot know the failing cycle at plan
// time, so it travels as the token `F<offset>` and the driver resolves it once
// the assert has (or has not) fired.
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
                  "post is the only observation point the generated code exposes; a `pre` phase is the first planned extension");
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
std::vector<size_t> resolve_sel(const Q_sel& sel, const Sim_catalog& cat, std::string_view where) {
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
size_t resolve_one(const Q_sel& sel, const Sim_catalog& cat, std::string_view where) {
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
size_t resolve_name(const std::string& name, const Sim_catalog& cat) {
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

// `signals` is answered ENTIRELY HERE: the catalog is static, so enumeration
// never needs a simulation. (The query still keeps its request position.)
std::string q_signals_result(const std::string& id, const std::vector<size_t>& hits, const Sim_catalog& cat, long max_results) {
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
    const auto& c = cat.sigs[hits[k]];
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
  return {name.substr(0, ob), std::stol(digits)};
}

// One `find` expression node -> POST-ORDER X lines (a tiny stack machine the
// driver evaluates without a parser). Returns the node count it emitted.
size_t emit_expr(const rj::Value& n, const Sim_catalog& cat, std::string& out) {
  if (!n.IsObject()) {
    query_usage("find.expr nodes must be objects");
  }
  if (n.HasMember("all") || n.HasMember("any")) {
    const bool  is_all = n.HasMember("all");
    const char* key    = is_all ? "all" : "any";
    reject_unknown_members(n, {"all", "any"}, "find.expr");
    if (!n[key].IsArray() || n[key].Empty()) {
      query_usage(std::format("find.expr.{} needs a non-empty array of sub-expressions", key));
    }
    size_t total = 0;
    for (const auto& sub : n[key].GetArray()) {
      total += emit_expr(sub, cat, out);
    }
    out += std::format("X {} {}\n", key, n[key].Size());
    return total + 1;
  }
  if (n.HasMember("not")) {
    reject_unknown_members(n, {"not"}, "find.expr");
    const size_t sub  = emit_expr(n["not"], cat, out);
    out              += "X not\n";
    return sub + 1;
  }
  reject_unknown_members(n, {"sig", "cmp", "value", "sig2", "slice"}, "find.expr leaf");
  if (!n.HasMember("sig") || !n["sig"].IsString() || !n.HasMember("cmp") || !n["cmp"].IsString()) {
    query_usage("a find.expr leaf needs a \"sig\" and a \"cmp\"");
  }
  if (n.HasMember("slice")) {
    throw Q_fail{"unsupported",
                 "find.expr leaf `slice` is not in the v1 plan protocol",
                 "compare the whole signal, or slice with a separate expression once the protocol carries it",
                 {}};
  }
  const std::string cmp = n["cmp"].GetString();
  const auto&       c   = cat.sigs[resolve_name(n["sig"].GetString(), cat)];
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
    out += std::format("X edge {} {}\n", c.name, cmp);
    return 1;
  }
  static constexpr std::string_view kCmps[] = {"==", "!=", "<", "<=", ">", ">="};
  if (std::find(std::begin(kCmps), std::end(kCmps), std::string_view{cmp}) == std::end(kCmps)) {
    query_usage(std::format("find.expr cmp '{}' is not a v1 comparator", cmp), "== != < <= > >= changed rising falling");
  }
  const bool ordered = cmp != "==" && cmp != "!=";
  // The driver compares UNSIGNED magnitudes at arbitrary width (it has no
  // catalog, so it cannot know an operand is signed). Refusing the query is the
  // only honest option: emitting the compare anyway is exactly the --break-when
  // strtoull hole one layer down — a confidently wrong cycle number.
  if (ordered && c.is_signed) {
    throw Q_fail{"unsupported",
                 std::format("ordered compare on the signed signal '{}' is not in the v1 plan protocol", c.name),
                 "the plan's comparator is an unsigned magnitude compare; use == / != , or compare an unsigned signal",
                 {}};
  }
  if (n.HasMember("sig2")) {
    if (n.HasMember("value")) {
      query_usage("find.expr leaf takes either a value or a sig2, not both");
    }
    if (!n["sig2"].IsString()) {
      query_usage("find.expr leaf sig2 must be a string");
    }
    const auto& c2 = cat.sigs[resolve_name(n["sig2"].GetString(), cat)];
    if (ordered && c2.is_signed) {
      throw Q_fail{"unsupported",
                   std::format("ordered compare on the signed signal '{}' is not in the v1 plan protocol", c2.name),
                   "the plan's comparator is an unsigned magnitude compare",
                   {}};
    }
    out += std::format("X cmp2 {} {} {}\n", c.name, cmp, c2.name);
    return 1;
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
  if (!lit.empty() && lit.front() == '-' && !c.is_signed) {
    query_usage(std::format("find.expr leaf on '{}': a negative literal against an unsigned {}-bit signal", c.name, c.bits));
  }
  std::string err;
  const auto  hex = literal_to_hex(lit, c.bits, err);  // bare lowercase hex: no 0x, no sign — that is the wire form
  if (!err.empty()) {
    query_usage(std::format("find.expr leaf on '{}': {}", c.name, err));
  }
  out += std::format("X cmp {} {} {}\n", c.name, cmp, hex);
  return 1;
}

// Validate + plan the whole batch. Throws Lhd_error{usage} for a malformed
// REQUEST; per-query problems land in plan.kernel_results as ok:false objects.
Query_plan plan_sim_query(const std::string& request, const Sim_catalog& cat, const std::string& plan_path) {
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
  if (doc.HasMember("allow_rng_divergence") && doc["allow_rng_divergence"].GetBool()) {
    // Recognized, and deliberately NOT silently ignored: honoring it means
    // replaying from a checkpoint whose PRNG stream position was never saved,
    // which every result would have to be marked `reproducible:false` for.
    throw Lhd_error{"unsupported",
                    "--query: allow_rng_divergence is not implemented yet",
                    "drop the field; a randomized run replays from cycle 0, which is the reproducible answer"};
  }

  Query_plan plan;
  plan.active = true;
  plan.test   = cat.test;
  plan.clock  = cat.clock;

  std::string body;  // the Q/G/S/X lines, in request order
  // The union of every replayed query's interval. -1 on either side means
  // "unbounded there", which is how the driver reads FROM/TO.
  bool        lo_unbounded = false, hi_unbounded = false;
  long        lo_min = std::numeric_limits<long>::max(), hi_max = -1;
  auto        note_lo = [&](long c) {
    if (c < 0) {
      lo_unbounded = true;
    } else {
      lo_min = std::min(lo_min, c);
    }
  };
  auto note_hi = [&](long c) {
    if (c < 0) {
      hi_unbounded = true;
    } else {
      hi_max = std::max(hi_max, c);
    }
  };

  // A failure-anchored bound has no value until the run happens, so the batch
  // cannot bound its sampling window: it must observe the whole run.
  bool        any_event = false;
  const auto  tok       = [&](const Q_time& t) {
    if (t.is_event) {
      any_event = true;
      return std::format("F{}{}", t.offset < 0 ? "" : "+", t.offset);
    }
    return std::to_string(t.cycle);
  };
  std::map<std::string, size_t> seen_ids;
  for (const auto& q : doc["queries"].GetArray()) {
    if (!q.IsObject()) {
      query_usage("every entry of \"queries\" must be an object");
    }
    if (!q.HasMember("id") || !q["id"].IsString()) {
      query_usage("every query needs a string \"id\" (the response echoes it)");
    }
    const std::string id = q["id"].GetString();
    // The id is a FIELD of the space-separated plan line, and it is the join key
    // of the response — so no whitespace, and no duplicates.
    if (id.empty() || id.find_first_of(" \t\n\r") != std::string::npos) {
      query_usage(std::format("query id '{}' must be non-empty and contain no whitespace", id));
    }
    if (!seen_ids.emplace(id, plan.ids.size()).second) {
      query_usage(std::format("duplicate query id '{}'", id));
    }
    if (!q.HasMember("op") || !q["op"].IsString()) {
      query_usage(std::format("query '{}' needs a string \"op\"", id));
    }
    const std::string op    = q["op"].GetString();
    const std::string where = std::format("query '{}'", id);
    plan.ids.push_back(id);
    plan.kernel_results.emplace_back();
    std::string& kres = plan.kernel_results.back();

    // Everything below may raise Q_fail (a per-query answer) — a bad name in one
    // query must not cost the batch its other answers.
    try {
      if (op == "signals") {
        reject_unknown_members(q, {"id", "op", "signal", "scope", "glob", "regex", "kind"}, where);
        const auto sel  = parse_selector(q, where);
        auto       hits = resolve_sel(sel, cat, where);
        if (hits.empty() && sel.by_name) {
          throw Q_fail{"unknown_signal", std::format("no signal named '{}'", sel.signal), "", nearest_names(cat, sel.signal)};
        }
        kres = q_signals_result(id, hits, cat, max_results);
        continue;
      }

      // Everything else needs the replay. Resolve first, THEN emit lines, so a
      // failing query contributes neither a plan line nor a time bound.
      std::string line;
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
          idx = q["index"].GetInt64();
        }
        const auto& c = cat.sigs[resolve_one(sel, cat, where)];
        const auto  t = parse_time(q["at"], std::format("{}.at", where));
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
        line = idx >= 0 ? std::format("Q {} memvalue {} {} {}\n", id, c.name, idx, tok(t))
                        : std::format("Q {} value {} {}\n", id, c.name, tok(t));
        note_lo(t.cycle);
        note_hi(t.cycle);
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
        const auto t = parse_time(q["at"], std::format("{}.at", where));
        auto hits = resolve_sel(sel, cat, where);
        std::erase_if(hits, [&](size_t i) { return cat.sigs[i].kind == "memory"; });  // selectors never enumerate memory words
        if (hits.empty()) {
          throw Q_fail{"unknown_signal",
                       std::format("{} selects no signal", where),
                       "",
                       sel.by_name ? nearest_names(cat, sel.signal) : std::vector<std::string>{}};
        }
        line = std::format("Q {} {} {} {}\n", id, op, tok(t), hits.size());
        for (size_t i : hits) {
          line += std::format("G {}\n", cat.sigs[i].name);
        }
        note_lo(t.cycle);
        note_hi(t.cycle);
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
        if (f.cycle > t.cycle) {
          query_usage(std::format("{}: from {} is after to {}", where, f.cycle, t.cycle));
        }
        auto hits = resolve_sel(sel, cat, where);
        std::erase_if(hits, [&](size_t i) { return cat.sigs[i].kind == "memory"; });
        if (hits.empty()) {
          throw Q_fail{"unknown_signal", std::format("{} selects no signal", where), "", {}};
        }
        line = std::format("Q {} diff {} {} {}\n", id, tok(f), tok(t), hits.size());
        for (size_t i : hits) {
          line += std::format("G {}\n", cat.sigs[i].name);
        }
        note_lo(f.cycle);
        note_hi(t.cycle);
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
        const auto& c = cat.sigs[resolve_one(sel, cat, where)];
        line          = std::format("Q {} changes {} {} {} {}\n", id, c.name, tok(f), tok(t), count_only ? 1 : 0);
        note_lo(f.cycle);
        note_hi(t.cycle);
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
        const auto& c = cat.sigs[resolve_one(sel, cat, where)];
        line          = std::format("Q {} next_change {} {} {}\n", id, c.name, tok(a), tok(t));
        note_lo(a.cycle);
        note_hi(t.cycle);
      } else if (op == "find") {
        reject_unknown_members(q, {"id", "op", "dir", "from", "to", "expr", "sample"}, where);
        std::string dir = "fwd";
        if (q.HasMember("dir")) {
          if (!q["dir"].IsString()) {
            query_usage(std::format("{}.dir must be a string", where));
          }
          const std::string d = q["dir"].GetString();
          if (d == "forward" || d == "fwd") {
            dir = "fwd";
          } else if (d == "backward" || d == "bwd") {
            dir = "bwd";
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
        std::string  xs;
        const size_t nx = emit_expr(q["expr"], cat, xs);
        std::string  ss;
        size_t       nsample = 0;
        if (q.HasMember("sample")) {
          if (!q["sample"].IsArray()) {
            query_usage(std::format("{}.sample must be an array of signal names", where));
          }
          for (const auto& s : q["sample"].GetArray()) {
            if (!s.IsString()) {
              query_usage(std::format("{}.sample entries must be strings", where));
            }
            ss += std::format("S {}\n", cat.sigs[resolve_name(s.GetString(), cat)].name);
            ++nsample;
          }
        }
        line = std::format("Q {} find {} {} {} {} {}\n", id, dir, tok(f), tok(t), nx, nsample) + xs + ss;
        note_lo(f.cycle);
        note_hi(t.cycle);
      } else {
        query_usage(std::format("{}: unknown op '{}'", where, op),
                    "signals | value | values | changes | next_change | find | snapshot | diff");
      }
      body += line;
    } catch (const Q_fail& f) {
      kres = q_error_result(id, f);
    }
  }

  if (body.empty()) {
    return plan;  // every question was answered from the catalog — no plan, no replay work
  }
  std::ofstream pf(plan_path);
  if (!pf.is_open()) {
    throw Lhd_error{"config", std::format("could not write the query plan {}", plan_path), ""};
  }
  pf << "V 1\n";
  pf << std::format("FROM {}\n", any_event || lo_unbounded || lo_min == std::numeric_limits<long>::max() ? -1 : lo_min);
  pf << std::format("TO {}\n", any_event || hi_unbounded ? -1 : hi_max);
  pf << std::format("MAXRES {}\n", max_results);
  pf << body;
  plan.wrote_plan = true;
  return plan;
}

// ---- value enrichment --------------------------------------------------------
//
// The driver has NO catalog — it knows bit patterns, not widths or signedness —
// so every value it writes is the minimal {"hex":"<full-width canonical hex>"}.
// Width, signedness, the decimal rendering and known_mask are catalog facts, and
// the kernel is the side that holds the catalog. Adding them here keeps exactly
// ONE copy of the metadata in the system; teaching the generated C++ a second
// one is how the two drift.
using Json_writer = rj::Writer<rj::StringBuffer>;

const Cat_sig* find_sig(const Sim_catalog& cat, std::string_view name) {
  for (const auto& c : cat.sigs) {
    if (c.name == name || c.alias == name) {
      return &c;
    }
  }
  return nullptr;
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
// the value actually fits there: an internal width carries one more bit than
// the declaration (9 for a `u8`), and truncating a value that uses it would
// silently report a different number than `hex` says.
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

void write_value_enriched(Json_writer& w, const rj::Value& v, const Cat_sig* c) {
  if (c == nullptr || !v.IsObject() || !v.HasMember("hex") || !v["hex"].IsString()) {
    v.Accept(w);  // nothing to enrich it WITH — never guess a width
    return;
  }
  const std::string hex = v["hex"].GetString();
  std::string       err;
  // All-ones at the internal width. v1 is strictly 2-state (Slop resolves x/?
  // to seeded-PRNG bits at parse time), so an engine reporting an unknown bit
  // here would be inventing one; the honest disclosure is run.seed/rng_draws.
  const auto        known_mask = literal_to_hex("-1", c->bits, err);
  w.StartObject();
  w.Key("bits");
  w.Int64(c->bits);
  w.Key("declared_bits");
  w.Int64(c->declared_bits);
  w.Key("signed");
  w.Bool(c->is_signed);
  w.Key("hex");
  w.String(hex.c_str());
  w.Key("dec");
  w.String(hex_to_dec(hex, c->bits, c->declared_bits, c->is_signed).c_str());
  w.Key("known_mask");
  w.String(known_mask.c_str());
  // WHEN in the cycle this value was observed. State settles at the end of the
  // period (where --probe has always sampled); a combinational OUTPUT is the
  // value it drove DURING the period, computed from the state entering it —
  // which is what the VCD shows at that period's timestamps. So a register and
  // an output reading it are one commit apart. That is correct and cheap (the
  // alternative is re-evaluating the design once per sampled cycle), but it is
  // surprising, so every value says which one it is instead of relying on the
  // reader having found the documentation.
  w.Key("sampled");
  w.String(c->kind == "output" ? "during_period" : "settled");
  for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
    const std::string_view k = it->name.GetString();
    if (k == "hex" || k == "bits" || k == "declared_bits" || k == "signed" || k == "dec" || k == "known_mask"
        || k == "sampled") {
      continue;  // ours; a driver that starts emitting them must not double-write
    }
    w.Key(it->name.GetString());
    it->value.Accept(w);
  }
  w.EndObject();
}

// Every member spelling that holds a value object, across the five operations:
// `value` (value/values), `from_value`/`to_value` (diff), `old`/`new` (changes).
bool is_value_key(std::string_view k) { return k == "value" || k == "from_value" || k == "to_value" || k == "old" || k == "new"; }

// One row of a values/diff/changes/sample array. A row names its own signal;
// a single-signal `changes` may leave that to the result level instead.
void write_row_enriched(Json_writer& w, const rj::Value& row, const Sim_catalog& cat, const Cat_sig* outer) {
  if (!row.IsObject()) {
    row.Accept(w);
    return;
  }
  const Cat_sig* c = outer;
  if (row.HasMember("signal") && row["signal"].IsString()) {
    if (const auto* f = find_sig(cat, row["signal"].GetString()); f != nullptr) {
      c = f;
    }
  }
  w.StartObject();
  for (auto it = row.MemberBegin(); it != row.MemberEnd(); ++it) {
    w.Key(it->name.GetString());
    if (is_value_key(it->name.GetString())) {
      write_value_enriched(w, it->value, c);
    } else {
      it->value.Accept(w);
    }
  }
  w.EndObject();
}

void write_result_enriched(Json_writer& w, const rj::Value& r, const Sim_catalog& cat) {
  if (!r.IsObject()) {
    r.Accept(w);
    return;
  }
  const Cat_sig* c = nullptr;
  if (r.HasMember("signal") && r["signal"].IsString()) {
    c = find_sig(cat, r["signal"].GetString());
  }
  w.StartObject();
  for (auto it = r.MemberBegin(); it != r.MemberEnd(); ++it) {
    w.Key(it->name.GetString());
    if (is_value_key(it->name.GetString())) {
      write_value_enriched(w, it->value, c);
    } else if (it->value.IsArray()) {
      w.StartArray();
      for (const auto& row : it->value.GetArray()) {
        write_row_enriched(w, row, cat, c);
      }
      w.EndArray();
    } else {
      it->value.Accept(w);
    }
  }
  w.EndObject();
}

// The response the envelope carries: the driver's sidecar with every value
// enriched from the catalog and the kernel-answered queries SPLICED BACK into
// their request positions. Request order is part of the contract, and `signals`
// results never reach the driver at all.
std::string finalize_sim_query(const Query_plan& plan, const Sim_catalog& cat, const std::string& sidecar_path) {
  std::string raw;
  if (plan.wrote_plan) {
    std::ifstream ifs(sidecar_path);
    if (ifs.is_open()) {
      std::stringstream ss;
      ss << ifs.rdbuf();
      raw = ss.str();
      while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r' || raw.back() == ' ' || raw.back() == '\t')) {
        raw.pop_back();
      }
    }
  }
  rj::Document doc;
  bool         have_driver = false;
  if (raw.size() >= 2 && raw.front() == '{') {
    doc.Parse(raw.c_str());
    have_driver = !doc.HasParseError() && doc.IsObject() && doc.HasMember("results") && doc["results"].IsArray();
  }
  // Index the driver's answers BY ID, not by position: the plan and the response
  // agree on ids, and a positional join would mis-attribute every later answer
  // if the driver ever dropped one.
  std::map<std::string, const rj::Value*> by_id;
  if (have_driver) {
    for (const auto& r : doc["results"].GetArray()) {
      if (r.IsObject() && r.HasMember("id") && r["id"].IsString()) {
        by_id.emplace(r["id"].GetString(), &r);
      }
    }
  }

  rj::StringBuffer             sb;
  rj::Writer<rj::StringBuffer> w(sb);
  w.StartObject();
  if (have_driver) {
    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
      const std::string_view key = it->name.GetString();
      if (key == "results") {
        continue;  // rebuilt below, in request order
      }
      w.Key(it->name.GetString());
      if (key != "run" || !it->value.IsObject()) {
        it->value.Accept(w);
        continue;
      }
      // The driver reports what it MEASURED (samples, replay bounds, seed,
      // fail_cycle); which test and which clock those belong to are selection
      // facts only this side knows, so they are added rather than duplicated
      // into the generated code.
      w.StartObject();
      for (auto rit = it->value.MemberBegin(); rit != it->value.MemberEnd(); ++rit) {
        w.Key(rit->name.GetString());
        rit->value.Accept(w);
      }
      if (!it->value.HasMember("test")) {
        w.Key("test");
        w.String(plan.test.c_str());
      }
      if (!it->value.HasMember("clock")) {
        w.Key("clock");
        if (plan.clock.empty()) {
          w.Null();
        } else {
          w.String(plan.clock.c_str());
        }
      }
      w.EndObject();
    }
  } else {
    // No replay was needed (or the driver produced nothing): the kernel is the
    // whole engine for this batch, and says only what it actually knows.
    w.Key("schema_version");
    w.Int(1);
    w.Key("kind");
    w.String("sim_query_result");
    w.Key("run");
    w.StartObject();
    w.Key("test");
    w.String(plan.test.c_str());
    w.Key("clock");
    if (plan.clock.empty()) {
      w.Null();
    } else {
      w.String(plan.clock.c_str());
    }
    w.Key("phase");
    w.String("post");
    w.EndObject();
  }
  w.Key("results");
  w.StartArray();
  for (size_t i = 0; i < plan.ids.size(); ++i) {
    if (!plan.kernel_results[i].empty()) {
      w.RawValue(plan.kernel_results[i].data(), plan.kernel_results[i].size(), rj::kObjectType);
      continue;
    }
    if (auto it = by_id.find(plan.ids[i]); it != by_id.end()) {
      write_result_enriched(w, *it->second, cat);
      continue;
    }
    // The query was planned but came back unanswered (an older driver, or one
    // that died mid-batch). An absent id would read as "not asked"; say why.
    const auto miss = q_error_result(plan.ids[i],
                                     Q_fail{"unsupported",
                                            "the generated sim driver returned no answer for this query",
                                            "regenerate the sim dir (drop --run-only) so the driver carries the query evaluator",
                                            {}});
    w.RawValue(miss.data(), miss.size(), rj::kObjectType);
  }
  w.EndArray();
  w.EndObject();
  return sb.GetString();
}

}  // namespace

// `lhd sim <file.prp> [test.name] [--arg k=v ...]` — lower the design's DUT
// modules to Slop<N> structs (inou.cgen.sim) and, for each `test` block,
// generate a C++ driver that runs the `tick` loop and turns `assert`s into
// runtime checks, then host-compile + run it and report pass/fail.
//
// NOT bazel — this comment used to say "bazel-build", and it was wrong. Setup
// does write a standalone bazel module next to the sources (BUILD +
// MODULE.bazel, see sim_into in lhd_kernel_common.cpp) so the dir stays
// hand-buildable with `cd <simdir> && bazel run //:drv`, but `lhd sim` never
// invokes it: it shells out to the host compiler directly (the "fast run path"
// below). A nested bazel would want its own server and lock, and that generated
// MODULE.bazel archive_overrides iassert from GitHub — so it needs the network
// on a cold build, and it pins hlop at `<cwd>/../hlop`, which only resolves in
// the dev layout. `lhd sim` has to work inside SOMEONE ELSE'S bazel test
// sandbox (that is how lhdsuite drives it), where none of that holds.
//
// The trade used to cost what bazel would have handed us for free — per-TU
// parallelism and incremental rebuilds. Both are back, without the sandbox
// problems: parallelism from the built-in fan-out (`sim.jobs`), and
// incrementality from a generated `build.ninja` used when ninja is present
// (`sim.ninja`). The build file is written either way, so `ninja -C <simdir>`
// always reproduces exactly what lhd did — unlike the bazel project, which
// nothing executes.
void sim_command(Options& opts, Result& res) {
  res.command = "sim pyrope";
  if (opts.files.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  // Positional args: every `.prp` is a SOURCE — `lhd sim a.prp b.prp top.prp`
  // loads all three so the top's `import("a")`/`import("b")` resolve to the
  // co-loaded units (no relative path or `-I` needed). A lone non-`.prp` token is
  // the test selector. The LAST source is the primary, test-containing file;
  // any earlier sources are the units it imports.
  //
  // `ln:DIR` / `lg:DIR` positionals never reach here: the option parser routes
  // them to opts.in_dirs / opts.ins exactly as it does for `lhd compile`, and
  // gather_ir_inputs below hands them to the same compile_sources. So the DUT
  // may equally be a pre-elaborated ln: forest (`import("ln:Cpu")`) or a
  // pre-compiled lg: library (`import("lg:Cpu")`) instead of a .prp beside the
  // testbench — the `test` blocks still need a .prp, which is why one is
  // required regardless.
  std::vector<std::string> sources;
  std::string              test_sel;
  for (const auto& f : opts.files) {
    if (str_tools::ends_with(f, ".prp")) {
      sources.push_back(f);
    } else {
      test_sel = f;
    }
  }
  if (sources.empty()) {
    throw Lhd_error{"usage", "sim requires a .prp file", "e.g. `lhd sim foo.prp` or `lhd sim foo.prp test.name`"};
  }
  // The LAST source is the primary, test-containing file by convention — but
  // accept either order: when the last source holds no `test` block and an
  // earlier positional does, that one is the testbench
  // (`lhd sim tb.prp dut.prp` behaves the same as `lhd sim dut.prp tb.prp`).
  std::string file = sources.back();
  if (sources.size() > 1) {
    std::vector<prp_sim::Test_info> probe;
    std::string                     perr;
    // A non-zero return means the probe FAILED — but list_tests conflates two
    // failures under rc=1: a real read/parse error (the user's problem, must
    // surface) and "parsed fine, holds no `test` block" (the REORDER SIGNAL —
    // the last positional is the DUT, `lhd sim tb.prp dut.prp`). Telling them
    // apart by the message keeps genuine errors loud while making both
    // argument orders work; treating both as fatal made tb-first invocations
    // die with "no test blocks found in <dut>.prp" before the reorder scan
    // ever ran.
    const int  rc            = prp_sim::list_tests(file, "", probe, perr);
    const bool last_testless = rc != 0 && perr.rfind("no test blocks found", 0) == 0;
    if (rc != 0 && !last_testless) {
      throw Lhd_error{"usage", perr.empty() ? std::format("cannot read `{}`", file) : perr, ""};
    }
    if (probe.empty()) {
      for (auto it = std::next(sources.rbegin()); it != sources.rend(); ++it) {
        probe.clear();
        perr.clear();
        if (prp_sim::list_tests(*it, "", probe, perr) == 0 && !probe.empty()) {
          file = *it;
          break;
        }
      }
    }
  }
  const bool        setup_only = opts.sim_setup_only;
  const bool        run_only   = opts.sim_run_only;
  const bool        list_only  = opts.list_tests;
  const bool        pretty     = opts.diag_fmt == Diag_fmt::pretty;
  if (setup_only && run_only) {
    throw Lhd_error{"usage", "--setup-only and --run-only are mutually exclusive", ""};
  }

  // ---- --list-tests: a pure parse of the source's `test` blocks -> the dotted
  // names + parameters. No DUT lowering / build, so tooling can enumerate the
  // tests cheaply (and even when the design does not compile). Output honors
  // --diag-fmt like every other command: JSON (machine-readable, the SAME shape
  // the built binary's `--list-tests` prints) by default when piped, or a
  // human-readable listing in pretty mode; `--diag-fmt` overrides the auto-pick.
  if (list_only) {
    std::vector<prp_sim::Test_info> tests;
    std::string                     err;
    if (prp_sim::list_tests(file, test_sel, tests, err) != 0) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = err;
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    if (pretty) {
      std::print("{} test(s) in {}:\n", tests.size(), file);
      for (const auto& t : tests) {
        std::print("  {}", t.name);
        for (size_t i = 0; i < t.params.size(); ++i) {
          const auto& p = t.params[i];
          std::print("{}{}{}",
                     i == 0 ? "(" : ", ",
                     p.name,
                     p.required ? std::string(": required") : std::format(" = {}", p.default_text));
        }
        if (!t.params.empty()) {
          std::print(")");
        }
        std::print("\n");
      }
    } else {
      std::print("{}\n", prp_sim::tests_to_json(file, tests));
    }
    std::fflush(stdout);
    return;  // status stays pass (a pure query — no output artifact)
  }

  // The sim build dir: --workdir if given (REUSED in place — generated files are
  // overwritten, nothing is deleted), else a fresh OS-temp dir. The temp dir is
  // OUTSIDE any workspace, so a later `bazel build //...` in the user's repo
  // never sweeps the nested BUILD package or follows its convenience symlinks.
  std::string simroot;
  if (!opts.workdir.empty()) {
    simroot = opts.workdir;
  } else {
    if (run_only) {
      throw Lhd_error{"usage", "--run-only needs --workdir pointing at a prior --setup-only build", ""};
    }
    auto              tmpl = (fs::temp_directory_path() / "lhd_sim_XXXXXX").string();
    std::vector<char> buf(tmpl.c_str(), tmpl.c_str() + tmpl.size() + 1);
    if (::mkdtemp(buf.data()) == nullptr) {
      throw Lhd_error{"config", "could not create a temp dir for the sim build", "set $TMPDIR or pass --workdir"};
    }
    simroot = buf.data();
  }
  const std::string simdir = simroot + "/sim";

  // --run-only against a workdir that holds no prior --setup-only build has
  // nothing to run. Say so instead of proceeding into a confusing downstream
  // failure (a missing driver source reads as a codegen bug, not a stale or
  // mistyped --workdir).
  if (run_only && !fs::exists(simdir)) {
    throw Lhd_error{"missing_file",
                    std::format("--run-only found no sim build in '{}'", simroot),
                    "run `lhd sim <tb.prp> --setup-only --workdir <dir>` first, or check --workdir points at that dir"};
  }

  // --set sim.vcd=true: dump one VCD per test, `<workdir>/<test.name>.vcd`. The
  // path is absolute (the driver binary is run from the caller's cwd), and when
  // on, the fast build also links hlop's VCD writer (vcd_writer.cpp).
  bool vcd_on            = false;
  bool vcd_fakedelay     = true;  // sim.vcdfakedelay: X + settle delay after each edge (default); false = edge-aligned
  bool vcd_fakedelay_set = false;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.vcd") {
      // bool-or-FILE: any non-false value turns tracing on (an explicit FILE
      // only matters for compiled/baked binaries; `lhd sim` derives per-test paths)
      vcd_on = !(v == "false" || v == "0" || v == "off" || v.empty());
    } else if (k == "sim.vcd_fake_delay") {
      vcd_fakedelay     = (v == "true" || v == "1" || v == "on");
      vcd_fakedelay_set = true;
    }
  }
  // A `--vcd-from` window or `--vcd-on-fail` implies VCD: the driver needs the
  // trace machinery emitted (the first run still produces none until a window opens).
  if (opts.sim_vcd_from >= 0 || opts.sim_vcd_on_fail) {
    vcd_on = true;
  }
  const std::string vcd_dir = vcd_on ? fs::absolute(simroot).string() : std::string{};

  // --set sim.checkpoint* : periodic editable checkpoints of DUT + testbench state
  // under <simroot>/ckpt/<test>/ckp<cycle>/ (regs.json + *.hex + tb.json +
  // meta.json). Default ON; a short run (< the min-secs floor) writes none. The
  // settings are forwarded to the driver, which owns the fork cadence + prune.
  bool        ckpt_on = true;
  std::string ckpt_min_secs, ckpt_max, ckpt_max_overhead, ckpt_every;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.checkpoint") {
      ckpt_on = (v == "true" || v == "1" || v == "on");
    } else if (k == "sim.checkpoint_min_secs") {
      ckpt_min_secs = v;
    } else if (k == "sim.checkpoint_max") {
      ckpt_max = v;
    } else if (k == "sim.checkpoint_max_overhead") {
      ckpt_max_overhead = v;
    } else if (k == "sim.checkpoint_every") {
      ckpt_every = v;
    }
  }
  const std::string ckpt_dir = ckpt_on ? (fs::absolute(simroot).string() + "/ckpt") : std::string{};

  // Debug-flag sanity (sim_checkpoint_debug_plan): catch contradictory combinations
  // up front instead of silently producing a degenerate run.
  if (opts.sim_vcd_to >= 0 && opts.sim_vcd_from < 0) {
    throw Lhd_error{"usage", "--vcd-to needs --vcd-from (the window start)", "e.g. --vcd-from 100 --vcd-to 140"};
  }
  if (opts.sim_vcd_from >= 0 && opts.sim_vcd_to >= 0 && opts.sim_vcd_from > opts.sim_vcd_to) {
    throw Lhd_error{"usage", std::format("--vcd-from {} is after --vcd-to {}", opts.sim_vcd_from, opts.sim_vcd_to), ""};
  }
  if (ckpt_dir.empty() && opts.sim_restart_at >= 0) {
    throw Lhd_error{"usage",
                    "--restart-at needs checkpoints — do not combine it with --set sim.checkpoint=false",
                    "run once with checkpointing on to create them, then --restart-at"};
  }
  // --query owns the replay: it plans ONE traversal over the union of the
  // batch's time ranges and picks its own checkpoint. --restart-at and the VCD
  // window flags plan the same replay differently (--vcd-from silently doubles
  // as a restart target), so the two planners would fight over one run. v1 says
  // so instead of picking a winner; composition can come later.
  if (!opts.sim_query.empty()
      && (opts.sim_restart_at >= 0 || opts.sim_vcd_from >= 0 || opts.sim_vcd_to >= 0 || opts.sim_vcd_on_fail)) {
    throw Lhd_error{"usage",
                    "--query cannot be combined with --restart-at / --vcd-from / --vcd-to / --vcd-on-fail",
                    "run the query batch on its own; a VCD or a restart is a separate invocation"};
  }

  std::vector<prp_sim::Test_info> tests;

  // ---- setup: lower DUT -> Slop, generate the single driver (drv.cpp)
  if (!run_only) {
    ensure_dir(simdir);
    opts.language = "pyrope";
    opts.files    = sources;  // compile ALL positional sources (imports resolve across them)
    if (vcd_on) {
      opts.sets.emplace_back("sim.vcd", "1");  // make cgen_sim emit the VCD machinery
      if (!vcd_fakedelay) {
        opts.sets.emplace_back("sim.vcd_fake_delay", "false");  // edge-aligned trace (cgen default is true)
      }
    }
    // `sim` is DYNAMIC verification: each `test` block's asserts are checked by
    // running the generated driver, not formally. Skip pass.formal — a `test`
    // lowers to a never-instantiated comb whose runtime parameters become free
    // inputs, so a concrete-valued assert (`assert(acc == 22)`) would otherwise
    // be "refuted" over those free inputs even though the bound run satisfies it.
    opts.sets.emplace_back("compile.formal.mode", "none");
    opts.emit_dirs.push_back(Typed_path{"sim", simdir});
    auto ir = gather_ir_inputs(opts, "sim");
    compile_sources(opts, res, ir);
    if (res.status != "pass") {
      return;
    }
    std::string err;
    if (prp_sim::generate(file, simdir, test_sel, vcd_dir, tests, err) != 0) {
      res.status        = "fail";
      res.error_class   = "unsupported";
      res.error_message = err;
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    // Also append a single `drv` cc_binary so the generated dir stays
    // bazel-buildable (`cd <simdir> && bazel run //:drv -- --test ...`); the
    // default `lhd sim` flow runs it via the fast host-compile below, no bazel.
    std::ofstream bf(std::format("{}/BUILD", simdir), std::ios::app);
    bf << "\nload(\"@rules_cc//cc:defs.bzl\", \"cc_binary\")\n";
    bf << std::format(
        "cc_binary(\n    name = \"{0}\",\n    srcs = [\"{0}.cpp\"],\n    copts = [\"-std=c++23\"],\n"
        "    deps = [\":sim\", \"@hlop//hlop\"],\n)\n",
        prp_sim::kDriverBasename);
    bf.close();
    res.recipe_steps.push_back(std::format("sim setup: {} test(s) in {}", tests.size(), simdir));
  }

  if (setup_only) {
    res.outputs.push_back(simdir);
    if (pretty) {
      std::print("  sim setup complete: {} test(s) generated in {}\n", tests.size(), simdir);
      std::print("  run with: lhd sim {} --run-only --workdir {}\n", file, simroot);
      std::fflush(stdout);
    }
    return;  // status stays pass
  }

  auto capture = [](const std::string& c, int& rc) {
    std::string out;
    char        buf[4096];
    FILE*       p = ::popen(c.c_str(), "r");
    if (p == nullptr) {
      rc = -1;
      return out;
    }
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) {
      out.append(buf, n);
    }
    int st = ::pclose(p);
    rc     = (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return out;
  };

  // ---- the single fast run path: host-compile drv.cpp + the DUT bodies (+ the
  // VCD writer when sim.vcd) with the host C++ compiler, then run the one binary.
  // The Slop runtime is header-only and, with -DNDEBUG, has no link deps — so
  // there is no nested bazel, no abseil, no network. (For --run-only the driver +
  // bodies are reused from a prior --setup-only; only the compile + run happen.)
  const std::string drv_cpp = std::format("{}/{}.cpp", simdir, prp_sim::kDriverBasename);
  if (::access(drv_cpp.c_str(), R_OK) != 0) {
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = std::format("no generated sim driver in {} (run --setup-only --workdir {} first)", simdir, simroot);
    res.exit_code     = exit_code_for(res.error_class);
    return;
  }
  // A VCD request against a prior --setup-only that was generated WITHOUT VCD (the
  // driver lacks the trace machinery) would silently produce no waveform — reject
  // it so the user regenerates instead. The `vcd::global_timestamp` line is emitted
  // iff VCD codegen was on (prp_sim).
  if (run_only) {
    std::ifstream     dfs(drv_cpp);
    std::stringstream dss;
    dss << dfs.rdbuf();
    const bool baked_vcd = dss.str().find("vcd::global_timestamp") != std::string::npos;
    if (vcd_on && !baked_vcd) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message
          = "this --run-only sim was generated without VCD; re-run without --run-only (or "
                          "--setup-only --set sim.vcd=true) so the driver gets the trace machinery";
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
    // PERSIST the setup-time decision. sim.vcd is a DRIVER-AFFECTING setting:
    // setup bakes the trace machinery in, and the run phase has to link hlop's
    // vcd_writer.cpp to satisfy it. Requiring the flag again on the run
    // invocation made the honest two-phase sequence die at link time with an
    // undefined `__vcd_init()`, so every caller had to repeat the flag. The
    // driver source already records what was baked — use it.
    if (baked_vcd && !vcd_on) {
      vcd_on = true;
    }
  }
  // Same staleness trap for the VCD style: sim.vcdfakedelay is BAKED at setup
  // (the X/settle phases are codegen), so an explicit request that disagrees
  // with the prior --setup-only would be silently ignored — reject instead.
  // The `__vcd_dump_x` method is emitted iff fake-delay codegen was on.
  if (run_only && vcd_on && vcd_fakedelay_set) {
    bool baked_fakedelay = false;
    for (const auto& de : fs::directory_iterator(simdir)) {
      if (de.path().extension() != ".hpp") {
        continue;
      }
      std::ifstream     hfs(de.path());
      std::stringstream hss;
      hss << hfs.rdbuf();
      if (hss.str().find("__vcd_dump_x") != std::string::npos) {
        baked_fakedelay = true;
        break;
      }
    }
    if (baked_fakedelay != vcd_fakedelay) {
      res.status        = "fail";
      res.error_class   = "usage";
      res.error_message = std::format(
          "this --run-only sim was generated with sim.vcd_fake_delay={}; the style is baked "
                                      "at codegen — re-run --setup-only with the desired --set sim.vcd_fake_delay",
                                      baked_fakedelay ? "true" : "false");
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
  }
  const std::string hlop_inc    = sim_hlop_include_dir(opts);
  const std::string iassert_inc = sim_iassert_include_dir(opts);
  if (hlop_inc.empty() || iassert_inc.empty()) {
    res.status        = "fail";
    res.error_class   = "dependency";
    res.error_message = std::format("could not locate the sim runtime headers (slop.hpp: {}, iassert.hpp: {})",
                                    hlop_inc.empty() ? "<not found>" : hlop_inc,
                                    iassert_inc.empty() ? "<not found>" : iassert_inc);
    res.exit_code     = exit_code_for(res.error_class);
    if (pretty) {
      std::print(
          "  hint: run `lhd` from bazel (its runfiles carry slop.hpp/iassert.hpp), or export "
          "RUNFILES_DIR=<...>/lhd.runfiles to run it by hand; a source checkout resolves them from the sibling "
          "../hlop and ../iassert, and --set sim.hlop_dir=DIR / "
          "--set sim.iassert_dir=DIR point at an explicit checkout\n");
      std::fflush(stdout);
    }
    return;
  }
  const std::string cxx = sim_host_cxx();

  // The DUT bodies: every non-driver *.cpp in simdir. inou.cgen.sim does NOT emit
  // the `%`-named `test` units, so these are exactly the real module bodies.
  std::vector<std::string> bodies;
  {
    std::error_code ec;
    for (auto& de : fs::directory_iterator(simdir, ec)) {
      if (!de.is_regular_file()) {
        continue;
      }
      auto fn = de.path().filename().string();
      if (fn.size() < 5 || fn.substr(fn.size() - 4) != ".cpp") {
        continue;
      }
      if (fn == std::string(prp_sim::kDriverBasename) + ".cpp") {
        continue;  // the driver itself (exact name — a prefix skip would also drop a `drv*.prp` design's DUT bodies)
      }
      bodies.push_back(de.path().string());
    }
    std::sort(bodies.begin(), bodies.end());
  }

  const std::string exe = std::format("{}/{}.bin", simdir, prp_sim::kDriverBasename);

  // The translation units, in a FIXED order — the sorted DUT bodies, then the
  // driver, then hlop's VCD writer when tracing was baked in. The order is what
  // makes the object names and build.log reproducible; the link does not care.
  std::vector<std::string> tus = bodies;
  tus.push_back(drv_cpp);
  if (vcd_on) {
    tus.push_back(hlop_inc + "/vcd_writer.cpp");
  }

  // -O2, not -O1 (todo/livehd/2f-latch M7 efficiency item b). The optimization
  // level is NOT the lever here; the job count is. Measured 2026-07-30 on
  // dino's whole-CPU driver (18 TUs, 5372 generated lines, 18-core arm64):
  //
  //   one serial clang++ over all TUs   -O2 36.5s  -O1 31.6s  -Os 30.9s  -O0 17.3s
  //   per-TU -c in parallel, then link  -O2  6.9s
  //   the simulation itself (20k cycles)  -O2 3.8s   -Os 6.5s  -Oz 11.0s
  //
  // So parallelism buys 5.3x where the level buys ~15%, and -Os pays for its
  // 16% of build with 68% of the RUN. The note that used to sit here — "compile
  // time is IDENTICAL at -O1/-O2/-O3" — was measured on a 1-2 TU driver and
  // does not survive at 18; it reached the right conclusion (keep -O2) for a
  // reason that no longer holds. -O3 still measures no better and inflates code
  // size on generated straight-line arithmetic.
  // ABSOLUTE -I: `--workdir` is routinely relative, and the ninja build runs
  // with its cwd set to the sim dir (so its .ninja_deps/.ninja_log land there),
  // where a relative `-Iw/sim` would resolve to nothing.
  const std::string simdir_abs = fs::absolute(simdir).string();
  const std::string cflags     = std::format("-std=c++23 -DNDEBUG -O2 -I{} -I{} -I{}",
                                             shell_quote(simdir_abs),
                                             shell_quote(hlop_inc),
                                             shell_quote(iassert_inc));

  // --set sim.jobs=N bounds the fan-out (0/unset = one per hardware thread).
  // Pin it to make a build-time measurement reproducible, or to leave the
  // machine usable while a big design builds. Also becomes `ninja -j`.
  int jobs = 0;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.jobs") {
      jobs = std::atoi(v.c_str());
    }
  }
  if (jobs <= 0) {
    jobs = static_cast<int>(std::thread::hardware_concurrency());
  }
  jobs = std::clamp(jobs, 1, static_cast<int>(tus.size()));

  // One object per TU. The objects are NOT wiped between runs and their names
  // are STABLE (derived from the source basename, never from a position in the
  // list) — a positional name would rename every object the moment a module is
  // added, turning an incremental build into a full one and orphaning the old
  // files. Design bodies all live in simdir, so their basenames are unique by
  // construction; only hlop's vcd_writer.cpp comes from outside, and it gets a
  // reserved name so a design module called `vcd_writer` cannot collide with it.
  const std::string objdir = simdir + "/obj";
  ensure_dir(objdir);
  std::vector<std::string> objs(tus.size());
  for (size_t i = 0; i < tus.size(); ++i) {
    const bool external = (fs::path(tus[i]).parent_path() != fs::path(simdir));
    objs[i]             = std::format("{}/{}{}.o", objdir, external ? "_rt_" : "", fs::path(tus[i]).stem().string());
  }
  {
    // Two TUs mapping to one object would make ninja hard-fail ("multiple rules
    // generate ...") but would silently make the built-in path link whichever
    // compile finished last — a wrong binary from a green run. Catch it here so
    // both paths report the same, nameable problem.
    auto sorted = objs;
    std::sort(sorted.begin(), sorted.end());
    auto dup = std::adjacent_find(sorted.begin(), sorted.end());
    if (dup != sorted.end()) {
      res.status        = "fail";
      res.error_class   = "unsupported";
      res.error_message = std::format("two sim translation units map to the same object file ({}) — rename one module", *dup);
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
  }

  // build.ninja, ALWAYS written (even when the build below does not use ninja).
  // It is the escape hatch that works: `ninja -C <simdir>` reproduces exactly
  // what lhd did. That matters because the OTHER generated build file, the
  // bazel one, is never executed by lhd and has silently rotted — its hlop
  // local_path_override resolves only in a dev layout. A build file lhd itself
  // runs cannot drift from the build lhd performs.
  //
  // Ninja is worth generating for ONE reason: incrementality. Parallelism is
  // already covered by the fan-out below. What ninja adds is depfile-accurate
  // staleness (`deps = gcc` + `-MD`), including the runtime headers
  // (slop.hpp/memory.hpp) that no hand-rolled rule would think to track. Its
  // mtime keying is sound here only because emission upstream is
  // WRITE-IF-DIFFERENT (File_output::same_on_disk): a comment-only Pyrope edit
  // rewrites nothing, and a body-only edit rewrites just that module's .cpp,
  // leaving its .hpp — and therefore every parent's object — untouched. If that
  // invariant ever breaks, this degrades to a full rebuild every run.
  //
  // Two limits of the mtime model, recorded because neither is enforced here.
  // (a) Ninja treats an output as fresh when it is NOT OLDER than its inputs,
  // so a source rewritten inside the same filesystem timestamp tick as its
  // object is missed. Sub-second stamps (APFS, ext4, btrfs, xfs) make the
  // window vanishingly small; a whole-second filesystem would make an
  // edit-and-rerun within one second able to link a stale object. (b) The link
  // line is `$in`, which is argument-list bound — around 8k objects on a 1 MiB
  // ARG_MAX. Both are far outside anything these designs reach; a build that
  // does would want `rspfile`, and a `restat`/digest rule respectively.
  const std::string ninja_file = simdir + "/build.ninja";
  {
    // Ninja path escaping (build statements): space, `:` and `$` are the only
    // characters with meaning. Commands additionally go through /bin/sh, so
    // single-input paths are double-quoted at the point of use. Paths are
    // written ABSOLUTE: ninja runs with -C into the sim dir, and the file is
    // also meant to be run by hand from anywhere.
    auto nesc = [](const std::string& p) {
      std::error_code ec_abs;
      std::string     s = fs::absolute(p, ec_abs).string();
      if (ec_abs) {
        s = p;
      }
      std::string o;
      o.reserve(s.size());
      for (char c : s) {
        if (c == ' ' || c == ':' || c == '$') {
          o += '$';
        }
        o += c;
      }
      return o;
    };
    // `cflags` already holds shell-quoted -I paths; `$` would be eaten by ninja
    // variable expansion before /bin/sh ever sees it, so double it.
    std::string nflags;
    for (char c : cflags) {
      nflags += (c == '$') ? "$$" : std::string(1, c);
    }
    std::ofstream nf(ninja_file);
    nf << "# Generated by `lhd sim`. Reproduces the exact host build lhd runs:\n"
       << "#   ninja -C " << simdir_abs << "\n"
       << "# Regenerated on every build, so edits here are lost.\n"
       << "ninja_required_version = 1.3\n\n"
       << "cxx = " << cxx << "\n"
       << "cflags = " << nflags << "\n\n"
       // $in/$out are NOT shell-quoted here: ninja already shell-escapes each
       // path as it expands them into `command`, so wrapping them would hand
       // the compiler an argument containing literal quote characters. The
       // `depfile` binding expands RAW instead, which is exactly why the
       // `-MF $out.d` in the command still names the file ninja goes on to
       // read. Rule bodies must be SPACE-indented and every rule must be
       // declared before the first build statement that uses it.
       << "rule cc\n"
       << "  command = $cxx $cflags -MD -MF $out.d -c $in -o $out\n"
       << "  description = CC $out\n"
       << "  depfile = $out.d\n"
       << "  deps = gcc\n\n"
       << "rule link\n"
       << "  command = $cxx $in -o $out\n"
       << "  description = LINK $out\n\n";
    for (size_t i = 0; i < tus.size(); ++i) {
      nf << "build " << nesc(objs[i]) << ": cc " << nesc(tus[i]) << "\n";
    }
    nf << "\nbuild " << nesc(exe) << ": link";
    for (const auto& o : objs) {
      nf << " " << nesc(o);
    }
    nf << "\n\ndefault " << nesc(exe) << "\n";
  }

  // --set sim.ninja: "" (default) = use ninja when it is on PATH, else the
  // built-in fan-out; false = never; true = require it (fail if absent);
  // PATH = use that binary. The DEFAULT must stay "use it only if present":
  // `lhd sim` runs inside other people's build sandboxes, and needing nothing
  // but a host C++ compiler is exactly why the fast path exists at all. A hard
  // ninja dependency would repeat the mistake the generated bazel project made.
  std::string ninja_set;
  for (const auto& [k, v] : opts.sets) {
    if (k == "sim.ninja") {
      ninja_set = v;
    }
  }
  std::string ninja_bin;
  if (ninja_set != "false" && ninja_set != "0" && ninja_set != "off") {
    if (!ninja_set.empty() && ninja_set != "true" && ninja_set != "1" && ninja_set != "on") {
      ninja_bin = ninja_set;  // an explicit path
    } else {
      int  probe_rc = 0;
      auto found    = capture("command -v ninja 2>/dev/null", probe_rc);
      if (probe_rc == 0) {
        while (!found.empty() && (found.back() == '\n' || found.back() == '\r')) {
          found.pop_back();
        }
        // Absolute only. A bare or `./`-relative hit would mean we picked up a
        // `ninja` sitting in the caller's cwd — and bazel runs tests with `.`
        // FIRST on PATH, so that is a live way to have the build hijacked by a
        // file that merely shares a name with the tool.
        if (!found.empty() && found.front() == '/') {
          ninja_bin = found;
        }
      }
    }
    if (ninja_bin.empty() && (ninja_set == "true" || ninja_set == "1" || ninja_set == "on")) {
      res.status        = "fail";
      res.error_class   = "dependency";
      res.error_message = "--set sim.ninja=true but no `ninja` on PATH (drop the flag to use the built-in parallel build)";
      res.exit_code     = exit_code_for(res.error_class);
      return;
    }
  }

  // fail_build LOG_BODY SHOWN — the one compile-failure exit, shared by both
  // build paths. Persists the full compiler output (in JSONL mode it was
  // previously swallowed entirely — "see the compiler output" pointed nowhere)
  // and surfaces the first error line in the machine-readable message.
  auto fail_build = [&](const std::string& log_body, const std::string& shown) {
    const std::string build_log = simdir + "/build.log";
    {
      std::ofstream bl(build_log);
      if (bl.is_open()) {
        bl << log_body;
      }
    }
    std::string        first_err;
    std::string        ln;
    std::istringstream iss(shown);
    while (std::getline(iss, ln)) {
      if (ln.find("error") != std::string::npos) {
        first_err = ln;
        break;
      }
    }
    res.status        = "fail";
    res.error_class   = "compile";
    res.error_message = std::format("the sim driver failed to compile ({}): {}",
                                    build_log,
                                    first_err.empty() ? std::string{"see the log"} : first_err);
    res.exit_code     = exit_code_for(res.error_class);
    if (pretty) {
      std::istringstream sis(shown);
      while (std::getline(sis, ln)) {
        std::print("    {}\n", ln);
      }
      std::fflush(stdout);
    }
  };

  if (!ninja_bin.empty()) {
    // ninja owns compile AND link, and skips whatever is already up to date.
    // Its own output is already per-edge buffered and ordered, so it is both
    // the log body and what gets shown.
    const std::string nc = std::format("{} -C {} -j {} 2>&1", shell_quote(ninja_bin), shell_quote(simdir), jobs);
    int               nrc = 0;
    auto              nout = capture(nc, nrc);
    if (nrc != 0) {
      fail_build(nc + "\n\n" + nout, nout);
      return;
    }
  } else {
    // Built-in fallback: compile every TU, `jobs` at a time, then link. No
    // staleness check — this path always rebuilds, because without depfiles it
    // cannot know which headers a TU read, and a wrong answer there is a
    // silently stale binary reporting wrong simulation values.
    std::vector<std::string> cmds(tus.size()), outs(tus.size());
    std::vector<int>         rcs(tus.size(), 0);
    {
      std::atomic<size_t> cursor{0};
      auto                worker = [&] {
        for (size_t i = cursor.fetch_add(1); i < tus.size(); i = cursor.fetch_add(1)) {
          cmds[i] = std::format("{} {} -c {} -o {} 2>&1",
                                shell_quote(cxx),
                                cflags,
                                shell_quote(tus[i]),
                                shell_quote(objs[i]));
          outs[i] = capture(cmds[i], rcs[i]);
        }
      };
      std::vector<std::thread> pool;
      pool.reserve(static_cast<size_t>(jobs));
      for (int t = 0; t < jobs; ++t) {
        pool.emplace_back(worker);
      }
      for (auto& t : pool) {
        t.join();
      }
    }

    // Every TU's command + output reaches build.log in TU order, so the log is
    // the same whatever order the workers finished in. What is PRINTED is only
    // the FIRST failing TU: one bad generated header fails all 18 identically,
    // and eighteen copies of the one diagnostic bury it.
    std::string log_body, shown;
    size_t      n_failed = 0;
    for (size_t i = 0; i < tus.size(); ++i) {
      log_body += cmds[i] + "\n\n" + outs[i] + "\n";
      if (rcs[i] != 0 && n_failed++ == 0) {
        shown = outs[i];
      }
    }
    if (n_failed != 0) {
      if (n_failed > 1) {
        shown += std::format("({} more translation unit(s) also failed; see build.log)\n", n_failed - 1);
      }
      fail_build(log_body, shown);
      return;
    }

    std::string link = shell_quote(cxx);
    for (const auto& o : objs) {
      link += " " + shell_quote(o);
    }
    link += " -o " + shell_quote(exe) + " 2>&1";  // merge linker diagnostics into the capture
    int  link_rc  = 0;
    auto link_out = capture(link, link_rc);
    if (link_rc != 0) {
      fail_build(link + "\n\n" + link_out, link_out);
      return;
    }
  }

  // Run the one binary. A test selector (`lhd sim foo.prp my.test`) becomes
  // `--test`; an explicit `--seed` and every `lhd sim --arg key=value` are
  // forwarded (the binary itself filters per-test params + warns on a `--arg`
  // that no run test consumes).
  std::string run_args;
  if (!test_sel.empty()) {
    run_args += " --test " + shell_quote(test_sel);
  }
  if (opts.seed_explicit) {
    run_args += " --seed " + shell_quote(opts.seed);
  }
  // Always ask the driver for its per-test result array (a sidecar JSON file);
  // it is read back below and embedded verbatim as the envelope's "tests" member
  // (so `lhd sim --result-json r.json` carries {test,status,cycle,failing_assert,
  // prp_file,line,msg} per test). Remove any stale sidecar from a reused workdir.
  const std::string sim_tests_path = std::format("{}/sim_tests.json", simdir);
  {
    std::error_code ec_rm;
    fs::remove(sim_tests_path, ec_rm);
  }
  run_args += " --result-json " + shell_quote(sim_tests_path);
  // Checkpoint creation (sim.checkpoint*): enabled by default; the driver owns the
  // fork cadence / prune and only writes once the min-secs floor elapses.
  if (!ckpt_dir.empty()) {
    run_args += " --ckpt-dir " + shell_quote(ckpt_dir);
    if (!ckpt_min_secs.empty()) {
      run_args += " --checkpoint-min-secs " + shell_quote(ckpt_min_secs);
    }
    if (!ckpt_max.empty()) {
      run_args += " --checkpoint-max " + shell_quote(ckpt_max);
    }
    if (!ckpt_max_overhead.empty()) {
      run_args += " --checkpoint-max-overhead " + shell_quote(ckpt_max_overhead);
    }
    if (!ckpt_every.empty()) {
      run_args += " --checkpoint-every " + shell_quote(ckpt_every);
    }
  } else {
    run_args += " --no-checkpoint";
  }
  // Debug replay: jump to the failure region (loads the nearest checkpoint <= N).
  if (opts.sim_restart_at >= 0) {
    run_args += " --restart-at " + shell_quote(std::to_string(opts.sim_restart_at));
  }
  // Windowed VCD: restart near Y, run silent to Y, trace [Y, Z].
  if (opts.sim_vcd_from >= 0) {
    run_args += " --vcd-from " + shell_quote(std::to_string(opts.sim_vcd_from));
    if (opts.sim_vcd_to >= 0) {
      run_args += " --vcd-to " + shell_quote(std::to_string(opts.sim_vcd_to));
    }
  }
  // On an assert fire, auto-dump a VCD of the failure region (re-run from the
  // nearest checkpoint with a window around the failing cycle).
  if (opts.sim_vcd_on_fail) {
    run_args += " --vcd-on-fail --vcd-fail-window " + shell_quote(std::to_string(opts.sim_vcd_fail_window));
  }
  // Observability: --list-signals / --probe / --break-when. Results go to the
  // debug sidecar, read back below and embedded as the envelope's "debug" member.
  const std::string sim_debug_path = std::format("{}/sim_debug.json", simdir);
  {
    std::error_code ec_rm2;
    fs::remove(sim_debug_path, ec_rm2);
  }
  const bool debug_requested = opts.sim_list_signals || !opts.sim_probe.empty() || !opts.sim_break_when.empty();
  if (debug_requested) {
    run_args += " --debug-json " + shell_quote(sim_debug_path);
    if (opts.sim_list_signals) {
      run_args += " --list-signals";
    }
    if (!opts.sim_probe.empty()) {
      run_args += " --probe " + shell_quote(opts.sim_probe);
      if (opts.sim_probe_from >= 0) {
        run_args += " --probe-from " + shell_quote(std::to_string(opts.sim_probe_from));
      }
      if (opts.sim_probe_to >= 0) {
        run_args += " --probe-to " + shell_quote(std::to_string(opts.sim_probe_to));
      }
    }
    if (!opts.sim_break_when.empty()) {
      run_args += " --break-when " + shell_quote(opts.sim_break_when);
    }
  }
  // --query: the kernel half of the batched query API. Everything JSON happens
  // HERE — validate the request, resolve every selector against the static
  // catalog, answer whatever needs no simulation, union the surviving time
  // ranges, and leave the driver a selector-free line plan. A batch of nothing
  // but `signals` needs no plan at all: the run still happens (a query run is
  // still a test run) but it carries no query work.
  const std::string sim_query_path  = std::format("{}/sim_query.json", simdir);
  const std::string query_plan_path = std::format("{}/sim_query_plan.txt", simdir);
  Sim_catalog       query_cat;
  Query_plan        query_plan;
  if (!opts.sim_query.empty()) {
    // Stale artifacts from a reused --workdir would read as THIS run's answers.
    std::error_code ec_rm3;
    fs::remove(sim_query_path, ec_rm3);
    fs::remove(query_plan_path, ec_rm3);
    query_cat  = load_sim_catalog(simdir, test_sel);
    query_plan = plan_sim_query(read_query_request(opts.sim_query), query_cat, query_plan_path);
    if (query_plan.wrote_plan) {
      run_args += " --query-plan " + shell_quote(query_plan_path) + " --query-json " + shell_quote(sim_query_path);
    }
  }
  // Forward each `--arg key=value` as `--key value`, but ONLY when `key` is a
  // parameter of a SELECTED test (`selected_params`). Two reasons:
  //  * a key that is a driver control flag (`--arg help=1` -> `--help`, `--arg
  //    test=x` -> `--test x`, `--arg seed=N`) would otherwise be intercepted by
  //    the binary and silently skip / restrict the run — a false green;
  //  * a key that is a real parameter of some test but not a selected one is
  //    irrelevant to this run, so it is dropped silently (not forwarded).
  // A key that is a parameter of NO test in the file (`all_params`) is a genuine
  // typo and is warned about unconditionally (visible in JSON mode too). This
  // restores the pre-single-driver two-layer guard. `tests` lists the SELECTED
  // tests' parameters (generate / list_tests already filtered by `test_sel`); for
  // --run-only re-derive them from the source.
  if (run_only && tests.empty()) {
    std::vector<prp_sim::Test_info> lt;
    std::string                     lerr;
    if (prp_sim::list_tests(file, test_sel, lt, lerr) == 0) {
      tests = std::move(lt);
    }
  }
  std::set<std::string> selected_params;
  for (const auto& t : tests) {
    for (const auto& p : t.params) {
      selected_params.insert(p.name);
    }
  }
  std::set<std::string> all_params = selected_params;  // == selected when no test_sel
  if (!test_sel.empty()) {
    std::vector<prp_sim::Test_info> allt;
    std::string                     aerr;
    if (prp_sim::list_tests(file, "", allt, aerr) == 0) {
      for (const auto& t : allt) {
        for (const auto& p : t.params) {
          all_params.insert(p.name);
        }
      }
    }
  }
  for (const auto& [k, v] : opts.sim_args) {
    if (selected_params.count(k) != 0) {
      run_args += " " + shell_quote("--" + k) + " " + shell_quote(v);
    } else if (all_params.count(k) == 0) {
      std::print(stderr, "lhd sim: warning: --arg {}={} matches no test parameter (ignored)\n", k, v);
    }
    // else: a real parameter of an unselected test — valid but not for this run.
  }
  // Capture the binary's STDOUT (its `puts` output + the per-test PASS/FAIL
  // verdict lines) for parsing + the pretty relay, but let its STDERR pass
  // through to the user's stderr UNCHANGED — that is where the driver prints its
  // warnings (e.g. a `--arg` that matches no test parameter) and usage errors, so
  // they stay visible in JSON mode too (not only in the pretty relay below).
  int  rc  = 0;
  auto out = capture(std::format("{}{}", shell_quote(exe), run_args), rc);

  // Read back the per-test result array (present whenever the driver ran, even on
  // assert failure); embedded as the envelope's "tests" member. Absent if the
  // driver crashed before writing it.
  if (std::error_code ec_st; fs::exists(sim_tests_path, ec_st)) {
    std::ifstream     ifs(sim_tests_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string tj = ss.str();
    while (!tj.empty() && (tj.back() == '\n' || tj.back() == '\r' || tj.back() == ' ' || tj.back() == '\t')) {
      tj.pop_back();
    }
    if (tj.size() >= 2 && tj.front() == '[') {  // a well-formed array, never a partial write
      res.sim_tests_json = tj;
    }
  }

  // Read back the observability sidecar ({signals?, probe?, break?}); embedded as
  // the envelope's "debug" member.
  if (std::error_code ec_dbg; debug_requested && fs::exists(sim_debug_path, ec_dbg)) {
    std::ifstream     ifs(sim_debug_path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string dj = ss.str();
    while (!dj.empty() && (dj.back() == '\n' || dj.back() == '\r' || dj.back() == ' ' || dj.back() == '\t')) {
      dj.pop_back();
    }
    if (dj.size() >= 2 && dj.front() == '{') {
      res.sim_debug_json = dj;
    }
  }

  // The --query answers, embedded as the envelope's "query" member. ABOVE the
  // rc mapping on purpose: an assert-firing replay is still exit 11, and its
  // query answers are exactly what the agent asked the failing run for.
  if (query_plan.active) {
    res.sim_query_json = finalize_sim_query(query_plan, query_cat, sim_query_path);
  }

  // The binary's EXIT CODE is the authoritative verdict (0 = all selected tests
  // passed, 1 = a test failed, 2 = a usage error, <0 = the driver crashed). The
  // per-test `PASS <name>` / `FAIL <name> (...)` lines are parsed only for the
  // structured recipe detail (best-effort: a test that itself `puts` a line
  // starting with "PASS "/"FAIL " adds a cosmetic recipe entry but never changes
  // the verdict, which is rc-driven).
  int    n_fail = 0;
  size_t n_run  = 0;
  {
    std::istringstream iss(out);
    std::string        ln;
    while (std::getline(iss, ln)) {
      const bool pass = ln.rfind("PASS ", 0) == 0;
      const bool fail = ln.rfind("FAIL ", 0) == 0;
      if (!pass && !fail) {
        continue;
      }
      ++n_run;
      std::string name = ln.substr(5);
      if (auto sp = name.find(" ("); sp != std::string::npos) {
        name = name.substr(0, sp);
      }
      res.recipe_steps.push_back(std::format("sim {} ({})", name, pass ? "pass" : "fail"));
      if (fail) {
        ++n_fail;
      }
    }
  }
  if (pretty) {
    std::istringstream iss(out);
    std::string        ln;
    while (std::getline(iss, ln)) {
      std::print("    {}\n", ln);
    }
    std::fflush(stdout);
  }
  res.outputs.push_back(simdir);
  if (rc == 0) {
    // every selected test passed — status stays pass.
  } else if (rc == 2) {
    // a usage error inside the binary (unknown flag / unknown `--test` name /
    // missing value / bad `--arg` value); the binary printed the specific reason
    // (relayed above in pretty mode).
    res.status        = "fail";
    res.error_class   = "usage";
    res.error_message = (!test_sel.empty() && n_run == 0) ? std::format("no test matched '{}' in {}", test_sel, file)
                                                          : std::format("the sim driver rejected an argument in {}", file);
    res.exit_code     = exit_code_for(res.error_class);
  } else {
    // rc == 1 (a test's assert fired) or rc < 0 (the driver crashed / signaled).
    res.status        = "fail";
    res.error_class   = "assert";
    res.error_message = (n_fail > 0 && n_run > 0) ? std::format("{} of {} test(s) failed", n_fail, n_run)
                                                  : std::format("the sim driver exited with code {}", rc);
    res.exit_code     = exit_code_for(res.error_class);
  }
}

// ---- check ------------------------------------------------------------------

std::string shell_quote(const std::string& s) {
  std::string out{"'"};
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += '\'';
  return out;
}

std::string locate_lgcheck() {
  if (const char* env = std::getenv("LHD_LGCHECK"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  auto exe_dir = file_utils::get_exe_path();
  for (const auto& cand : {std::string{"./inou/yosys/lgcheck"},
                           std::string{"inou/yosys/lgcheck"},
                           exe_dir + "/lhd.runfiles/_main/inou/yosys/lgcheck",
                           exe_dir + "/lhd.runfiles/livehd/inou/yosys/lgcheck"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  throw Lhd_error{"dependency",
                  "lgcheck (inou/yosys/lgcheck) not found",
                  "run from the LiveHD repo root or set LHD_LGCHECK=/path/to/lgcheck"};
}

// The yosys binary lgcheck shells out to. lgcheck's own fallbacks are
// cwd-relative, so pass an absolute path explicitly (lhd runs lgcheck from
// the scratch workdir to keep its trace*/log droppings out of the caller's
// cwd). Empty result -> let lgcheck try `which yosys`.
std::string locate_lgcheck_yosys() {
  if (const char* env = std::getenv("LHD_YOSYS"); env != nullptr && ::access(env, X_OK) == 0) {
    return fs::absolute(env).string();
  }
  auto exe_dir = file_utils::get_exe_path();
  for (const auto& cand : {std::string{"bazel-bin/inou/yosys/yosys2"},
                           exe_dir + "/../inou/yosys/yosys2",
                           exe_dir + "/lhd.runfiles/_main/inou/yosys/yosys2",
                           exe_dir + "/lhd.runfiles/livehd/inou/yosys/yosys2"}) {
    if (::access(cand.c_str(), X_OK) == 0) {
      return fs::absolute(cand).string();
    }
  }
  return "";
}

// Load one --impl/--ref side into `var.graphs` (no cgen). Defined below; both
// lec backends share it.
void load_side_graphs(Options& opts, Result& res, const std::string& kind, const std::string& path, std::string_view side,
                      Eprp_var& var);

// Return a verilog file for an --impl/--ref side (the lgyosys/lgcheck backend):
// a verilog side passes straight through (lgcheck reads Verilog directly);
// every other kind is loaded to graphs (load_side_graphs) and re-emitted with
// cgen into the scratch workdir.
std::string materialize_verilog(Options& opts, Result& res, const std::string& kind, const std::string& path,
                                std::string_view side) {
  if (kind == "verilog") {
    res.inputs.push_back(path);
    check_inputs_exist({path});
    return path;
  }
  Eprp_var var;
  load_side_graphs(opts, res, kind, path, side, var);  // lg/pyrope/ln -> graphs (throws if empty)
  auto          scratch = std::format("{}/check_{}", workdir(opts), side);
  auto          names   = cgen_into(opts, res, var, scratch);
  auto          out     = std::format("{}/check_{}.v", workdir(opts), side);
  std::ofstream ofs(out);
  for (const auto& n : names) {
    std::ifstream ifs(std::format("{}/{}.v", scratch, n));
    ofs << ifs.rdbuf();
  }
  return out;
}

// The yosys-slang plugin (slang.so) for lgcheck's `--gold_reader slang`: lets
// yosys read SystemVerilog packed-struct sources (CIRCT output) that
// read_verilog cannot parse. Same candidates inou_yosys_api probes.
std::string locate_yosys_slang_plugin() {
  auto exe_path = file_utils::get_exe_path();
  for (const auto& cand : {absl::StrCat(exe_path, "/../external/+_repo_rules+yosys_slang/slang.so"),
                           absl::StrCat(exe_path, "/../external/+http_archive+yosys_slang/slang.so"),
                           absl::StrCat(exe_path, "/lhd.runfiles/+http_archive+yosys_slang/slang.so")}) {
    if (::access(cand.c_str(), R_OK) == 0) {
      return cand;
    }
  }
  return "";
}

// The lgyosys backend (`--set formal.solver=lgyosys`): materialize both sides to
// Verilog and discharge with inou/yosys/lgcheck (the former `lhd check`).
// Verilog sides pass straight through; pyrope:/ln:/lg: are compiled first.
void lec_lgyosys(Options& opts, Result& res) {
  auto impl_v  = fs::absolute(materialize_verilog(opts, res, opts.impl_kind, opts.impl_path, "impl")).string();
  auto ref_v   = fs::absolute(materialize_verilog(opts, res, opts.ref_kind, opts.ref_path, "ref")).string();
  auto lgcheck = locate_lgcheck();
  auto yosys   = locate_lgcheck_yosys();

  // --set formal.lec.gold_reader=slang: read the REFERENCE side through yosys-slang
  // (SystemVerilog packed structs / '{...} patterns exceed read_verilog).
  std::string gold_reader = "verilog";
  for (const auto& [k, v] : opts.sets) {
    if (k == "formal.lec.gold_reader" && !v.empty()) {
      gold_reader = v;
    }
  }
  if (gold_reader != "verilog" && gold_reader != "slang") {
    throw Lhd_error{"usage", std::format("--set formal.lec.gold_reader expects verilog|slang, got '{}'", gold_reader), ""};
  }
  std::string slang_plugin;
  if (gold_reader == "slang") {
    slang_plugin = locate_yosys_slang_plugin();
    if (slang_plugin.empty()) {
      throw Lhd_error{"dependency",
                      "formal.lec.gold_reader=slang: yosys-slang plugin (slang.so) not found",
                      "build //inou/yosys (the @yosys_slang external) or use the default gold_reader"};
    }
  }

  // Run lgcheck FROM the scratch workdir so its cwd droppings (trace*.v,
  // lgcheck*.log) never land in the caller's directory (hermetic kernel).
  auto rundir = fs::absolute(workdir(opts)).string();
  auto cmd    = std::format("cd {} && {} --implementation {} --reference {}",
                            shell_quote(rundir),
                            shell_quote(lgcheck),
                            shell_quote(impl_v),
                            shell_quote(ref_v));
  if (!yosys.empty()) {
    cmd += std::format(" --yosys {}", shell_quote(yosys));
  }
  if (gold_reader == "slang") {
    cmd += std::format(" --gold_reader slang --slang_plugin {}", shell_quote(slang_plugin));
  }
  if (!opts.impl_top.empty()) {
    cmd += std::format(" --implementation_top {}", shell_quote(opts.impl_top));
  }
  if (!opts.ref_top.empty()) {
    cmd += std::format(" --reference_top {}", shell_quote(opts.ref_top));
  }
  if (opts.impl_top.empty() && opts.ref_top.empty() && !opts.top.empty()) {
    cmd += std::format(" --top {}", shell_quote(opts.top));
  }
  auto log  = next_log_path(opts, "lec.lgcheck");
  cmd      += std::format(" >> {} 2>&1", shell_quote(fs::absolute(log).string()));

  res.recipe_steps.emplace_back("pass.lec solver:lgyosys (lgcheck)");
  int rc   = std::system(cmd.c_str());
  int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
  if (opts.verbose) {
    mirror_log_to_stderr(log);
  }
  std::string name = !opts.impl_top.empty() ? opts.impl_top : opts.impl_path;
  // lgcheck exit codes: 0 = proven equivalent, 2 = INCONCLUSIVE (could not prove
  // AND found no counterexample — yosys' equiv flow often can't prove a
  // cgen-restructured netlist equal to its source even when it is), anything else
  // = a real refutation. Only a real refute is a hard failure.
  if (code == 2) {
    std::print("lec: '{}' INCONCLUSIVE (solver=lgyosys; could not prove, no counterexample)\n", name);
    return;
  }
  std::print("lec: '{}' {} (solver=lgyosys)\n", name, code == 0 ? "PROVEN equivalent" : "REFUTED (not equivalent)");
  if (code != 0) {
    throw Lhd_error{"equiv_fail",
                    std::format("equivalence check failed ({} vs {})", opts.impl_path, opts.ref_path),
                    std::format("see {}", log)};
  }
}

}  // namespace lhd
