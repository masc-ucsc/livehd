// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "query.hpp"

#include <cvc5/cvc5.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "encode.hpp"
#include "node_util.hpp"

namespace livehd::lec {

namespace {
// Strip the encoder's internal control prefixes so an unmatched cut point / output
// reads cleanly in a diagnostic. Next-state outputs are "\x01nxt:<hier>", other
// synthetic keys lead with a control byte; primary outputs are already clean.
std::string display_name(std::string_view name) {
  if (name.rfind("\x01nxt:", 0) == 0) {
    return "nxt:" + std::string(name.substr(5));
  }
  if (!name.empty() && static_cast<unsigned char>(name.front()) < 0x20) {
    // "\x01n:foo" / "\x01m:..." etc. -> drop the control byte and the kind tag.
    auto rest = name.substr(1);
    if (rest.size() >= 2 && rest[1] == ':') {
      return std::string(rest.substr(2));
    }
    return std::string(rest);
  }
  return std::string(name);
}

// Join a (sorted) token list, capping the count so a 1000-flop bank doesn't bury
// the message; appends "(+N more)" when truncated.
std::string join_capped(std::vector<std::string> toks, size_t cap = 24) {
  std::sort(toks.begin(), toks.end());
  std::string out;
  size_t      shown = std::min(cap, toks.size());
  for (size_t i = 0; i < shown; ++i) {
    if (!out.empty()) {
      out += ", ";
    }
    out += toks[i];
  }
  if (toks.size() > cap) {
    out += ", (+" + std::to_string(toks.size() - cap) + " more)";
  }
  return out;
}
}  // namespace

std::vector<std::pair<std::string, std::string>> parse_match_pairs(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t                                           i = 0;
  while (i < text.size()) {
    size_t end = text.find_first_of(",;\n", i);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(i, end - i);
    i                     = end + 1;
    // trim surrounding whitespace
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
      line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
      line.remove_suffix(1);
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    // split on '=' (preferred) or whitespace into exactly two names
    size_t sep = line.find('=');
    if (sep == std::string_view::npos) {
      sep = line.find_first_of(" \t");
    }
    if (sep == std::string_view::npos) {
      continue;  // a lone token is not a correspondence; ignore
    }
    auto trim = [](std::string_view s) {
      while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == '=')) {
        s.remove_prefix(1);
      }
      while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '=')) {
        s.remove_suffix(1);
      }
      return s;
    };
    std::string_view a = trim(line.substr(0, sep));
    std::string_view b = trim(line.substr(sep + 1));
    if (!a.empty() && !b.empty()) {
      out.emplace_back(std::string(a), std::string(b));
    }
  }
  return out;
}

namespace {

// ── auto portfolio (lec.engine=auto) ────────────────────────────────────────
// Race the inductive flop-cut miter and the BMC-from-reset engine, and take the
// first TRUSTWORTHY verdict. The pairing is complementary: the inductive miter
// PROVES (deep state — e.g. a register file), BMC finds shallow REAL bugs. The
// content is the verdict-trust asymmetry (see todo/livehd/lec.html):
//   inductive Proven (UNSAT + complete correspondence) -> trust as PASS;
//   BMC Refuted (reachable CEX from reset)             -> trust as FAIL + witness;
//   inductive Refuted (single-step assumes ARBITRARY equal state, so the CEX may
//        be an unreachable step-case)                  -> a HINT, never a FAIL;
//   BMC bounded-Proven (no CEX <= bound)               -> NOT a full PASS;
//   neither trustworthy                                -> inconclusive.
// cvc5 cross-instance thread-safety is unverified, so we race separate PROCESSES
// (fork two workers) — free timeout/kill, no threading landmine — and the
// per-query lec.timeout already bounds each child's checkSat so it self-limits.

void put_u32(std::string& b, uint32_t v) { b.append(reinterpret_cast<const char*>(&v), sizeof v); }
void put_str(std::string& b, std::string_view s) {
  put_u32(b, static_cast<uint32_t>(s.size()));
  b.append(s.data(), s.size());
}

// Serialize a Query_result over the worker pipe (same binary on both ends, so
// native-endian POD is fine). Layout: verdict byte, witness, detail, engine,
// elapsed_ms (i64), then the two unmatched-cut-point lists.
std::string serialize_result(const Query_result& r) {
  std::string b;
  b.push_back(static_cast<char>(static_cast<uint8_t>(r.verdict)));
  put_str(b, r.witness);
  put_str(b, r.detail);
  put_str(b, r.engine);
  int64_t ms = r.elapsed_ms;
  b.append(reinterpret_cast<const char*>(&ms), sizeof ms);
  put_u32(b, static_cast<uint32_t>(r.unmatched_ref.size()));
  for (const auto& s : r.unmatched_ref) {
    put_str(b, s);
  }
  put_u32(b, static_cast<uint32_t>(r.unmatched_impl.size()));
  for (const auto& s : r.unmatched_impl) {
    put_str(b, s);
  }
  put_u32(b, static_cast<uint32_t>(r.checked_steps));
  put_u32(b, static_cast<uint32_t>(r.output_checks));
  // Structured witness trace (the lecfail.prp reproduction sequence). Layout:
  // reset_cycles, diverge_cycle (as u32; -1 -> 0xffffffff), diverge_outputs list,
  // then the cycles: count, and per cycle {reset_asserted byte, inputs {count,
  // per input name+value+width}}.
  put_u32(b, static_cast<uint32_t>(r.trace.reset_cycles));
  put_u32(b, static_cast<uint32_t>(r.trace.diverge_cycle));
  put_u32(b, static_cast<uint32_t>(r.trace.diverge_outputs.size()));
  for (const auto& s : r.trace.diverge_outputs) {
    put_str(b, s);
  }
  put_u32(b, static_cast<uint32_t>(r.trace.cycles.size()));
  for (const auto& c : r.trace.cycles) {
    b.push_back(static_cast<char>(c.reset_asserted ? 1 : 0));
    put_u32(b, static_cast<uint32_t>(c.inputs.size()));
    for (const auto& in : c.inputs) {
      put_str(b, in.name);
      put_str(b, in.value);
      put_u32(b, static_cast<uint32_t>(in.width));
    }
  }
  put_str(b, r.split_used);  // strategy-hint selector (same binary both ends)
  return b;
}

bool get_u32(std::string_view& b, uint32_t& v) {
  if (b.size() < sizeof v) {
    return false;
  }
  std::memcpy(&v, b.data(), sizeof v);
  b.remove_prefix(sizeof v);
  return true;
}
bool get_str(std::string_view& b, std::string& s) {
  uint32_t n = 0;
  if (!get_u32(b, n) || b.size() < n) {
    return false;
  }
  s.assign(b.data(), n);
  b.remove_prefix(n);
  return true;
}
bool deserialize_result(std::string_view b, Query_result& r) {
  if (b.empty()) {
    return false;
  }
  auto v = static_cast<uint8_t>(b.front());
  b.remove_prefix(1);
  if (v > static_cast<uint8_t>(Verdict::Unknown)) {
    return false;
  }
  r.verdict = static_cast<Verdict>(v);
  if (!get_str(b, r.witness) || !get_str(b, r.detail) || !get_str(b, r.engine)) {
    return false;
  }
  int64_t ms = 0;
  if (b.size() < sizeof ms) {
    return false;
  }
  std::memcpy(&ms, b.data(), sizeof ms);
  b.remove_prefix(sizeof ms);
  r.elapsed_ms = ms;
  uint32_t n = 0;
  if (!get_u32(b, n)) {
    return false;
  }
  r.unmatched_ref.clear();
  for (uint32_t i = 0; i < n; ++i) {
    std::string s;
    if (!get_str(b, s)) {
      return false;
    }
    r.unmatched_ref.push_back(std::move(s));
  }
  if (!get_u32(b, n)) {
    return false;
  }
  r.unmatched_impl.clear();
  for (uint32_t i = 0; i < n; ++i) {
    std::string s;
    if (!get_str(b, s)) {
      return false;
    }
    r.unmatched_impl.push_back(std::move(s));
  }
  uint32_t cs = 0, oc = 0;
  if (get_u32(b, cs)) {
    r.checked_steps = static_cast<int>(cs);
  }
  if (get_u32(b, oc)) {
    r.output_checks = static_cast<int>(oc);
  }
  // Structured witness trace (mirror serialize_result). Best-effort: an older /
  // truncated blob simply leaves the trace empty (the fields are optional).
  uint32_t rc = 0, dc = 0, dn = 0;
  if (!get_u32(b, rc) || !get_u32(b, dc) || !get_u32(b, dn)) {
    return true;
  }
  r.trace.reset_cycles  = static_cast<int>(rc);
  r.trace.diverge_cycle = static_cast<int>(dc);  // 0xffffffff -> -1 round-trips
  r.trace.diverge_outputs.clear();
  for (uint32_t i = 0; i < dn; ++i) {
    std::string s;
    if (!get_str(b, s)) {
      return true;
    }
    r.trace.diverge_outputs.push_back(std::move(s));
  }
  uint32_t nc = 0;
  if (!get_u32(b, nc)) {
    return true;
  }
  r.trace.cycles.clear();
  for (uint32_t i = 0; i < nc; ++i) {
    if (b.empty()) {
      return true;
    }
    Witness_cycle cyc;
    cyc.reset_asserted = b.front() != 0;
    b.remove_prefix(1);
    uint32_t ni = 0;
    if (!get_u32(b, ni)) {
      return true;
    }
    for (uint32_t j = 0; j < ni; ++j) {
      Witness_in in;
      uint32_t   w = 0;
      if (!get_str(b, in.name) || !get_str(b, in.value) || !get_u32(b, w)) {
        return true;
      }
      in.width = static_cast<int>(w);
      cyc.inputs.push_back(std::move(in));
    }
    r.trace.cycles.push_back(std::move(cyc));
  }
  (void)get_str(b, r.split_used);  // best-effort tail field (mirror serialize)
  return true;
}

void write_all(int fd, const char* p, size_t n) {
  while (n > 0) {
    ssize_t w = ::write(fd, p, n);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
}

long long now_ms(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

const char* vname(Verdict v) { return v == Verdict::Proven ? "Proven" : (v == Verdict::Refuted ? "Refuted" : "Unknown"); }

// Build the inconclusive verdict from two completed engine results. Neither was
// trustworthy here (no BMC-Refuted, no inductive-Proven). The inductive CEX (if
// any) is surfaced as a HINT in `detail` only — NOT in `witness` — so the CLI's
// inconclusive policy (loud warning + clean exit, unless lec.strict) applies
// rather than the hard-fail-on-witness path: a single-step CEX may be unreachable.
Query_result make_inconclusive(const Query_result& ind, const Query_result& bmc, const Lec_options& opts, long long total_ms) {
  Query_result r;
  r.verdict    = Verdict::Unknown;
  r.engine     = "auto(ind|bmc)";
  r.elapsed_ms = total_ms;
  // A sub-millisecond-to-low-ms Unknown is almost never the solver genuinely
  // giving up — it's usually a structural encode failure (unsupported op, an
  // unresolved combinational-cycle-looking operand) or a now-caught engine
  // exception (see safe_prove_equal). Surface that reason instead of just the
  // timing, or a fast inconclusive looks identical to (and is easily mistaken
  // for) a hard query the solver actually spent its budget on.
  auto reason = [](const Query_result& e) -> std::string {
    if (e.detail.find("encode failed") != std::string::npos || e.detail.find("ENGINE CRASH") != std::string::npos) {
      return " [" + e.detail + "]";
    }
    return "";
  };
  std::string d
      = "auto INCONCLUSIVE: ind=" + std::string(vname(ind.verdict)) + "(" + std::to_string(ind.elapsed_ms) + "ms)" + reason(ind);
  if (ind.verdict == Verdict::Refuted) {
    d += " [single-step CEX — may be an UNREACHABLE step-case, treated as a HINT not a failure: "
         + (ind.witness.empty() ? std::string("(no witness)") : ind.witness) + "]";
  }
  d += ", bmc=" + std::string(vname(bmc.verdict)) + "(" + std::to_string(bmc.elapsed_ms) + "ms)" + reason(bmc);
  if (bmc.verdict == Verdict::Proven) {
    d += " [no CEX up to bound " + std::to_string(opts.bound) + " — bounded, NOT a full proof]";
  }
  r.detail = d;
  // Propagate the best-available correspondence info for iteration (not a witness).
  const Query_result& src = (!ind.unmatched_ref.empty() || !ind.unmatched_impl.empty()) ? ind : bmc;
  r.unmatched_ref         = src.unmatched_ref;
  r.unmatched_impl        = src.unmatched_impl;
  return r;
}

// Bounded-BMC PASS policy: a BMC that found no CEX up to the bound AND actually
// compared outputs is a BOUNDED proof — `auto` reports it as PROVEN (clearly
// labelled bounded), not inconclusive. Deeper-than-bound cycles are out of scope
// by design (see todo/livehd/2d-cex_debug, lec). An inductive FULL proof, if found,
// already won earlier in the race; this is the fallback when ind is Unknown.
bool try_bounded_proven(const Query_result& bmc, Query_result& out) {
  if (bmc.verdict != Verdict::Proven || bmc.output_checks <= 0) {
    return false;  // Unknown, or vacuous (no outputs compared) -> not a PASS
  }
  out        = bmc;
  out.engine = "bmc";
  out.detail = "auto: bmc BOUNDED-Proven (no CEX up to bound " + std::to_string(bmc.checked_steps) + ", "
             + std::to_string(bmc.output_checks) + " output checks; PASS by bounded-proof policy — "
               "cycles beyond the bound are unproven); "
             + bmc.detail;
  return true;
}

// Run one engine, converting any escaping C++ exception (e.g. a cvc5 API
// exception from a malformed term — see the seed_state width-fit fix above)
// into a clearly-tagged Unknown result instead of letting it propagate and
// kill the (forked-child or sequential-fallback) process. The tag lets a human
// or the verification harness distinguish "engine genuinely couldn't decide"
// from "the encoder crashed" even though both currently surface as Unknown.
Query_result safe_prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                              const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  try {
    return prove_equal(ref, impl, opts, sub_lib);
  } catch (const std::exception& e) {
    Query_result r;
    r.verdict = Verdict::Unknown;
    r.detail  = "ENGINE CRASH (" + opts.engine + "): " + e.what();
    return r;
  } catch (...) {
    Query_result r;
    r.verdict = Verdict::Unknown;
    r.detail  = "ENGINE CRASH (" + opts.engine + "): unknown C++ exception";
    return r;
  }
}

// Sequential fallback when fork/pipe is unavailable: run ind, then bmc, applying
// the same trust asymmetry. Used only on a (rare) fork failure.
Query_result run_auto_sequential(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                                 const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  auto         t0 = std::chrono::steady_clock::now();
  Lec_options  oi = opts;
  oi.engine       = "ind";
  auto         ti = std::chrono::steady_clock::now();
  Query_result ri = safe_prove_equal(ref, impl, oi, sub_lib);
  ri.engine       = "ind";
  ri.elapsed_ms   = now_ms(ti);
  if (ri.verdict == Verdict::Proven) {
    ri.detail = "auto(seq): ind Proven (k=1 induction); " + ri.detail;
    return ri;
  }
  Lec_options ob = opts;
  ob.engine      = "bmc";
  auto         tb = std::chrono::steady_clock::now();
  Query_result rb = safe_prove_equal(ref, impl, ob, sub_lib);
  rb.engine       = "bmc";
  rb.elapsed_ms   = now_ms(tb);
  if (rb.verdict == Verdict::Refuted) {
    rb.detail = "auto(seq): bmc Refuted (reachable CEX); " + rb.detail;
    return rb;
  }
  Query_result bp;
  if (try_bounded_proven(rb, bp)) {
    bp.elapsed_ms = now_ms(t0);
    return bp;
  }
  return make_inconclusive(ri, rb, opts, now_ms(t0));
}

// A LEC pair is "combinational" when neither design — nor any descended sub-body
// or sub_lib-resolved def — holds a state cell (Flop/Fflop/Latch/Memory). For
// such a pair BMC has nothing to unroll (its N-cycle unroll would just re-bit-
// blast the same combinational miter N times) and the inductive miter degenerates
// to plain combinational equivalence, which is COMPLETE (no unreachable-state
// spurious CEX, since there is no state). So the `auto` portfolio can skip the
// bmc racer — and the fork — entirely. Conservative: a Sub we cannot resolve to a
// body (a true blackbox that could hide state) forces the full portfolio.
bool graph_is_combinational(hhds::Graph* g, const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib,
                            absl::flat_hash_set<hhds::Graph*>& seen) {
  if (g == nullptr || !seen.insert(g).second) {
    return true;  // null / already-verified body (a real hw hierarchy is a finite DAG)
  }
  for (auto node : g->forward_class()) {  // single level; Sub bodies are descended explicitly below
    auto op = graph_util::type_op_of(node);
    if (op == Ntype_op::Flop || op == Ntype_op::Fflop || op == Ntype_op::Latch || op == Ntype_op::Memory) {
      return false;
    }
    if (op != Ntype_op::Sub) {
      continue;
    }
    auto         sg   = node.get_subnode_graph();      // shared_ptr; keep alive across the recursion
    hhds::Graph* body = sg ? sg.get() : nullptr;       // inlined body, if any
    if (body == nullptr && sub_lib != nullptr) {       // else the sub_lib-resolved def
      if (auto it = sub_lib->find(node.get_subnode_gid()); it != sub_lib->end()) {
        body = it->second;
      }
    }
    if (body == nullptr || !graph_is_combinational(body, sub_lib, seen)) {
      return false;  // blackbox we cannot prove combinational -> keep the full portfolio
    }
  }
  return true;
}

// ── lec.split=auto: pick a control input to case-split on ────────────────────
// The hardness of a combinational miter is dominated by WIDE operators whose
// control operand is input-VARIABLE (a variable barrel shift's amount, a mux
// selector). Fixing that control input to a constant collapses the operator
// (cvc5 folds a constant-amount shift to a static slice). So the best split
// signal is the small-width primary input that (transitively) feeds the widest
// such control pins — a structural backdoor. Returns {name,width}; name empty if
// no enumerable candidate (caller falls back to a single monolithic ind query).
struct Split_pick {
  std::string name;
  int         width = 0;
};

Split_pick pick_split_signal(hhds::Graph* g, const std::string& requested, int enum_cap_bits) {
  auto                                  gio = g->get_io();
  absl::flat_hash_map<std::string, int> in_w;  // enumerable primary inputs -> width
  for (const auto& d : gio->get_input_pin_decls()) {
    int w = real_width_io(g->get_input_pin(d.name), *gio, d.name);
    if (w >= 1 && w <= enum_cap_bits) {
      in_w[std::string(d.name)] = w;
    }
  }
  if (in_w.empty()) {
    return {};
  }
  // Explicit request: honor a named input if it is an enumerable primary input.
  if (!requested.empty() && requested != "auto" && requested != "none") {
    if (auto it = in_w.find(requested); it != in_w.end()) {
      return {it->first, it->second};
    }
    return {};
  }
  // auto: score each candidate by the weighted control fan-in it backs. Input
  // support is computed by a FORWARD pass over forward_class (topological order):
  // each node's cone-mask = OR of its input drivers' cones, seeding enumerable
  // graph-input drivers with their own candidate bit. This single topological
  // sweep is O(N): each node's cone is memoized in `cone` by nid and a driver's
  // source node is looked up there, never re-walked. (Historically this shape was
  // also FORCED because inp_edges() on a get_master_node()-derived node returned
  // an empty range; hhds fixed that — see graph_test test_get_master_node_edges —
  // so a backward walk would now be correct, just less efficient than this pass.)
  const bool                                  dbg = std::getenv("LEC_SPLIT_LOG") != nullptr;
  absl::flat_hash_map<std::string, long long> score;
  std::vector<std::string>                    cand;      // candidate index (bitmask, cap 64)
  absl::flat_hash_map<std::string, int>       cand_idx;
  for (const auto& [nm, w] : in_w) {
    if (cand.size() < 64) {
      cand_idx[nm] = static_cast<int>(cand.size());
      cand.push_back(nm);
    }
  }
  // Classify a driver: >=0 enumerable-candidate bit; -1 wide/other input (a
  // leaf, no cone); -2 internal node output (recurse). pin_name_of resolves a
  // graph-input driver's port name directly (via the graph's IO maps).
  auto classify = [&](const hhds::Pin& dp) -> int {
    if (!graph_util::is_graph_input_pin(dp)) {
      return -2;
    }
    auto it = cand_idx.find(graph_util::pin_name_of(dp));
    return it == cand_idx.end() ? -1 : it->second;
  };
  auto cone_of = [&](const absl::flat_hash_map<uint64_t, uint64_t>& cone, const hhds::Pin& dp) -> uint64_t {
    int b = classify(dp);
    if (b >= 0) {
      return uint64_t{1} << b;
    }
    if (b == -2) {
      if (auto it = cone.find(static_cast<uint64_t>(dp.get_master_node().get_debug_nid())); it != cone.end()) {
        return it->second;
      }
    }
    return 0;
  };
  absl::flat_hash_map<uint64_t, uint64_t> cone;  // node nid -> candidate bitmask of its input cone
  long long                               dbg_nodes = 0, dbg_sra = 0, dbg_shl = 0, dbg_mux = 0, dbg_varctrl = 0;
  auto                                    score_ctrl = [&](const hhds::Pin& ctrl, long long weight) {
    if (ctrl.is_invalid() || graph_util::is_const_pin(ctrl)) {
      return;
    }
    ++dbg_varctrl;
    uint64_t cm = cone_of(cone, ctrl);
    for (int i = 0; i < static_cast<int>(cand.size()); ++i) {
      if (cm & (uint64_t{1} << i)) {
        score[cand[i]] += weight;
      }
    }
  };
  for (auto node : g->forward_class()) {
    ++dbg_nodes;
    uint64_t m = 0;
    for (auto e : node.inp_edges()) {
      m |= cone_of(cone, e.driver);
    }
    cone[static_cast<uint64_t>(node.get_debug_nid())] = m;
    // The control driver's source node precedes `node` in topo order, so its cone
    // is already in the map — score it now.
    auto op = graph_util::type_op_of(node);
    if (op == Ntype_op::SRA || op == Ntype_op::SHL) {
      (op == Ntype_op::SRA) ? ++dbg_sra : ++dbg_shl;
      score_ctrl(graph_util::get_driver_of_sink_name(node, "b"), std::max(1, real_width(node.get_driver_pin(0))));
    } else if (op == Ntype_op::Mux || op == Ntype_op::Hotmux) {
      ++dbg_mux;
      score_ctrl(graph_util::get_driver_of_sink_name(node, "s"), std::max(1, real_width(node.get_driver_pin(0))) / 2 + 1);
    }
  }
  if (dbg) {
    std::fprintf(stderr,
                 "[SPLIT] census nodes=%lld sra=%lld shl=%lld mux/hotmux=%lld var-ctrl=%lld ; scores:",
                 dbg_nodes,
                 dbg_sra,
                 dbg_shl,
                 dbg_mux,
                 dbg_varctrl);
    for (const auto& [n, s] : score) {
      std::fprintf(stderr, " %s=%lld", n.c_str(), static_cast<long long>(s));
    }
    std::fprintf(stderr, "\n");
  }
  // Deterministic argmax: highest score, tie -> smaller width -> name.
  std::vector<std::string> cands;
  cands.reserve(score.size());
  for (const auto& [nm, sc] : score) {
    (void)sc;
    cands.push_back(nm);
  }
  std::sort(cands.begin(), cands.end());
  std::string best;
  long long   best_sc = 0;
  int         best_w  = 0;
  for (const auto& nm : cands) {
    long long sc = score[nm];
    int       w  = in_w[nm];
    if (sc > best_sc || (sc == best_sc && !best.empty() && w < best_w)) {
      best    = nm;
      best_sc = sc;
      best_w  = w;
    }
  }
  if (best.empty()) {
    return {};
  }
  return {best, best_w};
}

// ── lec.partitions: parallel input-space case-split ──────────────────────────
// Fork up to `opts.partitions` workers; each proves the miter UNSAT over a
// disjoint slice of the split signal's values (every value fully cofactored, so
// each cube is trivial). First worker to REFUTE wins (kill the rest); all workers
// Proven => Proven. Returns a sentinel (engine=="") when there is no good split,
// so the caller falls back to the single monolithic ind query. Combinational only
// (the caller gates on graph_is_combinational): a SAT cube is a genuine CEX.
Query_result run_case_split(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                            const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  constexpr int kEnumCapBits = 8;  // enumerate at most 2^8 = 256 selector values
  Query_result  none;
  none.engine  = "";  // sentinel: caller falls back
  none.verdict = Verdict::Unknown;

  Split_pick pick = pick_split_signal(ref, opts.split, kEnumCapBits);
  if (pick.name.empty() || pick.width < 1 || pick.width > kEnumCapBits) {
    return none;
  }
  const uint64_t nvals    = uint64_t{1} << pick.width;
  const int      nworkers = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(std::max(2, opts.partitions)), nvals));
  auto           t0       = std::chrono::steady_clock::now();

