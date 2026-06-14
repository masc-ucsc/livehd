//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_timecheck.hpp"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "hlop/dlop.hpp"
#include "lnast_ntype.hpp"
#include "pass.hpp"

namespace {

// A derived cycle interval. fixed() intervals are checkable; wider intervals
// (ranged stage decls) propagate but only fixed values discharge checks.
struct Cyc {
  int64_t            min = 0;
  int64_t            max = 0;
  [[nodiscard]] bool fixed() const { return min == max; }
};

// Stage-decl pending info (declare seen, din store not yet).
struct Stg {
  int64_t   min = 0;
  int64_t   max = 0;
  bool      nil = false;  // stage[] — unconstrained, result UNKNOWN here
  Lnast_nid decl_nid;
};

// Call-result info: the aligned operand cycle + the callee's declared
// interval. The following stage[N] binding REPLACES the interval with N.
struct CallInfo {
  Cyc     op;
  bool    op_known = false;
  int64_t cmin     = 0;
  int64_t cmax     = 0;  // pipe convention: cmax < cmin = unconstrained upper
  bool    is_pipe  = false;
};

[[nodiscard]] std::shared_ptr<Lnast> resolve_callee(std::string_view name, const uPass_timecheck::Registry& registry) {
  std::shared_ptr<Lnast> exact;
  std::shared_ptr<Lnast> suffix_hit;
  int                    suffix_matches = 0;
  const std::string      suffix         = "." + std::string(name);
  for (const auto& ln : registry) {
    if (!ln) {
      continue;
    }
    auto n = ln->get_top_module_name();
    if (n == name) {
      exact = ln;
    } else if (n.size() > suffix.size() && n.substr(n.size() - suffix.size()) == suffix) {
      suffix_hit = ln;
      ++suffix_matches;
    }
  }
  if (exact) {
    return exact;
  }
  return suffix_matches == 1 ? suffix_hit : nullptr;
}

[[nodiscard]] std::string loc_str(const std::shared_ptr<Lnast>& ln, const Lnast_nid& nid) {
  const auto span = ln->span_of(nid);  // SourceId resolved through the locator
  if (!span.start_line) {
    return std::string(ln->get_top_module_name());
  }
  return std::format("{}:{}", span.file.empty() ? std::string(ln->get_top_module_name()) : span.file, *span.start_line);
}

// Ops with the (dst, operand...) shape whose result inherits the operands'
// equal-meet cycle.
[[nodiscard]] bool is_meet_op(Lnast_ntype::Lnast_ntype_int t) {
  using N = Lnast_ntype;
  return N::is_bit_and(t) || N::is_bit_or(t) || N::is_bit_not(t) || N::is_bit_xor(t) || N::is_red_or(t) || N::is_red_and(t)
         || N::is_red_xor(t) || N::is_popcount(t) || N::is_log_and(t) || N::is_log_or(t) || N::is_log_not(t) || N::is_plus(t)
         || N::is_minus(t) || N::is_mult(t) || N::is_div(t) || N::is_mod(t) || N::is_shl(t) || N::is_sra(t) || N::is_sext(t)
         || N::is_set_mask(t) || N::is_get_mask(t) || N::is_ne(t) || N::is_eq(t) || N::is_lt(t) || N::is_le(t) || N::is_gt(t)
         || N::is_ge(t);
}

class Discharger {
public:
  Discharger(const std::shared_ptr<Lnast>& lnast, const uPass_timecheck::Registry& registry) : ln_(lnast), registry_(registry) {}

  // Stage a located Diagnostic (the node's SourceId resolved
  // through the Lnast's locator) before the Pass::error-style throw; the
  // downstream flush emits the staged record exactly once. The loc_str text
  // stays in the message for human/test continuity.
  template <typename... Args>
  [[noreturn]] void error_at(const Lnast_nid& nid, std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    livehd::diag::sink().stage(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "timecheck-error",
        .category = "type",
        .pass     = "upass.timecheck",
        .message  = msg,
        .span     = ln_ ? ln_->span_of(nid) : livehd::diag::Span{},
        .notes    = ln_ ? ln_->notes_of(nid, "reached via this site") : std::vector<livehd::diag::Note>{},
    });
    err_tracker::logger(msg);
    throw Eprp::parser_error(Pass::eprp, msg);
  }

