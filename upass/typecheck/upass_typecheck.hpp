//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "diag.hpp"  // livehd::diag::Span — located diagnostics for builtins (cassert)
#include "kind.hpp"
#include "upass_core.hpp"

// uPass_typecheck — read-only KIND checker (see /Users/renau/projs/livehd/optable.md).
//
// Owns the `type`/`name` diagnostic categories: operator/operand kind
// homogeneity, `nil` poison, bool conditions, and no-type-change on writes. It
// NEVER looks at max/min or signedness — that is the bitwidth pass's job,
// enforced at the assignment via `wrap`/`sat`.
//
// There are ZERO implicit kind conversions: bool and int do NOT interoperate
// (`true ^ 0` is an error — write `int(true) ^ 0`); a variable's kind cannot
// change after it is established.
//
// MIGRATED to push-based dispatch: the runner resolves every operand
// to a Bundle and pushes (dst, src) in; kinds live on the bundles' typed
// Entry.kind field (declared kinds baked by the runner's declare pre-step;
// established kinds written here). The old per-name `kind_map` is gone — the
// scope-aware symbol table makes the sibling-scope kind conflation
// structurally impossible. Control-flow checks (if/while/cassert) stay on
// the nullary cursor hooks (those nodes are not push-dispatched) and read
// kinds from the shared table.
struct uPass_typecheck : public upass::uPass {
public:
  using upass::uPass::uPass;

  using Kind = upass::Kind;
  using Vote = upass::Vote;

  // Expose the inferred scalar kind so constprop's type-aware
  // `does`/`equals` fold can reject cross-kind comparisons (bool vs int) on
  // variables that carry no `:type` annotation. Reads the shared table.

  // 2-child store: establish the dst kind, or reject a kind change.
  // Field-path stores pass through (per-field checks are follow-up work).
  Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;

  // The runner's synthesized constructor stores (defaults bind +
  // ref-self write-back) assemble a declared-shape value incrementally; the
  // closed-shape check pauses while the window is open (same contract as
  // the attributes const tally).
  void notify_init_construction_begin() override { ++init_construction_depth_; }
  void notify_init_construction_end() override {
    if (init_construction_depth_ > 0) {
      --init_construction_depth_;
    }
  }

  // if / elif / when / unless conditions must be boolean (nullary: `if` is
  // not push-dispatched).
  void process_if() override;
  // while condition must be boolean.
  void process_while() override;
  // Builtin `cassert(cond:bool=nil, msg:string="")` — type-check the optional
  // diagnostic-message argument (must be a string).
  void process_cassert() override;
  // `range` is dispatched verbatim (not push) — endpoints must be integers.
  void process_range() override;

  // Arithmetic / bitwise / shift — int operands, int result.
  Vote process_plus(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_minus(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_mult(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_div(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_mod(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_xor(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_bit_not(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_shl(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_sra(std::string_view, Bundle&, upass::Src_span) override;

  // Logical keywords — bool operands, bool result.
  Vote process_log_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_log_not(std::string_view, Bundle&, upass::Src_span) override;

  // Reductions / popcount — int operand; reductions → bool, popcount → int.
  Vote process_red_or(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_and(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_red_xor(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_popcount(std::string_view, Bundle&, upass::Src_span) override;

  // Comparison — eq/ne (same class → bool), ordering (int → bool).
  Vote process_eq(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_ne(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_lt(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_le(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_gt(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_ge(std::string_view, Bundle&, upass::Src_span) override;

  // Bit manipulation / type-id — result kinds only. (get_mask stamps nothing:
  // single-bit→bool vs range→int is ambiguous; tuple_get likewise.)
  Vote process_set_mask(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_sext(std::string_view, Bundle&, upass::Src_span) override;

  // Aggregates — passthrough kinds, no homogeneity check.
  Vote process_tuple_add(std::string_view, Bundle&, upass::Src_span) override;
  Vote process_tuple_concat(std::string_view, Bundle&, upass::Src_span) override;

  // ── shared helpers (also used by constprop's does-fold via the seam) ──────
  // Kind of a bundle: the "0" Entry's kind when set; else tuple when the
  // shape says so; else unknown.
  static Kind kind_of_bundle(const Bundle& b);
  static Kind seed_kind_from_const(std::string_view literal_text);

private:
  int init_construction_depth_{0};
  static const char* kind_name(Kind k);
  // Equality / assignment compatibility class: int / bool / string distinct;
  // range and tuple share a class (`range == flat tuple` is legal); -1 for
  // unknown/nil (no concrete class — the check is skipped).
  static int eq_class(Kind k);

  // "reading an unset/nil field is an error" (when USED as an
  // operand; copies and ==nil/!=nil stay legal): a ref whose VALUE is the
  // nil it was initialized with reads as Kind::nil — the same poison the
  // literal carries. The runner's inliner seeds are exempt (a body stmt may
  // legally reference a not-yet-bound/unresolved param without erroring;
  // it just stays unfolded).
  Kind kind_of_operand(const upass::Operand& o) const {
    const Kind k = kind_of_bundle(*o.bundle);
    if (k == Kind::unknown && !o.name.empty() && o.bundle && runner_st != nullptr) {
      // A TUPLE-FIELD read (the ref carries a tget_origin) whose value is
      // nil is an unset field — poison when USED as an operand. Whole
      // nil-typed scalars (set_mask bit-building from a nil init) and attr
      // reads (the cassert nil escape hatch) stay legal, as do the
      // inliner's synthesized nil seeds.
      const std::string name{o.name};
      if (runner_st->tget_origin.contains(name) && !runner_st->nil_seeded.contains(name)) {
        const auto& t = o.bundle->get_entry("0").trivial;
        if (!t.is_invalid() && t.is_nil()) {
          return Kind::nil;
        }
      }
    }
    return k;
  }

  // Kind of the name via the shared table (nullary control-flow checks).
  Kind kind_of(std::string_view name) const;
  // Kind of a ref-or-const operand under the cursor (nullary checks only).
  Kind kind_of_operand_at_cursor();

  // Write `k` into dst's "0" Entry (scalar slot only; tuple kinds are
  // shape-derived and never written).
  static void set_dst_kind(Bundle& dst, Kind k);

  // `sym` is the source operator (for the message). `allow_nil` permits a nil
  // operand (the open-range `..` sentinel).
  void require_all(Kind required, Kind result, std::string_view sym, std::string_view code, Bundle& dst, upass::Src_span src,
                   bool allow_nil = false);
  void require_same(Kind result, std::string_view sym, std::string_view code, Bundle& dst, upass::Src_span src);
  // `a << b`: `a` integer; `b` integer OR a tuple of bit positions (the
  // documented one-hot form `1 << (1,4,3)`). Result integer.
  void require_shift(std::string_view sym, Bundle& dst, upass::Src_span src);

  void emit_type_error(std::string_view code, const std::string& msg, std::string_view hint = {},
                       livehd::diag::Span span = {});

  // Source span for an nid that carries a loc (cassert/func_call). Null otherwise.
  livehd::diag::Span span_from_nid(const Lnast_nid& nid) const;
};