  // Round-robin the selector values across workers (load balance).
  std::vector<std::vector<uint64_t>> slices(nworkers);
  for (uint64_t v = 0; v < nvals; ++v) {
    slices[v % nworkers].push_back(v);
  }

  // Per-cube solve cap: a cube whose selector value folds the datapath is trivial
  // (ms), so a short cap costs nothing on a GOOD pick but makes a mis-pick (a
  // selector that does not fold the hard operator) fail fast -> worker Unknown ->
  // monolithic fallback, instead of grinding the full lec.timeout per cube.
  const int kCubeTimeout = (opts.timeout > 0 && opts.timeout < 30) ? opts.timeout : 30;

  auto run_seq = [&]() -> Query_result {  // fork-failure fallback: one in-process sweep
    Lec_options o   = opts;
    o.engine        = "ind";
    o.timeout       = kCubeTimeout;
    o._split_name   = pick.name;
    o._split_values.clear();
    for (uint64_t v = 0; v < nvals; ++v) {
      o._split_values.push_back(v);
    }
    Query_result r = safe_prove_equal(ref, impl, o, sub_lib);
    r.engine       = "casesplit";
    r.split_used   = pick.name;
    r.elapsed_ms   = now_ms(t0);
    r.detail       = "auto: case-split " + pick.name + "[" + std::to_string(pick.width) + "b] " + std::to_string(nvals)
             + " cubes (sequential fallback); " + r.detail;
    return r;
  };

  std::vector<int>   rfd(nworkers, -1), wfd(nworkers, -1);
  std::vector<pid_t> pid(nworkers, -1);
  bool               fork_ok = true;
  for (int i = 0; i < nworkers; ++i) {
    int p[2];
    if (::pipe(p) != 0) {
      fork_ok = false;
      break;
    }
    rfd[i]  = p[0];
    wfd[i]  = p[1];
    pid_t c = ::fork();
    if (c < 0) {
      fork_ok = false;
      break;
    }
    if (c == 0) {
      // child i: keep only its own write fd, run its slice, serialize, _exit.
      for (int j = 0; j < nworkers; ++j) {
        if (rfd[j] >= 0) {
          ::close(rfd[j]);
        }
        if (j != i && wfd[j] >= 0) {
          ::close(wfd[j]);
        }
      }
      Lec_options o    = opts;
      o.engine         = "ind";
      o.timeout        = kCubeTimeout;
      o._split_name    = pick.name;
      o._split_values  = slices[i];
      Query_result r   = safe_prove_equal(ref, impl, o, sub_lib);
      std::string  blob = serialize_result(r);
      write_all(wfd[i], blob.data(), blob.size());
      ::close(wfd[i]);
      ::_exit(0);
    }
    pid[i] = c;
    ::close(wfd[i]);  // parent never writes
    wfd[i] = -1;
  }

  if (!fork_ok) {
    for (int j = 0; j < nworkers; ++j) {
      if (rfd[j] >= 0) {
        ::close(rfd[j]);
      }
      if (wfd[j] >= 0) {
        ::close(wfd[j]);
      }
      if (pid[j] > 0) {
        ::kill(pid[j], SIGKILL);
        int st = 0;
        ::waitpid(pid[j], &st, 0);
      }
    }
    return run_seq();
  }

  // Parent: poll all children; the first REFUTED wins (kill the rest).
  std::vector<std::string>  buf(nworkers);
  std::vector<bool>         done(nworkers, false);
  std::vector<Query_result> got(nworkers);
  int                       remaining = nworkers;
  int                       refuter   = -1;
  while (remaining > 0 && refuter < 0) {
    std::vector<struct pollfd> pfds;
    std::vector<int>           map;
    for (int i = 0; i < nworkers; ++i) {
      if (!done[i]) {
        struct pollfd pf;
        pf.fd      = rfd[i];
        pf.events  = POLLIN;
        pf.revents = 0;
        pfds.push_back(pf);
        map.push_back(i);
      }
    }
    int pr = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (size_t k = 0; k < pfds.size() && refuter < 0; ++k) {
      if (pfds[k].revents == 0) {
        continue;
      }
      int     i = map[k];
      char    tmp[8192];
      ssize_t n = ::read(rfd[i], tmp, sizeof tmp);
      if (n > 0) {
        buf[i].append(tmp, static_cast<size_t>(n));
        continue;
      }
      done[i] = true;
      --remaining;
      if (!deserialize_result(buf[i], got[i])) {
        got[i]         = Query_result{};
        got[i].verdict = Verdict::Unknown;
      }
      if (got[i].verdict == Verdict::Refuted) {
        refuter = i;
      }
    }
  }
  for (int i = 0; i < nworkers; ++i) {
    if (!done[i] && pid[i] > 0) {
      ::kill(pid[i], SIGKILL);
    }
    if (rfd[i] >= 0) {
      ::close(rfd[i]);
    }
  }
  for (int i = 0; i < nworkers; ++i) {
    if (pid[i] > 0) {
      int st = 0;
      ::waitpid(pid[i], &st, 0);
    }
  }

  std::string tag = pick.name + "[" + std::to_string(pick.width) + "b] " + std::to_string(nworkers) + " workers x "
                  + std::to_string(nvals) + " cubes";
  if (refuter >= 0) {
    Query_result out = got[refuter];
    out.engine       = "casesplit";
    out.split_used   = pick.name;
    out.elapsed_ms   = now_ms(t0);
    out.detail       = "auto: case-split " + tag + " REFUTED; " + out.detail;
    return out;
  }
  int proven = 0, unknown = 0;
  for (int i = 0; i < nworkers; ++i) {
    if (done[i] && got[i].verdict == Verdict::Proven) {
      ++proven;
    } else {
      ++unknown;
    }
  }
  Query_result out;
  out.engine     = "casesplit";
  out.split_used = pick.name;
  out.elapsed_ms = now_ms(t0);
  if (unknown == 0) {
    out.verdict = Verdict::Proven;
    out.detail  = "auto: case-split " + tag + " PROVEN (every cube UNSAT)";
  } else {
    out.verdict = Verdict::Unknown;  // inconclusive: caller falls back to monolithic ind
    out.detail  = "auto: case-split " + tag + " inconclusive (" + std::to_string(proven) + " workers proven, "
               + std::to_string(unknown) + " not)";
  }
  return out;
}