  void run() {
    for (const auto& e : ln_->io_meta().inputs) {
      known_[e.name] = Cyc{0, 0};
    }

    auto top = ln_->get_root();
    for (auto c = ln_->get_first_child(top); !c.is_invalid(); c = ln_->get_sibling_next(c)) {
      if (Lnast_ntype::is_stmts(ln_->get_type(c))) {
        walk_stmts(c);
      }
    }

    verify_checks();
    check_outputs();
  }

private:
  void walk_stmts(const Lnast_nid& stmts) {
    using N = Lnast_ntype;
    for (auto c = ln_->get_first_child(stmts); !c.is_invalid(); c = ln_->get_sibling_next(c)) {
      const auto t = ln_->get_type(c);
      if (N::is_declare(t)) {
        do_declare(c);
      } else if (N::is_store(t)) {
        do_store(c);
      } else if (N::is_func_call(t)) {
        do_call(c);
      } else if (N::is_timecheck(t)) {
        do_timecheck(c);
      } else if (is_meet_op(t)) {
        do_meet_op(c);
      } else if (N::is_if_like(t) || N::is_while(t) || N::is_for(t)) {
        // Branch-written values need the LG checker's classification; mark
        // every name written inside as unknown. Checks inside stay for LG.
        forget_written(c);
      } else if (N::is_range(t) || N::is_cassert(t)) {
        // no cycle effect
      } else {
        // Conservative: any other statement with a ref dst makes it unknown.
        auto c0 = ln_->get_first_child(c);
        if (!c0.is_invalid() && N::is_ref(ln_->get_type(c0))) {
          forget(ln_->get_name(c0));
        }
      }
    }
  }

  void do_declare(const Lnast_nid& nid) {
    using N       = Lnast_ntype;
    auto name_nid = ln_->get_first_child(nid);
    if (name_nid.is_invalid()) {
      return;
    }
    const std::string name(ln_->get_name(name_nid));
    forget(name);
    auto type_nid = ln_->get_sibling_next(name_nid);
    auto mode_nid = type_nid.is_invalid() ? type_nid : ln_->get_sibling_next(type_nid);
    if (mode_nid.is_invalid() || !N::is_const(ln_->get_type(mode_nid))) {
      return;
    }
    auto mode = ln_->get_name(mode_nid);
    if (mode != "reg" && !mode.starts_with("reg ")) {
      return;
    }
    for (auto c = ln_->get_sibling_next(mode_nid); !c.is_invalid(); c = ln_->get_sibling_next(c)) {
      if (!N::is_stages(ln_->get_type(c))) {
        continue;
      }
      auto mn = ln_->get_first_child(c);
      if (mn.is_invalid()) {
        return;
      }
      auto mx = ln_->get_sibling_next(mn);
      Stg  s;
      s.decl_nid   = nid;
      auto min_txt = ln_->get_name(mn);
      auto max_txt = mx.is_invalid() ? min_txt : ln_->get_name(mx);
      s.nil        = min_txt == "nil" || max_txt == "nil";
      if (!s.nil) {
        s.min = const_of(mn);
        s.max = mx.is_invalid() ? s.min : const_of(mx);
      }
      pending_[name] = s;
      return;
    }
    // plain reg — unknown until the LG checker classifies it (state
    // pins sigma(q)=sigma(din); a feedforward stage reg crosses +1; both need
    // the graph-level enable/SCC analysis). Remember the name so its stores
    // never bind a sigma here.
    plain_regs_.insert(name);
  }

