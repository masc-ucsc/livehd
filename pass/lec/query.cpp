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
  std::string d = "auto INCONCLUSIVE: ind=" + std::string(vname(ind.verdict)) + "(" + std::to_string(ind.elapsed_ms) + "ms)";
  if (ind.verdict == Verdict::Refuted) {
    d += " [single-step CEX — may be an UNREACHABLE step-case, treated as a HINT not a failure: "
         + (ind.witness.empty() ? std::string("(no witness)") : ind.witness) + "]";
  }
  d += ", bmc=" + std::string(vname(bmc.verdict)) + "(" + std::to_string(bmc.elapsed_ms) + "ms)";
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

// Sequential fallback when fork/pipe is unavailable: run ind, then bmc, applying
// the same trust asymmetry. Used only on a (rare) fork failure.
Query_result run_auto_sequential(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                                 const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
  auto         t0 = std::chrono::steady_clock::now();
  Lec_options  oi = opts;
  oi.engine       = "ind";
  auto         ti = std::chrono::steady_clock::now();
  Query_result ri = prove_equal(ref, impl, oi, sub_lib);
  ri.engine       = "ind";
  ri.elapsed_ms   = now_ms(ti);
  if (ri.verdict == Verdict::Proven) {
    ri.detail = "auto(seq): ind Proven (k=1 induction); " + ri.detail;
    return ri;
  }
  Lec_options ob = opts;
  ob.engine      = "bmc";
  auto         tb = std::chrono::steady_clock::now();
  Query_result rb = prove_equal(ref, impl, ob, sub_lib);
  rb.engine       = "bmc";
  rb.elapsed_ms   = now_ms(tb);
  if (rb.verdict == Verdict::Refuted) {
    rb.detail = "auto(seq): bmc Refuted (reachable CEX); " + rb.detail;
    return rb;
  }
  return make_inconclusive(ri, rb, opts, now_ms(t0));
}