Query_result run_auto_portfolio(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                                const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  if (opts._isolated_worker) {
    // The hierarchy Taskflow already supplies parallelism and process
    // isolation. Run the method ladder in this one child to prevent nested
    // auto/case-split forks from exceeding formal.jobs.
    Lec_options oi                       = opts;
    oi.engine                            = "ind";
    oi.partitions                        = 1;
    auto                              ri = safe_prove_equal(ref, impl, oi, sub_lib);
    absl::flat_hash_set<hhds::Graph*> seen;
    const bool combinational = graph_is_combinational(ref, sub_lib, seen) && graph_is_combinational(impl, sub_lib, seen);
    if (ri.verdict == Verdict::Proven || (combinational && ri.verdict == Verdict::Refuted)) {
      ri.engine = "ind";
      return ri;
    }
    Lec_options ob = opts;
    ob.engine      = "bmc";
    ob.partitions  = 1;
    auto rb        = safe_prove_equal(ref, impl, ob, sub_lib);
    if (rb.verdict == Verdict::Refuted) {
      rb.engine = "bmc";
      return rb;
    }
    Query_result bp;
    if (try_bounded_proven(rb, bp)) {
      bp.elapsed_ms = ri.elapsed_ms + rb.elapsed_ms;
      return bp;
    }
    return make_inconclusive(ri, rb, opts, ri.elapsed_ms + rb.elapsed_ms);
  }
  // Purely combinational pair: skip the bmc racer (and the fork). Run one ind
  // query and return its verdict directly — for a stateless design the inductive
  // miter is the combinational miter, so even a Refuted here is a genuine CEX.
  {
    absl::flat_hash_set<hhds::Graph*> seen;
    if (graph_is_combinational(ref, sub_lib, seen) && graph_is_combinational(impl, sub_lib, seen)) {
      // Parallel input-space case-split accelerator (lec.partitions): split the
      // hard combinational miter into per-selector cubes solved across workers.
      // Only trust a DECISIVE case-split verdict; on no-split/inconclusive fall
      // back to the single monolithic ind query below (never a regression).
      if (opts.partitions >= 2 && opts.split != "none" && !opts.split.empty()) {
        Query_result cs = run_case_split(ref, impl, opts, sub_lib);
        if (cs.engine == "casesplit" && (cs.verdict == Verdict::Proven || cs.verdict == Verdict::Refuted)) {
          return cs;
        }
      }
      Lec_options  o  = opts;
      o.engine        = "ind";
      auto         tc = std::chrono::steady_clock::now();
      Query_result r  = safe_prove_equal(ref, impl, o, sub_lib);
      r.engine        = "ind";
      r.elapsed_ms    = now_ms(tc);
      r.detail        = "auto: combinational (no flop/latch/mem) -> single ind query, bmc skipped; " + r.detail;
      return r;
    }
  }
  const char* const engines[2] = {"ind", "bmc"};  // index 0 = inductive, 1 = bmc
  int               p0[2]       = {-1, -1};
  int               p1[2]       = {-1, -1};
  if (::pipe(p0) != 0 || ::pipe(p1) != 0) {
    if (p0[0] >= 0) {
      ::close(p0[0]);
      ::close(p0[1]);
    }
    return run_auto_sequential(ref, impl, opts, sub_lib);
  }
  int   rfd[2] = {p0[0], p1[0]};
  int   wfd[2] = {p0[1], p1[1]};
  pid_t pid[2] = {-1, -1};

  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 2; ++i) {
    pid_t c = ::fork();
    if (c < 0) {
      // fork failed: clean up everything spawned so far and fall back.
      for (int j = 0; j < 2; ++j) {
        if (rfd[j] >= 0) {
          ::close(rfd[j]);
        }
        if (wfd[j] >= 0) {
          ::close(wfd[j]);
        }
      }
      for (int j = 0; j < i; ++j) {
        if (pid[j] > 0) {
          ::kill(pid[j], SIGKILL);
          int st = 0;
          ::waitpid(pid[j], &st, 0);
        }
      }
      return run_auto_sequential(ref, impl, opts, sub_lib);
    }
    if (c == 0) {
      // ── child i: run exactly one engine, serialize, _exit (no atexit/dtors).
      ::close(rfd[0]);
      ::close(rfd[1]);
      ::close(wfd[1 - i]);
      Lec_options  o = opts;
      o.engine       = engines[i];
      auto         ci = std::chrono::steady_clock::now();
      Query_result r  = safe_prove_equal(ref, impl, o, sub_lib);
      r.engine        = engines[i];
      r.elapsed_ms    = now_ms(ci);
      std::string blob = serialize_result(r);
      write_all(wfd[i], blob.data(), blob.size());
      ::close(wfd[i]);
      ::_exit(0);
    }
    pid[i] = c;
  }
  // ── parent: poll both pipes; first TRUSTWORTHY verdict wins, kill the loser.
  ::close(wfd[0]);
  ::close(wfd[1]);
  std::string  buf[2];
  bool         done[2] = {false, false};
  Query_result got[2];
  int          remaining = 2;
  int          winner    = -1;
  while (remaining > 0) {
    struct pollfd pfds[2];
    int           map[2];
    int           nf = 0;
    for (int i = 0; i < 2; ++i) {
      if (!done[i]) {
        pfds[nf].fd     = rfd[i];
        pfds[nf].events = POLLIN;
        map[nf]         = i;
        ++nf;
      }
    }
    int pr = ::poll(pfds, static_cast<nfds_t>(nf), -1);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (int k = 0; k < nf && winner < 0; ++k) {
      if (pfds[k].revents == 0) {
        continue;
      }
      int  i = map[k];
      char tmp[8192];
      ssize_t n = ::read(rfd[i], tmp, sizeof tmp);
      if (n > 0) {
        buf[i].append(tmp, static_cast<size_t>(n));
        continue;  // worker may still be writing; EOF (n==0) marks done
      }
      done[i] = true;
      --remaining;
      if (!deserialize_result(buf[i], got[i])) {
        got[i]         = Query_result{};
        got[i].verdict = Verdict::Unknown;
        got[i].engine  = engines[i];
        got[i].detail  = std::string(engines[i]) + " worker terminated without a result";
      }
      const bool is_ind      = i == 0;
      const bool trustworthy = (is_ind && got[i].verdict == Verdict::Proven) || (!is_ind && got[i].verdict == Verdict::Refuted);
      if (trustworthy) {
        winner    = i;
        remaining = 0;
      }
    }
  }
  // Reap: kill any worker still running (the loser), wait on both (no zombies).
  for (int i = 0; i < 2; ++i) {
    if (!done[i] && pid[i] > 0) {
      ::kill(pid[i], SIGKILL);
    }
    if (rfd[i] >= 0) {
      ::close(rfd[i]);
    }
  }
  for (int i = 0; i < 2; ++i) {
    if (pid[i] > 0) {
      int st = 0;
      ::waitpid(pid[i], &st, 0);
    }
  }

  if (winner >= 0) {
    Query_result r = got[winner];
    r.engine       = engines[winner];
    r.detail       = "auto: " + std::string(engines[winner]) + " reached " + vname(r.verdict) + " first in "
             + std::to_string(r.elapsed_ms) + "ms (raced ind|bmc); " + r.detail;
    return r;
  }
  Query_result bp;
  if (try_bounded_proven(got[1], bp)) {
    bp.elapsed_ms = now_ms(t0);
    return bp;
  }
  return make_inconclusive(got[0], got[1], opts, now_ms(t0));
}

}  // namespace

Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                         const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  Query_result res;
  res.detail = "solver=" + opts.solver + " (cvc5 direct, flop-cut inductive miter)";
  res.engine = opts.engine;  // the auto portfolio overrides this with the winning engine

  if (opts.solver != "cvc5") {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; solver backend '" + opts.solver + "' not built (cvc5 only)";
    return res;
  }

  // `auto` = parallel portfolio: race the inductive miter and BMC as two forked
  // workers and take the first trustworthy verdict (inductive-Proven => PASS,
  // BMC-Refuted => FAIL; everything else is a hint / bounded / inconclusive).
  // Must short-circuit BEFORE any cvc5 TermManager exists in this process, so the
  // children fork from a clean state and each build their own solver.
  if (opts.engine == "auto") {
    return run_auto_portfolio(ref, impl, opts, sub_lib);
  }

  // `full` = require equivalence in BOTH the during-reset (just_reset) and the
  // free-running (after_reset) windows. Run them as two self-contained sub-queries
  // (each builds its own cvc5 solver, so they are independent) and AND the
  // verdicts. Sequential + short-circuit: a bring-up bug usually trips just_reset
  // first, and skipping the after_reset solve once it has already failed is faster
  // than running both. They could run on two threads (hhds reads are thread-safe),
  // but cvc5's cross-instance thread-safety is unverified, so we keep it serial.
  if (opts.engine == "bmc" && opts.phase == "full") {
    Lec_options  o1 = opts;
    o1.phase        = "just_reset";
    Query_result r1 = prove_equal(ref, impl, o1, sub_lib);
    if (r1.verdict != Verdict::Proven) {
      r1.detail = "full / just_reset stage: " + r1.detail;
      return r1;
    }
    Lec_options  o2 = opts;
    o2.phase        = "after_reset";
    Query_result r2 = prove_equal(ref, impl, o2, sub_lib);
    r2.detail       = (r2.verdict == Verdict::Proven ? "full: equivalent in BOTH just_reset and after_reset; after_reset stage: "
                           : "full / after_reset stage (just_reset already PROVEN): ")
                + r2.detail;
    return r2;
  }

  // Explicit state-correspondence (lec.match): canon(impl_name) -> canon(ref_name),
  // so a flop the user paired with a differently-named flop on the other design
  // collapses onto one shared cut key (the ref-side canon). `eff(hier)` is the
  // canonical key used everywhere a flop is keyed (shared symbols + encoder).
  Io_name_map<std::string> name_alias;
  for (const auto& [ref_name, impl_name] : opts.match) {
    std::string rc = canon_flop_name(ref_name);
    std::string ic = canon_flop_name(impl_name);
    if (!ic.empty() && ic != rc) {
      name_alias[ic] = rc;
    }
  }
  auto eff = [&](std::string_view hier) -> std::string {
    std::string c = canon_flop_name(hier);
    if (auto it = name_alias.find(c); it != name_alias.end()) {
      return it->second;
    }
    return c;
  };

  // Def-name canonicalization across front-ends. A def's FULL callee name
  // embeds its front-end namespace (Pyrope "file.entity" vs slang's flat
  // "entity"), so the SAME module never carries the same full name on a
  // cross-front-end pair. Every cross-design def match below (the collapse
  // set, the box-correspondence def key) therefore works on the ENTITY (the
  // post-'.' tail) — but only when that entity names exactly ONE def within
  // its design: two same-entity defs from different files would alias, and a
  // mispaired box is a false-PROVEN hazard. An ambiguous entity keeps the
  // full name; its pairing degrades to one-sided obligations, which the
  // miters already gate to inconclusive (never a Proven/Refuted).
  auto entity_of = [](std::string_view n) -> std::string {
    auto d = n.rfind('.');
    return std::string(d == std::string_view::npos ? n : n.substr(d + 1));
  };
  // Per design: entity -> the unique full callee name, or "" when ambiguous.
  absl::flat_hash_map<std::string, std::string> ref_ent_uniq, impl_ent_uniq;
  for (auto* g : {ref, impl}) {
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> ents;
    for (auto node : g->forward_hier()) {
      if (graph_util::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (sio == nullptr) {
        continue;
      }
      std::string full(sio->get_name());
      ents[entity_of(full)].insert(std::move(full));
    }
    auto& uniq = g == ref ? ref_ent_uniq : impl_ent_uniq;
    for (auto& [e, fulls] : ents) {
      uniq[e] = fulls.size() == 1 ? *fulls.begin() : std::string{};
    }
  }
  auto canon_def = [&](bool in_ref, std::string_view full) -> std::string {
    const auto& uniq = in_ref ? ref_ent_uniq : impl_ent_uniq;
    std::string e    = entity_of(full);
    if (auto it = uniq.find(e); it != uniq.end() && it->second == full) {
      return e;  // unique within this design -> the entity is the cross-design key
    }
    return std::string(full);
  };

  // Proven-module collapse set (lec.collapse): def-NAMES forced to the blackbox
  // path (case-sensitive). Applied identically by the encoder (skip flatten)
  // and the box-correspondence builder below (pre-build the boxes' shared symbols).
  // A requested def resolves by ANY of its spellings (canonical entity or a
  // side's full name); all spellings are inserted so the encoder's exact
  // per-side name match keeps working on both designs.
  Io_name_map<bool> collapse_defs;
  for (const auto& d : opts.collapse) {
    if (!d.empty()) {
      collapse_defs[d] = true;
    }
  }
  for (const auto& d : opts.collapse) {
    if (d.empty()) {
      continue;
    }
    const std::string e   = entity_of(d);
    const auto        rit = ref_ent_uniq.find(e);
    const auto        iit = impl_ent_uniq.find(e);
    const std::string rf  = rit != ref_ent_uniq.end() ? rit->second : std::string{};
    const std::string im  = iit != impl_ent_uniq.end() ? iit->second : std::string{};
    if (d == e || d == rf || d == im) {  // d names the (side-)unique entity e
      collapse_defs[e] = true;
      if (!rf.empty()) {
        collapse_defs[rf] = true;
      }
      if (!im.empty()) {
        collapse_defs[im] = true;
      }
    }
  }
  const Io_name_map<bool>* collapse_ptr = collapse_defs.empty() ? nullptr : &collapse_defs;

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);

  // Subnode Gids of collapsed leaves, so the inductive/BMC flop collection
  // below can skip descending into them (their state is the box's, not a set of
  // internal flop cuts). Scanned HIERARCHICALLY (a nested-only collapse
  // instance would be missed by a top-level scan — the encoder's own opaque
  // scan matches). gids are name-hash stable across designs.
  ankerl::unordered_dense::set<hhds::Gid> collapse_gids;
  if (collapse_ptr != nullptr) {
    for (auto* g : {ref, impl}) {
      for (auto node : g->forward_hier()) {
        if (graph_util::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto sio = node.get_subnode_io();
        if (sio != nullptr && collapse_ptr->count(std::string(sio->get_name())) > 0) {
          collapse_gids.insert(node.get_subnode_gid());
        }
      }
    }
  }
  const ankerl::unordered_dense::set<hhds::Gid>* collapse_gids_ptr = collapse_gids.empty() ? nullptr : &collapse_gids;

  // ── Box correspondence (collapsed / blackbox Sub instances) ────────────────
  // ONE hierarchical walk per design enumerates every Sub the encoder will treat
  // as a box — the encoder's own scope (collapsed leaves opaque) and its exact
  // blackbox predicate — and records it under its structural node key. Products:
  //   * ref_box_keys / impl_box_keys: box_node_key -> cross-design correspondence
  //     key "<defname>#<tag>". tag is "n:<canon instance hier-name>" when that
  //     name occurs exactly ONCE on EACH side (name-first pairing: a Verilog
  //     instance name and a Pyrope ::[name=] attr both survive their front-ends
  //     onto the Sub node), else "o<idx>" over the unnamed/unmatched remainder
  //     in walk order (the legacy occurrence fallback, now only for the
  //     instances a name cannot pair). A key present on ONE side only (count
  //     mismatch) yields one-sided obligations, which the miters gate to an
  //     incomplete correspondence — never a Proven/Refuted.
  //   * comb_boxes: pairing-free per-def UFs for STATELESS collapsed leaves
  //     (see Comb_box in encode.hpp — no correspondence to get wrong).
  //   * state_boxes: shared UF symbols per STATEFUL collapsed leaf instance,
  //     keyed by the correspondence key.
  //   * shared_bbox: shared output symbols for TRUE blackboxes (unresolved defs).
  // Building all of these from one enumeration retires the old three-counter
  // scheme (encoder forward_hier vs two forward_class builders), whose drift on
  // nested boxes silently degraded a stateful box to a stateless constant one —
  // a false-PASS hazard. The encoder now hard-errors on a key it cannot find.
  struct Box_inst {
    std::string      nk;     // box_node_key (per-design structural identity)
    std::string      cname;  // canonicalized instance hier-name
    hhds::Node_class node;
    bool             in_ref;
  };
  absl::flat_hash_map<std::string, std::vector<Box_inst>> boxes_by_def;
  std::vector<std::string>                                box_defnames;  // insertion order (walk-deterministic)
  absl::flat_hash_map<std::string, bool>                  def_stateful;  // collapse defs: ANY resolvable body has state?
  absl::flat_hash_map<std::string, bool>                  def_has_body;  // collapse defs: ANY instance resolves a body?
  absl::flat_hash_set<const void*>                        scanned_bodies;  // def graphs already state-scanned
  // hhds Hier_index uniqueness is only guaranteed within ONE level of subnode
  // instantiation: two instances of the same grand-subgraph below a
  // multiply-instantiated middle def collide on gid:hier_pos:nid. A collision
  // among BOX instances would silently alias their keys/symbols and drop one
  // instance's obligations, so detect it and bail to a sound INCONCLUSIVE.
  absl::flat_hash_set<std::string> seen_ref_nk, seen_impl_nk;
  bool                             nk_collision = false;
  auto scan_boxes = [&](hhds::Graph* g, bool in_ref) {
    hhds::Hier_opaque_scope sc(collapse_gids_ptr);  // mirror the encoder's ambient opacity
    for (auto node : g->forward_hier(true, false, collapse_gids_ptr)) {
      if (graph_util::type_op_of(node) != Ntype_op::Sub || !node.has_out_edges()) {
        continue;  // the encoder skips consumer-less nodes before its box path
      }
      auto sio = node.get_subnode_io();
      if (sio == nullptr) {
        continue;
      }
      std::string defname(sio->get_name());
      // The cross-design def key: entity when side-unique (a Pyrope
      // "file.entity" and a slang flat "entity" must land on ONE key for
      // their boxes to correspond), else the full per-side name.
      const std::string defkey         = canon_def(in_ref, defname);
      const bool        force_collapse = collapse_ptr != nullptr && collapse_ptr->count(defname) > 0;
      // Mirror encode.cpp exactly: a sub with a body is DESCENDED (not a box)
      // unless force-collapsed; a sub_lib-resolvable combinational def is
      // flattened inline (not a box); everything else is a box.
      if (node.get_subnode_graph() != nullptr && !force_collapse) {
        continue;
      }
      if (!force_collapse && sub_lib != nullptr) {
        if (auto git = sub_lib->find(node.get_subnode_gid()); git != sub_lib->end() && git->second != nullptr) {
          bool lib_stateful = false;
          for (auto dn : git->second->forward_class()) {
            auto dop = graph_util::type_op_of(dn);
            if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
              lib_stateful = true;
              break;
            }
          }
          if (!lib_stateful) {
            continue;  // flattened inline by the encoder
          }
        }
      }
      // Classification accumulates as an OR over ALL instances on BOTH sides:
      // pinning it to the first-scanned instance (whose body may be null while
      // the other side resolves a STATEFUL body) would route a stateful def to
      // the time-frozen shared-symbol blackbox — the stateful-degraded-to-
      // constant false-PASS this builder exists to retire.
      if (force_collapse) {
        if (auto def = node.get_subnode_graph(); def != nullptr) {
          def_has_body[defkey] = true;
          if (scanned_bodies.insert(def.get()).second) {
            for (auto dn : def->forward_class()) {
              auto dop = graph_util::type_op_of(dn);
              if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
                def_stateful[defkey] = true;
                break;
              }
            }
          }
        }
      }
      if (boxes_by_def.find(defkey) == boxes_by_def.end()) {
        box_defnames.push_back(defkey);
      }
      std::string nk = box_node_key(node);
      if (!(in_ref ? seen_ref_nk : seen_impl_nk).insert(nk).second) {
        nk_collision = true;
      }
      boxes_by_def[defkey].push_back(Box_inst{std::move(nk), canon_flop_name(node.get_hier_name()), node, in_ref});
    }
  };
  scan_boxes(ref, true);
  scan_boxes(impl, false);
  if (nk_collision) {
    Query_result r0;
    r0.verdict = Verdict::Unknown;
    r0.engine  = opts.engine;
    r0.detail
        = "box-correspondence: duplicate structural node key among box instances (hhds Hier_index collides for a "
                 "grand-subgraph below a multiply-instantiated middle def) — instance identity is ambiguous, so neither a "
                 "proof nor a refutation would be trustworthy; prove/collapse the middle def first";
    return r0;
  }
  // A box inst's per-side FULL spelling that coincides with a DIFFERENT def's
  // canonical key would alias two defs in the by-name lookups (the collapse
  // set, the Comb_box map); def identity is then ambiguous, so bail to a
  // sound INCONCLUSIVE (same policy as the nk collision above).
  for (const auto& [defkey, insts] : boxes_by_def) {
    for (const auto& bi : insts) {
      std::string full(bi.node.get_subnode_io()->get_name());
      if (full != defkey && boxes_by_def.find(full) != boxes_by_def.end()) {
        Query_result r0;
        r0.verdict = Verdict::Unknown;
        r0.engine  = opts.engine;
        r0.detail  = "box-correspondence: box def '" + full + "' collides with another def's canonical (entity) key — def "
                     "identity is ambiguous, so neither a proof nor a refutation would be trustworthy; rename one module";
        return r0;
      }
    }
  }

  // Name-first pairing per def: a canon instance name unique on BOTH sides is
  // the tag; the remainder pairs by walk-order occurrence.
  Io_name_map<std::string> ref_box_keys, impl_box_keys;
  for (const auto& defname : box_defnames) {
    const auto&      insts = boxes_by_def[defname];
    Io_name_map<int> ref_cnt, impl_cnt;
    for (const auto& bi : insts) {
      (bi.in_ref ? ref_cnt : impl_cnt)[bi.cname]++;
    }
    int ref_res = 0, impl_res = 0;
    for (const auto& bi : insts) {
      const bool  named = ref_cnt[bi.cname] == 1 && impl_cnt[bi.cname] == 1;
      std::string tag   = named ? ("n:" + bi.cname) : ("o" + std::to_string(bi.in_ref ? ref_res++ : impl_res++));
      (bi.in_ref ? ref_box_keys : impl_box_keys)[bi.nk] = defname + "#" + tag;
    }
  }

  absl::flat_hash_map<std::string, State_box> state_boxes;
  absl::flat_hash_map<std::string, Comb_box>  comb_boxes;
  Io_name_map<Val>                            shared_bbox;
  {
    Io_name_map<int>  bw;   // true-blackbox "key:port" -> max real width across designs
    Io_name_map<bool> bsg;  // "key:port" -> signedness of the widest side
    auto out_port_name = [](const std::shared_ptr<hhds::GraphIO>& sio, const hhds::Pin_class& dp) -> std::string {
      for (const auto& d : sio->get_output_pin_decls()) {
        if (sio->get_output_port_id(d.name) == dp.get_port_id()) {
          return d.name;
        }
      }
      return std::to_string(dp.get_port_id());
    };
    // NAME-SORTED UF input layout for a box def, unioned across all instances
    // on BOTH designs. Per port the width is max(declared bits, DRIVEN real
    // width): a 0-bit (unknown-width) decl must not narrow the slot — the
    // encoder feeds the untruncated driver value, and a 1-bit slot would
    // truncate it to its LSB, letting two genuinely different inputs collide in
    // the UF domain (a false PROVEN with no bbin backstop on the comb path).
    // CONNECTED ports with no IO decl (the encoder's pid-fallback names) are
    // included too — dropping them would model the leaf as independent of a
    // real input.
    auto build_in_ports = [&](const std::vector<Box_inst>& insts_) {
      Io_name_map<int> in_pw;
      for (const auto& bi : insts_) {
        auto                                             sio = bi.node.get_subnode_io();
        absl::flat_hash_map<hhds::Port_id, std::string>  in_name;
        for (const auto& d : sio->get_input_pin_decls()) {
          in_name[sio->get_input_port_id(d.name)] = d.name;
          int w = static_cast<int>(d.bits);
          if (auto it = in_pw.find(d.name); it == in_pw.end() || w > it->second) {
            in_pw[d.name] = w;
          }
        }
        for (const auto& e : bi.node.inp_edges()) {
          auto        pid  = e.sink.get_port_id();
          auto        nit  = in_name.find(pid);
          std::string port = nit != in_name.end() ? nit->second : std::to_string(pid);
          int         w    = real_width(e.driver);
          if (auto it = in_pw.find(port); it == in_pw.end() || w > it->second) {
            in_pw[port] = w;
          }
        }
      }
      std::vector<std::string> pnames;
      pnames.reserve(in_pw.size());
      for (const auto& [n, w] : in_pw) {
        pnames.push_back(n);
      }
      std::sort(pnames.begin(), pnames.end());  // identical layout on both sides
      std::pair<std::vector<std::pair<std::string, int>>, int> out{{}, 0};
      for (const auto& n : pnames) {
        int w = std::max(1, in_pw[n]);
        out.first.emplace_back(n, w);
        out.second += w;
      }
      return out;
    };
    for (const auto& defname : box_defnames) {
      const auto& insts          = boxes_by_def[defname];
      const bool  collapsed      = collapse_ptr != nullptr && collapse_ptr->count(defname) > 0;
      const bool  body_resolved  = def_has_body.count(defname) > 0;
      if (collapsed && body_resolved && !def_stateful[defname]) {
        // STATELESS collapsed leaf -> pairing-free per-def UF box (Comb_box).
        Comb_box box;
        for (const auto& bi : insts) {
          auto sio = bi.node.get_subnode_io();
          for (const auto& e : bi.node.out_edges()) {
            auto        dp   = e.driver;
            std::string port = out_port_name(sio, dp);
            int         w    = real_width(dp);
            if (w == 0) {
              w = 1;
            }
            if (auto it = box.out_w.find(port); it == box.out_w.end() || w > it->second) {
              box.out_w[port]   = w;
              box.out_sgn[port] = !graph_util::is_unsign(dp);
            }
          }
        }
        std::tie(box.in_ports, box.in_w) = build_in_ports(insts);
        if (box.in_w > 0) {
          cvc5::Sort dom = tm.mkBitVectorSort(static_cast<uint32_t>(box.in_w));
          for (const auto& [port, ow] : box.out_w) {
            box.out_fn[port] = tm.mkConst(tm.mkFunctionSort({dom}, tm.mkBitVectorSort(static_cast<uint32_t>(ow))),
                                          "uf_cb:" + defname + ":" + port);
          }
        } else {  // no-input leaf = a constant: one shared symbol per (def, port)
          for (const auto& [port, ow] : box.out_w) {
            box.out_const[port] = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(ow)), "cb0:" + defname + ":" + port),
                                      ow,
                                      box.out_sgn[port]};
          }
        }
        // The encoder looks a Comb_box up by the PER-SIDE callee name; alias
        // every spelling (e.g. slang's flat "entity", Pyrope's "file.entity")
        // onto the same box — the copies share the cvc5 UF terms, so
        // congruence still spans the designs.
        for (const auto& bi : insts) {
          std::string full(bi.node.get_subnode_io()->get_name());
          if (full != defname) {
            comb_boxes[full] = box;
          }
        }
        comb_boxes[defname] = std::move(box);
        continue;
      }
      if (collapsed && body_resolved && def_stateful[defname]) {
        // STATEFUL collapsed leaf -> shared state-aware box per instance pair.
        // Outputs are MOORE — UF_out(state) ONLY, not (inputs, state). A Mealy box
        // (output depends on the current input) adds a combinational input->output
        // path through the leaf; in a pipeline a stage register's q feeds glue that
        // feeds its own d (stall/enable), so q=UF(d,..) -> d -> q is a FALSE
        // combinational cycle that the encoder cannot resolve. Output-from-state-only
        // matches the registered output exactly and stays sound (both designs share
        // UF_out + state; divergent leaf inputs are still caught by the bbin compare
        // points). The next-state transition keeps its (inputs, state) dependence —
        // it feeds the state cut a cycle later, never a combinational loop.
        // Per-DEF io layout, unioned across BOTH designs (max width per port,
        // NAME-SORTED inputs — see build_in_ports) so every instance pair
        // applies the shared UFs to an identically-laid-out concat
        // (State_box::in_ports). Outputs union the decls with the actually
        // driven pins (an undeclared output would otherwise miss its UF and
        // gratuitously fail the encode).
        Io_name_map<int> out_pw;
        for (const auto& bi : insts) {
          auto sio = bi.node.get_subnode_io();
          for (const auto& d : sio->get_output_pin_decls()) {
            int w = std::max(1, static_cast<int>(d.bits));
            if (auto it = out_pw.find(d.name); it == out_pw.end() || w > it->second) {
              out_pw[d.name] = w;
            }
          }
          for (const auto& e : bi.node.out_edges()) {
            auto        dp   = e.driver;
            std::string port = out_port_name(sio, dp);
            int         w    = std::max(1, real_width(dp));
            if (auto it = out_pw.find(port); it == out_pw.end() || w > it->second) {
              out_pw[port] = w;
            }
          }
        }
        auto [in_ports, in_w] = build_in_ports(insts);
        for (const auto& bi : insts) {
          const std::string& bk = (bi.in_ref ? ref_box_keys : impl_box_keys).at(bi.nk);
          if (state_boxes.count(bk)) {
            continue;  // the other design's corresponding instance built it (shared UFs)
          }
          State_box box;
          box.in_ports        = in_ports;
          box.in_w            = in_w;
          box.state_w         = 64;  // abstract state width (the same for both designs -> sound)
          cvc5::Sort bv_state = tm.mkBitVectorSort(static_cast<uint32_t>(box.state_w));
          for (const auto& [pname, ow] : out_pw) {
            box.out_w[pname]  = ow;
            box.out_fn[pname] = tm.mkConst(tm.mkFunctionSort({bv_state}, tm.mkBitVectorSort(static_cast<uint32_t>(ow))),
                                           "uf_out:" + bk + ":" + pname);
          }
          std::vector<cvc5::Sort> next_dom;  // next_state = UF_next(inputs, state)
          if (box.in_w > 0) {
            next_dom.push_back(tm.mkBitVectorSort(static_cast<uint32_t>(box.in_w)));
          }
          next_dom.push_back(bv_state);
          box.next_fn     = tm.mkConst(tm.mkFunctionSort(next_dom, bv_state), "uf_next:" + bk);
          state_boxes[bk] = std::move(box);
        }
        continue;
      }
      // TRUE blackbox (unresolved def, or a collapse def with no body): shared
      // output symbols per correspondence key, at the max width across designs.
      for (const auto& bi : insts) {
        const std::string& bk  = (bi.in_ref ? ref_box_keys : impl_box_keys).at(bi.nk);
        auto               sio = bi.node.get_subnode_io();
        for (const auto& e : bi.node.out_edges()) {
          auto        dp   = e.driver;
          std::string port = out_port_name(sio, dp);
          std::string key  = bk + ":" + port;
          int         w    = real_width(dp);
          if (w == 0) {
            w = 1;
          }
          if (auto it = bw.find(key); it == bw.end() || w > it->second) {
            bw[key]  = w;
            bsg[key] = !graph_util::is_unsign(dp);
          }
        }
      }
    }
    for (const auto& [key, w] : bw) {
      shared_bbox[key] = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "bb:" + key), w, bsg[key]};
    }
  }
  const auto* state_boxes_ptr = state_boxes.empty() ? nullptr : &state_boxes;
  const auto* comb_boxes_ptr  = comb_boxes.empty() ? nullptr : &comb_boxes;

  // A stateful or stateless collapse adds uninterpreted functions; widen the
  // logic to include UF (and keep arrays for the memory cut). The eager internal
  // bit-blaster has no UF/array theory, so a UF query keeps the default solver
  // (below).
  solver.setLogic(state_boxes_ptr != nullptr || comb_boxes_ptr != nullptr ? "QF_AUFBV" : "QF_ABV");  // BV + arrays (+UF)

  // Per-checkSat wall-clock bound (lec.timeout seconds; 0 = unbounded). Hard
  // nonlinear miters (a chain of two multiplies — associativity, distributivity,
  // 3-way commutativity at >=16 bits) make cvc5's bit-blast never return. Without
  // this, `lhd lec` freezes forever. A bound makes the timed-out checkSat come
  // back `unknown`, which both checkSat sites already map to Verdict::Unknown — a
  // SOUND degrade (never a false Proven/Refuted). tlimit-per resets per query, so
  // it bounds each decisive checkSat in the comb / inductive / BMC frames.
  if (opts.timeout > 0) {
    solver.setOption("tlimit-per", std::to_string(static_cast<long long>(opts.timeout) * 1000));
  }

  // Bit-blasting BV sub-solver. cvc5's default LAZY bitblast solver can return a
  // spurious SAT on these wide multi-output arithmetic miters: it reports SAT
  // while the underlying CaDiCaL is actually UNSAT, so the miter false-REFUTEs
  // and a witness query (getValue) then aborts inside CaDiCaL ("can only get
  // value in satisfied state"). The EAGER internal bit-blaster solves the same
  // miters correctly and keeps the SAT assignment, so witness extraction is
  // safe — but it has no theory of arrays, so restrict it to array-free queries.
  // Memory cuts (M4) introduce array symbols; those keep the default solver.
  bool has_mem = false;
  for (auto* g : {ref, impl}) {
    for (auto node : g->forward_class()) {
      if (graph_util::type_op_of(node) == Ntype_op::Memory) {
        has_mem = true;
        break;
      }
    }
    if (has_mem) {
      break;
    }
  }
  if (!has_mem && state_boxes_ptr == nullptr && comb_boxes_ptr == nullptr) {
    solver.setOption("bv-solver", "bitblast-internal");  // UF/array queries keep the default solver
  }
  if (opts.witness) {
    solver.setOption("produce-models", "true");
  }

  // Shared current-state ARRAY symbol per memory cut key, across both designs
  // (the "collapse corresponding memories" assumption). Built once; the encoder
  // reuses the symbol for the matching memory in each design.
  auto build_shared_mems = [&](std::string_view tag) {
    Io_name_map<cvc5::Term> sm;
    Io_name_map<int>        occ;
    auto                                         add = [&](hhds::Graph* g) {
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
          continue;
        }
        Mem_sig sig = read_mem_sig(node);
        if (sig.bits <= 0 || sig.size <= 0) {
          continue;
        }
        std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits);  // shape only; occ matches by RTL order
        std::string key = mem_state_key(sig, occ[sg]++);
        if (sm.count(key)) {
          continue;
        }
        cvc5::Sort asort = tm.mkArraySort(tm.mkBitVectorSort(static_cast<uint32_t>(sig.addr_w)),
                                          tm.mkBitVectorSort(static_cast<uint32_t>(sig.bits)));
        sm[key]          = tm.mkConst(asort, std::string(tag) + key);
      }
    };
    add(ref);
    add(impl);
    return sm;
  };

  // ── BMC engine: unroll N cycles from the reset state ──────────────────────
  // The single-step inductive miter (below) assumes an arbitrary equal current
  // state, so it false-REFUTEs on UNREACHABLE states where the two front-ends
  // resolve a don't-care differently. BMC instead starts from the reset state
  // (each flop's constant `initial`, or a shared fresh symbol for a reset-less
  // flop) and chains each design's own next-state forward, so only states
  // reachable within N cycles are explored — sound for that bound (this is what
  // yosys' own lgcheck does with its bounded miter).
  if (opts.engine == "bmc") {
    const int N = opts.bound > 0 ? opts.bound : 6;
    Encoder   enc(tm);
    enc.set_sub_lib(sub_lib);
    enc.set_name_alias(&name_alias);
    enc.set_collapse_defs(collapse_ptr);
    enc.set_state_boxes(state_boxes_ptr);
    enc.set_comb_boxes(comb_boxes_ptr);
    enc.set_shared_bbox(&shared_bbox);

    struct In {
      int  w;
      bool sgn;
    };
    Io_name_map<In> ins;
    auto                                 collect_ins = [&](hhds::Graph* g) {
      auto gio = g->get_io();
      for (const auto& d : gio->get_input_pin_decls()) {
        auto pin = g->get_input_pin(d.name);
        int  w   = real_width_io(pin, *gio, d.name);
        if (w == 0) {
          w = 1;
        }
        if (auto it = ins.find(d.name); it != ins.end() && it->second.w >= w) {
          continue;  // keep the max-width view across both designs (bit-width trap)
        }
        bool sgn    = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
        ins[d.name] = In{w, sgn};
      }
    };
    collect_ins(ref);
    collect_ins(impl);

    // Reset-phase setup. A PRIMARY reset input is a TOP-level input name that
    // drives some flop's reset_pin directly; its asserted level is 0 when that
    // flop is active-low (negreset), else 1. (Derived resets — driven by a mux,
    // not a primary input — are not directly controllable and are simply left
    // free.) The top-graph check matters: a subgraph flop's reset driver is the
    // SUBGRAPH's own input pin, and matching its port name against the top-level
    // `ins` map by name alone could pin an unrelated same-named top input
    // (under-exploration -> unsound PROVEN). Subgraph-local resets are left
    // free; the canonical-name fallback below still covers pass-through resets.
    Io_name_map<bool> reset_negset;  // name -> negreset
    auto                                   collect_resets = [&](hhds::Graph* g) {
      for (auto node : g->forward_hier()) {  // descend hierarchy: flops at every level
        if (graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto rst_d = graph_util::get_driver_of_sink_name(node, "reset_pin");
        if (rst_d.is_invalid() || !graph_util::is_graph_input_pin(rst_d) || rst_d.get_graph() != g) {
          continue;
        }
        auto nm = std::string(graph_util::pin_name_of(rst_d));
        if (nm.empty() || !ins.count(nm)) {
          continue;
        }
        bool negreset = false;
        if (auto neg_d = graph_util::get_driver_of_sink_name(node, "negreset");
            !neg_d.is_invalid() && graph_util::is_const_pin(neg_d)) {
          negreset = !graph_util::hydrate_const(neg_d).is_known_false();
        }
        reset_negset[nm] = negreset;
      }
    };
    // Canonical reset-name test (token-aware to avoid matching "first"/"burst").
    // negreset (active-low) inferred from an _n / _ni / n suffix.
    auto reset_name_polarity = [](const std::string& nm, bool& negreset) -> bool {
      std::string lc = nm;
      for (auto& c : lc) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      bool tok_match = false;
      size_t start   = 0;
      for (size_t i = 0; i <= lc.size(); ++i) {
        if (i == lc.size() || lc[i] == '_') {
          std::string tok = lc.substr(start, i - start);
          if (tok == "rst" || tok == "reset" || tok == "rstn" || tok == "resetn" || tok == "arst" || tok == "areset"
              || tok == "nrst" || tok == "nreset" || tok == "por") {
            tok_match = true;
          }
          start = i + 1;
        }
      }
      if (!tok_match) {
        return false;
      }
      auto ends = [&](std::string_view s) { return lc.size() >= s.size() && lc.compare(lc.size() - s.size(), s.size(), s) == 0; };
      negreset = ends("_n") || ends("_ni") || ends("_n_i") || ends("_ni_i") || lc == "rstn" || lc == "resetn" || ends("nrst")
                 || ends("nreset");
      return true;
    };

    const bool phase_reset = opts.phase == "just_reset";
    const bool phase_run   = opts.phase == "after_reset";
    if (phase_reset || phase_run) {
      if (!opts.reset.empty()) {
        // Explicit override: authoritative list of reset inputs + polarity.
        std::string spec = opts.reset;
        size_t      p    = 0;
        while (p < spec.size()) {
          size_t      comma = spec.find(',', p);
          std::string tok   = spec.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
          p                 = comma == std::string::npos ? spec.size() : comma + 1;
          if (tok.empty()) {
            continue;
          }
          bool        negreset = false;
          std::string nm       = tok;
          if (auto colon = tok.find(':'); colon != std::string::npos) {
            nm           = tok.substr(0, colon);
            auto pol     = tok.substr(colon + 1);
            negreset     = (pol == "lo" || pol == "low" || pol == "n" || pol == "0");
          } else {
            reset_name_polarity(nm, negreset);  // infer from suffix
          }
          if (ins.count(nm)) {
            reset_negset[nm] = negreset;
          }
        }
      } else {
        // Auto: structural async resets (reset_pin-driven inputs)...
        collect_resets(ref);
        collect_resets(impl);
        // ...plus canonical reset-named inputs (sync resets folded into din).
        for (const auto& [name, info] : ins) {
          if (reset_negset.count(name)) {
            continue;
          }
          bool negreset = false;
          if (reset_name_polarity(name, negreset)) {
            reset_negset[name] = negreset;
          }
        }
      }
    }
    // Pipeline flush: a depth-d flop (pipe_min) is a d-stage shift register, and
    // chained flops (e.g. stage[3] feeding a stage[1] add) accumulate latency, so
    // a reset-less pipeline's power-on state needs `latency` cycles of (shared)
    // inputs to flush before the two designs' outputs are input-determined.
    // `pipeline_latency` is the longest flop-weighted path from inputs to any
    // node — MAX over parallel flops, SUM over series (so it never blows up on
    // wide register files the way a flop-count sum would), cycle-guarded so
    // feedback loops don't recurse. Extend the prologue to it: the checked window
    // then compares only flushed, input-determined behavior. (Sound either way —
    // the window still exercises `bound` free-running cycles, and UNDER-flushing
    // can only cause a false REFUTE, never a false PROVEN.)
    auto pipeline_latency = [&](hhds::Graph* g) -> int {
      absl::flat_hash_map<std::string, int> memo;
      absl::flat_hash_set<std::string>      on_stack;
      std::function<int(const hhds::Node_class&)> lat = [&](const hhds::Node_class& n) -> int {
        std::string id = n.get_hier_name();
        if (auto it = memo.find(id); it != memo.end()) {
          return it->second;
        }
        if (!on_stack.insert(id).second) {
          return 0;  // feedback back-edge: do not recurse (loop state never flushes)
        }
        int base = 0;
        for (auto e : n.inp_edges()) {
          if (graph_util::is_graph_input_pin(e.driver) || graph_util::is_const_pin(e.driver)) {
            continue;  // primary input / constant: latency 0
          }
          base = std::max(base, lat(e.driver.get_master_node()));
        }
        on_stack.erase(id);
        int depth = 0;
        if (graph_util::type_op_of(n) == Ntype_op::Flop) {
          depth = 1;
          auto pm = graph_util::get_driver_of_sink_name(n, "pipe_min");
          if (!pm.is_invalid() && graph_util::is_const_pin(pm)) {
            int d = static_cast<int>(graph_util::hydrate_const(pm).to_just_i64());
            if (d > 1) {
              depth = d;
            }
          }
        }
        int total  = base + depth;
        memo[id]   = total;
        return total;
      };
      int m = 0;
      for (auto node : g->forward_hier()) {
        m = std::max(m, lat(node));
      }
      return m;
    };
    int reset_hold = phase_run ? (opts.reset_cycles > 0 ? opts.reset_cycles : 1) : 0;
    if (phase_run) {
      int flush = std::max(pipeline_latency(ref), pipeline_latency(impl));
      if (flush > reset_hold) {
        reset_hold = flush;  // flush the deepest pipeline before checking
      }
    }
    const int total_cyc = N + reset_hold;  // run: prologue + checked window

    // state[0]: reset initial per flop (shared equal across designs). In `run`
    // phase we instead start from a FRESH arbitrary equal state and let the
    // reset-hold prologue drive both designs into their reset state — so the
    // checked window genuinely exercises free-running behavior from reset.
    Io_name_map<Val> ref_state, impl_state;
    absl::flat_hash_set<std::string> bank_hold_keys;  // reset-less bank flops: hold across the reset prologue
    {
      Io_name_map<int>  fw;
      Io_name_map<bool> fsgn;  // sign of the NARROWEST decl (the value semantics of the shared init)
      Io_name_map<Val>  init;
      auto                                  collect_flops = [&](hhds::Graph* g) {
        for (auto node : g->forward_hier()) {  // descend hierarchy: cut flops at every level
          if (graph_util::type_op_of(node) != Ntype_op::Flop) {
            continue;
          }
          auto q = node.get_driver_pin(0);
          if (q.is_invalid()) {
            continue;
          }
          auto key = eff(node.get_hier_name());  // hier correspondence key (matches encode())
          int  w   = real_width(q);
          if (w == 0) {
            w = 1;
          }
          // MIN width across both designs: the shared power-on symbol is the
          // VALUE the two flops both hold; a wider decl (cgen's spare-sign-bit
          // reg) EXTENDS it in seed_state per this sign, so its headroom bits
          // start value-consistent instead of ranging free (a free headroom
          // bit has no narrow-side counterpart and spuriously refutes designs
          // whose control reads the flop unmasked before the first real write
          // — see tests/equiv/flop_init_headroom). Widths written by the
          // transition still use each side's full local width, so a REAL
          // wide-vs-narrow divergence is still caught from the first write on.
          if (auto it = fw.find(key); it == fw.end() || w < it->second) {
            fw[key]   = w;
            fsgn[key] = !graph_util::is_unsign(q);
          }
          if (!init.count(key)) {
            if (auto iv = flop_initial(tm, node, w, opts.gold_x != "zero")) {
              init[key] = *iv;
            }
          }
        }
      };
      {
        hhds::Hier_opaque_scope sc(collapse_gids_ptr);  // a collapsed leaf's state is the box's, not its flops'
        collect_flops(ref);
        collect_flops(impl);
      }
      if (std::getenv("LEC_DUMP_FLOPS") != nullptr) {
        auto dump_keys = [&](hhds::Graph* g, const char* tag) {
          std::set<std::string> keys;
          for (auto node : g->forward_hier()) {
            if (graph_util::type_op_of(node) != Ntype_op::Flop) {
              continue;
            }
            auto q = node.get_driver_pin(0);
            if (q.is_invalid()) {
              continue;
            }
            int  w   = real_width(q);
            bool rst = flop_initial(tm, node, w > 0 ? w : 1).has_value();
            keys.insert(std::string(rst ? "[reset] " : "[UNRST] ") + eff(node.get_hier_name()) + "  <=  " + node.get_hier_name());
          }
          for (const auto& k : keys) {
            std::fprintf(stderr, "[LEC_FLOP %s] %s\n", tag, k.c_str());
          }
        };
        dump_keys(ref, "REF");
        dump_keys(impl, "IMPL");
      }
      for (const auto& [key, w] : fw) {
        Val v;
        if (!phase_run && init.count(key)) {
          v = init.at(key);
        } else {
          v = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "s0_" + key), w, fsgn.at(key)};
        }
        ref_state[key]  = v;
        impl_state[key] = v;
      }
      // Matched-reset shared init for each STATEFUL collapsed leaf: both designs
      // start the box's state cut at the SAME symbol (so the box doesn't lose the
      // leaf's reset behavior), then the next-state UF threads it forward — making
      // the leaf's output VARY per cycle (a constant box false-proves a timing diff).
      for (const auto& [bk, box] : state_boxes) {
        std::string state_key = std::string("\x01") + "leafstate:" + bk;
        Val         v{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(box.state_w)), "s0_" + state_key), box.state_w, false};
        ref_state[state_key]  = v;
        impl_state[state_key] = v;
      }
      // A RESET-LESS register-file BANK (contiguous <base>_0.._N-1 flops with no
      // constant init, e.g. an architectural register file) must HOLD during the
      // reset prologue. The real RTL gates its write by reset (or by a reset flop,
      // so the write cannot fire while reset is asserted), but the BMC seeds every
      // flop free, so cycle 0 evaluates the writeback with a free, un-reset write
      // enable and stores the SHARED bank to a per-design free value — diverging two
      // equal designs on state real reset never touches. Holding the bank across the
      // prologue (reset window) keeps it at its shared initial value, exactly like a
      // gated-by-reset write. Sync-reset pipeline flops (no init, NOT a bank) are
      // left alone — they reset to 0 through their din during the prologue.
      for (const auto& [key, w] : fw) {
        if (init.count(key)) {
          continue;  // has a constant reset value -> resets normally
        }
        auto us = key.rfind('_');
        if (us == std::string::npos || us + 1 >= key.size()) {
          continue;
        }
        std::string idx = key.substr(us + 1);
        if (idx.empty() || !std::all_of(idx.begin(), idx.end(), [](unsigned char c) { return std::isdigit(c); })) {
          continue;
        }
        // Count siblings sharing this base with no init: a >1 contiguous group is a bank.
        std::string base = key.substr(0, us);
        int         n    = 0;
        for (const auto& [k2, w2] : fw) {
          if (k2.rfind(base + "_", 0) == 0 && !init.count(k2)) {
            auto t = k2.substr(base.size() + 1);
            if (!t.empty() && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); })) {
              ++n;
            }
          }
        }
        if (n > 1) {
          bank_hold_keys.insert(key);
        }
      }
    }
    std::vector<std::pair<std::string, cvc5::Term>> dbg_init;
    if (std::getenv("LEC_DUMP_WIT") != nullptr) {
      for (const auto& [k, v] : ref_state) {
        dbg_init.emplace_back("init:" + k, v.term);
      }
    }
    struct DbgNs {
      int         cyc;
      char        side;
      std::string key;
      cvc5::Term  t;
    };
    std::vector<DbgNs> dbg_ns;

    // Memory state[0]: one shared array per cut key (corresponding memories
    // collapse to the same initial contents); threaded forward like flop state.
    Io_name_map<cvc5::Term> ref_mem = build_shared_mems("m0_");
    Io_name_map<cvc5::Term> impl_mem = ref_mem;

    // ── Memory <-> single-wide-flop init bridge ───────────────────────────────
    // A behavioral memory (one SMT array) on one design can appear on the OTHER
    // design as ONE packed scalar flop of width size*bits: the slang reader lowers
    // a const-indexed unpacked-array reg `reg [W] d[N]` to a single N*W-bit flop
    // with bit-slice element accesses, while the Pyrope reference keeps it a
    // Memory. (The inductive mem<->flop-bank bridge below matches a memory against
    // N SEPARATE flops; this matches it against ONE wide flop.) Both are
    // uninitialized here, so their initial states are INDEPENDENT free symbols
    // (m0_<key> vs s0_<flop>) and a committed read before any write diverges even
    // though the designs are equivalent. Tie the memory's initial array to
    // array_from_bus(flop_init_bus) — entry i = bus[(i+1)*bits-1 : i*bits], entry
    // 0 in the low bits, matching the encoder's array_from_bus and the slang
    // flat-bus element packing. SOUND: the array becomes a deterministic FUNCTION
    // of the flop's free init symbol (never a constant), so equal designs prove
    // and a divergent written value still refutes on the write path.
    {
      // Per-design memory cut keys (shape-only occ, matching build_shared_mems).
      auto collect_mem_keys = [&](hhds::Graph* g) {
        Io_name_map<Mem_sig> out;
        Io_name_map<int>     occ;
        for (auto node : g->forward_class()) {
          if (graph_util::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
            continue;
          }
          Mem_sig sig = read_mem_sig(node);
          if (sig.bits <= 0 || sig.size <= 0) {
            continue;
          }
          std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits);
          std::string key = mem_state_key(sig, occ[sg]++);
          out.emplace(key, sig);  // first occurrence wins (matches build_shared_mems)
        }
        return out;
      };
      // Per-design flop cut key -> width (hier-canon key, matching ref_state above).
      auto collect_flop_w = [&](hhds::Graph* g) {
        Io_name_map<int>        out;
        hhds::Hier_opaque_scope sc(collapse_gids_ptr);
        for (auto node : g->forward_hier()) {
          if (graph_util::type_op_of(node) != Ntype_op::Flop) {
            continue;
          }
          auto q = node.get_driver_pin(0);
          if (q.is_invalid()) {
            continue;
          }
          int w = real_width(q);
          if (w == 0) {
            w = 1;
          }
          auto key = eff(node.get_hier_name());
          if (auto it = out.find(key); it == out.end() || w > it->second) {
            out[key] = w;
          }
        }
        return out;
      };
      auto ref_mem_keys  = collect_mem_keys(ref);
      auto impl_mem_keys = collect_mem_keys(impl);
      auto ref_flop_w    = collect_flop_w(ref);
      auto impl_flop_w   = collect_flop_w(impl);

      // Pair a memory owned by ONE design with a UNIQUE same-width wide flop on the
      // other (require uniqueness so an arbitrary width collision does not mis-pair
      // — sound either way, but a mis-pair would leave the block REFUTED, no worse
      // than today's independent-init behavior).
      auto try_bridge = [&](const Io_name_map<Mem_sig>& mem_side,
                            const Io_name_map<Mem_sig>& other_mem_side,
                            const Io_name_map<int>&     mem_side_flops,
                            const Io_name_map<int>&     other_side_flops) {
        for (const auto& [mkey, sig] : mem_side) {
          if (other_mem_side.count(mkey)) {
            continue;  // memory corresponds to a memory -> already shared, no bridge
          }
          auto sit = ref_mem.find(mkey);
          if (sit == ref_mem.end()) {
            continue;  // no shared array was built for this key
          }
          const int64_t total = static_cast<int64_t>(sig.size) * sig.bits;
          std::string   cand;
          int           n_cand = 0;
          for (const auto& [fk, fw] : other_side_flops) {
            if (fw != total || mem_side_flops.count(fk)) {
              continue;  // width mismatch, or a flop shared with the memory side
            }
            cand = fk;
            ++n_cand;
          }
          if (n_cand != 1) {
            continue;  // no unique wide-flop counterpart
          }
          auto fvit = ref_state.find(cand);
          if (fvit == ref_state.end() || fvit->second.width != total) {
            continue;
          }
          cvc5::Term bus = fvit->second.term;  // the flop's free init symbol (s0_<flop>)
          cvc5::Term arr = sit->second;        // base = shared free array; entries overwritten below
          for (int i = 0; i < sig.size; ++i) {
            auto op = tm.mkOp(cvc5::Kind::BITVECTOR_EXTRACT,
                              {static_cast<uint32_t>((i + 1) * sig.bits - 1), static_cast<uint32_t>(i * sig.bits)});
            cvc5::Term slice = tm.mkTerm(op, {bus});
            arr = tm.mkTerm(cvc5::Kind::STORE,
                            {arr, tm.mkBitVector(static_cast<uint32_t>(sig.addr_w), static_cast<uint64_t>(i)), slice});
          }
          ref_mem[mkey]  = arr;
          impl_mem[mkey] = arr;
        }
      };
      try_bridge(ref_mem_keys, impl_mem_keys, ref_flop_w, impl_flop_w);   // ref memory <-> impl wide flop
      try_bridge(impl_mem_keys, ref_mem_keys, impl_flop_w, ref_flop_w);   // impl memory <-> ref wide flop
    }

    // Sync-read (type==1) registered douts: a per-side latency-1 state, threaded
    // forward like memory state. Empty at cycle 0 (encode mints a fresh power-on
    // dout via the cycle prefix, flushed within the reset-hold window); each side
    // carries its OWN keys (a sync memory on one side may correspond to a comb
    // array + external flop on the other, whose latency comes from that flop).
    Io_name_map<cvc5::Term> ref_reads, impl_reads;

    cvc5::Term bad;
    std::vector<std::pair<std::string, cvc5::Term>> decomp_diffs;  // per-(output,cycle) diffs for decomposed proof
    int        uid = 0;  // unique suffix for fresh per-cycle state vars

    // Witness recording: keep each checked cycle's per-output (ref,impl) terms and
    // each cycle's primary-input symbol, so a REFUTED BMC run can report WHICH
    // output diverges at WHICH step under WHAT input trace (the BMC engine, unlike
    // the inductive miter, otherwise emits no counterexample — leaving `lhd lec`
    // with nothing to iterate on). Populated only when witnesses are requested.
    struct Wit_out {
      int         cyc;
      std::string name;
      cvc5::Term  rv;
      cvc5::Term  iv;
    };
    struct Wit_in {
      int         cyc;
      std::string name;
      cvc5::Term  t;
    };
    std::vector<Wit_out> wit_outs;
    std::vector<Wit_in>  wit_ins;
    // Stage-0 cex-debug: per-cycle, per-flop NEXT-state of BOTH sides, paired by
    // canonical key (identical on both designs). On REFUTE the EARLIEST diverging
    // state cut is the ROOT that a diverging output merely inherits. Pure read-only
    // (getValue on already-solved terms — no new SMT terms, no UNKNOWN risk), so it
    // replaces the env-gated LEC_DUMP_WIT/keep() debug path for localization.
    struct Wit_state {
      int         cyc;
      std::string key;
      cvc5::Term  rv;
      cvc5::Term  iv;
    };
    std::vector<Wit_state> wit_state;
    // Correspondence completeness: a compare point (primary output or a
    // collapsed-box bbin obligation) present on ONE side only means the two
    // designs did not expose the same compare set — e.g. mismatched box
    // instance counts, or a box the correspondence builder could not pair.
    // Silently skipping it (the old behavior) let an UNSAT return Proven with
    // part of the obligation set never checked, and let a SAT refute through
    // an unjustified shared-box assumption — so the verdict is gated to
    // Unknown below whenever these are non-empty (the inductive engine's rule).
    std::set<std::string> bmc_unmatched_ref, bmc_unmatched_impl;  // ordered: deterministic detail text

    for (int cyc = 0; cyc < total_cyc; ++cyc) {
      // `run` phase: the first `reset_hold` cycles hold reset asserted and are
      // NOT mitered (prologue); the rest are the checked window. `reset` phase:
      // reset asserted on every cycle and every cycle is checked.
      const bool checking = phase_run ? (cyc >= reset_hold) : true;
      // Current state for cycle i>0 is a FRESH symbol per flop, pinned to the
      // previous cycle's next-state by a flat equality assertion. Substituting
      // the next-state term directly instead would re-nest the whole transition
      // every cycle (a counter's state becomes (((q+1)+1)...) — cvc5's recursive
      // term walk then stack-overflows past ~13 steps). Fresh-var + assert keeps
      // every cycle's terms shallow; the unrolling lives in the assertion set.
      if (cyc > 0) {
        Io_name_map<Val> ns_ref, ns_impl;
        auto             pin_state = [&](const Io_name_map<Val>& prev_next, Io_name_map<Val>& cur) {
          for (const auto& [key, pv] : prev_next) {
            cvc5::Term s = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(pv.width)), "st" + std::to_string(uid++));
            solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv.term}));
            Val nv{s, pv.width, pv.is_signed};
            nv.x_mask = pv.x_mask;  // ref X-plane threads across the cycle boundary (impl's is null)
            cur[key] = nv;
          }
        };
        pin_state(ref_state, ns_ref);
        pin_state(impl_state, ns_impl);
        ref_state  = std::move(ns_ref);
        impl_state = std::move(ns_impl);

        // Same fresh-var pinning for memory arrays (keeps each cycle's array
        // terms shallow; the unrolling lives in the equality assertions).
        Io_name_map<cvc5::Term> nm_ref, nm_impl;
        auto                    pin_mem = [&](const Io_name_map<cvc5::Term>& prev_next, Io_name_map<cvc5::Term>& cur) {
          for (const auto& [key, pv] : prev_next) {
            cvc5::Term s = tm.mkConst(pv.getSort(), "ma" + std::to_string(uid++));
            solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv}));
            cur[key] = s;
          }
        };
        pin_mem(ref_mem, nm_ref);
        pin_mem(impl_mem, nm_impl);
        ref_mem  = std::move(nm_ref);
        impl_mem = std::move(nm_impl);
      }

      Io_name_map<Val> sh_ref, sh_impl;
      for (const auto& [name, info] : ins) {
        cvc5::Term t = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(info.w)), "c" + std::to_string(cyc) + "_" + name);
        // Phase control: pin a primary reset input to its asserted level during
        // the reset phase / run-prologue, and to its deasserted level in the
        // run-checked window. (active-low reset -> asserted=0, deasserted=all-1.)
        if (auto rit = reset_negset.find(name); rit != reset_negset.end()) {
          const bool assert_reset = phase_reset || (phase_run && cyc < reset_hold);
          const bool negreset     = rit->second;
          // asserted level: 0 if active-low else all-ones; deasserted is the dual.
          const bool drive_zero = assert_reset ? negreset : !negreset;
          cvc5::Term lvl        = drive_zero ? tm.mkBitVector(static_cast<uint32_t>(info.w), 0)
                                      : tm.mkTerm(cvc5::Kind::BITVECTOR_NOT, {tm.mkBitVector(static_cast<uint32_t>(info.w), 0)});
          solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {t, lvl}));
        }
        Val v{t, info.w, info.sgn};
        sh_ref[name]  = v;
        sh_impl[name] = v;
        if (opts.witness) {
          wit_ins.push_back({cyc, name, t});
        }
      }
      for (const auto& [k, v] : ref_state) {
        sh_ref[k] = v;
      }
      for (const auto& [k, v] : impl_state) {
        sh_impl[k] = v;
      }

      enc.set_x_dontcare(opts.gold_x != "zero");  // ref X = don't-care (lec.gold_x)
      enc.set_box_keys(&ref_box_keys);            // per-design box correspondence
      Encoded re = enc.encode(ref, &sh_ref, "r" + std::to_string(cyc) + "_", &ref_mem, &ref_reads);
      enc.set_x_dontcare(false);
      if (!re.ok) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; ref encode failed: " + re.error;
        return res;
      }
      enc.set_box_keys(&impl_box_keys);
      Encoded ie = enc.encode(impl, &sh_impl, "i" + std::to_string(cyc) + "_", &impl_mem, &impl_reads);
      if (!ie.ok) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; impl encode failed: " + ie.error;
        return res;
      }
      for (const auto& [l, r] : re.equalities) {
        solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
      }
      for (const auto& [l, r] : ie.equalities) {
        solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
      }

      if (checking) {
        for (const auto& [name, rv] : re.outputs) {
          if (!name.empty() && (name[0] == '\x01' || name[0] == '\x03')) {
            continue;  // next-state / env-gated debug tap, not a primary output
          }
          auto it = ie.outputs.find(name);
          if (it == ie.outputs.end()) {
            bmc_unmatched_ref.insert(display_name(name));
            continue;
          }
          if (name.rfind("\x02"
                         "bbox:",
                         0)
              == 0) {
            continue;  // two-sided box presence marker: constant on both sides, never a diff
          }
          int        w    = std::max(rv.width, it->second.width);
          cvc5::Term rfit = fit_to(tm, rv, w);
          cvc5::Term ifit = fit_to(tm, it->second, w);
          if (cvc5::Term u = fit_x_mask_to(tm, rv, w); !u.isNull()) {
            // ref X = don't-care: exclude ref-unknown bits from the compare
            cvc5::Term keep = tm.mkTerm(cvc5::Kind::BITVECTOR_NOT, {u});
            rfit            = tm.mkTerm(cvc5::Kind::BITVECTOR_AND, {rfit, keep});
            ifit            = tm.mkTerm(cvc5::Kind::BITVECTOR_AND, {ifit, keep});
          }
          cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rfit, ifit});
          bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
          decomp_diffs.push_back({name + "@" + std::to_string(cyc), diff});
          if (opts.witness) {
            wit_outs.push_back({cyc, name, rfit, ifit});
          }
        }
        for (const auto& [name, iv] : ie.outputs) {
          if (!name.empty() && (name[0] == '\x01' || name[0] == '\x03')) {
            continue;
          }
          if (re.outputs.find(name) == re.outputs.end()) {
            bmc_unmatched_impl.insert(display_name(name));
          }
        }
      }

      // Stash each design's next-state terms (built on this cycle's shallow
      // current-state symbols) for the next cycle's fresh-var pinning.
      Io_name_map<Val> rn, in;
      for (const auto& [name, rv] : re.outputs) {
        if (name.rfind("\x01nxt:", 0) == 0) {
          rn[name.substr(5)] = rv;
        }
      }
      for (const auto& [name, iv] : ie.outputs) {
        if (name.rfind("\x01nxt:", 0) == 0) {
          in[name.substr(5)] = iv;
        }
      }
      // During the reset prologue, a reset-less bank (register file) HOLDS — its
      // RTL write is gated by reset, but the encoded next-state used a free, un-reset
      // write enable. Override it to this cycle's current state so the bank keeps its
      // shared initial value through reset (see bank_hold_keys above).
      if (phase_run && cyc < reset_hold && !bank_hold_keys.empty()) {
        for (const auto& key : bank_hold_keys) {
          if (auto it = ref_state.find(key); it != ref_state.end()) {
            rn[key] = it->second;
          }
          if (auto it = impl_state.find(key); it != impl_state.end()) {
            in[key] = it->second;
          }
        }
      }
      if (std::getenv("LEC_DUMP_WIT") != nullptr) {
        auto keep = [&](const std::string& k) {
          return k.find("exmem") != std::string::npos || k.find("ex_mem") != std::string::npos
                 || k.find("idex") != std::string::npos || k.find("id_ex") != std::string::npos || k == "pc"
                 || k.find("taken") != std::string::npos || k.find("nextpc") != std::string::npos;
        };
        for (const auto& [k, v] : rn) {
          if (keep(k)) {
            dbg_ns.push_back({cyc, 'R', k, v.term});
          }
        }
        for (const auto& [k, v] : in) {
          if (keep(k)) {
            dbg_ns.push_back({cyc, 'I', k, v.term});
          }
        }
        for (const auto& [k, v] : re.outputs) {
          if (k.rfind("\x03"
                      "dbg:",
                      0)
              == 0) {
            dbg_ns.push_back({cyc, 'R', k.substr(5), v.term});
          }
        }
        for (const auto& [k, v] : ie.outputs) {
          if (k.rfind("\x03"
                      "dbg:",
                      0)
              == 0) {
            dbg_ns.push_back({cyc, 'I', k.substr(5), v.term});
          }
        }
      }
      if (opts.witness) {
        // Capture every flop's paired next-state for the Stage-0 first-diverging-cut
        // scan (no keep() filter — all flops, both sides, by canonical key).
        for (const auto& [k, rv] : rn) {
          if (auto it = in.find(k); it != in.end()) {
            wit_state.push_back({cyc, k, rv.term, it->second.term});
          }
        }
      }
      ref_state  = std::move(rn);
      impl_state = std::move(in);
      ref_mem    = std::move(re.next_mem);
      impl_mem   = std::move(ie.next_mem);
      ref_reads  = std::move(re.next_read);
      impl_reads = std::move(ie.next_read);
    }

    res.detail = "solver=cvc5 (bmc, phase=" + opts.phase + ", " + std::to_string(N) + " checked steps"
               + (reset_hold ? " after " + std::to_string(reset_hold) + " reset-hold" : "")
               + (reset_negset.empty() && opts.phase != "free_toreset" ? "; WARNING no primary reset input found" : "") + ")";
    // Bound bookkeeping for the auto bounded-Proven policy: N checked cycles and
    // the count of (output,cycle) comparisons actually run (0 => vacuous, no PASS).
    res.checked_steps = N;
    res.output_checks = static_cast<int>(decomp_diffs.size());
    // Incomplete correspondence gates BOTH verdict directions (see the set's
    // declaration): no Proven with unchecked obligations, no Refuted through an
    // unjustified shared-box assumption — only Unknown, with the sets surfaced.
    res.unmatched_ref.assign(bmc_unmatched_ref.begin(), bmc_unmatched_ref.end());
    res.unmatched_impl.assign(bmc_unmatched_impl.begin(), bmc_unmatched_impl.end());
    const bool incomplete = !res.unmatched_ref.empty() || !res.unmatched_impl.empty();
    if (!res.unmatched_ref.empty()) {
      res.detail
          += "; " + std::to_string(res.unmatched_ref.size()) + " ref-only cut point(s) {" + join_capped(res.unmatched_ref) + "}";
    }
    if (!res.unmatched_impl.empty()) {
      res.detail
          += "; " + std::to_string(res.unmatched_impl.size()) + " impl-only cut point(s) {" + join_capped(res.unmatched_impl) + "}";
    }
    if (bad.isNull()) {
      if (incomplete) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; no COMMON outputs to compare (correspondence incomplete)";
      } else {
        res.verdict = Verdict::Proven;
      }
      return res;
    }
    // Decomposed proof: a monolithic `bad` = OR of every (output,cycle) diff is a
    // single huge miter that cvc5's eager bitblast can choke on (UNKNOWN) for a
    // complex pipeline. Proving each diff UNSAT independently is the SAME proof
    // (OR is UNSAT iff every disjunct is) but each query is small and focused, so
    // the easy outputs discharge instantly and only the genuinely-hard cone is
    // ever the bottleneck. Opt-in via LEC_DECOMP; on the first SAT/unknown we fall
    // through to the monolithic solve so the existing witness path is unchanged.
    if (lec_decompose_try(opts.decompose) || std::getenv("LEC_DECOMP") != nullptr) {
      bool all_unsat = true;
      for (const auto& [dn, dt] : decomp_diffs) {
        solver.push();
        solver.assertFormula(dt);
        cvc5::Result dr = solver.checkSat();
        solver.pop();
        if (!dr.isUnsat()) {
          all_unsat = false;  // SAT or unknown -> let the monolithic solve decide + build the witness
          break;
        }
      }
      if (all_unsat) {
        if (incomplete) {
          res.verdict  = Verdict::Unknown;
          res.detail  += "; matched portion EQUIVALENT (only the unmatched cut points above remain)";
        } else {
          res.verdict = Verdict::Proven;
        }
        return res;
      }
      if (!lec_decompose_fallback(opts.decompose)) {
        // decompose=true (diagnostic): a cut did not discharge; do NOT spend time on
        // the monolithic solve (this mode exists to isolate the hard cone fast).
        res.verdict  = Verdict::Unknown;
        res.detail  += "; decomposed: a cut did not discharge (monolithic skipped, decompose=true)";
        return res;
      }
      // auto: fall through to the monolithic solve for a definitive verdict + witness.
    }
    solver.assertFormula(bad);
    cvc5::Result r = solver.checkSat();
    if (r.isUnsat()) {
      if (incomplete) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; matched portion EQUIVALENT (only the unmatched cut points above remain)";
      } else {
        res.verdict = Verdict::Proven;
      }
    } else if (r.isSat()) {
      // A concrete reachable divergence is a genuine refutation only when the
      // correspondence is complete; with unmatched cut points the divergence can
      // be an artifact of a one-sided shared-box symbol, so report Unknown (the
      // witness below still surfaces the divergence for iteration).
      if (incomplete) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; matched portion DIFFERS (witness below; may be an artifact of the unmatched cut points)";
      } else {
        res.verdict = Verdict::Refuted;
      }
      if (std::getenv("LEC_DUMP_WIT") != nullptr) {
        for (const auto& [nm, t] : dbg_init) {
          cvc5::Term vv = solver.getValue(t);
          if (!vv.isNull()) {
            std::fprintf(stderr, "[LEC_WIT] %s = %s\n", nm.c_str(), vv.getBitVectorValue(10).c_str());
          }
        }
        for (const auto& wi : wit_ins) {
          cvc5::Term vv = solver.getValue(wi.t);
          if (!vv.isNull() && vv.getBitVectorValue(10) != "0") {
            std::fprintf(stderr, "[LEC_IN] cyc=%d %s = %s\n", wi.cyc, wi.name.c_str(), vv.getBitVectorValue(10).c_str());
          }
        }
        for (const auto& o : wit_outs) {
          cvc5::Term rval = solver.getValue(o.rv);
          cvc5::Term ival = solver.getValue(o.iv);
          if (rval.isNull() || ival.isNull()) {
            continue;
          }
          auto rs = rval.getBitVectorValue(10);
          auto is = ival.getBitVectorValue(10);
          if (o.cyc == 4 || rs != is) {
            std::fprintf(stderr,
                         "[LEC_WIT] cyc=%d OUT %s ref=%s impl=%s%s\n",
                         o.cyc,
                         o.name.c_str(),
                         rs.c_str(),
                         is.c_str(),
                         rs != is ? "  <<DIFF" : "");
          }
        }
        for (const auto& d : dbg_ns) {
          cvc5::Term vv = solver.getValue(d.t);
          if (!vv.isNull()) {
            std::fprintf(stderr, "[LEC_NS] cyc=%d %c %s = %s\n", d.cyc, d.side, d.key.c_str(), vv.getBitVectorValue(10).c_str());
          }
        }
      }
      // Build the counterexample: the EARLIEST checked step that diverges (the
      // origin of the bug — later steps just inherit it), the diverging outputs
      // there (ref vs impl decimal values), and the primary-input trace driving
      // them up to that step. This is what `lhd lec` surfaces so the user can
      // localize the mismatch and replay it.
      auto join_ordered = [](const std::vector<std::string>& toks, size_t cap) {
        std::string out;
        size_t      shown = std::min(cap, toks.size());
        for (size_t i = 0; i < shown; ++i) {
          if (!out.empty()) {
            out += ", ";
          }
          out += toks[i];
        }
        if (toks.size() > cap) {
          out += ", (+" + std::to_string(toks.size() - cap) + " more)";
        }
        return out;
      };
      int first_bad = -1;
      for (const auto& o : wit_outs) {
        cvc5::Term rval = solver.getValue(o.rv);
        cvc5::Term ival = solver.getValue(o.iv);
        if (rval.isNull() || ival.isNull()) {
          continue;
        }
        if (rval.getBitVectorValue(10) != ival.getBitVectorValue(10) && (first_bad < 0 || o.cyc < first_bad)) {
          first_bad = o.cyc;
        }
      }
      if (first_bad >= 0) {
        std::vector<std::string> dtoks;
        for (const auto& o : wit_outs) {
          if (o.cyc != first_bad) {
            continue;
          }
          cvc5::Term rval = solver.getValue(o.rv);
          cvc5::Term ival = solver.getValue(o.iv);
          if (rval.isNull() || ival.isNull()) {
            continue;
          }
          auto rs = rval.getBitVectorValue(10);
          auto is = ival.getBitVectorValue(10);
          if (rs != is) {
            dtoks.push_back(display_name(o.name) + "(ref=" + rs + " impl=" + is + ")");
          }
        }
        std::sort(dtoks.begin(), dtoks.end());
        std::vector<std::string> itoks;
        for (const auto& in : wit_ins) {
          if (in.cyc > first_bad) {
            continue;  // trace only up to the first diverging step
          }
          cvc5::Term v = solver.getValue(in.t);
          if (v.isNull()) {
            continue;
          }
          std::string lbl
              = in.cyc < reset_hold ? ("rst" + std::to_string(in.cyc)) : ("s" + std::to_string(in.cyc - reset_hold + 1));
          itoks.push_back(lbl + "." + in.name + "=" + v.getBitVectorValue(10));
        }
        std::sort(itoks.begin(), itoks.end());
        // Stage-0: the EARLIEST diverging internal STATE cut — the ROOT a diverging
        // output merely inherits. A state computed at cycle C is the value going
        // INTO cycle C+1; report it as the root when it diverges no later than the
        // inherited output (state_bad+1 <= first_bad). Pure getValue, no new terms.
        int         state_bad = -1;
        std::string state_tok;
        for (const auto& s : wit_state) {
          if (state_bad >= 0 && s.cyc >= state_bad) {
            continue;  // can't beat the current earliest
          }
          cvc5::Term rval = solver.getValue(s.rv);
          cvc5::Term ival = solver.getValue(s.iv);
          if (rval.isNull() || ival.isNull()) {
            continue;
          }
          auto rs = rval.getBitVectorValue(10);
          auto is = ival.getBitVectorValue(10);
          if (rs != is) {
            state_bad = s.cyc;
            state_tok = display_name(s.key) + "(ref=" + rs + " impl=" + is + ")";
          }
        }
        std::string root;
        if (state_bad >= 0 && state_bad + 1 <= first_bad) {
          // wit_state iterates a hash map, so state_tok is an ARBITRARY member of
          // the earliest diverging cycle's cut set — collect ALL cuts diverging at
          // that same cycle (sorted) so the witness names the true root set, not a
          // hash-order pick (a single wide-fanin bug diverges many flops at once).
          std::vector<std::string> rtoks;
          for (const auto& s : wit_state) {
            if (s.cyc != state_bad) {
              continue;
            }
            cvc5::Term rval = solver.getValue(s.rv);
            cvc5::Term ival = solver.getValue(s.iv);
            if (rval.isNull() || ival.isNull()) {
              continue;
            }
            auto rs = rval.getBitVectorValue(10);
            auto is = ival.getBitVectorValue(10);
            if (rs != is) {
              rtoks.push_back(display_name(s.key) + "(ref=" + rs + " impl=" + is + ")");
            }
          }
          std::sort(rtoks.begin(), rtoks.end());
          std::string slbl
              = state_bad < reset_hold ? ("rst" + std::to_string(state_bad)) : ("s" + std::to_string(state_bad - reset_hold + 1));
          root = "earliest diverging STATE cut(s) (root): " + join_ordered(rtoks, 16) + " — state after step " + slbl
               + " (the diverging output inherits it); ";
        }
        res.witness = root + "first divergence at checked step " + std::to_string(first_bad - reset_hold + 1) + "/"
                    + std::to_string(N) + ": " + join_ordered(dtoks, 32);
        if (!itoks.empty()) {
          res.witness += " @ inputs " + join_ordered(itoks, 64);
        }
        // Structured, UNCAPPED reproduction trace (the display witness above caps
        // its input tokens): every primary input's value for each cycle up to the
        // first divergence, grouped by cycle, so `lhd lec` can regenerate the exact
        // driving sequence into a Pyrope testbench (lecfail.prp). Same getValue
        // reads as the display witness — read-only on already-solved terms.
        res.trace.reset_cycles   = reset_hold;
        res.trace.diverge_cycle  = first_bad;
        res.trace.diverge_outputs = dtoks;
        res.trace.cycles.assign(static_cast<size_t>(first_bad) + 1, Witness_cycle{});
        for (int c = 0; c <= first_bad; ++c) {
          res.trace.cycles[static_cast<size_t>(c)].reset_asserted = c < reset_hold;
        }
        for (const auto& in : wit_ins) {
          if (in.cyc < 0 || in.cyc > first_bad) {
            continue;
          }
          cvc5::Term v = solver.getValue(in.t);
          if (v.isNull()) {
            continue;
          }
          Witness_in wi;
          wi.name  = in.name;
          wi.value = v.getBitVectorValue(10);
          wi.width = static_cast<int>(in.t.getSort().getBitVectorSize());
          res.trace.cycles[static_cast<size_t>(in.cyc)].inputs.push_back(std::move(wi));
        }
      }
    } else {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; checkSat returned unknown";
      if (opts.timeout > 0) {
        res.detail += " (hit lec.timeout=" + std::to_string(opts.timeout) + "s; raise --set lec.timeout)";
      }
    }
    return res;
  }

  // Shared primary inputs: one symbolic BV per input name (union of both
  // designs). Both encodings reuse these terms, so "equal inputs" is structural
  // (the miter constrains nothing on the inputs -> they range freely). The symbol
  // is built at the MAX real-width seen across the two designs: the readers can
  // disagree on a bus's width by a sign-bit slot (the "bit-width trap"), and
  // sharing at the max keeps the extra bit FREE (sound) — sharing at the min
  // would constrain it (false PROVE), truncating it in the encoder drops it
  // (false REFUTE, e.g. an input 0x80 -> 0).
  Io_name_map<Val> shared;
  auto                                  add_inputs = [&](hhds::Graph* g) {
    auto gio = g->get_io();
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(d.name);
      int  w   = real_width_io(pin, *gio, d.name);
      if (w == 0) {
        w = 1;
      }
      if (auto it = shared.find(d.name); it != shared.end() && it->second.width >= w) {
        continue;  // already have an equal-or-wider shared symbol
      }
      bool sgn       = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
      cvc5::Term t   = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), d.name);
      shared[d.name] = Val{t, w, sgn};
    }
  };
  add_inputs(ref);
  add_inputs(impl);

  // Shared current-state symbols: one symbolic BV per corresponding flop, keyed
  // by its cross-design state key (source span preferred). Both encodings reuse
  // these, so the miter assumes equal current state and proves equal next state
  // + outputs (the M2 register-correspondence inductive step).
  auto add_flops = [&](hhds::Graph* g) {
    for (auto node : g->forward_hier()) {  // descend hierarchy: cut flops at every level
      if (graph_util::type_op_of(node) != Ntype_op::Flop) {
        continue;
      }
      auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      std::string key = eff(node.get_hier_name());  // hier correspondence key (matches encode())
      int         w   = real_width(q);
      if (w == 0) {
        w = 1;
      }
      // Keep the NARROWER decl (min width across designs — the opposite of the
      // input rule above): the shared current-state symbol is the VALUE both
      // flops hold, and the wider side (cgen's spare-sign-bit reg) EXTENDS it
      // in seed_state per this sign. Sharing at the max instead leaves the
      // wide side's headroom bit FREE with no narrow-side counterpart — an
      // assumed current state the narrow design can never mirror, so the step
      // spuriously diverges wherever control reads the flop unmasked (see
      // tests/equiv/flop_init_headroom). The step's next-state comparison
      // fits both sides to the max width, so the proven invariant is exactly
      // wide' == extend(narrow') — a REAL width divergence still refutes.
      if (auto it = shared.find(key); it != shared.end() && it->second.width <= w) {
        continue;
      }
      bool       sgn = !graph_util::is_unsign(q);
      cvc5::Term t   = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), key);
      shared[key]    = Val{t, w, sgn};
    }
  };
  {
    // Skip the flops INSIDE a collapsed leaf — its state is the box's one cut,
    // not a set of internal flop cuts.
    hhds::Hier_opaque_scope sc(collapse_gids_ptr);
    add_flops(ref);
    add_flops(impl);
  }
  // One shared current-state symbol per STATEFUL collapsed leaf (the box's state
  // cut), corresponding on both designs: the inductive miter assumes it equal and
  // proves the box's next-state (UF) equal alongside the parent's.
  for (const auto& [bk, box] : state_boxes) {
    std::string state_key = std::string("\x01") + "leafstate:" + bk;
    shared[state_key]     = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(box.state_w)), state_key), box.state_w, false};
  }

  // Shared current-state memory arrays (M4 cut): corresponding memories collapse
  // to one array symbol, so the step proves equal next-state contents + douts.
  Io_name_map<cvc5::Term> shared_mems = build_shared_mems("s_");

  // ── Memory <-> flop-bank correspondence bridge ────────────────────────────
  // A behavioral memory (one Memory cell / SMT array) on one side often appears
  // as a synthesized BANK of identically-shaped flops "<base>_0..<base>_{N-1}" on
  // the other — the register file is the canonical case (Pyrope lowers a dynamic
  // `reg regs:[32]u64` to a Memory; firtool flattens RegisterFile to 32 flops
  // `registers.regs_N`). The inductive flop-cut miter can't match a flop against an
  // array, so when one side has an unmatched Memory of size N and the other an
  // unmatched COMPLETE bank of N same-width flops, collapse the bank onto the
  // array: every flop_i shares select(A, i) as current state, and the post-cycle
  // miter compares flop_i's next state against select(A_next, i). `bridges` is
  // resolved here (current-state wiring) and consumed after encode (next-state).
  struct Bridge {
    std::string              mem_key;     // shared-array cut key
    int                      addr_w = 1;  // index width
    int                      bits   = 0;  // array element width
    bool                     mem_in_impl = true;  // which design owns the Memory
    std::vector<std::string> flop_keys;   // canon flop key per index 0..N-1
    std::vector<int>         flop_w;       // real width per index
    std::vector<bool>        flop_sgn;
  };
  std::vector<Bridge> bridges;
  {
    struct FlopRec {
      int  w;
      bool sgn;
    };
    auto collect_flops = [&](hhds::Graph* g) {
      Io_name_map<FlopRec> out;
      for (auto node : g->forward_hier()) {
        if (graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto q = node.get_driver_pin(0);
        if (q.is_invalid()) {
          continue;
        }
        int w = real_width(q);
        if (w == 0) {
          w = 1;
        }
        out[eff(node.get_hier_name())] = FlopRec{w, !graph_util::is_unsign(q)};
      }
      return out;
    };
    struct MemRec {
      Mem_sig sig;
    };
    auto collect_mems = [&](hhds::Graph* g) {
      Io_name_map<MemRec> out;
      Io_name_map<int>    occ;
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
          continue;
        }
        Mem_sig sig = read_mem_sig(node);
        if (sig.bits <= 0 || sig.size <= 0) {
          continue;
        }
        std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits) + ":r" + std::to_string(sig.n_rd) + "w"
                       + std::to_string(sig.n_wr);
        std::string key = mem_state_key(sig, occ[sg]++);
        out[key]        = MemRec{sig};
      }
      return out;
    };
    auto ref_flops  = collect_flops(ref);
    auto impl_flops = collect_flops(impl);
    auto ref_mems   = collect_mems(ref);
    auto impl_mems  = collect_mems(impl);

    // Banks among the flops that have NO counterpart on the other side: group keys
    // by stripping a trailing "_<idx>"; a base with a contiguous 0..N-1 of uniform
    // width is a bank candidate.
    auto detect_banks = [](const Io_name_map<FlopRec>& flops, const Io_name_map<FlopRec>& other) {
      struct Elt {
        std::string key;
        int         w;
        bool        sgn;
      };
      absl::flat_hash_map<std::string, absl::flat_hash_map<int, Elt>> by_base;
      for (const auto& [key, rec] : flops) {
        if (other.count(key)) {
          continue;  // already matches a flop on the other side -> not a bridge bank
        }
        auto us = key.rfind('_');
        if (us == std::string::npos || us + 1 >= key.size()) {
          continue;
        }
        std::string idx = key.substr(us + 1);
        if (idx.empty() || !std::all_of(idx.begin(), idx.end(), [](unsigned char c) { return std::isdigit(c); })) {
          continue;
        }
        by_base[key.substr(0, us)][std::stoi(idx)] = Elt{key, rec.w, rec.sgn};
      }
      struct Bank {
        int                      n;
        int                      w;
        bool                     sgn;
        std::vector<std::string> keys;
      };
      std::vector<Bank> banks;
      for (auto& [base, elts] : by_base) {
        int  n        = static_cast<int>(elts.size());
        bool complete = n > 1;
        int  w        = elts.begin()->second.w;
        bool sgn      = elts.begin()->second.sgn;
        for (int i = 0; i < n; ++i) {
          auto it = elts.find(i);
          if (it == elts.end() || it->second.w != w) {
            complete = false;
            break;
          }
        }
        if (!complete) {
          continue;
        }
        Bank b{n, w, sgn, {}};
        for (int i = 0; i < n; ++i) {
          b.keys.push_back(elts.at(i).key);
        }
        banks.push_back(std::move(b));
      }
      return banks;
    };

    // Pair a bank (one side) with an unmatched Memory of equal size on the other.
    auto pair_side = [&](const Io_name_map<FlopRec>& bank_flops,
                         const Io_name_map<FlopRec>& other_flops,
                         const Io_name_map<MemRec>&  bank_mems,
                         const Io_name_map<MemRec>&  mem_mems,
                         bool                        mem_in_impl) {
      for (auto& b : detect_banks(bank_flops, other_flops)) {
        for (const auto& [mkey, mrec] : mem_mems) {
          if (bank_mems.count(mkey)) {
            continue;  // memory matches a memory on the bank side -> not a bridge
          }
          if (mrec.sig.size != b.n) {
            continue;
          }
          if (!shared_mems.count(mkey)) {
            continue;
          }
          Bridge br;
          br.mem_key     = mkey;
          br.addr_w      = mrec.sig.addr_w;
          br.bits        = mrec.sig.bits;
          br.mem_in_impl = mem_in_impl;
          br.flop_keys   = b.keys;
          br.flop_w.assign(b.keys.size(), b.w);
          br.flop_sgn.assign(b.keys.size(), b.sgn);
          bridges.push_back(std::move(br));
          break;  // one bank -> one memory
        }
      }
    };
    pair_side(ref_flops, impl_flops, ref_mems, impl_mems, /*mem_in_impl=*/true);   // ref bank <-> impl memory
    pair_side(impl_flops, ref_flops, impl_mems, ref_mems, /*mem_in_impl=*/false);  // impl bank <-> ref memory

    // Wire each bank flop's CURRENT state to select(A, i) so the two designs start
    // the step from the same register-file contents.
    for (auto& br : bridges) {
      cvc5::Term A = shared_mems.at(br.mem_key);
      for (size_t i = 0; i < br.flop_keys.size(); ++i) {
        cvc5::Term sel
            = tm.mkTerm(cvc5::Kind::SELECT, {A, tm.mkBitVector(static_cast<uint32_t>(br.addr_w), static_cast<uint64_t>(i))});
        Val        cur = Val{fit_to(tm, Val{sel, br.bits, false}, br.flop_w[i]), br.flop_w[i], br.flop_sgn[i]};
        shared[br.flop_keys[i]] = cur;
      }
    }
  }

  Encoder enc(tm);
  enc.set_sub_lib(sub_lib);
  enc.set_name_alias(&name_alias);
  enc.set_collapse_defs(collapse_ptr);
  enc.set_state_boxes(state_boxes_ptr);
  enc.set_comb_boxes(comb_boxes_ptr);
  enc.set_shared_bbox(&shared_bbox);
  enc.set_x_dontcare(opts.gold_x != "zero");  // ref X = don't-care (lec.gold_x)
  enc.set_box_keys(&ref_box_keys);            // per-design box correspondence
  Encoded re = enc.encode(ref, &shared, "", &shared_mems);
  enc.set_x_dontcare(false);
  if (!re.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; ref encode failed: " + re.error;
    return res;
  }
  enc.set_box_keys(&impl_box_keys);
  Encoded ie = enc.encode(impl, &shared, "", &shared_mems);
  if (!ie.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; impl encode failed: " + ie.error;
    return res;
  }
  // Tie deferred memory-read douts to their select(array, addr) values.
  for (const auto& [l, r] : re.equalities) {
    solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
  }
  for (const auto& [l, r] : ie.equalities) {
    solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
  }

  // ── Bridge miter: bank-flop next state vs select(memory_next, i) ──────────
  // Consume the memory<->flop-bank bridges resolved above. The bank-flop next
  // states and the paired memory's next array are pulled from whichever design
  // owns each, and these keys are recorded so the generic miter below does NOT
  // re-report them as unmatched cut points.
  absl::flat_hash_set<std::string> bridged_ref_out, bridged_impl_out, bridged_ref_mem, bridged_impl_mem;
  std::vector<cvc5::Term>          bridge_diffs;
  for (const auto& br : bridges) {
    const Encoded& bank_enc = br.mem_in_impl ? re : ie;  // design holding the flop bank
    const Encoded& mem_enc  = br.mem_in_impl ? ie : re;  // design holding the Memory
    auto&          bank_out_set = br.mem_in_impl ? bridged_ref_out : bridged_impl_out;
    auto&          mem_set      = br.mem_in_impl ? bridged_impl_mem : bridged_ref_mem;
    auto           mem_it       = mem_enc.next_mem.find(br.mem_key);
    if (mem_it == mem_enc.next_mem.end()) {
      continue;  // memory had no next-state (shouldn't happen) -> leave unmatched
    }
    mem_set.insert(br.mem_key);
    for (size_t i = 0; i < br.flop_keys.size(); ++i) {
      std::string nxt_key = std::string("\x01nxt:") + br.flop_keys[i];
      bank_out_set.insert(nxt_key);
      auto fit_it = bank_enc.outputs.find(nxt_key);
      if (fit_it == bank_enc.outputs.end()) {
        continue;
      }
      cvc5::Term sel = tm.mkTerm(cvc5::Kind::SELECT,
                                 {mem_it->second, tm.mkBitVector(static_cast<uint32_t>(br.addr_w), static_cast<uint64_t>(i))});
      int        w   = std::max(br.flop_w[i], br.bits);
      cvc5::Term bnk = fit_to(tm, fit_it->second, w);
      cvc5::Term mem = fit_to(tm, Val{sel, br.bits, false}, w);
      bridge_diffs.push_back(tm.mkTerm(cvc5::Kind::DISTINCT, {bnk, mem}));
    }
  }

  // ── Structural correspondence + miter over the COMMON outputs ─────────────
  // Rather than bail on the FIRST unmatched cut point (which made `lhd lec`
  // un-iterable when two designs don't yet expose the same state set), collect
  // ALL unmatched outputs / memories on each side and STILL miter the ones that
  // DO correspond. A non-empty unmatched set blocks a definitive Proven (the
  // correspondence is incomplete), but the matched-portion result + per-output
  // divergences are exactly the signal needed to drive the design — and the LEC's
  // own cut-correspondence — toward agreement.
  cvc5::Term bad;
  std::vector<std::pair<std::string, cvc5::Term>> ind_diffs;  // per-cut diffs for decomposed proof
  for (const auto& t : bridge_diffs) {  // bank<->memory next-state equalities
    bad = bad.isNull() ? t : tm.mkTerm(cvc5::Kind::OR, {bad, t});
    ind_diffs.push_back({"bridge", t});
  }
  for (const auto& [name, rv] : re.outputs) {
    if (!name.empty() && name[0] == '\x03') {
      continue;  // env-gated debug tap, never a compare point
    }
    if (bridged_ref_out.count(name)) {
      continue;  // bank-flop next state -> compared via the memory bridge above
    }
    auto it = ie.outputs.find(name);
    if (it == ie.outputs.end()) {
      res.unmatched_ref.push_back(display_name(name));
      continue;
    }
    if (name.rfind("\x02"
                   "bbox:",
                   0)
        == 0) {
      continue;  // two-sided box presence marker: constant on both sides, never a diff
    }
    int        w    = std::max(rv.width, it->second.width);
    cvc5::Term rfit = fit_to(tm, rv, w);
    cvc5::Term ifit = fit_to(tm, it->second, w);
    if (cvc5::Term u = fit_x_mask_to(tm, rv, w); !u.isNull()) {
      // ref X = don't-care (lec.gold_x=ignore): exclude ref-unknown bits. The
      // shared current-state hypothesis already binds ref X-state to the
      // impl's value, so this masks exactly the ref-side don't-care choices.
      cvc5::Term keep = tm.mkTerm(cvc5::Kind::BITVECTOR_NOT, {u});
      rfit            = tm.mkTerm(cvc5::Kind::BITVECTOR_AND, {rfit, keep});
      ifit            = tm.mkTerm(cvc5::Kind::BITVECTOR_AND, {ifit, keep});
    }
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rfit, ifit});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
    ind_diffs.push_back({display_name(name), diff});
  }
  for (const auto& [name, iv] : ie.outputs) {
    if (!name.empty() && name[0] == '\x03') {
      continue;  // env-gated debug tap, never a compare point
    }
    if (bridged_impl_out.count(name)) {
      continue;
    }
    if (!re.outputs.count(name)) {
      res.unmatched_impl.push_back(display_name(name));
    }
  }
  // Memory next-state contents: corresponding memories must hold equal contents
  // after the step (DISTINCT over array sort). A memory present on only one side
  // is an unmatched cut — unless it was paired with a flop bank above.
  for (const auto& [key, rmem] : re.next_mem) {
    if (bridged_ref_mem.count(key)) {
      continue;
    }
    auto it = ie.next_mem.find(key);
    if (it == ie.next_mem.end()) {
      res.unmatched_ref.push_back("mem:" + display_name(key));
      continue;
    }
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rmem, it->second});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
    ind_diffs.push_back({"mem:" + display_name(key), diff});
  }
  for (const auto& [key, imem] : ie.next_mem) {
    if (bridged_impl_mem.count(key)) {
      continue;
    }
    if (!re.next_mem.count(key)) {
      res.unmatched_impl.push_back("mem:" + display_name(key));
    }
  }

  const bool incomplete = !res.unmatched_ref.empty() || !res.unmatched_impl.empty();
  if (!res.unmatched_ref.empty()) {
    res.detail
        += "; " + std::to_string(res.unmatched_ref.size()) + " ref-only cut point(s) {" + join_capped(res.unmatched_ref) + "}";
  }
  if (!res.unmatched_impl.empty()) {
    res.detail
        += "; " + std::to_string(res.unmatched_impl.size()) + " impl-only cut point(s) {" + join_capped(res.unmatched_impl) + "}";
  }

  if (bad.isNull()) {
    // Nothing common to compare. Vacuously equal only if the sets also fully
    // matched (both empty); otherwise the comparison is empty AND incomplete.
    if (incomplete) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; no COMMON outputs to compare (correspondence incomplete)";
    } else {
      res.verdict  = Verdict::Proven;
      res.detail  += "; no comparable outputs";
    }
    return res;
  }

  // Witness = diverging COMMON outputs + the satisfying assignment. Built on every
  // SAT (useful for iteration even when the verdict is gated to Unknown by an
  // incomplete correspondence). Collect-then-sort: `re.outputs`/`shared` are
  // flat_hash_maps with run-varying order, which would make the text irreproducible.
  auto build_witness = [&]() -> std::string {
    if (!opts.witness) {
      return {};
    }
    std::vector<std::string> diff_toks;
    for (const auto& [name, rv] : re.outputs) {
      auto it = ie.outputs.find(name);
      if (it == ie.outputs.end()) {
        continue;
      }
      int        w    = std::max(rv.width, it->second.width);
      cvc5::Term rval = solver.getValue(fit_to(tm, rv, w));
      cvc5::Term ival = solver.getValue(fit_to(tm, it->second, w));
      if (!rval.isNull() && !ival.isNull() && rval.getBitVectorValue(10) != ival.getBitVectorValue(10)) {
        diff_toks.push_back(display_name(name) + "(ref=" + rval.getBitVectorValue(10) + " impl=" + ival.getBitVectorValue(10)
                            + ")");
      }
    }
    std::sort(diff_toks.begin(), diff_toks.end());
    std::string diffs;
    for (const auto& t : diff_toks) {
      if (!diffs.empty()) {
        diffs += ", ";
      }
      diffs += t;
    }
    std::vector<std::string> toks;
    for (const auto& [name, v] : shared) {
      cvc5::Term val = solver.getValue(v.term);
      if (val.isNull()) {
        continue;
      }
      toks.push_back(display_name(name) + "=" + val.getBitVectorValue(10));
    }
    std::sort(toks.begin(), toks.end());
    std::string w;
    for (const auto& t : toks) {
      if (!w.empty()) {
        w += ", ";
      }
      w += t;
    }
    return (diffs.empty() ? "" : "diff " + diffs + " @ ") + w;
  };

  // ── Input-space case-split cube sweep (lec.partitions / lec.split) ─────────
  // run_case_split forks N workers and hands each a disjoint slice of the control
  // input's values via opts._split_values. Here (inside one worker) we prove `bad`
  // UNSAT under sel==v for every assigned v: substituting the constant folds every
  // control-dependent wide operator (a variable barrel shift becomes a static
  // slice), so each cube is a trivial query. Combinational only (run_case_split
  // gates on it), so no unreachable-state concern — a SAT cube is a genuine CEX.
  if (!opts._split_values.empty()) {
    if (auto sit = shared.find(opts._split_name); sit != shared.end()) {
      cvc5::Term sel    = sit->second.term;
      uint32_t   sel_w  = static_cast<uint32_t>(sit->second.width);
      int        proven = 0, unknown = 0;
      for (uint64_t v : opts._split_values) {
        cvc5::Term vc = tm.mkBitVector(sel_w, v);  // mkBitVector takes v mod 2^sel_w
        solver.push();
        solver.assertFormula(bad.substitute(sel, vc));                  // folded miter
        solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {sel, vc}));  // pin sel so the witness reads it
        cvc5::Result cr = solver.checkSat();
        if (cr.isSat()) {
          res.witness  = build_witness();
          solver.pop();
          res.detail  += "; case-split " + opts._split_name + "=" + std::to_string(v) + " DIFFERS";
          res.verdict  = incomplete ? Verdict::Unknown : Verdict::Refuted;
          return res;
        }
        if (cr.isUnsat()) {
          ++proven;
        } else {
          ++unknown;
        }
        solver.pop();
      }
      res.detail += "; case-split " + opts._split_name + "[" + std::to_string(sel_w) + "b] " + std::to_string(proven) + "/"
                  + std::to_string(opts._split_values.size()) + " cubes UNSAT"
                  + (unknown ? (", " + std::to_string(unknown) + " unknown") : std::string{});
      if (unknown > 0) {
        res.verdict = Verdict::Unknown;
        return res;
      }
      res.verdict = incomplete ? Verdict::Unknown : Verdict::Proven;
      return res;
    }
    // sel symbol absent (should not happen): fall through to the monolithic solve.
  }

  // Combinational / inductive miter: UNSAT iff the designs agree on every COMMON
  // output for all inputs (and assumed-equal current state). The monolithic OR of
  // ~100 next-state + output diffs is a single huge query that cvc5's eager
  // bitblast chokes on (UNKNOWN) for a complex dual-issue pipeline. Proving each
  // cut's diff UNSAT independently is the SAME proof (OR UNSAT iff every disjunct
  // is) but each query is a small, focused combinational cone, so the easy cuts
  // discharge instantly and only the genuinely-hard ones (the branch/forwarding
  // cone) are ever the bottleneck. Opt-in via LEC_DECOMP; any SAT/unknown falls
  // through to the monolithic solve so the witness path is unchanged.
  if ((lec_decompose_try(opts.decompose) || std::getenv("LEC_DECOMP") != nullptr) && !ind_diffs.empty()) {
    int                      proven  = 0;
    bool                     any_sat = false;
    std::vector<std::string> hard;
    for (const auto& [dn, dt] : ind_diffs) {
      solver.push();
      solver.assertFormula(dt);
      cvc5::Result dr = solver.checkSat();
      solver.pop();
      if (dr.isUnsat()) {
        ++proven;
      } else {
        hard.push_back(dn);  // SAT (real diff) or unknown (cvc5 too weak for this cone)
        if (dr.isSat()) {
          any_sat = true;
        }
      }
      if (std::getenv("LEC_DECOMP_LOG") != nullptr) {
        std::fprintf(stderr, "[LEC_CUT] %-44s %s\n", dn.c_str(), dr.isUnsat() ? "PROVEN" : (dr.isSat() ? "DIFF" : "unknown"));
      }
    }
    if (hard.empty() && !incomplete) {
      res.verdict  = Verdict::Proven;
      res.detail  += "; decomposed (" + std::to_string(proven) + " cuts each UNSAT)";
      return res;
    }
    res.detail += "; decomposed: " + std::to_string(proven) + "/" + std::to_string(ind_diffs.size()) + " cuts PROVEN"
                + (hard.empty() ? std::string{} : ", " + std::to_string(hard.size()) + " unresolved {" + join_capped(hard) + "}");
    if (!lec_decompose_fallback(opts.decompose)) {
      // decompose=true (diagnostic): report the per-cut residue, do NOT run the
      // monolithic solve. (proven==0 && complete learned nothing -> fall through.)
      if (proven > 0 || incomplete) {
        res.verdict = Verdict::Unknown;
        return res;
      }
    } else if (!any_sat && !hard.empty()) {
      // auto, but every residual cut was UNKNOWN (cvc5 too weak): the larger
      // monolithic OR is only harder, so skip it and report Unknown directly.
      res.verdict  = Verdict::Unknown;
      res.detail  += "; monolithic skipped (residual cuts UNKNOWN, larger miter no easier)";
      return res;
    }
    // auto with a SAT cut, or all cuts proven but the correspondence is incomplete
    // (a cheap monolithic UNSAT yields the right "matched portion" verdict): fall
    // through to the monolithic solve for the definitive verdict + witness.
  }
  solver.assertFormula(bad);
  cvc5::Result r = solver.checkSat();

  if (r.isUnsat()) {
    // COMMON outputs agree. Proven only if the correspondence is also complete.
    if (incomplete) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; matched portion EQUIVALENT (only the unmatched cut points above remain)";
    } else {
      res.verdict = Verdict::Proven;
    }
  } else if (r.isSat()) {
    res.witness = build_witness();
    // A divergence on a COMMON output is a genuine refutation when the
    // correspondence is complete. With unmatched cut points the engine cannot
    // attribute the divergence soundly (a matched ref output may read a ref-only
    // flop), so it reports Unknown but still surfaces the witness for iteration.
    if (incomplete) {
      res.verdict  = Verdict::Unknown;
      res.detail  += "; matched portion DIFFERS (witness below; may be an artifact of the unmatched cut points)";
    } else {
      res.verdict = Verdict::Refuted;
    }
  } else {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; checkSat returned unknown";
    if (opts.timeout > 0) {
      res.detail += " (hit lec.timeout=" + std::to_string(opts.timeout) + "s; raise --set lec.timeout)";
    }
  }
  return res;
}