  void do_store(const Lnast_nid& nid) {
    using N  = Lnast_ntype;
    auto lhs = ln_->get_first_child(nid);
    if (lhs.is_invalid()) {
      return;
    }
    auto rhs = lhs.is_invalid() ? lhs : ln_->get_sibling_next(lhs);
    if (rhs.is_invalid()) {
      return;
    }
    const std::string name(ln_->get_name(lhs));
    if (!ln_->get_sibling_next(rhs).is_invalid()) {  // tuple/field store
      forget(name);
      return;
    }

    // A store to a plain reg is a next-state din write; sigma(q) is
    // the LG checker's call (state vs stage classification). Never bind it.
    if (plain_regs_.contains(name)) {
      forget(name);
      return;
    }

    if (auto pit = pending_.find(name); pit != pending_.end()) {
      const Stg stg = pit->second;
      pending_.erase(pit);
      do_stage_store(nid, name, stg, rhs);
      return;
    }

    forget(name);
    if (!N::is_ref(ln_->get_type(rhs))) {
      return;  // const rhs unifies with any cycle — no record needed
    }
    const std::string rname(ln_->get_name(rhs));
    if (auto cit = calls_.find(rname); cit != calls_.end()) {
      // Bare consumption of a call result: cycle = op + declared interval.
      // COPY before the operator[] insert below — flat_hash_map rehash would
      // invalidate the reference mid-assignment (the upass_attributes UAF
      // family).
      const CallInfo ci = cit->second;
      if (ci.op_known) {
        known_[name] = Cyc{ci.op.min + ci.cmin, ci.op.max + ci.cmax};
      }
      return;
    }
    if (auto kit = known_.find(rname); kit != known_.end()) {
      const Cyc v  = kit->second;  // copy BEFORE operator[] may rehash
      known_[name] = v;
    }
  }

  void do_stage_store(const Lnast_nid& store_nid, const std::string& name, const Stg& stg, const Lnast_nid& rhs) {
    using N = Lnast_ntype;
    if (!N::is_ref(ln_->get_type(rhs))) {
      // stage over a constant: a const unifies with any cycle; nothing checkable.
      return;
    }
    const std::string rname(ln_->get_name(rhs));
    if (auto cit = calls_.find(rname); cit != calls_.end()) {
      const CallInfo ci = cit->second;  // copy — known_ writes below may rehash
      if (stg.nil || stg.min != stg.max) {
        return;  // stage[] / ranged pick over a call — tolg/LG territory
      }
      const int64_t n = stg.min;
      if (ci.is_pipe) {
        if (n < ci.cmin || (ci.cmax >= ci.cmin && n > ci.cmax)) {
          if (ci.cmax >= ci.cmin) {
            error_at(stg.decl_nid.is_invalid() ? store_nid : stg.decl_nid,
                     "{}: stage[{}] on '{}' is outside the callee's declared latency range [{}, {}]",
                     loc_str(ln_, stg.decl_nid.is_invalid() ? store_nid : stg.decl_nid),
                     n,
                     name,
                     ci.cmin,
                     ci.cmax);
          } else {
            error_at(stg.decl_nid.is_invalid() ? store_nid : stg.decl_nid,
                     "{}: stage[{}] on '{}' is below the callee's declared minimum latency {}",
                     loc_str(ln_, stg.decl_nid.is_invalid() ? store_nid : stg.decl_nid),
                     n,
                     name,
                     ci.cmin);
          }
          return;
        }
      } else {
        if (ci.cmin != ci.cmax || n != ci.cmin) {
          error_at(stg.decl_nid.is_invalid() ? store_nid : stg.decl_nid,
                   "{}: mod call result '{}' lands at its declared cycle {} — `stage[{}]` must match it",
                   loc_str(ln_, stg.decl_nid.is_invalid() ? store_nid : stg.decl_nid),
                   name,
                   ci.cmin,
                   n);
          return;
        }
      }
      if (ci.op_known) {
        known_[name] = Cyc{ci.op.min + n, ci.op.max + n};  // the pick replaces the interval
      }
      return;
    }
    if (auto kit = known_.find(rname); kit != known_.end()) {
      const Cyc v = kit->second;  // copy BEFORE operator[] may rehash
      if (!stg.nil) {
        known_[name] = Cyc{v.min + stg.min, v.max + stg.max};
      }
    }
  }