Query_result run_auto_portfolio(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts,
                                const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib) {
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
      Query_result r  = prove_equal(ref, impl, o, sub_lib);
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
    r2.detail       = (r2.verdict == Verdict::Proven
                           ? "full: equivalent in BOTH just_reset and after_reset; after_reset stage: "
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

  // Proven-module collapse set (lec.collapse): def-NAMES forced to the blackbox
  // path (case-sensitive). Applied identically by the encoder (skip flatten)
  // and build_shared_bbox below (pre-build the box's shared output symbols).
  Io_name_map<bool> collapse_defs;
  for (const auto& d : opts.collapse) {
    if (!d.empty()) {
      collapse_defs[d] = true;
    }
  }
  const Io_name_map<bool>* collapse_ptr = collapse_defs.empty() ? nullptr : &collapse_defs;

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);

  // Subnode Gids of collapsed leaves (top-level), so the inductive/BMC flop
  // collection below can skip descending into them (their state is the box's, not
  // a set of internal flop cuts). gids are name-hash stable across designs.
  ankerl::unordered_dense::set<hhds::Gid> collapse_gids;
  if (collapse_ptr != nullptr) {
    for (auto* g : {ref, impl}) {
      for (auto node : g->forward_class()) {
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

  // Shared state-aware boxes for STATEFUL collapsed leaves (sequential collapse).
  // For each collapsed leaf instance whose def has state, build the shared UF
  // symbols (out per port + next) over (concatenated-inputs, state). Both designs
  // share these, so equal inputs + equal corresponding state => equal outputs +
  // next-state. Keyed defname#occ to align with the encoder's blackbox occurrence.
  absl::flat_hash_map<std::string, State_box> state_boxes;
  if (collapse_ptr != nullptr) {
    auto add_boxes = [&](hhds::Graph* g) {
      Io_name_map<int> occ;
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto        sio = node.get_subnode_io();
        std::string defname(sio->get_name());
        if (collapse_ptr->count(defname) == 0) {
          continue;
        }
        std::string bk = defname + "#" + std::to_string(occ[defname]++);
        if (state_boxes.count(bk)) {
          continue;
        }
        auto def      = node.get_subnode_graph();
        bool stateful = false;
        if (def != nullptr) {
          for (auto dn : def->forward_class()) {
            auto dop = graph_util::type_op_of(dn);
            if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
              stateful = true;
              break;
            }
          }
        }
        if (!stateful) {
          continue;  // combinational leaf -> the stateless shared-symbol box suffices
        }
        State_box box;
        for (const auto& d : sio->get_input_pin_decls()) {
          box.in_w += std::max(1, static_cast<int>(d.bits));
        }
        box.state_w = 64;  // abstract state width (the same for both designs -> sound)
        cvc5::Sort bv_state = tm.mkBitVectorSort(static_cast<uint32_t>(box.state_w));
        // Outputs are MOORE — UF_out(state) ONLY, not (inputs, state). A Mealy box
        // (output depends on the current input) adds a combinational input->output
        // path through the leaf; in a pipeline a stage register's q feeds glue that
        // feeds its own d (stall/enable), so q=UF(d,..) -> d -> q is a FALSE
        // combinational cycle that the encoder cannot resolve. Output-from-state-only
        // matches the registered output exactly and stays sound (both designs share
        // UF_out + state; divergent leaf inputs are still caught by the bbin compare
        // points). The next-state transition keeps its (inputs, state) dependence —
        // it feeds the state cut a cycle later, never a combinational loop.
        for (const auto& d : sio->get_output_pin_decls()) {
          int ow             = std::max(1, static_cast<int>(d.bits));
          box.out_w[d.name]  = ow;
          box.out_fn[d.name] = tm.mkConst(tm.mkFunctionSort({bv_state}, tm.mkBitVectorSort(static_cast<uint32_t>(ow))),
                                          "uf_out:" + bk + ":" + d.name);
        }
        std::vector<cvc5::Sort> next_dom;  // next_state = UF_next(inputs, state)
        if (box.in_w > 0) {
          next_dom.push_back(tm.mkBitVectorSort(static_cast<uint32_t>(box.in_w)));
        }
        next_dom.push_back(bv_state);
        box.next_fn = tm.mkConst(tm.mkFunctionSort(next_dom, bv_state), "uf_next:" + bk);
        state_boxes[bk] = std::move(box);
      }
    };
    add_boxes(ref);
    add_boxes(impl);
  }
  const auto* state_boxes_ptr = state_boxes.empty() ? nullptr : &state_boxes;

  // A stateful collapse adds uninterpreted functions; widen the logic to include
  // UF (and keep arrays for the memory cut). The eager internal bit-blaster has
  // no UF/array theory, so a UF query keeps the default solver (below).
  solver.setLogic(state_boxes_ptr != nullptr ? "QF_AUFBV" : "QF_ABV");  // BV + arrays (+UF)

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
  if (!has_mem && state_boxes_ptr == nullptr) {
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
        std::string sg  = std::to_string(sig.size) + "x" + std::to_string(sig.bits) + ":r" + std::to_string(sig.n_rd)
                       + "w" + std::to_string(sig.n_wr);
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

  // Shared output symbols for BLACKBOX Sub instances (missing/empty defs). Each
  // blackbox output becomes one free symbol SHARED across both designs, keyed
  // "<defname>#<occ>:<port>" (matches the encoder). Built at the max output width
  // seen across the two designs (bit-width trap). A Sub is a blackbox unless the
  // resolution library has a purely-combinational def for it (mirrors encode.cpp).
  auto build_shared_bbox = [&]() {
    Io_name_map<Val>  bb;
    Io_name_map<int>  bw;   // key -> max real width
    Io_name_map<bool> bsg;  // key -> signed
    auto                                   add = [&](hhds::Graph* g) {
      Io_name_map<int> occ;
      for (auto node : g->forward_class()) {
        if (graph_util::type_op_of(node) != Ntype_op::Sub) {
          continue;
        }
        auto        sub_io  = node.get_subnode_io();
        std::string defname(sub_io->get_name());
        // Mirror encode.cpp's blackbox predicate EXACTLY so the box's shared
        // symbols are pre-built for every sub the encoder will blackbox: a forced
        // collapse (proven module), else a sub with no flattenable combinational
        // def in the resolution library.
        bool blackbox = true;
        if (collapse_ptr != nullptr && collapse_ptr->count(defname) > 0) {
          blackbox = true;  // proven-module collapse: always a blackbox
        } else if (sub_lib != nullptr) {
          if (auto git = sub_lib->find(node.get_subnode_gid()); git != sub_lib->end() && git->second != nullptr) {
            blackbox = false;
            for (auto dn : git->second->forward_class()) {
              auto dop = graph_util::type_op_of(dn);
              if (dop == Ntype_op::Flop || dop == Ntype_op::Fflop || dop == Ntype_op::Latch || dop == Ntype_op::Memory) {
                blackbox = true;
                break;
              }
            }
          }
        }
        if (!blackbox) {
          continue;
        }
        std::string bk = defname + "#" + std::to_string(occ[defname]++);
        absl::flat_hash_map<hhds::Port_id, std::string> out_name;
        for (const auto& d : sub_io->get_output_pin_decls()) {
          out_name[sub_io->get_output_port_id(d.name)] = d.name;
        }
        for (const auto& e : node.out_edges()) {
          auto        dp   = e.driver;
          auto        nit  = out_name.find(dp.get_port_id());
          std::string port = nit != out_name.end() ? nit->second : std::to_string(dp.get_port_id());
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
    };
    add(ref);
    add(impl);
    for (const auto& [key, w] : bw) {
      bb[key] = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "bb:" + key), w, bsg[key]};
    }
    return bb;
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
    Io_name_map<Val> shared_bbox = build_shared_bbox();
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

    // Reset-phase setup. A PRIMARY reset input is an input name that drives some
    // flop's reset_pin directly; its asserted level is 0 when that flop is
    // active-low (negreset), else 1. (Derived resets — driven by a mux, not a
    // primary input — are not directly controllable and are simply left free.)
    Io_name_map<bool> reset_negset;  // name -> negreset
    auto                                   collect_resets = [&](hhds::Graph* g) {
      for (auto node : g->forward_hier()) {  // descend hierarchy: flops at every level
        if (graph_util::type_op_of(node) != Ntype_op::Flop) {
          continue;
        }
        auto rst_d = graph_util::get_driver_of_sink_name(node, "reset_pin");
        if (rst_d.is_invalid() || !graph_util::is_graph_input_pin(rst_d)) {
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
      Io_name_map<int> fw;
      Io_name_map<Val> init;
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
          if (auto it = fw.find(key); it == fw.end() || w > it->second) {
            fw[key] = w;  // max-width across both designs (bit-width trap)
          }
          if (!init.count(key)) {
            if (auto iv = flop_initial(tm, node, w)) {
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
            keys.insert(std::string(rst ? "[reset] " : "[UNRST] ") + eff(node.get_hier_name()) + "  <=  "
                        + node.get_hier_name());
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
          v = Val{tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(w)), "s0_" + key), w, false};
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
        auto pin_state = [&](const Io_name_map<Val>& prev_next,
                             Io_name_map<Val>&       cur) {
          for (const auto& [key, pv] : prev_next) {
            cvc5::Term s = tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(pv.width)), "st" + std::to_string(uid++));
            solver.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {s, pv.term}));
            cur[key] = Val{s, pv.width, pv.is_signed};
          }
        };
        pin_state(ref_state, ns_ref);
        pin_state(impl_state, ns_impl);
        ref_state  = std::move(ns_ref);
        impl_state = std::move(ns_impl);

        // Same fresh-var pinning for memory arrays (keeps each cycle's array
        // terms shallow; the unrolling lives in the equality assertions).
        Io_name_map<cvc5::Term> nm_ref, nm_impl;
        auto pin_mem = [&](const Io_name_map<cvc5::Term>& prev_next,
                           Io_name_map<cvc5::Term>&       cur) {
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
                                             : tm.mkTerm(cvc5::Kind::BITVECTOR_NOT,
                                                         {tm.mkBitVector(static_cast<uint32_t>(info.w), 0)});
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

      Encoded re = enc.encode(ref, &sh_ref, "r" + std::to_string(cyc) + "_", &ref_mem);
      if (!re.ok) {
        res.verdict  = Verdict::Unknown;
        res.detail  += "; ref encode failed: " + re.error;
        return res;
      }
      Encoded ie = enc.encode(impl, &sh_impl, "i" + std::to_string(cyc) + "_", &impl_mem);
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
          if (!name.empty() && name[0] == '\x01') {
            continue;  // next-state, not a primary output
          }
          auto it = ie.outputs.find(name);
          if (it == ie.outputs.end()) {
            continue;
          }
          int        w    = std::max(rv.width, it->second.width);
          cvc5::Term rfit = fit_to(tm, rv, w);
          cvc5::Term ifit = fit_to(tm, it->second, w);
          cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {rfit, ifit});
          bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
          decomp_diffs.push_back({name + "@" + std::to_string(cyc), diff});
          if (opts.witness) {
            wit_outs.push_back({cyc, name, rfit, ifit});
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
                 || k.find("idex") != std::string::npos || k.find("id_ex") != std::string::npos
                 || k == "pc" || k.find("taken") != std::string::npos || k.find("nextpc") != std::string::npos;
        };
        for (const auto& [k, v] : rn) {
          if (keep(k)) dbg_ns.push_back({cyc, 'R', k, v.term});
        }
        for (const auto& [k, v] : in) {
          if (keep(k)) dbg_ns.push_back({cyc, 'I', k, v.term});
        }
        for (const auto& [k, v] : re.outputs) {
          if (k.rfind("\x03" "dbg:", 0) == 0) {
            dbg_ns.push_back({cyc, 'R', k.substr(5), v.term});
          }
        }
        for (const auto& [k, v] : ie.outputs) {
          if (k.rfind("\x03" "dbg:", 0) == 0) {
            dbg_ns.push_back({cyc, 'I', k.substr(5), v.term});
          }
        }
      }
      ref_state  = std::move(rn);
      impl_state = std::move(in);
      ref_mem    = std::move(re.next_mem);
      impl_mem   = std::move(ie.next_mem);
    }

    res.detail = "solver=cvc5 (bmc, phase=" + opts.phase + ", " + std::to_string(N) + " checked steps"
               + (reset_hold ? " after " + std::to_string(reset_hold) + " reset-hold" : "")
               + (reset_negset.empty() && opts.phase != "free_toreset" ? "; WARNING no primary reset input found" : "") + ")";
    if (bad.isNull()) {
      res.verdict = Verdict::Proven;
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
        res.verdict = Verdict::Proven;
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
      res.verdict = Verdict::Proven;
    } else if (r.isSat()) {
      res.verdict = Verdict::Refuted;
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
            std::fprintf(stderr, "[LEC_WIT] cyc=%d OUT %s ref=%s impl=%s%s\n", o.cyc, o.name.c_str(), rs.c_str(), is.c_str(),
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
          std::string lbl = in.cyc < reset_hold ? ("rst" + std::to_string(in.cyc))
                                                : ("s" + std::to_string(in.cyc - reset_hold + 1));
          itoks.push_back(lbl + "." + in.name + "=" + v.getBitVectorValue(10));
        }
        std::sort(itoks.begin(), itoks.end());
        res.witness = "first divergence at checked step " + std::to_string(first_bad - reset_hold + 1) + "/"
                    + std::to_string(N) + ": " + join_ordered(dtoks, 32);
        if (!itoks.empty()) {
          res.witness += " @ inputs " + join_ordered(itoks, 64);
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
      if (auto it = shared.find(key); it != shared.end() && it->second.width >= w) {
        continue;  // keep the wider shared state symbol (see input note above)
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
  Io_name_map<Val>        shared_bbox = build_shared_bbox();

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
    auto pair_side = [&](const Io_name_map<FlopRec>& bank_flops, const Io_name_map<FlopRec>& other_flops,
                         const Io_name_map<MemRec>& bank_mems, const Io_name_map<MemRec>& mem_mems, bool mem_in_impl) {
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
        cvc5::Term sel = tm.mkTerm(cvc5::Kind::SELECT, {A, tm.mkBitVector(static_cast<uint32_t>(br.addr_w), static_cast<uint64_t>(i))});
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
  enc.set_shared_bbox(&shared_bbox);
  Encoded re = enc.encode(ref, &shared, "", &shared_mems);
  if (!re.ok) {
    res.verdict  = Verdict::Unknown;
    res.detail  += "; ref encode failed: " + re.error;
    return res;
  }
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
    if (bridged_ref_out.count(name)) {
      continue;  // bank-flop next state -> compared via the memory bridge above
    }
    auto it = ie.outputs.find(name);
    if (it == ie.outputs.end()) {
      res.unmatched_ref.push_back(display_name(name));
      continue;
    }
    int        w    = std::max(rv.width, it->second.width);
    cvc5::Term diff = tm.mkTerm(cvc5::Kind::DISTINCT, {fit_to(tm, rv, w), fit_to(tm, it->second, w)});
    bad             = bad.isNull() ? diff : tm.mkTerm(cvc5::Kind::OR, {bad, diff});
    ind_diffs.push_back({display_name(name), diff});
  }
  for (const auto& [name, iv] : ie.outputs) {
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
    res.detail += "; " + std::to_string(res.unmatched_ref.size()) + " ref-only cut point(s) {" + join_capped(res.unmatched_ref) + "}";
  }
  if (!res.unmatched_impl.empty()) {
    res.detail += "; " + std::to_string(res.unmatched_impl.size()) + " impl-only cut point(s) {" + join_capped(res.unmatched_impl) + "}";
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
        diff_toks.push_back(display_name(name) + "(ref=" + rval.getBitVectorValue(10) + " impl=" + ival.getBitVectorValue(10) + ")");
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

}  // namespace livehd::lec