Query_result prove_equal_isolated(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                                  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  int p[2];
  if (::pipe(p) != 0) {
    Lec_options fallback = opts;
    fallback.partitions  = 1;
    return safe_prove_equal(ref, impl, fallback, sub_lib);
  }
  pid_t c = ::fork();
  if (c < 0) {
    ::close(p[0]);
    ::close(p[1]);
    Lec_options fallback = opts;
    fallback.partitions  = 1;
    return safe_prove_equal(ref, impl, fallback, sub_lib);
  }
  if (c == 0) {
    ::close(p[0]);
    Lec_options worker      = opts;
    worker._isolated_worker = true;
    worker.partitions       = 1;
    Query_result r          = safe_prove_equal(ref, impl, worker, sub_lib);
    std::string  blob       = serialize_result(r);
    write_all(p[1], blob.data(), blob.size());
    ::close(p[1]);
    ::_exit(0);
  }
  ::close(p[1]);
  std::string blob;
  char        buf[8192];
  while (true) {
    ssize_t n = ::read(p[0], buf, sizeof(buf));
    if (n > 0) {
      blob.append(buf, static_cast<size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  ::close(p[0]);
  int status = 0;
  ::waitpid(c, &status, 0);
  Query_result out;
  if (!deserialize_result(blob, out)) {
    out.verdict = Verdict::Unknown;
    out.engine  = "isolated-worker";
    out.detail  = "isolated proof worker exited without a valid result";
  }
  return out;
}

// ── 2f-verify: single-design property BMC ───────────────────────────────────
// Mirrors the prove_equal bmc branch's unroll (reset detection, phases,
// fresh-var state pinning, bank hold) simplified to ONE design, with the solve
// restructured per the 2f-verify plan: instead of one monolithic OR + one
// checkSat, each fproperty obligation is checked per cycle as a retractable
// checkSatAssuming, and every proven obligation is immediately re-asserted as
// a fact (a "frontier assume" — an entailed fact, so it prunes the search for
// everything checked after it without ever masking a violation). A timeout
// therefore costs one obligation at one cycle, not the run. The single-design
// helpers below are deliberate simplified COPIES of prove_equal's in-function
// lambdas (no cross-design width unification / correspondence); sharing them
// is a follow-up refactor, not worth destabilizing prove_equal for.
namespace {

// "\x04prop:<occ>\x1f<kind>\x1f<loc>\x1f<msg>" (encoder set_emit_props contract;
// msg is last so it may contain anything).
struct Prop_key {
  int         occ = -1;
  std::string kind{"assert"};
  std::string loc, msg;
};
std::optional<Prop_key> parse_prop_key(std::string_view name) {
  constexpr std::string_view pfx{"\x04prop:", 6};
  if (name.substr(0, pfx.size()) != pfx) {
    return std::nullopt;
  }
  Prop_key    k;
  std::string_view rest = name.substr(pfx.size());
  auto        take = [&rest]() -> std::string_view {
    auto p = rest.find('\x1f');
    auto f = rest.substr(0, p);
    rest   = p == std::string_view::npos ? std::string_view{} : rest.substr(p + 1);
    return f;
  };
  auto occ_s = take();
  k.occ      = 0;
  for (char c : occ_s) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    k.occ = k.occ * 10 + (c - '0');
  }
  if (auto f = take(); !f.empty()) {
    k.kind = std::string(f);
  }
  k.loc = std::string(take());
  k.msg = std::string(rest);  // remainder: the user message, verbatim
  return k;
}

// Rule-F verify cache key: hash the exact asserted SMT encoding accumulated for
// this cycle plus the obligation term. This is deliberately downstream of the
// encoder; any semantic encoding change changes the serialized terms, while the
// cache-wide formal source salt handles changes that preserve their spelling.
std::string verify_obligation_key(const std::vector<cvc5::Term>& assertions, const cvc5::Term& bad, const Lec_options& opts) {
  uint64_t h0  = 1469598103934665603ULL;
  uint64_t h1  = 1099511628211ULL ^ 0x9e3779b97f4a7c15ULL;
  auto     mix = [&](std::string_view s) {
    for (unsigned char c : s) {
      h0  = (h0 ^ c) * 1099511628211ULL;
      h1 ^= c + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2);
    }
    h0 = (h0 ^ 0xffU) * 1099511628211ULL;  // term boundary
  };
  mix("verify-obligation-v1");
  mix(opts.solver);
  mix(opts.phase);
  mix(opts.reset);
  mix(std::to_string(opts.reset_cycles));
  for (const auto& a : assertions) {
    mix(a.toString());
  }
  mix(bad.toString());
  return std::format("verify:{:016x}{:016x}", h0, h1);
}

}  // namespace

Verify_result prove_properties(hhds::Graph* design, const Lec_options& opts,
                               const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib, const std::vector<Monitor>* monitors) {
  const auto    t0 = std::chrono::steady_clock::now();
  Verify_result res;
  res.detail = "solver=cvc5 (bmc property engine, phase=" + opts.phase + ")";
  if (opts.solver != "cvc5") {
    res.detail += "; solver backend '" + opts.solver + "' not built for verify (cvc5 only)";
    return res;
  }
  if (opts.engine != "bmc") {
    res.detail += "; engine '" + opts.engine + "' not supported for verify (bmc only)";
    return res;
  }

  // `full` = the obligations must hold BOTH during reset and free-running:
  // run the two windows as independent sub-queries and merge per property
  // (walk order is deterministic, so props line up by index).
  if (opts.phase == "full") {
    Lec_options o1 = opts;
    o1.phase       = "just_reset";
    Lec_options o2 = opts;
    o2.phase       = "after_reset";
    Verify_result r1 = prove_properties(design, o1, sub_lib, monitors);
    Verify_result r2 = prove_properties(design, o2, sub_lib, monitors);
    Verify_result m  = std::move(r2);  // after_reset is the primary view
    m.detail = "full: just_reset AND after_reset; " + m.detail + " | just_reset: " + r1.detail;
    if (m.props.size() == r1.props.size()) {
      for (size_t i = 0; i < m.props.size(); ++i) {
        auto&       a = m.props[i];
        const auto& b = r1.props[i];
        if (b.verdict == Verdict::Refuted && a.verdict != Verdict::Refuted) {
          a = b;  // a during-reset violation dominates
        } else if (b.verdict == Verdict::Unknown && a.verdict == Verdict::Proven && a.kind != "assume") {
          a.verdict    = Verdict::Unknown;
          a.unknown_at = b.unknown_at;
        }
      }
    }
    m.vacuous = m.vacuous || r1.vacuous;
    if (r1.verdict == Verdict::Refuted || m.verdict == Verdict::Refuted) {
      m.verdict = Verdict::Refuted;
    } else if (r1.verdict == Verdict::Unknown || m.verdict == Verdict::Unknown) {
      m.verdict = Verdict::Unknown;
    }
    m.elapsed_ms = now_ms(t0);
    return m;
  }

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_ABV");
  if (opts.timeout > 0) {
    solver.setOption("tlimit-per", std::to_string(static_cast<long long>(opts.timeout) * 1000));
  }
  // Same solver-config rules as prove_equal: the eager internal bit-blaster is
  // the trustworthy config (the lazy default has a spurious-SAT history) but has
  // no array theory, so memory designs keep the default solver.
  bool has_mem = false;
  for (auto node : design->forward_class()) {
    if (graph_util::type_op_of(node) == Ntype_op::Memory) {
      has_mem = true;
      break;
    }
  }
  if (!has_mem) {
    solver.setOption("bv-solver", "bitblast-internal");
  }
  if (opts.witness) {
    solver.setOption("produce-models", "true");
  }
  std::vector<cvc5::Term> asserted;
  auto                    assert_formula = [&](const cvc5::Term& t) {
    solver.assertFormula(t);
    asserted.push_back(t);
  };

  // Blackbox presence: a Sub with no body that sub_lib cannot flatten becomes
  // free per-cycle output symbols — an over-approximation. Proven stays sound
  // (holds for ALL box behaviors); a Refuted may be an artifact of the free box,
  // so it is downgraded to Unknown below (the single-design analog of
  // prove_equal's incomplete-correspondence gate).
  bool has_bbox = false;
  for (auto node : design->forward_hier()) {
    if (graph_util::type_op_of(node) != Ntype_op::Sub || node.get_subnode_graph() != nullptr) {
      continue;
    }
    auto sio = node.get_subnode_io();
    if (sio != nullptr
        && (sio->get_name() == graph_util::fproperty_module_name || sio->get_name() == graph_util::lgassert_module_name)) {
      continue;  // property primitives, not real boxes
    }
    bool flattenable = false;
    if (sub_lib != nullptr) {
      if (auto git = sub_lib->find(node.get_subnode_gid()); git != sub_lib->end() && git->second != nullptr) {
        flattenable = true;
        for (auto dn : git->second->forward_class()) {
          auto dop = graph_util::type_op_of(dn);
          if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
            flattenable = false;
            break;
          }
        }
      }
    }
    if (!flattenable) {
      has_bbox = true;
      break;
    }
  }

  Encoder enc(tm);
  enc.set_sub_lib(sub_lib);
  enc.set_emit_props(true);

  // Primary inputs (one design).
  struct In {
    int  w;
    bool sgn;
  };
  Io_name_map<In> ins;
  {
    auto gio = design->get_io();
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = design->get_input_pin(d.name);
      int  w   = real_width_io(pin, *gio, d.name);
      if (w == 0) {
        w = 1;
      }
      bool sgn    = pin.is_invalid() ? !gio->is_unsign(d.name) : !graph_util::is_unsign(pin);
      ins[d.name] = In{w, sgn};
    }
  }

  // Reset detection (structural reset_pin inputs + canonical names, or the
  // authoritative lec.reset spec) — single-design copy of prove_equal's rules.
  Io_name_map<bool> reset_negset;
  auto reset_name_polarity = [](const std::string& nm, bool& negreset) -> bool {
    std::string lc = nm;
    for (auto& c : lc) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    bool   tok_match = false;
    size_t start     = 0;
    for (size_t i = 0; i <= lc.size(); ++i) {
      if (i == lc.size() || lc[i] == '_') {
        std::string tok = lc.substr(start, i - start);
        if (tok == "rst" || tok == "reset" || tok == "rstn" || tok == "resetn" || tok == "arst" || tok == "areset" || tok == "nrst"
            || tok == "nreset" || tok == "por") {
          tok_match = true;
        }
        start = i + 1;
      }
    }
    if (!tok_match) {
      return false;
    }
    auto ends = [&](std::string_view s) { return lc.size() >= s.size() && lc.compare(lc.size() - s.size(), s.size(), s) == 0; };
    negreset  = ends("_n") || ends("_ni") || ends("_n_i") || ends("_ni_i") || lc == "rstn" || lc == "resetn" || ends("nrst")
               || ends("nreset");
    return true;
  };
  const bool phase_reset = opts.phase == "just_reset";
  const bool phase_run   = opts.phase == "after_reset";
  if (phase_reset || phase_run) {
    if (!opts.reset.empty()) {
      std::string spec = opts.reset;
      size_t      p    = 0;
      while (p < spec.size()) {
        size_t      comma = spec.find(',', p);
        std::string tok   = spec.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
        p                 = comma == std::string::npos ? spec.size() : comma + 1;
        if (tok.empty()) {
          continue;
        }
        bool        negreset = false;
        std::string nm       = tok;
        if (auto colon = tok.find(':'); colon != std::string::npos) {
          nm       = tok.substr(0, colon);
          auto pol = tok.substr(colon + 1);
          negreset = (pol == "lo" || pol == "low" || pol == "n" || pol == "0");
        } else {
          reset_name_polarity(nm, negreset);
        }
        if (ins.count(nm)) {
          reset_negset[nm] = negreset;
        }
      }
    } else {
      // Structural async resets. Top-graph-only, like prove_equal's collect_resets:
      // a subgraph flop's reset driver is the SUBGRAPH's own input pin, and name-
      // matching its port against the top-level `ins` map could pin an unrelated
      // same-named top input (under-exploration -> unsound PROVEN).
      for (auto node : design->forward_hier()) {
        if (graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto rst_d = graph_util::get_driver_of_sink_name(node, "reset_pin");
        if (rst_d.is_invalid() || !graph_util::is_graph_input_pin(rst_d) || rst_d.get_graph() != design) {
          continue;
        }
        auto nm = std::string(graph_util::pin_name_of(rst_d));
        if (nm.empty() || !ins.count(nm)) {
          continue;
        }
        bool negreset = false;
        if (auto neg_d = graph_util::get_driver_of_sink_name(node, "negreset");
            !neg_d.is_invalid() && graph_util::is_const_pin(neg_d)) {
          negreset = !graph_util::hydrate_const(neg_d).is_known_false();
        }
        reset_negset[nm] = negreset;
      }
      for (const auto& [name, info] : ins) {  // canonical reset-named (sync) resets
        if (reset_negset.count(name)) {
          continue;
        }
        bool negreset = false;
        if (reset_name_polarity(name, negreset)) {
          reset_negset[name] = negreset;
        }
      }
    }
  }

  // Reset-hold prologue: reset_cycles extended to the pipeline flush latency
  // (same MAX-parallel / SUM-series flop-weighted metric as prove_equal).
  int reset_hold = phase_run ? (opts.reset_cycles > 0 ? opts.reset_cycles : 1) : 0;
  if (phase_run) {
    absl::flat_hash_map<std::string, int>        memo;
    absl::flat_hash_set<std::string>             on_stack;
    std::function<int(const hhds::Node_class&)> lat = [&](const hhds::Node_class& n) -> int {
      std::string id = n.get_hier_name();
      if (auto it = memo.find(id); it != memo.end()) {
        return it->second;
      }
      if (!on_stack.insert(id).second) {
        return 0;
      }
      int base = 0;
      for (auto e : n.inp_edges()) {
        if (graph_util::is_graph_input_pin(e.driver) || graph_util::is_const_pin(e.driver)) {
          continue;
        }
        base = std::max(base, lat(e.driver.get_master_node()));
      }
      on_stack.erase(id);
      int depth = 0;
      if (graph_util::type_op_of(n) == Ntype_op::Flop) {
        depth   = 1;
        auto pm = graph_util::get_driver_of_sink_name(n, "pipe_min");
        if (!pm.is_invalid() && graph_util::is_const_pin(pm)) {
          int d = static_cast<int>(graph_util::hydrate_const(pm).to_just_i64());
          if (d > 1) {
            depth = d;
          }
        }
      }
      int total = base + depth;
      memo[id]  = total;
      return total;
    };
    int flush = 0;
    for (auto node : design->forward_hier()) {
      flush = std::max(flush, lat(node));
    }
    if (flush > reset_hold) {
      reset_hold = flush;
    }
  }
  const int N         = opts.bound > 0 ? opts.bound : 6;
  const int total_cyc = N + reset_hold;
  res.checked_steps   = N;
  res.reset_hold      = reset_hold;

  // state[0]: reset init (just_reset / free_toreset) or fresh symbols with a
  // driven reset prologue (after_reset); reset-less register-file banks HOLD
  // across the prologue (same rule + rationale as prove_equal).
  Io_name_map<Val>                 state;
  absl::flat_hash_set<std::string> bank_hold_keys;
  {
    Io_name_map<int>  fw;
    Io_name_map<bool> fsgn;
    Io_name_map<Val>  init;
    for (auto node : design->forward_hier()) {
      if (graph_util::type_op_of(node) != Ntype_op::Flop) {
        continue;
      }
      auto q = node.get_driver_pin(0);
      if (q.is_invalid()) {
        continue;
      }
      auto key = canon_flop_name(node.get_hier_name());
      int  w   = real_width(q);
      if (w == 0) {
        w = 1;
      }
      if (auto it = fw.find(key); it == fw.end() || w > it->second) {
        fw[key]   = w;
        fsgn[key] = !graph_util::is_unsign(q);
      }
      if (!init.count(key)) {
        if (auto iv = flop_initial(tm, node, w)) {
          init[key] = *iv;
        }
      }
    }
    for (const auto& [key, w] : fw) {
      if (!phase_run && init.count(key)) {
        state[key] = init.at(key);
      } else {
        state[key] = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "s0_" + key), w, fsgn.at(key)};
      }
    }
    for (const auto& [key, w] : fw) {
      if (init.count(key)) {
        continue;
      }
      auto us = key.rfind('_');
      if (us == std::string::npos || us + 1 >= key.size()) {
        continue;
      }
      std::string idx = key.substr(us + 1);
      if (idx.empty() || !std::all_of(idx.begin(), idx.end(), [](unsigned char c) { return std::isdigit(c); })) {
        continue;
      }
      std::string base = key.substr(0, us);
      int         n    = 0;
      for (const auto& [k2, w2] : fw) {
        if (k2.rfind(base + "_", 0) == 0 && !init.count(k2)) {
          auto t = k2.substr(base.size() + 1);
          if (!t.empty() && std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c); })) {
            ++n;
          }
        }
      }
      if (n > 1) {
        bank_hold_keys.insert(key);
      }
    }
  }

  // Memory state[0]: fresh array per cut key.
  Io_name_map<cvc5::Term> mem;
  {
    Io_name_map<int> occ;
    for (auto node : design->forward_class()) {
      if (graph_util::type_op_of(node) != Ntype_op::Memory || !node.has_out_edges()) {
        continue;
      }
      Mem_sig sig = read_mem_sig(node);
      if (sig.bits <= 0 || sig.size <= 0) {
        continue;
      }
      std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits);
      std::string key = mem_state_key(sig, occ[sg]++);
      if (mem.count(key)) {
        continue;
      }
      cvc5::Sort asort = tm.mkArraySort(tm.mkBitVectorSort(static_cast<uint32_t>(sig.addr_w)),
                                        tm.mkBitVectorSort(static_cast<uint32_t>(sig.bits)));
      mem[key]         = tm.mkConst(asort, "m0_" + key);
    }
  }
  Io_name_map<cvc5::Term> reads;

  // Per-cycle input symbols for the witness trace.
  struct Wit_in {
    int         cyc;
    std::string name;
    cvc5::Term  t;
  };
  std::vector<Wit_in> wit_ins;
  // Structured trace up to (and including) the violating cycle — model values
  // are only valid right after the SAT checkSatAssuming, so extract eagerly.
  auto build_input_trace = [&](int up_to_cyc) -> Witness_trace {
    Witness_trace tr;
    if (!opts.witness) {
      return tr;
    }
    tr.reset_cycles  = phase_run ? reset_hold : 0;
    tr.diverge_cycle = up_to_cyc;
    tr.cycles.resize(static_cast<size_t>(up_to_cyc) + 1);
    for (int c = 0; c <= up_to_cyc; ++c) {
      tr.cycles[static_cast<size_t>(c)].reset_asserted = phase_reset || (phase_run && c < reset_hold);
    }
    for (const auto& wi : wit_ins) {
      if (wi.cyc > up_to_cyc) {
        continue;
      }
      cvc5::Term v = solver.getValue(wi.t);
      if (v.isNull()) {
        continue;
      }
      int w = static_cast<int>(v.getSort().getBitVectorSize());
      tr.cycles[static_cast<size_t>(wi.cyc)].inputs.push_back(Witness_in{wi.name, v.getBitVectorValue(10), w});
    }
    for (auto& cy : tr.cycles) {
      std::sort(cy.inputs.begin(), cy.inputs.end(), [](const Witness_in& a, const Witness_in& b) { return a.name < b.name; });
    }
    return tr;
  };
  auto witness_string = [&](const Witness_trace& tr) -> std::string {
    std::string out;
    for (size_t c = 0; c < tr.cycles.size(); ++c) {
      std::vector<std::string> toks;
      for (const auto& in : tr.cycles[c].inputs) {
        toks.push_back(in.name + "=" + in.value);
      }
      if (!out.empty()) {
        out += " | ";
      }
      out += "cyc" + std::to_string(c) + ": " + join_capped(toks, 12);
    }
    return out;
  };

  absl::flat_hash_map<int, size_t> prop_ix;  // encoder occ -> res.props index
  int  uid         = 0;
  bool any_refuted = false, any_unknown = false;
  int                              split_checks = 0;
  const Split_pick                 verify_split = (opts.partitions >= 2 && opts.split != "none" && !opts.split.empty())
                                                      ? pick_split_signal(design, opts.split, 8)
                                                      : Split_pick{};

  for (int cyc = 0; cyc < total_cyc; ++cyc) {
    const bool checking = phase_run ? (cyc >= reset_hold) : true;

    if (cyc > 0) {  // fresh-var state pinning (keeps per-cycle terms shallow)
      Io_name_map<Val> ns;
      for (const auto& [key, pv] : state) {
        cvc5::Term s = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(pv.width)), "st" + std::to_string(uid++));
        assert_formula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv.term}));
        ns[key] = Val{s, pv.width, pv.is_signed};
      }
      state = std::move(ns);
      Io_name_map<cvc5::Term> nm;
      for (const auto& [key, pv] : mem) {
        cvc5::Term s = tm.mkConst(pv.getSort(), "ma" + std::to_string(uid++));
        assert_formula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv}));
        nm[key] = s;
      }
      mem = std::move(nm);
    }

    Io_name_map<Val> sh;
    for (const auto& [name, info] : ins) {
      cvc5::Term t = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(info.w)), "c" + std::to_string(cyc) + "_" + name);
      if (auto rit = reset_negset.find(name); rit != reset_negset.end()) {
        const bool assert_reset = phase_reset || (phase_run && cyc < reset_hold);
        const bool negreset     = rit->second;
        const bool drive_zero   = assert_reset ? negreset : !negreset;
        cvc5::Term lvl          = drive_zero ? tm.mkBitVector(static_cast<uint32_t>(info.w), 0)
                                    : tm.mkTerm(cvc5::Kind::BITVECTOR_NOT, {tm.mkBitVector(static_cast<uint32_t>(info.w), 0)});
        assert_formula(tm.mkTerm(cvc5::Kind::EQUAL, {t, lvl}));
      }
      sh[name] = Val{t, info.w, info.sgn};
      if (opts.witness) {
        wit_ins.push_back({cyc, name, t});
      }
    }
    for (const auto& [k, v] : state) {
      sh[k] = v;
    }

    Encoded e = enc.encode(design, &sh, "d" + std::to_string(cyc) + "_", &mem, &reads);
    if (!e.ok) {
      res.verdict = Verdict::Unknown;
      res.detail += "; encode failed: " + e.error;
      res.elapsed_ms = now_ms(t0);
      return res;
    }
    for (const auto& [l, r] : e.equalities) {
      assert_formula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
    }

    // Process one Encoded's property conds (the design's, or a monitor's),
    // SORTED by walk occ (outputs is a hash map — unsorted iteration would make
    // frontier order, witness text and solver behavior run-varying). occ_base
    // namespaces each source so a monitor's occ 0 never collides with the
    // design's; `mon` supplies the block name + generated-line -> original loc.
    auto process_props = [&](const Encoded& eo, int occ_base, const Monitor* mon) {
      struct Ob {
        Prop_key k;
        Val      cond;
      };
      std::vector<Ob> obs;
      for (const auto& [name, v] : eo.outputs) {
        if (auto k = parse_prop_key(name)) {
          obs.push_back({std::move(*k), v});
        }
      }
      std::sort(obs.begin(), obs.end(), [](const Ob& a, const Ob& b) { return a.k.occ < b.k.occ; });

      for (const auto& ob : obs) {
        size_t    ix;
        const int occ_key = occ_base + ob.k.occ;
        if (auto it = prop_ix.find(occ_key); it != prop_ix.end()) {
          ix = it->second;
        } else {
          ix = res.props.size();
          prop_ix.emplace(occ_key, ix);
          Prop_result pr;
          pr.kind = ob.k.kind;
          pr.loc  = ob.k.loc;
          pr.msg  = ob.k.msg;
          if (mon != nullptr) {
            pr.block = mon->block;
            // The fproperty loc points into the GENERATED monitor source; map
            // its line back to the user's formal-block statement.
            if (auto colon = pr.loc.rfind(':'); colon != std::string::npos) {
              int line = std::atoi(pr.loc.c_str() + colon + 1);
              if (auto lit = mon->line2loc.find(line); lit != mon->line2loc.end()) {
                pr.loc = lit->second;
              }
            }
          }
          res.props.push_back(std::move(pr));
        }
        auto&      pr    = res.props[ix];
        const int  w     = ob.cond.width > 0 ? ob.cond.width : 1;
        cvc5::Term zero  = tm.mkBitVector(static_cast<uint32_t>(w), 0);
        cvc5::Term holds = tm.mkTerm(cvc5::Kind::DISTINCT, {fit_to(tm, ob.cond, w), zero});

        if (pr.kind == "assume") {
          // Environment constraint: in force at EVERY cycle, reset prologue
          // included (SVA semantics — otherwise an assert_always checked during
          // the prologue would run unconstrained and false-refute). A
          // contradiction with the driven reset behavior is caught by the
          // vacuity check below. Disclosed via n_assumes.
          assert_formula(holds);
          continue;
        }
        const bool check_now = pr.kind == "assert_always" ? true : checking;
        if (!check_now || pr.refuted_at >= 0 || pr.unknown_at >= 0) {
          continue;  // outside this property's window, or already resolved/stuck
        }
        cvc5::Term   bad = tm.mkTerm(cvc5::Kind::EQUAL, {fit_to(tm, ob.cond, w), zero});
        std::string ckey   = verify_obligation_key(asserted, bad, opts);
        bool        cached = opts.verify_cache_lookup && opts.verify_cache_lookup(ckey);
        bool        unsat  = cached;
        bool        sat    = false;
        if (!cached && !verify_split.name.empty()) {
          auto sit = sh.find(verify_split.name);
          if (sit != sh.end()) {
            ++split_checks;
            const uint64_t nvals = uint64_t{1} << verify_split.width;
            unsat                = true;
            for (uint64_t v = 0; v < nvals; ++v) {
              cvc5::Term vc = tm.mkBitVector(static_cast<uint32_t>(sit->second.width), v);
              cvc5::Term cube_bad
                  = tm.mkTerm(cvc5::Kind::AND,
                              {bad.substitute(sit->second.term, vc), tm.mkTerm(cvc5::Kind::EQUAL, {sit->second.term, vc})});
              cvc5::Result cr = solver.checkSatAssuming(cube_bad);
              if (cr.isSat()) {
                sat   = true;
                unsat = false;
                break;
              }
              if (!cr.isUnsat()) {
                unsat = false;
                break;
              }
            }
          }
        }
        if (!cached && !unsat && !sat) {
          // A poor selector is only an accelerator miss: fall back to the
          // monolithic obligation so splitting can never weaken the verdict.
        cvc5::Result r   = solver.checkSatAssuming(bad);
          unsat          = r.isUnsat();
          sat            = r.isSat();
        }
        if (unsat) {
          pr.proven_to = cyc;
          if (!cached && opts.verify_cache_store) {
            opts.verify_cache_store(std::move(ckey));
          }
          // Frontier assume: an entailed fact — later obligations (same cycle
          // included) and deeper cycles solve in the pruned space.
          assert_formula(holds);
        } else if (sat) {
          pr.refuted_at = cyc;
          pr.trace      = build_input_trace(cyc);  // model valid only until the next assert
          pr.witness    = witness_string(pr.trace);
          any_refuted   = true;
        } else {
          pr.unknown_at = cyc;
          any_unknown   = true;
        }
      }
    };
    process_props(e, /*occ_base=*/0, nullptr);

    // Formal-block monitors: encode each with its inputs bound to THIS cycle's
    // design signals (current state / this cycle's input symbols / this cycle's
    // outputs), then run its obligations through the identical machinery.
    for (size_t mi = 0; monitors != nullptr && mi < monitors->size(); ++mi) {
      const Monitor& mon = (*monitors)[mi];
      if (mon.graph == nullptr) {
        continue;
      }
      Io_name_map<Val> msh;
      bool             bind_ok = true;
      for (const auto& b : mon.binds) {
        const Val* v = nullptr;
        switch (b.src) {
          case Monitor::Bind::Src::input: {
            if (auto it = sh.find(b.key); it != sh.end()) {
              v = &it->second;
            }
            break;
          }
          case Monitor::Bind::Src::output: {
            if (auto it = e.outputs.find(b.key); it != e.outputs.end()) {
              v = &it->second;
            }
            break;
          }
          case Monitor::Bind::Src::state: {
            if (auto it = state.find(b.key); it != state.end()) {
              v = &it->second;
            }
            break;
          }
        }
        if (v == nullptr) {  // validated at setup; defensive: fail loudly, not silently
          res.verdict = Verdict::Unknown;
          res.detail += "; monitor '" + mon.block + "' binding '" + b.ident + "' <- '" + b.key + "' did not resolve";
          res.elapsed_ms = now_ms(t0);
          bind_ok        = false;
          break;
        }
        msh[b.ident] = *v;
      }
      if (!bind_ok) {
        return res;
      }
      Encoded me = enc.encode(mon.graph, &msh, "m" + std::to_string(mi) + "c" + std::to_string(cyc) + "_", nullptr, nullptr);
      if (!me.ok) {
        res.verdict = Verdict::Unknown;
        res.detail += "; monitor '" + mon.block + "' encode failed: " + me.error;
        res.elapsed_ms = now_ms(t0);
        return res;
      }
      for (const auto& [l, r] : me.equalities) {
        assert_formula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
      }
      process_props(me, /*occ_base=*/static_cast<int>((mi + 1) * 100000), &mon);
    }

    // Thread next state (bank flops HOLD through the reset prologue).
    Io_name_map<Val> nstate;
    for (const auto& [name, v] : e.outputs) {
      if (name.rfind("\x01nxt:", 0) == 0) {
        nstate[name.substr(5)] = v;
      }
    }
    if (phase_run && cyc < reset_hold && !bank_hold_keys.empty()) {
      for (const auto& key : bank_hold_keys) {
        if (auto it = state.find(key); it != state.end()) {
          nstate[key] = it->second;
        }
      }
    }
    state = std::move(nstate);
    mem   = std::move(e.next_mem);
    reads = std::move(e.next_read);
  }

  // ── V3 induction rung: upgrade bounded-proven asserts to UNBOUNDED ────────
  // Simultaneous (conjunctive) 1-induction over the still-clean asserts: a
  // free-state step frame (fresh state/memory/input symbols, reset free —
  // weaker but sound), all candidates + env assumes assumed at frame 0, each
  // candidate proven at frame 1; Houdini-drop failures and repeat. At the
  // fixpoint the SURVIVORS' conjunction is inductive and the BMC run above is
  // its base case, so every survivor holds at every cycle of every bound (the
  // sound conjunctive form — rule E; a dropped candidate keeps its bounded
  // verdict). Everything lives under one push/pop so the BMC frame and the
  // final vacuity check are untouched.
  if (!res.props.empty()) {
    std::vector<size_t> cand;
    for (size_t i = 0; i < res.props.size(); ++i) {
      const auto& pr = res.props[i];
      if (pr.kind != "assume" && pr.refuted_at < 0 && pr.unknown_at < 0 && pr.proven_to >= 0) {
        cand.push_back(i);
      }
    }
    if (!cand.empty()) {
      solver.push();
      // Frame-0 free state; frame-1 state pinned to frame-0's next-state.
      // Step-frame inputs are free EXCEPT the primary resets, which are pinned
      // to their DEASSERTED level in the after_reset phase (user ruling,
      // 2026-07-08): the step then quantifies over post-reset free-running
      // states only, upgrading invariants a mid-trace reset would break. The
      // narrowing is disclosed in the run detail below.
      auto frame_inputs = [&](int f) {
        Io_name_map<Val> s;
        for (const auto& [name, info] : ins) {
          cvc5::Term t = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(info.w)), "k" + std::to_string(f) + "_" + name);
          if (phase_run) {
            if (auto rit = reset_negset.find(name); rit != reset_negset.end()) {
              const bool drive_zero = !rit->second;  // deasserted: active-low -> all-1, active-high -> 0
              cvc5::Term lvl = drive_zero
                                   ? tm.mkBitVector(static_cast<uint32_t>(info.w), 0)
                                   : tm.mkTerm(cvc5::Kind::BITVECTOR_NOT, {tm.mkBitVector(static_cast<uint32_t>(info.w), 0)});
              solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {t, lvl}));
            }
          }
          s[name] = Val{t, info.w, info.sgn};
        }
        return s;
      };
      Io_name_map<Val> fstate;
      for (const auto& [key, v] : state) {
        fstate[key] = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(v.width)), "ks0_" + key), v.width, v.is_signed};
      }
      Io_name_map<cvc5::Term> fmem;
      for (const auto& [key, a] : mem) {
        fmem[key] = tm.mkConst(a.getSort(), "km0_" + key);
      }
      Io_name_map<cvc5::Term> freads;
      // occ_key -> per-frame cond Val (design + monitor obligations alike).
      absl::flat_hash_map<int, Val> cond_f[2];
      bool                          ind_ok = true;
      for (int f = 0; f < 2 && ind_ok; ++f) {
        Io_name_map<Val> fsh = frame_inputs(f);
        for (const auto& [k, v] : fstate) {
          fsh[k] = v;
        }
        Encoded fe = enc.encode(design, &fsh, "kf" + std::to_string(f) + "_", &fmem, &freads);
        if (!fe.ok) {
          ind_ok = false;  // induction is best-effort: bounded verdicts stand
          break;
        }
        for (const auto& [l, r] : fe.equalities) {
          solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
        }
        auto collect = [&](const Encoded& eo, int occ_base) {
          for (const auto& [name, v] : eo.outputs) {
            if (auto k = parse_prop_key(name)) {
              cond_f[f].emplace(occ_base + k->occ, v);
            }
          }
        };
        collect(fe, 0);
        for (size_t mi = 0; monitors != nullptr && mi < monitors->size(); ++mi) {
          const Monitor& mon = (*monitors)[mi];
          if (mon.graph == nullptr) {
            continue;
          }
          Io_name_map<Val> msh;
          bool             ok = true;
          for (const auto& b : mon.binds) {
            const Val* v = nullptr;
            if (b.src == Monitor::Bind::Src::state) {
              if (auto it = fstate.find(b.key); it != fstate.end()) {
                v = &it->second;
              }
            } else if (b.src == Monitor::Bind::Src::input) {
              if (auto it = fsh.find(b.key); it != fsh.end()) {
                v = &it->second;
              }
            } else {
              if (auto it = fe.outputs.find(b.key); it != fe.outputs.end()) {
                v = &it->second;
              }
            }
            if (v == nullptr) {
              ok = false;
              break;
            }
            msh[b.ident] = *v;
          }
          if (!ok) {
            continue;  // best-effort: this monitor's props just stay bounded
          }
          Encoded me = enc.encode(mon.graph, &msh, "kf" + std::to_string(f) + "m" + std::to_string(mi) + "_", nullptr, nullptr);
          if (!me.ok) {
            continue;
          }
          for (const auto& [l, r] : me.equalities) {
            solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {l, r}));
          }
          collect(me, static_cast<int>((mi + 1) * 100000));
        }
        if (f == 0) {  // pin frame-1 state to frame-0's next-state
          Io_name_map<Val> ns;
          for (const auto& [name, v] : fe.outputs) {
            if (name.rfind("\x01nxt:", 0) == 0) {
              cvc5::Term s = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(v.width)), "ks1_" + name.substr(5));
              solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {s, fit_to(tm, v, v.width)}));
              ns[name.substr(5)] = Val{s, v.width, v.is_signed};
            }
          }
          fstate = std::move(ns);
          fmem   = std::move(fe.next_mem);
          freads = std::move(fe.next_read);
        }
      }
      auto truth = [&](const Val& v) {
        const int w = v.width > 0 ? v.width : 1;
        return tm.mkTerm(cvc5::Kind::DISTINCT, {fit_to(tm, v, w), tm.mkBitVector(static_cast<uint32_t>(w), 0)});
      };
      if (ind_ok) {
        // Env assumes constrain BOTH frames (they hold on every real cycle).
        for (const auto& [occ_key, ix] : prop_ix) {
          if (res.props[ix].kind != "assume") {
            continue;
          }
          for (int f = 0; f < 2; ++f) {
            if (auto it = cond_f[f].find(occ_key); it != cond_f[f].end()) {
              solver.assertFormula(truth(it->second));
            }
          }
        }
        // Step-vacuity guard: a contradictory free frame would "prove" anything.
        cvc5::Result base = solver.checkSat();
        if (base.isSat()) {
          // Houdini fixpoint over the candidate set.
          std::vector<size_t> alive;
          for (size_t ix : cand) {
            bool have = false;
            for (const auto& [occ_key, pix] : prop_ix) {
              if (pix == ix && cond_f[0].count(occ_key) != 0 && cond_f[1].count(occ_key) != 0) {
                have = true;
              }
            }
            if (have) {
              alive.push_back(ix);
            }
          }
          auto occ_of = [&](size_t ix) {
            for (const auto& [occ_key, pix] : prop_ix) {
              if (pix == ix) {
                return occ_key;
              }
            }
            return -1;
          };
          bool changed = true;
          while (changed && !alive.empty()) {
            changed = false;
            solver.push();
            for (size_t ix : alive) {
              solver.assertFormula(truth(cond_f[0].at(occ_of(ix))));  // hypothesis at frame 0
            }
            std::vector<size_t> keep;
            for (size_t ix : alive) {
              const int  w   = cond_f[1].at(occ_of(ix)).width > 0 ? cond_f[1].at(occ_of(ix)).width : 1;
              cvc5::Term bad = tm.mkTerm(cvc5::Kind::EQUAL,
                                         {fit_to(tm, cond_f[1].at(occ_of(ix)), w), tm.mkBitVector(static_cast<uint32_t>(w), 0)});
              if (solver.checkSatAssuming(bad).isUnsat()) {
                keep.push_back(ix);
              } else {
                changed = true;  // dropped: not inductive relative to the set
              }
            }
            solver.pop();
            alive = std::move(keep);
          }
          for (size_t ix : alive) {
            res.props[ix].unbounded = true;
          }
          if (!alive.empty() && phase_run && !reset_negset.empty()) {
            res.detail += "; induction step holds reset deasserted (unbounded verdicts assume no mid-trace reset)";
          }
        }
      }
      solver.pop();
    }
  }

  // Finalize per-property verdicts.
  for (auto& pr : res.props) {
    if (pr.kind == "assume") {
      ++res.n_assumes;
      continue;
    }
    if (pr.refuted_at >= 0) {
      pr.verdict = has_bbox ? Verdict::Unknown : Verdict::Refuted;  // free-box artifact gate
    } else if (pr.unknown_at >= 0) {
      pr.verdict = Verdict::Unknown;
    } else if (pr.proven_to >= 0) {
      pr.verdict = Verdict::Proven;  // BOUNDED: every checked cycle <= proven_to is UNSAT
    }
  }

  // Vacuity: contradictory assumes make every proof vacuous. One plain checkSat
  // over the frame + assumes (+ entailed frontier facts, which cannot flip it).
  if (res.n_assumes > 0) {
    cvc5::Result r = solver.checkSat();
    if (r.isUnsat()) {
      res.vacuous = true;
      res.detail += "; assume set CONTRADICTORY (all proofs vacuous)";
    } else if (!r.isSat()) {
      res.detail += "; vacuity check inconclusive (assume consistency unconfirmed)";
      res.vacuous = true;  // conservative: do not report vacuum-tainted Proven
    }
    if (res.vacuous) {
      for (auto& pr : res.props) {
        if (pr.kind != "assume" && pr.verdict == Verdict::Proven) {
          pr.verdict = Verdict::Unknown;
        }
      }
    }
  }

  if (has_bbox && any_refuted) {
    res.detail += "; blackbox instance(s) present: a violation may be an artifact of the free box outputs";
  }
  res.detail += "; " + std::to_string(N) + " checked steps"
              + (reset_hold ? " after " + std::to_string(reset_hold) + " reset-hold" : "")
              + (reset_negset.empty() && opts.phase != "free_toreset" ? "; WARNING no primary reset input found" : "");
  if (split_checks > 0) {
    res.detail += "; case-split " + verify_split.name + " on " + std::to_string(split_checks) + " obligation check(s)";
  }

  // Aggregate: Refuted dominates; anything unresolved -> Unknown; else bounded Proven.
  // A design with no assert-kind obligation at all is Unknown, not a vacuous PASS.
  bool all_proven = true;
  int  n_asserts  = 0;
  for (const auto& pr : res.props) {
    if (pr.kind == "assume") {
      continue;
    }
    ++n_asserts;
    if (pr.verdict != Verdict::Proven) {
      all_proven = false;
    }
  }
  if (n_asserts == 0) {
    res.detail += "; no assert/assert_always obligations found";
    res.verdict = Verdict::Unknown;
  } else if (any_refuted && !has_bbox) {
    res.verdict = Verdict::Refuted;
  } else if (any_unknown || res.vacuous || (any_refuted && has_bbox) || !all_proven) {
    res.verdict = Verdict::Unknown;
  } else {
    res.verdict = Verdict::Proven;
  }
  res.elapsed_ms = now_ms(t0);
  return res;
}

}  // namespace livehd::lec