  void do_call(const Lnast_nid& nid) {
    using N  = Lnast_ntype;
    auto dst = ln_->get_first_child(nid);
    if (dst.is_invalid()) {
      return;
    }
    auto callee_n = ln_->get_sibling_next(dst);
    if (callee_n.is_invalid()) {
      return;
    }
    const std::string dst_name(ln_->get_name(dst));
    forget(dst_name);

    auto callee = resolve_callee(ln_->get_name(callee_n), registry_);
    if (!callee) {
      return;
    }
    auto kind = callee->get_lambda_kind();
    if ((kind != "pipe" && kind != "mod") || callee->io_meta().outputs.size() != 1) {
      return;
    }
    if (callee->io_meta().outputs.front().stages_min < 0) {
      return;  // callee output declared `@[]` — its landing cycle is unconstrained
    }

    // Equal-meet over the ref actuals (consts unify with anything). Two
    // KNOWN fixed operands at different cycles is the 06c misalignment.
    Cyc  op{0, 0};
    bool have        = false;
    bool any_unknown = false;
    for (auto a = ln_->get_sibling_next(callee_n); !a.is_invalid(); a = ln_->get_sibling_next(a)) {
      Lnast_nid val = a;
      if (N::is_store(ln_->get_type(a))) {
        auto an = ln_->get_first_child(a);
        if (an.is_invalid()) {
          continue;
        }
        val = ln_->get_sibling_next(an);
        if (val.is_invalid()) {
          continue;
        }
      }
      if (!N::is_ref(ln_->get_type(val))) {
        continue;  // const actual
      }
      auto kit = known_.find(std::string(ln_->get_name(val)));
      if (kit == known_.end() || !kit->second.fixed()) {
        any_unknown = true;
        continue;
      }
      if (have && kit->second.min != op.min) {
        error_at(nid,
                 "{}: call operands at different cycles ({} vs {}) — align them with a `stage[N]` binding first",
                 loc_str(ln_, nid),
                 op.min,
                 kit->second.min);
        return;
      }
      op   = kit->second;
      have = true;
    }

    const auto& oe = callee->io_meta().outputs.front();
    CallInfo    ci;
    ci.op            = op;
    ci.op_known      = have && !any_unknown;
    ci.cmin          = oe.stages_min;
    ci.cmax          = oe.stages_max;
    ci.is_pipe       = kind == "pipe";
    calls_[dst_name] = ci;
  }

  void do_meet_op(const Lnast_nid& nid) {
    using N  = Lnast_ntype;
    auto dst = ln_->get_first_child(nid);
    if (dst.is_invalid() || !N::is_ref(ln_->get_type(dst))) {
      return;
    }
    const std::string dst_name(ln_->get_name(dst));
    forget(dst_name);

    Cyc  meet{0, 0};
    bool have        = false;
    bool any_unknown = false;
    for (auto a = ln_->get_sibling_next(dst); !a.is_invalid(); a = ln_->get_sibling_next(a)) {
      if (!N::is_ref(ln_->get_type(a))) {
        continue;  // const operand unifies
      }
      auto kit = known_.find(std::string(ln_->get_name(a)));
      if (kit == known_.end() || !kit->second.fixed()) {
        any_unknown = true;
        continue;
      }
      if (have && kit->second.min != meet.min) {
        error_at(nid,
                 "{}: '{}' mixes operands at different cycles ({} vs {}) — align them with `stage[N]` or check with `@[N]`",
                 loc_str(ln_, nid),
                 dst_name,
                 meet.min,
                 kit->second.min);
        return;
      }
      meet = kit->second;
      have = true;
    }
    if (have && !any_unknown) {
      known_[dst_name] = meet;
    }
  }

  // Queue the check; verification happens AFTER the walk against the final
  // derived state — a check is order-independent (the LHS form `x@[N] = …`
  // emits its timecheck BEFORE the store that defines x).
  void do_timecheck(const Lnast_nid& nid) {
    using N  = Lnast_ntype;
    auto ref = ln_->get_first_child(nid);
    if (ref.is_invalid()) {
      return;
    }
    auto mn = ln_->get_sibling_next(ref);
    if (mn.is_invalid()) {
      return;
    }
    auto mx = ln_->get_sibling_next(mn);
    // Already discharged in an earlier pass.upass run?
    if (!mx.is_invalid()) {
      auto extra = ln_->get_sibling_next(mx);
      if (!extra.is_invalid() && N::is_const(ln_->get_type(extra)) && ln_->get_name(extra) == "checked") {
        return;
      }
      // A ranged assert (`@[A..=B]` — multi-path values) needs the LG
      // checker's interval propagation; this pass only derives fixed cycles.
      if (const_of(mx) != const_of(mn)) {
        return;
      }
    }
    queued_checks_.push_back({nid, std::string(ln_->get_name(ref)), const_of(mn)});
  }

  void verify_checks() {
    for (const auto& qc : queued_checks_) {
      auto kit = known_.find(qc.name);
      if (kit == known_.end() || !kit->second.fixed()) {
        continue;  // not statically derivable — stays as an LG pending check
      }
      if (kit->second.min != qc.asserted) {
        error_at(qc.nid,
                 "{}: '{}' lands at cycle {}, not {} as `@[{}]` asserts",
                 loc_str(ln_, qc.nid),
                 qc.name,
                 kit->second.min,
                 qc.asserted,
                 qc.asserted);
        return;
      }
      ln_->add_child(qc.nid, Lnast_node::create_const("checked"));  // discharged
    }
  }

  void check_outputs() {
    const auto kind = ln_->get_lambda_kind();
    for (const auto& oe : ln_->io_meta().outputs) {
      auto kit = known_.find(oe.name);
      if (kit == known_.end() || !kit->second.fixed()) {
        continue;  // LG checker territory
      }
      const int64_t sigma = kit->second.min;
      if (kind == "mod") {
        // Declared single cycle (N,N). The `@[]` opt-out harvests as -1 —
        // explicitly unchecked, skip it.
        if (oe.stages_min >= 0 && oe.stages_min == oe.stages_max && sigma != oe.stages_min) {
          error_at(Lnast_nid{},
                   "{}: mod output '{}' lands at cycle {} but its interface declares @[{}]",
                   ln_->get_top_module_name(),
                   oe.name,
                   sigma,
                   oe.stages_min);
          return;
        }
      } else if (kind == "pipe") {
        // sigma <= declared min honesty (the body must support the ENTIRE
        // declared range; output padding fills the difference).
        if (oe.stages_min >= 1 && sigma > oe.stages_min) {
          error_at(Lnast_nid{},
                   "{}: pipe output '{}' lands at stage {} but the pipe declares a minimum of {}",
                   ln_->get_top_module_name(),
                   oe.name,
                   sigma,
                   oe.stages_min);
          return;
        }
      }
    }
  }

  void forget(std::string_view name) {
    known_.erase(std::string(name));
    calls_.erase(std::string(name));
  }

  void forget_written(const Lnast_nid& nid) {
    using N      = Lnast_ntype;
    const auto t = ln_->get_type(nid);
    if (N::is_store(t) || N::is_declare(t) || is_meet_op(t) || N::is_func_call(t)) {
      auto c0 = ln_->get_first_child(nid);
      if (!c0.is_invalid() && N::is_ref(ln_->get_type(c0))) {
        forget(ln_->get_name(c0));
        pending_.erase(std::string(ln_->get_name(c0)));
      }
    }
    for (auto c = ln_->get_first_child(nid); !c.is_invalid(); c = ln_->get_sibling_next(c)) {
      forget_written(c);
    }
  }

  [[nodiscard]] int64_t const_of(const Lnast_nid& nid) {
    auto c = Dlop::from_pyrope(ln_->get_name(nid));
    return (c && c->is_just_i64()) ? c->to_just_i64() : 0;
  }

  struct Queued_check {
    Lnast_nid   nid;
    std::string name;
    int64_t     asserted = 0;
  };

  std::shared_ptr<Lnast>                     ln_;
  const uPass_timecheck::Registry&           registry_;
  absl::flat_hash_map<std::string, Cyc>      known_;
  absl::flat_hash_map<std::string, Stg>      pending_;
  absl::flat_hash_map<std::string, CallInfo> calls_;
  absl::flat_hash_set<std::string>           plain_regs_;  // sigma owned by the LG checker
  std::vector<Queued_check>                  queued_checks_;
};

}  // namespace

void uPass_timecheck::run(const std::shared_ptr<Lnast>& lnast, const Registry& registry) {
  if (!lnast) {
    return;
  }
  if (lnast->get_skip_timecheck()) {
    return;  // todo/ 1s subtask E — inou.slang suppresses timechecks on its mods
  }
  const auto kind = lnast->get_lambda_kind();
  if (kind != "pipe" && kind != "mod") {
    return;  // comb/file trees carry no timing obligations
  }
  Discharger d(lnast, registry);
  d.run();
}
