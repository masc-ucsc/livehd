//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_constprop.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <unordered_map>

#include "cell.hpp"
#include "diag.hpp"
#include "lnast_ntype.hpp"
#include "str_tools.hpp"
#include "upass_verifier.hpp"

// Registered once here (not in the header) to avoid duplicate-registration
// errors when multiple TUs include upass_constprop.hpp.
static upass::uPass_plugin cprop("constprop", upass::uPass_wrapper<uPass_constprop>::get_upass);

static constexpr std::string_view call_ref_arg_marker  = "__ref_arg";
// Task 1k — positional UFCS-receiver marker (see prp2lnast / upass_runner).
static constexpr std::string_view call_ufcs_arg_marker = "__ufcs_arg";

// Coerce one value to its text-form Const. Mirrors Pyrope's `string()` cast:
//   nil    → "nil"
//   string → as-is
//   int    → decimal text
// Returns invalid if `v` is invalid; the caller decides whether to bail or
// keep going.
static Const stringify_one(const Const& v) {
  if (v.is_invalid()) {
    return *Dlop::invalid();
  }
  if (v.is_nil()) {
    return *Dlop::from_string("nil");
  }
  if (v.is_string()) {
    return v;
  }
  // Render the value to a string Const (the result feeds a TEXT concat in
  // stringify_concat_trivials — returning `v` itself would bit-concat). The
  // `string()` cast wants decimal text; to_decimal_string is width-safe for
  // >64-bit values, unlike std::to_string(to_i()) which overflows.
  return *Dlop::from_string(std::string(v.to_decimal_string()));
}

// Stringify each entry in `vals` and text-concat them. Returns nullopt if any
// entry is invalid or has unknown bits (caller should bail and retry later);
// returns an empty string Const for an empty input.
static std::optional<Const> stringify_concat_trivials(const std::vector<Const>& vals) {
  if (vals.empty()) {
    return *Dlop::from_string("");
  }
  Const acc;
  bool  first = true;
  for (const auto& v : vals) {
    if (v.is_invalid() || v.has_unknowns()) {
      return std::nullopt;
    }
    auto s = stringify_one(v);
    if (s.is_invalid()) {
      return std::nullopt;
    }
    acc   = first ? s : *acc.concat_op(s);
    first = false;
  }
  return acc;
}

// Group an MSB-first binary string (from Dlop::to_binary) into a power-of-two
// base, `bpd` bits per digit, dropping leading zeros. Used by the `:b`/`:o`/
// `:x`/`:X` interpolation specs so they share one two's-complement bit view.
static std::string bits_to_grouped(std::string_view bits, int bpd, bool upper) {
  static constexpr std::string_view lo     = "0123456789abcdef";
  static constexpr std::string_view hi     = "0123456789ABCDEF";
  std::string_view                  digits = upper ? hi : lo;
  std::string                       padded;  // left-pad to a multiple of bpd
  if (int rem = static_cast<int>(bits.size()) % bpd; rem != 0) {
    padded.assign(static_cast<size_t>(bpd - rem), '0');
  }
  padded.append(bits);
  std::string out;
  for (size_t i = 0; i < padded.size(); i += static_cast<size_t>(bpd)) {
    unsigned v = 0;
    for (int k = 0; k < bpd; ++k) {
      v = (v << 1) | (padded[i + static_cast<size_t>(k)] == '1' ? 1U : 0U);
    }
    out.push_back(digits[v]);
  }
  auto first = out.find_first_not_of('0');
  return first == std::string::npos ? std::string("0") : out.substr(first);
}

// Render `v` per a std::format-style presentation spec for `"{expr:spec}"`
// interpolation (the `__fmt(value, 'spec')` cast emitted by prp2lnast). b/o/x/X
// share the two's-complement bit view (no prefix; matches std::format for
// non-negative values, e.g. 16 → "10000"/"20"/"10"); d (or empty) is signed
// decimal. Strings/nil keep their stringify_one rendering. Unsupported specs
// raise a compile error rather than silently mis-rendering.
// Enum identity tag of a bundle, or nullptr. The parse-time `__enumentry`
// attr (lower_enum_def) interns as "__enumentry" on a named-payload carrier
// but as "0.__enumentry" once a scalar carrier round-trips through
// Bundle::set/get_bundle — accept both spellings.
static const Const* enum_identity_of(const std::shared_ptr<Bundle const>& b) {
  if (!b) {
    return nullptr;
  }
  for (const auto& [k, ep] : b->get_attrs()) {
    if (Bundle::get_last_level(k) != "__enumentry") {
      continue;
    }
    // Top-level identity only. Scalar-carrier canonicalization may intern the
    // tag under one or more positional layers ("0.__enumentry",
    // "0.0.__enumentry"); an ENTRY tag of an enum-TYPE bundle
    // ("Red.__enumentry") has a named prefix and is NOT this bundle's own.
    auto             prefix     = Bundle::get_all_but_last_level(k);
    bool             zeros_only = true;
    std::string_view rest{prefix};
    while (!rest.empty() && zeros_only) {
      auto seg   = Bundle::get_first_level(rest);
      zeros_only = (seg == "0");
      rest       = Bundle::get_all_but_first_level(rest);
    }
    if (zeros_only && !ep->trivial.is_invalid()) {
      return &ep->trivial;
    }
  }
  return nullptr;
}

static std::string format_interp_value(const Const& v, std::string_view spec) {
  if (v.is_nil()) {
    return "nil";
  }
  if (v.is_string()) {
    upass::error("string-interpolation format spec ':{}' cannot apply to a string value\n", spec);
  }
  if (spec.size() > 1) {
    upass::error("unsupported string-interpolation format spec ':{}' (expected one of b/o/x/X/d)\n", spec);
  }
  switch (spec.empty() ? 'd' : spec.front()) {
    case 'd': return std::string(v.to_decimal_string());
    case 'b': return bits_to_grouped(v.to_binary(), 1, false);
    case 'o': return bits_to_grouped(v.to_binary(), 3, false);
    case 'x': return bits_to_grouped(v.to_binary(), 4, false);
    case 'X': return bits_to_grouped(v.to_binary(), 4, true);
    default : upass::error("unsupported string-interpolation format spec ':{}' (expected one of b/o/x/X/d)\n", spec);
  }
}

Const uPass_constprop::apply_range_mask(const Const& value, const Const& start, const Const& end) {
  // Bit-slice `value#[start..=end]` (and the open `value#[start..]` when `end`
  // is nil). Everything stays in Const arithmetic — no is_i/to_i, no int round-
  // trip, no width/limit guards (a nonsense range just yields a degenerate
  // mask). This handles arbitrary-precision values that would overflow int64.
  //
  // A concrete negative `start` (or negative width below) would hard-assert in
  // Dlop::shln / sra. This only happens for a nonsense range — e.g. folding a
  // still-parametric body (`x#[x.[bits]-1-i]` with `x` unbound) — so yield a
  // degenerate empty slice (0) rather than crash; real bound sites are valid.
  if (start.is_i() && start.to_i() < 0) {
    return *Dlop::create_integer(0);
  }
  // Open-ended `start..`: right-shift by `start` (upper bits packed LSB-first).
  if (end.is_nil()) {
    return *value.sra_op(start);
  }
  // Closed `start..=end`: build a contiguous mask of (end-start+1) ones at
  // position `start`, then get_mask_op selects+packs those bits to bit 0.
  //   mask = ((1 << (end - start + 1)) - 1) << start
  auto one   = Dlop::create_integer(1);
  auto width = end.sub_op(start)->add_op(*one);  // end - start + 1
  if (width->is_i() && width->to_i() < 0) {
    return *Dlop::create_integer(0);  // negative-width (empty) slice — degenerate
  }
  auto mask = one->shl_op(*width)->sub_op(*one)->shl_op(start);
  return *value.get_mask_op(*mask);
}

uPass_constprop::uPass_constprop(std::shared_ptr<upass::Lnast_manager>& _lm) : uPass(_lm) {
  st.function_scope(_lm->get_top_module_name());
}

void uPass_constprop::end_run() {
  if (pending_unsigned_overflow_msg_) {
    throw std::runtime_error(*pending_unsigned_overflow_msg_);
  }
}

void uPass_constprop::check_unsigned_positive_overflow(std::string_view lhs, const Const& value) {
  if (value.is_invalid() || value.is_nil() || value.is_string() || value.has_unknowns() || value.is_negative()) {
    return;
  }
  auto it = decl_unsigned_max_.find(std::string(lhs));
  if (it == decl_unsigned_max_.end()) {
    return;
  }
  const auto delta = value.sub_op(it->second);
  if (!delta || delta->is_invalid() || delta->has_unknowns() || delta->is_known_zero() || !delta->is_positive()) {
    return;
  }

  auto msg = std::format("`{}` (value {}) does not fit its declared range [0, {}]",
                         lhs,
                         value.to_decimal_string(),
                         it->second.to_decimal_string());
  if (!pending_unsigned_overflow_msg_) {
    pending_unsigned_overflow_msg_ = msg;
  }
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = "bitwidth-overflow",
      .category = "bitwidth",
      .pass     = "upass.constprop",
      .message  = msg,
      .hint     = "widen the declared type, force fewer bits with a bit-select, or apply a wrap/saturate policy",
  });
}

void uPass_constprop::set_function_registry(const std::vector<std::shared_ptr<Lnast>>& lnasts) {
  function_registry.clear();
  for (const auto& ln : lnasts) {
    if (!ln) {
      continue;
    }
    function_registry.emplace(std::string(ln->get_top_module_name()), ln);
  }
}

void uPass_constprop::process_assign() {
  move_to_child();

  auto lhs_text = current_text();

  // 2d-reg — a store to a reg-declared name is a next-state (din) write:
  // never bind it in the symbol table. Reads of the reg see the flop's q
  // (Verilog `<=` semantics), not the value written this cycle, so folding
  // the written value into later reads — or dropping the store because the
  // value "is known" — corrupts the state machine. tolg consumes the store.
  if (reg_decl_names_.contains(std::string(lhs_text))) {
    move_to_parent();
    return;
  }
  move_to_sibling();

  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    // RHS is a variable reference: alias the bundle in the symbol table.
    // Bundle aliasing (A = ___t1) is just pointer bookkeeping — the scalar
    // values it carries are propagated by process_tuple_get, not here.
    auto rhs_bundle = current_bundle();
    if (rhs_bundle) {
      // Type-shape preservation: when LHS is a *purely-named* bundle
      // (the shape declared by `mut foo:(x=…, y=…) = …` — no unnamed
      // slots) and RHS is a pure positional tuple, bind RHS unnamed
      // entries onto LHS's named slots in canonical (alphabetical) order.
      // This is what makes
      //   mut case1:(x=0, y=1) = (0, 1)
      //   case1 = (3, 4)        // → case1 becomes (x=3, y=4)
      // work.
      //
      // Skip the merge when LHS is mixed (named+unnamed, e.g. from
      // `mut tup0 = (0, y=1)`) — that's a plain initializer, not a typed
      // shape, and the user expects subsequent assignments to fully
      // replace. Also skip when RHS carries names of its own (full copy).
      auto existing = st.get_bundle(lhs_text);

      // Task 1t — named-type default materialization. When LHS was declared with
      // a named type `ref(NAMED)` and has no value yet, use NAMED's resolved
      // bundle (its default field values + per-field types) as the base, then
      // overlay the init bundle: named init fields override by name; positional
      // init entries bind to NAMED's named slots in canonical order. NAMED may
      // be declared via `type T=(…)` or `const T=(…)` — both leave T's bundle in
      // the symbol table. Example:
      //   const v_type=(x:u3=nil, b:string="foo"); mut c:v_type=(x=3) → c={x:3, b:"foo"}.
      // An ENUM VALUE rhs (carries the parse-time __enumentry identity attr)
      // binds whole — never merged over the named type's defaults: for
      // `mut y:Color = Color.Red` the named type IS the enum-type bundle
      // (all entries), and overlaying the value onto it would turn y into
      // the whole enum table.
      if ((!existing || existing->non_attr_entries().empty()) && enum_identity_of(rhs_bundle) == nullptr) {
        if (auto nt = decl_named_type_.find(std::string(lhs_text)); nt != decl_named_type_.end()) {
          auto base = st.get_bundle(nt->second);
          if (base && base.get() != rhs_bundle.get() && !base->non_attr_entries().empty()) {
            auto merged = std::make_shared<Bundle>(std::string(lhs_text));
            for (const auto& [bk, bep] : base->non_attr_entries()) {
              merged->set(bk, *bep);  // the named type's default fields/values
            }
            std::vector<std::string> base_named;  // canonical-order named slots
            for (const auto& tl : base->top_levels()) {
              if (tl.pos < 0) {
                base_named.emplace_back(tl.name);
              }
            }
            size_t pidx = 0;
            for (const auto& [rk, rep] : rhs_bundle->non_attr_entries()) {
              bool numeric = !rk.empty();
              for (char ch : rk) {
                if (ch < '0' || ch > '9') {
                  numeric = false;
                  break;
                }
              }
              if (numeric) {  // positional init entry → bind to next named slot
                if (pidx < base_named.size()) {
                  merged->set(base_named[pidx], *rep);
                  ++pidx;
                } else {
                  merged->set(rk, *rep);
                }
              } else {  // named init field → overlay by name
                merged->set(rk, *rep);
              }
            }
            st.set(lhs_text, merged);
            if (tuple_typed_names.contains(std::string(current_text()))) {
              tuple_typed_names.insert(std::string(lhs_text));
            }
            move_to_parent();
            return;
          }
        }
      }

      bool                     do_merge = false;
      // LHS named slots in canonical (alphabetical) order via top_levels().
      // Nested entries (`x.b`, `x.c`) collapse to one Top_level_entry by
      // construction, so no manual de-duplication is needed.
      std::vector<std::string> lhs_named_order;
      if (existing && existing.get() != rhs_bundle.get() && !existing->non_attr_entries().empty()) {
        bool lhs_has_unnamed = false;
        for (const auto& tl : existing->top_levels()) {
          if (tl.pos < 0) {
            lhs_named_order.emplace_back(tl.name);
          } else {
            lhs_has_unnamed = true;
            break;
          }
        }
        bool rhs_pure_positional = true;
        if (!lhs_has_unnamed && !lhs_named_order.empty()) {
          for (const auto& tl : rhs_bundle->top_levels()) {
            if (tl.pos < 0) {
              rhs_pure_positional = false;
              break;
            }
          }
        }
        do_merge = !lhs_has_unnamed && !lhs_named_order.empty() && rhs_pure_positional;
      }
      if (do_merge) {
        // RHS unnamed entries in numeric (canonical) order, walked by
        // looking them up at "0", "1", …
        auto merged = std::make_shared<Bundle>(std::string(lhs_text));
        // Start by copying LHS shape verbatim (incl. nil placeholders and
        // any nested sub-entries).
        for (const auto& [lhs_key, lhs_ep] : existing->non_attr_entries()) {
          merged->set(lhs_key, *lhs_ep);
        }
        // Bind each RHS positional value to the next LHS named slot.
        size_t idx = 0;
        for (const auto& name : lhs_named_order) {
          auto pos_key = std::to_string(idx);
          if (!rhs_bundle->has_trivial(pos_key)) {
            break;
          }
          merged->set(name, rhs_bundle->get_trivial(pos_key));
          ++idx;
        }
        // RHS positions beyond LHS shape append as unnamed slots after
        // the named ones — preserves "wider RHS appends new positions"
        // semantics, picking up where the named binding stopped.
        size_t append_pos = 0;
        for (;; ++append_pos) {
          auto pos_key = std::to_string(idx + append_pos);
          if (!rhs_bundle->has_trivial(pos_key)) {
            break;
          }
          merged->set(std::to_string(append_pos), rhs_bundle->get_trivial(pos_key));
        }
        st.set(lhs_text, merged);
      } else {
        st.set(lhs_text, rhs_bundle);
      }
      // Propagate the tuple-typed flag across aliasing assigns. `c = ___4`
      // where ___4 came from a tuple_concat keeps c marked as a tuple, so
      // a later tuple_concat in this walk reads c via bundle mode rather
      // than mis-classifying it as a scalar wrapper.
      if (tuple_typed_names.contains(std::string(current_text()))) {
        tuple_typed_names.insert(std::string(lhs_text));
      }
    } else if (st.has_trivial(current_text())) {
      // Scalar RHS (stored as trivial, not a bundle). Propagate the value so
      // subsequent uses of `lhs_text` resolve.
      const auto value = st.get_trivial(current_text());
      if (!st.has_trivial(lhs_text)) {
        check_unsigned_positive_overflow(lhs_text, value);
      }
      store_trivial(lhs_text, value);
    } else if (st.has_trivial(lhs_text) && st.in_uncertain_scope()) {
      // 2d-reg — a RUNTIME-rhs write inside an uncertain if-arm: the LHS can
      // no longer be claimed comptime-known. Comptime writes get invalidated
      // on arm exit via record_uncertain_modification, but a runtime rhs
      // never touched the varmap, so the stale pre-branch value would leak
      // through fold_ref/classify — folding `if c { r = <runtime> }; out = r`
      // down to the stale init and dropping the `out` store entirely.
      // (Straight-line runtime reassignments don't need this: SSA versions
      // them; branch bodies are copied verbatim, hence the uncertain gate.)
      st.set(lhs_text, Symbol_table::invalid_lconst);
    }
  } else if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    Const v = current_pyrope_value();
    // Task 1t — coerce a known-negative literal to its unsigned N-bit pattern
    // on the var's FIRST scalar write (the declaration's initializer), so
    // constprop's own folding of later reads sees the unsigned value. The
    // gate mirrors the attributes side: the value must sign-extend a KNOWN 1
    // past the declared width (`bit_test`+!`unknown_bit_test` — `0sb?` keeps
    // its natural width), and `!st.has_trivial` restricts it to the first
    // write so per-statement wrap/sat reassignments stay in control.
    // Task 1b — reinterpret a known-negative first-write literal to its
    // unsigned pattern via `v & max` (for uN, max is the N-bit all-ones mask).
    // No width/to_i. `is_negative()` is false for an unknown sign bit (`0sb?`),
    // so that case keeps its natural width (see valid_simple); a known-1 sign
    // bit (incl. interior-unknown patterns like `0sb1?01_?000`) is reinterpreted.
    if (auto it = decl_unsigned_max_.find(std::string(lhs_text));
        it != decl_unsigned_max_.end() && !st.has_trivial(lhs_text) && v.is_negative()) {
      v = *v.and_op(it->second);
    }
    if (!st.has_trivial(lhs_text)) {
      check_unsigned_positive_overflow(lhs_text, v);
    }
    // Whole-var scalar (re)assignment replaces any prior tuple shape. A bare
    // `store_trivial` only writes field "0", so if `lhs_text` currently holds a
    // multi-field bundle (e.g. the match-local `t = (a=2,b=60)` released via the
    // synthesized `t = nil` after the match block) its other fields stay live and
    // `t` keeps reading as a tuple. Replace the bundle wholesale (st.set with a
    // bare-var key swaps the whole bundle) so `t == nil` sees nil. Bare-var LHS
    // only (no '.') — a field write like `t.0 = …` must keep the surrounding shape.
    if (lhs_text.find('.') == std::string_view::npos) {
      if (auto b = st.get_bundle(lhs_text); b && !b->is_empty() && !b->is_scalar()) {
        auto fresh = std::make_shared<Bundle>(std::string(lhs_text));
        fresh->set(v);
        st.set(lhs_text, fresh);
        move_to_parent();
        return;
      }
    }
    store_trivial(lhs_text, v);
  } else {
    // RHS is a compound expression (tuple_get, tuple_set, func_call, attr_get, attr_set, etc.)
    // Not yet handled by constprop.  Skip this assignment to avoid crashes.
    // TODO: Recursively process RHS expression and track the result.
  }

  move_to_parent();
}

void uPass_constprop::process_declare() {
  // declare(ref(var), TYPE, const(mode), [value]). Record the declared
  // UNSIGNED width (uN, or int(max,min≥0)) so the var's first scalar write can
  // coerce a known-negative literal to its unsigned N-bit pattern (see the
  // const path of process_assign). Signed / none-typed decls are ignored.
  if (!move_to_child()) {
    return;
  }
  auto var = std::string(current_text());
  // 2d-reg — record reg-declared names (mode const at child 2): their reads
  // are runtime q reads that must never fold, and their stores are next-state
  // din writes that must survive to tolg. Declare the name valueless in the
  // symbol table so `q == nil` doesn't take the undeclared-ref fold.
  {
    const auto here = lm->save_cursor();
    if (move_to_sibling() && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_const)) {
      auto mode = current_text();
      if (mode == "reg" || mode.starts_with("reg ")) {
        reg_decl_names_.insert(var);
        st.var(var);
      }
    }
    lm->restore_cursor(here);
  }
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  const auto t = get_raw_ntype();
  if (Lnast_ntype::is_ref(t)) {
    // Task 1t — named type: the type slot is a `ref(NAMED)`. Record it so the
    // var's init write materializes NAMED's default fields (see process_assign).
    decl_named_type_[var] = std::string(current_text());
  } else if (Lnast_ntype::is_prim_type_int(t)) {
    // prim_type_int(max, min): unsigned iff min is known and ≥ 0. Record the
    // declared MAX as a Const (for uN this IS the N-bit all-ones mask) so the
    // var's first scalar write can reinterpret a negative literal to its
    // unsigned pattern via `v & max` (see process_assign). No bits/to_i — works
    // for arbitrarily wide maxes. ("nil" children parse non-integer → unset.)
    std::optional<Const> max_v;
    std::optional<Const> min_v;
    if (move_to_child()) {
      if (Lnast_ntype::is_const(get_raw_ntype())) {
        auto v = Dlop::from_pyrope(current_text());
        if (v->is_integer()) {
          max_v = *v;
        }
      }
      if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {
        auto v = Dlop::from_pyrope(current_text());
        if (v->is_integer()) {
          min_v = *v;
        }
      }
      move_to_parent();
    }
    if (min_v && !min_v->is_negative() && max_v && !max_v->is_negative()) {
      move_to_parent();
      decl_unsigned_max_[var] = *max_v;
      return;
    }
  }
  move_to_parent();
}

template <typename F>
void uPass_constprop::process_nary(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const r = current_prim_value();
  if (!is_numeric(r)) {
    move_to_parent();
    return;
  }
  while (move_to_sibling()) {
    auto operand = current_prim_value();
    if (!is_numeric(operand)) {
      move_to_parent();
      return;
    }
    op(r, operand);
  }
  move_to_parent();
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

template <typename F>
void uPass_constprop::process_binary(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const n1 = current_prim_value();
  move_to_sibling();
  Const n2 = current_prim_value();
  move_to_parent();
  if (!foldable(n1) || !foldable(n2)) {
    return;
  }
  Const r = op(n1, n2);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

// Same shape as process_binary but does NOT pre-reject unknowns. Used for
// ops like lt/le/gt/ge whose Dlop implementations propagate unknowns to a
// 1-bit unknown result themselves — the constprop wrapper just hands the
// raw operands through.
template <typename F>
void uPass_constprop::process_binary_passthrough(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const n1 = current_prim_value();
  move_to_sibling();
  Const n2 = current_prim_value();
  move_to_parent();
  if (!is_numeric(n1) || !is_numeric(n2)) {
    return;
  }
  Const r = op(n1, n2);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

template <typename F>
void uPass_constprop::process_unary(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const r = current_prim_value();
  move_to_parent();
  // Delegate to Dlop: only skip non-values (invalid/string). Unknown (`?`)
  // bits flow through — not_op et al. propagate them bit-precisely.
  if (!is_numeric(r)) {
    return;
  }
  op(r);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

void uPass_constprop::process_plus() {
  process_nary([](Const& r, Const n) { r = r.add_op(n); });
}

void uPass_constprop::process_minus() {
  process_nary([](Const& r, Const n) { r = r.sub_op(n); });
}

void uPass_constprop::process_mult() {
  process_nary([](Const& r, Const n) { r = r.mult_op(n); });
}

void uPass_constprop::process_div() {
  process_nary([](Const& r, Const n) { r = r.div_op(n); });
}

void uPass_constprop::process_bit_and() {
  process_nary([](Const& r, Const n) { r = r.and_op(n); });
}

void uPass_constprop::process_bit_or() {
  process_nary([](Const& r, Const n) { r = r.or_op(n); });
}

void uPass_constprop::process_bit_not() {
  process_unary([](Const& r) { r = r.not_op(); });
}

void uPass_constprop::process_bit_xor() {
  process_nary([](Const& r, Const n) { r = r.xor_op(n); });
}

void uPass_constprop::process_mod() {
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.mod_op(n2); });
}

void uPass_constprop::process_shl() {
  // A negative shift amount would hard-assert in Dlop::shln (src2 >= 0). It can
  // arise when folding a still-parametric body — e.g. evaluating the extracted
  // standalone copy of `comb f(x){ … x.[bits]-1-i … }` whose param is unbound,
  // so the index folds to a bogus negative. Leave the shl unresolved instead of
  // crashing; real (bound) call sites always fold a non-negative shift.
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  Const n1 = current_prim_value();
  move_to_sibling();
  Const n2 = current_prim_value();
  move_to_parent();
  if (!is_numeric(n1) || !is_numeric(n2)) {
    return;
  }
  if (n2.is_i() && n2.to_i() < 0) {
    // A negative shift amount is a comptime error (it would also hard-assert in
    // Dlop::shln). Diagnose it — but only in a real top module, not when folding
    // an EXTRACTED parametric function body (named "<top>.<fn>"): there the
    // params are unbound, so an index like `x.[bits]-1-i` can fold bogus-
    // negative as an optimization artifact, not a user mistake. A genuine
    // in-function negative shift still surfaces via the body's inlined copy,
    // which folds under the real (dot-less) top module.
    if (lm->get_top_module_name().find('.') == std::string_view::npos) {
      livehd::diag::Span span;
      if (const auto& ln = lm->get_lnast()) {
        const auto loc = ln->get_loc(lm->get_current_nid());
        const auto fn  = ln->get_fname(lm->get_current_nid());
        if (!fn.empty()) {
          span.file = std::string{fn};
        }
        if (loc.line != 0) {
          span.start_line = loc.line;
        }
      }
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "negative-shift",
          .category = "type",
          .pass     = "upass.constprop",
          .message  = std::format("shift amount is negative ({})", std::string(n2.to_pyrope())),
          .span     = std::move(span),
          .hint     = "a shift / bit-select count must be >= 0",
      });
    }
    return;  // invalid (negative) shift — do not fold
  }
  Const r = *n1.shl_op(n2);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

void uPass_constprop::process_sra() {
  process_binary_passthrough([](Const n1, Const n2) -> Const { return *n1.sra_op(n2); });
}

// log_* results are bool-TYPED in Pyrope; Dlop's bitwise ops return integer
// payloads (true == ~0 == -1). Re-type a known result so downstream
// bool-sensitive folds (`int(true) == 1`, bool-typed bundle fields) see a
// real bool instead of `-1`. Unknown/invalid/nil results pass through.
static Const log_result_as_bool(const Const& r) {
  if (r.is_invalid() || r.is_nil() || r.has_unknowns()) {
    return r;
  }
  return *Dlop::from_pyrope(r.is_known_true() ? "true" : "false");
}

void uPass_constprop::process_log_and() {
  // Pyrope's type rule (future type-check): `and` operands must already be
  // bool — non-bool sources are required to write `foo != 0 and bar != 0`.
  // With bool operands, bitwise AND equals logical AND, so we just call
  // Dlop::and_op and let it deal with unknowns.
  process_binary_passthrough([](Const n1, Const n2) -> Const { return log_result_as_bool(*n1.and_op(n2)); });
}

void uPass_constprop::process_log_or() {
  // Same rationale as process_log_and: assume bool operands; bitwise OR over
  // bool values equals logical OR.
  process_binary_passthrough([](Const n1, Const n2) -> Const { return log_result_as_bool(*n1.or_op(n2)); });
}

void uPass_constprop::process_log_not() {
  // Pyrope's type rule (future type-check): `not` operand must be bool.
  // Bitwise NOT over a 1-bit bool (0 ↔ -1) flips it. nil stays nil — chained
  // `and` with a known-true operand still collapses to true (the cassert
  // escape hatch for unset attrs documented in attribute_todo.md §Phase 2).
  process_unary([](Const& r) {
    if (r.is_nil()) {
      return;
    }
    r = log_result_as_bool(*r.not_op());
  });
}

// Bundle-aware equality. Returns nullopt when constprop can't decide
// (unknown entries, attribute round-trip artefacts). Otherwise true/false.
//
// Pyrope tuple equality is order-insensitive on names: `(x=1,d=4) ==
// (d=4,x=1)` is true, since both bundles carry the same {name → value}
// mapping. After the bundle_sorted refactor, Bundle storage is already
// in canonical order (attributes → named alphabetical → unnamed by
// index), so name matching is direct and position matching falls out of
// the unnamed-by-index ordering. Nested paths (`b.c`) walk by exact key.
// Canonical key shape detection:
//   - "__attr"          → attribute (covered by Bundle::is_attribute)
//   - "0", "1", …       → unnamed (first char is a digit)
//   - anything else     → named (bare name)

// Bundle == result is *three-valued*:
//   - std::nullopt        → can't decide (operand has unknowns)
//   - Const{create_bool}  → true / false (definite shape+value verdict)
//
// Tuple equality is purely structural value comparison: attributes
// (declared mut/const, type tags, user-set decorations) are NOT compared.
// `non_attr_entries()` already strips attribute keys from both sides, so
// only data fields participate. When both operands have concrete
// (no-unknowns) values at the same key and those values differ, the
// result is concrete `false` — there is no "nil-as-pass" softening for
// named-key mismatches. (Earlier versions returned nil so that
// `cassert m1 == t` would discharge as pass when m1 and t shared keys
// but values diverged; that masked real comparison failures and broke
// `match`/`case` folding because the resulting nil cond marked downstream
// arms as uncertain.)
static std::optional<Const> compare_bundles_eq(const std::shared_ptr<Bundle const>& a, const std::shared_ptr<Bundle const>& b) {
  // Walks `src` and inspects each entry in `other`. Returns:
  //   - 'f' definite false (key missing on the other side, or concrete
  //          values mismatch at a shared key)
  //   - 't' all entries matched concretely
  //   - '?' undecidable (invalid scalar, or eq returned unknowns)
  auto walk = [](const std::shared_ptr<Bundle const>& src, const std::shared_ptr<Bundle const>& other) -> char {
    char worst = 't';
    for (const auto& [k, ep] : src->non_attr_entries()) {
      if (ep->trivial.is_invalid()) {
        // Both sides agreeing a slot is undefined (e.g. the value-less
        // `mut c:u24` field of a payload type) is a match on that key, not
        // an undecidable — payload-enum compares would otherwise never fold.
        if (other->has_trivial(k) && other->get_trivial(k).is_invalid()) {
          continue;
        }
        worst = '?';
        continue;
      }
      if (!other->has_trivial(k)) {
        return 'f';
      }
      const Const& ov = other->get_trivial(k);
      if (ov.is_invalid()) {
        worst = '?';
        continue;
      }
      if (ov.same_repr(ep->trivial)) {
        continue;
      }
      // Pyrope forbids MIXING TYPES across a comparison (like `bool + int`):
      // comparing a KNOWN `bool` to a known non-bool (or string vs number) is a
      // COMPILE ERROR, not an implicit convert — write it explicitly, e.g.
      // `(x != 0) == b`. Gated on both-known + non-nil (nil = `x == nil` idiom;
      // an unknown cross-type compare falls through to eq_op's tri-state).
      if (!ov.is_nil() && !ep->trivial.is_nil() && !ov.has_unknowns() && !ep->trivial.has_unknowns()
          && (ov.is_bool() != ep->trivial.is_bool() || ov.is_string() != ep->trivial.is_string())) {
        upass::error("uPass_constprop: comparison mixes types (bool vs int/string) — convert explicitly, e.g. `(x != 0) == b`\n");
      }
      const Const eq = *ov.eq_op(ep->trivial);
      if (eq.is_known_true()) {
        continue;
      }
      if (eq.is_known_false()) {
        return 'f';  // concrete value mismatch — definite inequality
      }
      worst = '?';
    }
    return worst;
  };

  const char ab = walk(a, b);
  if (ab == 'f') {
    return *Dlop::create_bool(false);
  }
  if (ab == '?') {
    return std::nullopt;
  }
  const char ba = walk(b, a);
  if (ba == 'f') {
    return *Dlop::create_bool(false);
  }
  if (ba == '?') {
    return std::nullopt;
  }
  return *Dlop::create_bool(true);
}

// Structural-only `a does b` check.
//
// `a does b` succeeds when a's tuple shape covers every non-attribute
// first-level entry in b:
//   - For every NAMED field in b (bare name), a must have any first-level
//     key with the same `name` — position is irrelevant. So
//     `(a=1,b=0) does (b=2,a=0)` is true even though `a` and `b` swap
//     positions on the two sides.
//   - For every UNNAMED entry in b (key starts with a digit, like `1`), a
//     must have an UNNAMED entry at the same position. A named slot at
//     that position does *not* satisfy the unnamed requirement, so
//     `(a=3) does (1)` is false (b has unnamed pos 0, a has only a named
//     field).
//
// Values are not compared here — this is the structural half of `does`.
// Bundle keys may be hierarchical (e.g. `foo.bar`); we look at the first
// level only, which is enough for the flat-tuple cases the comptime
// tests exercise. Nested-tuple structural matching can be added later.
static bool structural_does(const std::shared_ptr<Bundle const>& a, const std::shared_ptr<Bundle const>& b) {
  // Per top-level segment of b: a must have a matching top-level entry of
  // the same kind. top_levels() collapses nested keys (`foo.x`/`foo.y`)
  // into one entry, so each is checked once.
  for (const auto& tb : b->top_levels()) {
    if (tb.pos < 0) {
      if (!a->has_top_named(tb.name)) {
        return false;
      }
    } else {
      if (!a->has_top_unnamed(tb.pos)) {
        return false;
      }
    }
  }
  return true;
}

// process_eq / process_ne with bundle awareness. Falls back to the scalar
// process_binary path when neither operand is a tracked bundle, so plain
// integer compares are unchanged.
//
// Result handling is tri-state to support unknowns (`0sb?` literals): the
// final stored value is `Dlop::create_bool(true)` for known-true (negated for
// ne), the matching `Dlop::create_bool(false)`, or a 1-bit unknown when the
// comparison itself is undecidable. Storing the 1-bit unknown lets a downstream
// `(v != 0) == 0sb?` cassert fold via Lconst::eq_op's structural-identity
// short-circuit.
template <bool Negate>
void uPass_constprop::process_eq_ne_impl() {
  // Resolve an operand to one of three states:
  //   - bundle: a tracked tuple (multi-entry or non-scalar wrapper)
  //   - scalar: a known Const (default is invalid; never zero)
  //   - is_const_nil: literal `nil` const
  // (Reading an undeclared/out-of-scope name is a prp2lnast compile error —
  // check_undefined_reads — so no undeclared-reads-as-nil folding here.)
  struct Operand {
    std::shared_ptr<Bundle const> bundle;
    Const                         scalar;
    bool                          is_const_nil = false;
  };
  auto resolve = [this]() -> Operand {
    Operand o;
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto name = current_text();
      // Cross-pass override: when uPass_attributes published a narrowed
      // value for this ref (wrap/sat policy, see attribute_todo.md §Phase
      // 5), the override must win over the raw bundle in our ST. The
      // override is published via runner_fold_fn / fold_ref before the
      // operand is read here.
      if (runner_fold_fn) {
        auto folded = runner_fold_fn(name);
        if (folded && !folded->is_invalid()) {
          o.scalar = *folded;
          return o;
        }
      }
      auto b = st.get_bundle(name);
      if (b && !b->is_scalar()) {
        o.bundle = b;
      } else if (b && b->is_scalar()) {
        // Single-entry bundle: flatten via Bundle::get_trivial() (no-arg)
        // which returns the lone non-attr entry's value regardless of key
        // depth. Symbol_table::has_trivial keys by field "0" which fails to
        // match nested named keys like `first.second`, so for a tuple-of-
        // tuple-of-… single-entry bundle we'd otherwise miss the scalar
        // (`cassert x == 3` where `x = (first=(second=3))`).
        auto v = b->get_trivial();
        if (!v.is_invalid()) {
          o.scalar = v;
        }
      } else if (st.has_trivial(name)) {
        o.scalar = st.get_trivial(name);
      } else if (runner_fold_fn) {
        // Cross-pass fallback: another pass (e.g. uPass_attributes folding
        // an `attr_get` destination) may have a concrete value for this
        // ref even though constprop never assigned it.
        auto v = runner_fold_fn(name);
        if (v && !v->is_invalid()) {
          o.scalar = *v;
        }
      }
      // else: no concrete value yet — leave scalar invalid.
    } else {
      o.scalar       = current_pyrope_value();
      o.is_const_nil = o.scalar.is_nil();
    }
    return o;
  };

  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Operand a = resolve();
  move_to_sibling();
  Operand b = resolve();
  move_to_parent();

  // Mixed nil propagation: exactly one operand is a known-nil scalar. The
  // result is nil (indeterminate) — typically because the var was
  // mutated under an uncertain arm and Symbol_table::leave_scope
  // re-pinned it to nil. Both-sides-nil falls through to the regular eq
  // path (same_repr → known-true). nil-vs-bundle is handled here too
  // (a bundle never compares equal to nil concretely).
  const bool a_nil            = (!a.bundle && !a.scalar.is_invalid() && a.scalar.is_nil());
  const bool b_nil            = (!b.bundle && !b.scalar.is_invalid() && b.scalar.is_nil());
  const bool a_concrete_other = (a.bundle != nullptr) || (!a.scalar.is_invalid() && !a.scalar.is_nil());
  const bool b_concrete_other = (b.bundle != nullptr) || (!b.scalar.is_invalid() && !b.scalar.is_nil());
  if ((a_nil && b_concrete_other) || (b_nil && a_concrete_other)) {
    // A *literal* `nil` compared against a concrete value is decidable, not
    // indeterminate: a real value (int, string, or bundle) is never the nil
    // literal, so `x == nil` folds to known-false and `x != nil` to known-true
    // — there is nothing special about `nil` on the RHS of an `==`. Only an
    // *indeterminate* nil (a var re-pinned to nil by Symbol_table::leave_scope
    // under an uncertain arm, which carries no is_const_nil flag) stays nil so
    // the cassert discharges as pass (see the verifier nil branch /
    // attributes_spec §Phase 2).
    const bool literal_nil_cmp = (a_nil && a.is_const_nil) || (b_nil && b.is_const_nil);
    store_trivial(var, literal_nil_cmp ? *Dlop::create_bool(Negate) : *Dlop::nil());
    return;
  }

  // Three outcomes the rest of the pass cares about: known-true, known-false,
  // or a 1-bit unknown. Bundles only produce known true/false; the scalar
  // path may produce unknowns when an operand has them.
  std::optional<Const> result;
  // Mixed bundle-vs-scalar: a multi-entry tuple can never structurally
  // equal a scalar, but a 1-entry tuple `(v,)` is equivalent to its scalar
  // `v`. Without this case `(1,2) != (1,)` (where Symbol_table::set wraps
  // the 1-tuple as a scalar at position 0) hits neither the bundle-eq
  // nor scalar-eq paths and stays unfolded.
  auto                 bundle_count_non_attr
      = [](const std::shared_ptr<Bundle const>& b) { return static_cast<int>(b->non_attr_entries().size()); };
  if (a.bundle && b.bundle) {
    // Same bundle object (whole-bundle alias, e.g. `mut y:Color = Color.Red`)
    // is equal by identity — even when entries carry undefined slots.
    if (a.bundle.get() == b.bundle.get()) {
      store_trivial(var, *Dlop::create_bool(!Negate));
      return;
    }
    if (auto eq = compare_bundles_eq(a.bundle, b.bundle); eq.has_value()) {
      // Tri-state: nil propagates verbatim (the
      // structural-match-with-value-mismatch case discharges as pass via
      // cassert); concrete bool flips for Negate.
      if (eq->is_nil()) {
        result = *eq;
      } else if (eq->is_known_true()) {
        result = *Dlop::create_bool(!Negate);
      } else {
        result = *Dlop::create_bool(Negate);
      }
    }
  } else if (a.bundle && !b.scalar.is_invalid() && bundle_count_non_attr(a.bundle) > 1) {
    result = *Dlop::create_bool(Negate);
  } else if (b.bundle && !a.scalar.is_invalid() && bundle_count_non_attr(b.bundle) > 1) {
    result = *Dlop::create_bool(Negate);
  } else if (!a.bundle && !b.bundle && !a.scalar.is_invalid() && !b.scalar.is_invalid()) {
    // same_repr gives bit-identity including unknown positions (so
    // `0sb? == 0sb?` is known-true). eq_op only reduces to known-false
    // when neither side carries unknowns; otherwise it returns a 1-bit
    // unknown which we propagate. `ne` is bitwise-not of `eq`; a 1-bit
    // unknown inverted is still a 1-bit unknown.
    if (a.scalar.same_repr(b.scalar)) {
      result = *Dlop::create_bool(!Negate);
    } else if (same_bits_ignore_type(a.scalar, b.scalar)) {
      // Type-agnostic structural identity (same bit pattern, differing Pyrope
      // type). NOTE: a strict "mixing types in a comparison is a compile error"
      // rule belongs here for the SCALAR path too, but enforcing it currently
      // breaks bit_select/bitreduce/cellmap_comb, which intentionally compare
      // bool-vs-int (and mix bool/int inside `^`/`|`), so the strict scalar +
      // all-operator typecheck is a separate pass. The mixed-type error IS
      // enforced for TUPLE comparisons (see the bundle `walk` above).
      result = *Dlop::create_bool(!Negate);
    } else {
      // Same type (both int / both string), or one side unknown: fold via eq_op.
      const Const eq = *a.scalar.eq_op(b.scalar);
      if (eq.is_known_true()) {
        result = *Dlop::create_bool(!Negate);
      } else if (eq.is_known_false()) {
        result = *Dlop::create_bool(Negate);
      } else if (eq.has_unknowns()) {
        result = eq;
      }
    }
  }

  if (result.has_value()) {
    store_trivial(var, *result);
  }
}

void uPass_constprop::process_ne() { process_eq_ne_impl<true>(); }
void uPass_constprop::process_eq() { process_eq_ne_impl<false>(); }

void uPass_constprop::process_lt() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.lt_op(y); });
}
void uPass_constprop::process_le() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.le_op(y); });
}
void uPass_constprop::process_gt() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.gt_op(y); });
}

void uPass_constprop::process_ge() {
  process_binary_passthrough([](Const x, Const y) -> Const { return *x.ge_op(y); });
}

void uPass_constprop::process_is() {
  // `is` is folded by upass_attributes (which has the typename data);
  // constprop is a no-op for now beyond keeping the node alive so its
  // dst tmp is still readable downstream.
}

void uPass_constprop::process_if() {
  // Observe the condition so the symbol table is populated before the runner
  // queries try_fold_ref(). The runner's process_if (Slice 7) performs the
  // actual dead-branch elimination based on the folded condition value.
  //
  // Two if shapes flow through here (see runner's process_if for the
  // structural distinction):
  //   * Scoped form  — (cond, stmts, [cond, stmts]…, [stmts]). Walked
  //     here as alternating cond/stmts pairs.
  //   * Flat form    — (cond, stmt, [stmt]…). Lowered from
  //     `s when c` / `s unless c`. when/unless conditions are required
  //     to be comptime-known: there is no codegen lowering for an
  //     unresolved gate, so a non-folded cond is reported as a build
  //     error by the verifier downstream. Constprop only needs to peek
  //     at the cond here; the runner handles drop / splice based on the
  //     folded value.
  if (!move_to_child()) {
    return;
  }

  while (true) {
    if (is_type(Lnast_ntype::Lnast_ntype_stmts)) {
      break;  // bare else-stmts: no condition
    }
    if (is_type(Lnast_ntype::Lnast_ntype_ref) || is_type(Lnast_ntype::Lnast_ntype_const)) {
      [[maybe_unused]] const auto cond_val = current_prim_value();
    }
    if (!move_to_sibling()) {
      break;
    }
    if (!move_to_sibling()) {
      break;
    }
  }

  move_to_parent();
}
// Block scope push/pop. Each `stmts` LNAST node gets its own persistent
// scope keyed by its nid hash, so a `mut b = 2` inside `{ … }` is invisible
// outside the block, but its state persists for the rest of the walk
// (see Symbol_table::block_scope).
//
// The runner calls process_stmts BEFORE descending into children and
// process_stmts_post AFTER, mirroring the pattern used for `if`. The
// outermost stmts of every function body is itself wrapped here too;
// the function_scope established in the constructor remains the parent.
void uPass_constprop::process_stmts() {
  // current_scope_uid() folds in the inline-frame salt so a callee body's
  // block-scope keys don't collide with the caller's nids during a 1i
  // source-swap (salt 0 with no active frame → plain class_index).
  st.block_scope(lm->current_scope_uid());
  // The runner sets next_block_uncertain via notify_uncertain_arm_begin
  // immediately before descending into an if-arm whose cond didn't fold to
  // known-true/known-false. Mark the just-pushed scope so leave_scope can
  // invalidate any var assigned inside the body — without this, the
  // sequential walk leaves the last-branch's writes visible to code after
  // the if and folds casserts that depend on them.
  if (next_block_uncertain) {
    st.mark_current_uncertain();
    next_block_uncertain = false;
  }
}

void uPass_constprop::process_stmts_post() {
  // Task 1m — when the FILE-scope block is about to pop (stack is exactly
  // [function, top-block]; nested arms/loops/inline frames sit deeper),
  // capture the folded value of every `pub` value export into the Lnast's
  // pub-values side channel. The kernel synthesizes the `<unit>.__pub`
  // wrapper from it — the symbol table is the only place a fully-folded
  // scalar lives (its const store is dropped from the materialized tree).
  //
  // Skipped when the unit is already published (a `<unit>.__pub` wrapper
  // rides the loaded ln: dir — re-elaborating a loaded post-upass tree
  // cannot re-fold values whose stores were dropped) and when THIS unit hit
  // an unresolved import (it defers wholesale; erroring on a pub value that
  // depends on the missing import would mask the defer).
  if (st.stack.size() == 2 && !lm->get_lnast()->get_pub_list().empty()) {
    const auto unit = std::string(lm->get_top_module_name());
    // Already published: a loaded ln: dir carries the `<unit>.__pub` wrapper;
    // an in-invocation completed unit carries stamped pub_values_. Either way
    // a LATER round re-walks the materialized tree, where fully-folded const
    // stores no longer exist — re-harvesting would spuriously fail.
    bool skip = function_registry.contains(unit + ".__pub") || !lm->get_lnast()->get_pub_values().empty();
    if (!skip) {
      for (const auto& p : pending_imports_) {
        if (p.unit == unit) {
          skip = true;
          break;
        }
      }
    }
    if (!skip) {
      harvest_pub_values();
    }
  }
  st.leave_scope();
}

void uPass_constprop::harvest_pub_values() {
  const auto& ln = lm->get_lnast();

  // A pub value must fold to a comptime constant when its file is elaborated
  // (docs/contracts/task_1m_plan.md §1) — reject at the exporting side.
  auto fail = [&](std::string_view name, std::string_view why) {
    auto msg = std::format("pub `{}` of unit `{}` is not comptime-foldable ({})", name, ln->get_top_module_name(), why);
    livehd::diag::sink().emit(livehd::diag::Diagnostic{.severity = livehd::diag::Severity::error,
                                                       .code     = "pub-not-comptime",
                                                       .category = "type",
                                                       .pass     = "upass.constprop",
                                                       .message  = msg,
                                                       .hint = "a `pub const` initializer must fold to a comptime constant"});
    throw std::runtime_error(msg);
  };

  std::vector<std::pair<std::string, std::string>> leaves;
  for (const auto& p : ln->get_pub_list()) {
    if (p.kind != "value") {
      continue;  // lambda exports carry a tree url; nothing to fold
    }
    if (st.is_known_const(p.name)) {
      leaves.emplace_back(p.name, st.get_trivial(p.name).to_pyrope());
      continue;
    }
    if (st.has_bundle(p.name)) {
      const auto b = st.get_bundle(p.name);
      if (!b || b->non_attr_entries().empty()) {
        fail(p.name, "empty or unresolved bundle");
      }
      for (const auto& [key, ep] : b->non_attr_entries()) {
        const auto& v = ep->trivial;
        if (v.is_invalid() || v.has_unknowns()) {
          fail(p.name, key == "0" ? "not a known constant" : std::format("field `{}` is not a known constant", key));
        }
        if (key == "0") {  // trivial scalar slot — export under the bare name
          leaves.emplace_back(p.name, v.to_pyrope());
        } else {
          leaves.emplace_back(absl::StrCat(p.name, ".", key), v.to_pyrope());
        }
      }
      continue;
    }
    fail(p.name, "no comptime value");
  }
  lm->get_lnast()->set_pub_values(std::move(leaves));
}

// ── Tuple Operations ─────────────────────────────────────────────────────────
//
// Layout reference (matches opt_lnast):
//   tuple_add:  ref(dst), [const|ref|assign(ref(key),const/ref(val))]...
//   tuple_get:  ref(dst), ref(src), (const|ref)(field)...
//   tuple_set:  ref(tuple), (const|ref)(field)..., (const|ref)(value)
//
// Bundle keys after the bundle_sorted refactor:
//   - named fields are the bare name ("foo")
//   - unnamed fields are the bare decimal index ("0", "1", …)
// The two key spaces are independent: a named slot does NOT consume an
// unnamed position. So the literal `(bar=true, 4)` stores {bar:true, 0:4},
// matching what `(4, bar=true)` (and bundle-built concat output) produces —
// Decision 4 (§1.4) of the plan: unnamed position = canonical vector index.

void uPass_constprop::process_tuple_add() {
  // Build (or update in-place) a Bundle for the destination from each entry.
  // We reuse the existing Bundle object if one already exists so the pointer
  // stored in process_assign ("A = ___t1") stays stable.
  move_to_child();
  auto dst = std::string(current_text());

  // Get or create the bundle for dst.
  auto bundle = st.get_bundle(dst);
  if (!bundle) {
    bundle = std::make_shared<Bundle>(dst);
    st.set(dst, bundle);
  }
  // Mark the destination as tuple-typed so a downstream tuple_concat
  // routes it through bundle mode even if the bundle ends up looking
  // scalar (single positional entry, which is_scalar() can't distinguish
  // from a true scalar). See process_tuple_concat.
  tuple_typed_names.insert(dst);

  // Function-name detection helper: when a ref's text names a function in
  // the registry, store the qualified function name as a string Const into
  // the bundle slot so downstream method dispatch (x.method(...) where
  // method is a tuple field) can look it up via tuple_get + fcall.
  auto try_store_fn_name = [&](std::string_view key, std::string_view ref_text) -> bool {
    std::string qualified = std::string(lm->get_top_module_name()) + "." + std::string(ref_text);
    if (!function_registry.count(qualified)) {
      return false;
    }
    bundle->set(key, *Dlop::from_string(qualified));
    return true;
  };

  int unnamed_pos = 0;  // advances only on unnamed entries
  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      bundle->set(std::to_string(unnamed_pos), *Dlop::from_pyrope(current_text()));
      ++unnamed_pos;

    } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      auto       slot = std::to_string(unnamed_pos);
      const auto txt  = current_text();
      if (!try_store_fn_name(slot, txt)) {
        // A parenthesized scalar `(expr)` lowers to a 1-element tuple_add. When
        // the element is an attributes-pass cross-pass fold (an `is`/`.[comptime]`
        // result — constprop never assigns it, so st.get_bundle()=null), store
        // that scalar so `!(p is yy)` folds over the 1-element bundle. Only
        // runner_fold_fn (NOT st.has_trivial), so constprop's own trivials keep
        // their original bundle-only behavior.
        std::optional<Const> xfold;
        if (!st.get_bundle(txt) && runner_fold_fn) {
          if (auto f = runner_fold_fn(txt); f && !f->is_invalid()) {
            xfold = *f;
          }
        }
        if (xfold) {
          bundle->set(slot, *xfold);
        } else {
          bundle->set(slot, st.get_bundle(txt));
        }
      }
      ++unnamed_pos;

    } else if (is_type(Lnast_ntype::Lnast_ntype_store)) {
      // Named field: assign(ref(key), const/ref(val)). Stored under the
      // bare name — named slots don't advance unnamed_pos.
      move_to_child();
      auto key = std::string(current_text());
      move_to_sibling();
      if (is_type(Lnast_ntype::Lnast_ntype_const)) {
        bundle->set(key, *Dlop::from_pyrope(current_text()));
      } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
        const auto txt = current_text();
        if (!try_store_fn_name(key, txt)) {
          bundle->set(key, st.get_bundle(txt));
        }
      }
      move_to_parent();
    }
  }
  // Bundle was updated in-place; scalar values are propagated by process_tuple_get.
  move_to_parent();
}

// Track `attr_set var typename 'TypeName'` so method dispatch can look up
// methods on the declared type when the instance bundle alone doesn't carry
// them. Layout: ref(var), const('typename'), const('TypeName').
void uPass_constprop::process_attr_set() {
  move_to_child();
  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    move_to_parent();
    return;
  }
  std::string var(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  if (!is_type(Lnast_ntype::Lnast_ntype_const) || current_text() != "typename") {
    move_to_parent();
    return;
  }
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  std::string raw(current_text());
  move_to_parent();
  // raw is a pyrope-quoted string like `'Pair'`. Strip the quotes.
  if (raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'') {
    raw = raw.substr(1, raw.size() - 2);
  }
  if (!raw.empty()) {
    typename_of_var[var] = std::move(raw);
  }
}

void uPass_constprop::process_tuple_concat() {
  // Pyrope `++` semantics (matches the explicit `string(...)` cast for the
  // string case):
  //   - Build a Bundle via Bundle::concat over every operand. Trivial
  //     operands (int/string/nil) wrap as 1-element positional tuples first.
  //   - If any first-level entry of the result is a string trivial, fold
  //     the whole thing to a single text-concatenated string (this matches
  //     `string("hello", " world") == "hello world"` and
  //     `string(1, "2", 3) == "123"`). The collapse needs every entry to be
  //     a single-level trivial — a nested sub-bundle means we can't decide
  //     yet, so we keep the bundle.
  //   - Bail (no store) on unknowns / invalid operands; the value stays
  //     unresolved for this walk (the op emits verbatim, unfolded).
  // Layout: ref(dst), (const|ref)...
  move_to_child();
  auto dst = std::string(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  auto acc = std::make_shared<Bundle>(dst);

  // Wrap a single trivial as a 1-entry positional bundle so Bundle::concat
  // appends it as a slot instead of mishandling the bare key.
  auto wrap_trivial = [&](const Const& val) -> std::shared_ptr<Bundle> {
    auto b = std::make_shared<Bundle>(dst);
    b->set("0", val);
    return b;
  };

  // Resolve one operand to a Bundle-shaped value. Returns nullptr on
  // unfoldable (caller bails). Empty bundles are returned as-is; Bundle::concat
  // treats them as identity.
  auto resolve_operand = [&]() -> std::shared_ptr<Bundle const> {
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      Const val = *Dlop::from_pyrope(current_text());
      if (val.is_invalid()) {
        return nullptr;
      }
      return wrap_trivial(val);
    }
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      // Prefer the bundle view: a tuple_add/tuple_concat producer stores its
      // result as a bundle. For names that only hold a trivial scalar (string
      // or int from a constprop store), fall back to wrapping as a 1-tuple.
      if (auto b = st.get_bundle(current_text())) {
        return b;
      }
      if (st.has_trivial(current_text())) {
        auto val = st.get_trivial(current_text());
        if (val.is_invalid() || val.has_unknowns()) {
          return nullptr;
        }
        return wrap_trivial(val);
      }
    }
    return nullptr;
  };

  do {
    auto op = resolve_operand();
    if (!op) {
      move_to_parent();
      return;  // unfoldable; leave dst unchanged
    }
    acc->concat(op);
  } while (move_to_sibling());

  move_to_parent();

  // Any-string → stringify all. Mirrors the `string()` cast rule. Aborts the
  // collapse if any first-level entry is a sub-bundle or carries unknowns —
  // the bundle stays unfolded.
  auto try_stringify = [&]() -> std::optional<Const> {
    std::vector<Const> entries;
    bool               any_string = false;
    for (const auto& [k, ep] : acc->non_attr_entries()) {
      if (!Bundle::is_single_level(k)) {
        return std::nullopt;  // nested sub-bundle — don't try to stringify
      }
      const auto& v = ep->trivial;
      if (v.is_invalid()) {
        return std::nullopt;
      }
      if (v.is_string()) {
        any_string = true;
      }
      entries.push_back(v);
    }
    if (!any_string) {
      return std::nullopt;
    }
    return stringify_concat_trivials(entries);
  };

  if (auto folded = try_stringify(); folded.has_value()) {
    store_trivial(dst, *folded);
  } else {
    st.set(dst, acc);
    // Tuple-shaped result: keep the tuple-typed marker for downstream code
    // that distinguishes a real tuple from a scalar-wrapper bundle.
    tuple_typed_names.insert(dst);
  }
}

// Distinguish a "tuple-shaped" bundle from the symbol-table wrapper used
// for plain scalars. `Symbol_table::set(name, Const)` stores scalars in a
// wrapper bundle keyed at position 0, which is *indistinguishable* from a
// real single-element tuple `(3)` in the bundle layer. We can't tell them
// apart without type info, so we stay conservative: a bundle counts as
// tuple-shaped only when it carries a named first-level key, or when it
// has two-or-more distinct first-level positional entries.
//
// Consequence: scalar-vs-scalar `does` (e.g. `i3 does b1`) stays
// unresolved instead of folding to a wrong answer based on type. The cost
// is that genuine single-element tuples like `(3) does ...` also stay
// unresolved; that's a small subset of the test surface and the right
// trade for correctness without type tracking.
static bool is_tuple_shaped(const std::shared_ptr<Bundle const>& b) {
  // O(1) via the bundle_sorted §3 side-indices: any named first-level slot
  // marks the bundle as tuple-shaped; otherwise we need at least two
  // distinct unnamed first-level positions.
  return b->has_named_top() || b->unnamed_top_count() >= 2;
}

// Returns true if the bundle has any first-level non-attribute key with a
// recognizable shape — named or positional. Used as the "weak" requirement
// for the other operand once we've already decided one side is clearly
// tuple-shaped: a bundle whose first-level keys are e.g. `0` (single
// positional) doesn't itself prove tuple-ness, but it's enough structure
// to compare against a known tuple.
static bool has_first_level_shape(const std::shared_ptr<Bundle const>& b) {
  // O(1) via §3 side-indices: any non-attribute first-level entry counts.
  return b->has_named_top() || b->has_unnamed_top();
}

// Task 1g — decode a primitive type token (`u32`/`s8`/`i4`/`int`/`integer`/
// `uint`/`unsigned`/`bool`/`string`) in `does`/`equals`/`case` operand
// position to its kind+envelope. prp2lnast leaves these as a bare ref (no read
// site) precisely so this decode can run. Returns nullopt for any other name.
std::optional<uPass_constprop::Does_operand> uPass_constprop::decode_prim_type_token(std::string_view name) {
  Does_operand op;
  if (name == "bool") {
    op.kind = Does_operand::Kind::boolean;
    op.min  = *Dlop::create_integer(-1);
    op.max  = *Dlop::create_integer(0);
    return op;
  }
  if (name == "string") {
    op.kind = Does_operand::Kind::string;
    return op;
  }
  const bool is_u = (name == "uint" || name == "unsigned");
  const bool is_s = (name == "int" || name == "integer");
  if (is_u || is_s) {
    op.kind    = Does_operand::Kind::integer;
    op.max_inf = true;  // unsized → unbounded above
    if (is_u) {
      op.min = *Dlop::create_integer(0);  // unsigned floor
    } else {
      op.min_inf = true;  // signed → unbounded below
    }
    return op;
  }
  // Width sugar `u<N>` / `s<N>` / `i<N>`: bounds from the bit count.
  if (name.size() >= 2 && (name[0] == 'u' || name[0] == 's' || name[0] == 'i')
      && std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    const int n = std::stoi(std::string(name.substr(1)));
    op.kind     = Does_operand::Kind::integer;
    if (name[0] == 'u') {
      op.min = *Dlop::create_integer(0);
      op.max = *Dlop::get_mask_value(n);  // 2^n - 1
    } else {
      op.max = *Dlop::get_mask_value(n - 1);      // 2^(n-1) - 1
      op.min = *Dlop::get_neg_mask_value(n - 1);  // -2^(n-1)
    }
    return op;
  }
  return std::nullopt;
}

std::shared_ptr<Bundle> uPass_constprop::single_positional_bundle(const Const& v) {
  auto b = std::make_shared<Bundle>("__does_scalar");
  b->set("0", v);
  return b;
}

// Resolve the operand at the current cursor (a ref or const) for a type-aware
// `does`/`equals`. nullopt means "undecidable this walk" — the caller defers.
std::optional<uPass_constprop::Does_operand> uPass_constprop::resolve_does_operand() {
  Does_operand op;
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    auto txt = current_text();
    if (txt == "nil") {
      op.kind = Does_operand::Kind::nil;
      return op;
    }
    if (txt == "true" || txt == "false") {
      op.kind      = Does_operand::Kind::boolean;
      op.min       = *Dlop::create_integer(-1);
      op.max       = *Dlop::create_integer(0);
      op.value     = *Dlop::create_bool(txt == "true");
      op.has_value = true;
      return op;
    }
    auto v = Dlop::from_pyrope(txt);
    if (v->is_nil()) {
      op.kind = Does_operand::Kind::nil;
      return op;
    }
    if (v->is_string()) {
      op.kind      = Does_operand::Kind::string;
      op.value     = *v;
      op.has_value = true;
      return op;
    }
    if (v->is_bool()) {
      op.kind      = Does_operand::Kind::boolean;
      op.min       = *Dlop::create_integer(-1);
      op.max       = *Dlop::create_integer(0);
      op.value     = *v;
      op.has_value = true;
      return op;
    }
    if (v->is_integer()) {
      // Literal `v` → the point envelope [v, v] (1g ruling).
      op.kind      = Does_operand::Kind::integer;
      op.max       = *v;
      op.min       = *v;
      op.value     = *v;
      op.has_value = true;
      return op;
    }
    return std::nullopt;  // unknown-bit / undecidable literal
  }

  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    return std::nullopt;
  }
  auto name = current_text();
  // A real variable wins over a type-token spelling — Pyrope allows a variable
  // literally named `i3` (a 3-bit-signed type token) or `u8`. Gather the var's
  // state first: a bundle, a folded scalar value, and the combined type query.
  // Only when NONE of these identify a variable do we decode `name` as a
  // primitive type literal (`u32`/`int`/`bool`/…) — the 1g operand form.
  auto                            bundle = current_ref_bundle();
  upass::uPass::Scalar_type_query q;
  if (runner_type_query_fn) {
    q = runner_type_query_fn(name);
  }
  // A tuple-shaped bundle (a named field, or ≥2 positional entries) is a real
  // tuple. A scalar-wrapper bundle (`mut a:u32=3` stores `a` as `{0:3}`, a lone
  // `(3)` single positional) is a scalar — its declared kind/envelope ride the
  // type query, not the bundle shape.
  const bool bundle_is_tuple = bundle && is_tuple_shaped(bundle);
  Const      folded          = *Dlop::invalid();
  if (bundle && !bundle_is_tuple && bundle->has_trivial()) {
    folded = bundle->get_trivial();
  } else if (st.has_trivial(name)) {
    folded = st.get_trivial(name);
  }
  const bool is_known_var = bundle || !folded.is_invalid() || q.kind != Io_kind::none;
  if (!is_known_var) {
    if (auto t = decode_prim_type_token(name)) {
      return t;
    }
  }
  if (bundle_is_tuple) {
    op.kind   = Does_operand::Kind::tuple;
    op.bundle = bundle;
    return op;
  }
  return build_scalar_operand(q, folded);
}

// Build a scalar Does_operand from a declared type query (kind + integer
// envelope) plus an optional folded value. KIND comes from the query first
// (typecheck infers bool vs int even for un-annotated vars); when the query is
// silent it falls back to the value's own Dlop type. An un-annotated integer
// reads as an unbounded envelope (superset of any int). Stays kind=unknown
// when nothing is known (caller defers).
uPass_constprop::Does_operand uPass_constprop::build_scalar_operand(const upass::uPass::Scalar_type_query& q,
                                                                    const Const&                           folded) {
  Does_operand op;
  if (!folded.is_invalid()) {
    op.value     = folded;
    op.has_value = true;
  }
  switch (q.kind) {
    case Io_kind::boolean:
      op.kind = Does_operand::Kind::boolean;
      op.min  = *Dlop::create_integer(-1);
      op.max  = *Dlop::create_integer(0);
      return op;
    case Io_kind::string: op.kind = Does_operand::Kind::string; return op;
    case Io_kind::integer:
      op.kind = Does_operand::Kind::integer;
      if (q.annotated) {
        op.max_inf = !q.range_max.has_value();
        op.min_inf = !q.range_min.has_value();
        if (q.range_max) {
          op.max = *q.range_max;
        }
        if (q.range_min) {
          op.min = *q.range_min;
        }
      } else {
        // Un-annotated integer var → unbounded (superset of any int).
        op.max_inf = true;
        op.min_inf = true;
      }
      return op;
    case Io_kind::none: break;
  }
  // Kind unknown from the type passes — last resort: infer from the folded
  // value's own Dlop type (handles `nil`-valued vars). Stays kind=unknown
  // (→ the caller defers) when nothing is known.
  if (op.has_value) {
    if (folded.is_nil()) {
      op.kind = Does_operand::Kind::nil;
    } else if (folded.is_string()) {
      op.kind = Does_operand::Kind::string;
    } else if (folded.is_bool()) {
      op.kind = Does_operand::Kind::boolean;
      op.min  = *Dlop::create_integer(-1);
      op.max  = *Dlop::create_integer(0);
    } else if (folded.is_integer()) {
      op.kind    = Does_operand::Kind::integer;
      op.max_inf = true;  // untyped → unbounded
      op.min_inf = true;
    }
  }
  return op;
}

// Resolve one field of a bundle for the per-field type check (1g-D). The
// declared type rides the dotted query (`bundle_name.field` — the same alias
// chase the 1k typed-self does-check uses); a nested sub-bundle becomes a
// tuple operand. nullopt when the bundle has no resolvable name (e.g. a
// coerced literal) and the field carries no value.
std::optional<uPass_constprop::Does_operand> uPass_constprop::resolve_field_operand(const Bundle& b, std::string_view field,
                                                                                   bool declared_only) {
  if (b.has_bundle(field)) {
    if (auto sub = b.get_bundle(field); sub && !sub->is_scalar()) {
      Does_operand op;
      op.kind   = Does_operand::Kind::tuple;
      op.bundle = sub;
      return op;
    }
  }
  upass::uPass::Scalar_type_query q;
  auto                            base = b.get_name();
  if (runner_type_query_fn && !base.empty() && base != "__does_scalar") {
    q = runner_type_query_fn(absl::StrCat(base, ".", field));
  }
  if (declared_only && q.kind == Io_kind::none) {
    return std::nullopt;  // untyped field — no type constraint to enforce
  }
  Const folded = b.has_trivial(field) ? b.get_trivial(field) : *Dlop::invalid();
  if (q.kind == Io_kind::none && folded.is_invalid()) {
    return std::nullopt;
  }
  return build_scalar_operand(q, folded);
}

// Tri-state kind+envelope `a does b` for two NON-tuple operands. nullopt =
// undecidable this walk.
std::optional<bool> uPass_constprop::scalar_does(const Does_operand& a, const Does_operand& b) {
  using Kind = Does_operand::Kind;
  // `x does nil` is true for any non-tuple x (1g ruling). nil on the LHS, or
  // tuple-vs-nil, is deferred (pinned as future behavior).
  if (b.kind == Kind::nil) {
    return true;  // a is non-tuple here (tuple path handled by the caller)
  }
  if (a.kind == Kind::nil) {
    return std::nullopt;  // nil does <concrete>: pinned — defer
  }
  if (a.kind == Kind::unknown || b.kind == Kind::unknown || a.kind == Kind::tuple || b.kind == Kind::tuple) {
    return std::nullopt;  // kind unset, or a scalar-wrapper bundle → can't decide
  }
  // Across kinds the kind is a difference flag → false (`int does bool`,
  // `string does int`, …).
  if (a.kind != b.kind) {
    return false;
  }
  if (a.kind == Kind::string || a.kind == Kind::boolean) {
    return true;  // same basic type of boolean/string → true
  }
  // Both integer: envelope superset `a.max>=b.max and a.min<=b.min`, with
  // ±∞ flags short-circuiting the finite Const compares.
  I(a.kind == Kind::integer);
  bool max_ok = a.max_inf || (!b.max_inf && a.max.ge_op(b.max)->is_known_true());
  bool min_ok = a.min_inf || (!b.min_inf && a.min.le_op(b.min)->is_known_true());
  return max_ok && min_ok;
}

// Tri-state `a does b` for any two resolved operands. Structural (tuple) path
// when a side is a real tuple (named field or ≥2 positional), coercing the
// other side — a real tuple as-is, a value-bearing scalar to a single-
// positional bundle (`(100,30) does 30`). A type-token / value-less operand
// against a tuple can't be coerced → undecidable. Otherwise the kind+envelope
// scalar rule decides.
std::optional<bool> uPass_constprop::compute_does(const Does_operand& a, const Does_operand& b) {
  const bool a_tuple = (a.kind == Does_operand::Kind::tuple) && is_tuple_shaped(a.bundle);
  const bool b_tuple = (b.kind == Does_operand::Kind::tuple) && is_tuple_shaped(b.bundle);
  if (a_tuple || b_tuple) {
    auto to_bundle = [](const Does_operand& o) -> std::shared_ptr<Bundle const> {
      if (o.kind == Does_operand::Kind::tuple) {
        return o.bundle;
      }
      if (o.has_value) {
        return single_positional_bundle(o.value);
      }
      return nullptr;
    };
    auto ba = to_bundle(a);
    auto bb = to_bundle(b);
    if (!ba || !bb || !has_first_level_shape(ba) || !has_first_level_shape(bb)) {
      return std::nullopt;
    }
    if (!structural_does(ba, bb)) {
      return false;  // shape: a must cover every top-level field of b
    }
    // Per-field types (1g-D): for each top-level field of b that a also has,
    // `a.field does b.field`. A KNOWN field-type mismatch makes the whole
    // `does` false; an undecidable field is ignored (shape already matched —
    // this only adds rejection power, never removes it). `(a=1) does
    // (a:u32=…)` thus stops silently ignoring the `:u32`.
    for (const auto& tb : bb->top_levels()) {
      std::string key = tb.pos < 0 ? std::string(tb.name) : std::to_string(tb.pos);
      auto        fa  = resolve_field_operand(*ba, key, /*declared_only=*/true);
      auto        fb  = resolve_field_operand(*bb, key, /*declared_only=*/true);
      if (fa && fb) {
        if (auto r = compute_does(*fa, *fb); r && !*r) {
          return false;
        }
      }
    }
    return true;
  }
  return scalar_does(a, b);
}

// Fold `dst = does(l, r)`. Cursor is on the does node's first child (dst ref);
// reads l and r as Does_operands and stores the boolean result. Leaves the
// result unresolved when a side is undecidable.
//
// On exit the cursor is left on whichever child we last visited; the caller
// (process_func_does) is responsible for `move_to_parent`.
void uPass_constprop::fold_does(const std::string& dst) {
  if (!move_to_sibling()) {
    return;
  }
  auto a = resolve_does_operand();
  if (!move_to_sibling()) {
    return;
  }
  auto b = resolve_does_operand();
  if (!a || !b) {
    return;
  }
  if (auto r = compute_does(*a, *b)) {
    store_trivial(dst, *Dlop::create_bool(*r));
  }
}

// Per-first-level summary used by fold_in / fold_has: groups a bundle's flat
// (single-level) entries by their first-level key, marking sub-bundle entries
// (multi-level keys like `a.0`) so the comparison can distinguish a scalar
// `a=1` from a nested `a=(1,2)`.
struct Bundle_flat_entry {
  std::string_view name;  // empty for unnamed positional
  int              pos = -1;
  Const            value;  // valid only when !is_sub_bundle
  bool             is_sub_bundle = false;
};

static std::vector<Bundle_flat_entry> collect_first_level(const std::shared_ptr<Bundle const>& b) {
  // Per top-level segment of b's non-attr data, materialize a
  // Bundle_flat_entry. top_levels() already groups by first-level
  // segment and reports scalar (collapsed when leaf_count == 1) and
  // has_leafs, so this is now a straight projection.
  std::vector<Bundle_flat_entry> entries;
  for (const auto& tl : b->top_levels()) {
    Bundle_flat_entry n;
    if (tl.pos < 0) {
      n.name = tl.name;  // bare name in canonical storage
    } else {
      n.pos = tl.pos;
    }
    n.is_sub_bundle = tl.has_leafs;
    if (tl.has_leafs) {
      n.value = Dlop::invalid();
    } else {
      n.value = tl.scalar;
    }
    entries.emplace_back(std::move(n));
  }
  return entries;
}

// Fold `dst = in(l, r)`. Cursor is on the const("in") marker. Walks forward
// to read l and r, evaluates the membership predicate, and stores /*FIXME-LCONST-CTOR*/Lconst(0/1)
// in the symbol table.
//
// Semantics for `lhs in rhs` (matches Pyrope tuple-membership):
//   - Each first-level entry in lhs must be "satisfied" by rhs.
//   - Named entry `name=v` in lhs: rhs must have a first-level entry with the
//     same name whose scalar value equals v. nil-on-LHS acts as a wildcard.
//     A sub-bundle on the rhs side at that name fails the scalar comparison
//     (e.g. `(a=1) in (a=(1,2))` is false).
//   - Unnamed entry `v` in lhs: there must exist some first-level rhs entry
//     (named or unnamed) whose scalar value equals v.
// Sub-bundle entries on lhs cause the fold to defer until they collapse.
//
// On exit the cursor is left on the last visited child; the caller
// (process_func_call) is responsible for `move_to_parent`.
void uPass_constprop::fold_in(const std::string& dst) {
  if (!move_to_sibling()) {
    return;
  }
  auto ba = current_ref_bundle();
  if (!move_to_sibling()) {
    return;
  }
  auto bb = current_ref_bundle();
  if (!ba || !bb) {
    return;  // need both sides as known bundles
  }

  // Enum membership: `r in Mammal` — enum VALUES carry a parse-time
  // `__enumentry` identity attr ('Mammal.rat'), and the enum-TYPE bundle's
  // entries each carry theirs as a dotted attr leaf ('rat.__enumentry').
  // Membership is identity-based, never raw-value-based: one-hot encodings
  // collide across enums (Bird.parroket == Mammal.rat == 1), so a value
  // compare would claim cross-enum membership (enum_hier).
  if (const auto* lid_p = enum_identity_of(ba); lid_p != nullptr) {
    const Const& lid   = *lid_p;
    bool         found = false;
    for (const auto& [k, ep] : bb->get_attrs()) {
      if (Bundle::get_last_level(k) != "__enumentry") {
        continue;
      }
      if (Bundle::get_first_level(k) == k) {
        continue;  // the rhs bundle's own top-level tag, not a member entry's
      }
      if (!ep->trivial.is_invalid() && ep->trivial.same_repr(lid)) {
        found = true;
        break;
      }
    }
    store_trivial(dst, *Dlop::create_bool(found));
    return;
  }

  auto lhs_flat = collect_first_level(ba);
  auto rhs_flat = collect_first_level(bb);

  // Tri-state result: nullopt = defer until more info, true/false = decided.
  std::optional<bool> outcome;
  outcome = true;
  for (const auto& le : lhs_flat) {
    if (le.is_sub_bundle) {
      outcome.reset();  // defer: nested-LHS not modelled yet
      break;
    }
    if (le.value.is_invalid()) {
      outcome.reset();  // defer: lhs entry not yet folded
      break;
    }

    if (!le.name.empty()) {
      // Named entry — wildcard if value is nil.
      if (le.value.is_nil()) {
        continue;
      }
      bool                found_name = false;
      std::optional<bool> matched_value;
      for (const auto& re : rhs_flat) {
        if (re.name != le.name) {
          continue;
        }
        found_name = true;
        if (re.is_sub_bundle) {
          matched_value = false;  // scalar lhs vs sub-bundle rhs → unequal
          break;
        }
        if (re.value.is_invalid()) {
          break;  // defer (matched_value stays nullopt)
        }
        const Const eq = *le.value.eq_op(re.value);
        if (eq.is_known_true()) {
          matched_value = true;
        } else if (eq.is_known_false()) {
          matched_value = false;
        }
        break;
      }
      if (!found_name) {
        outcome = false;
        break;
      }
      if (!matched_value.has_value()) {
        outcome.reset();  // defer
        break;
      }
      if (!*matched_value) {
        outcome = false;
        break;
      }
    } else {
      // Unnamed entry — value must occur in any rhs first-level scalar.
      bool any_unknown = false;
      bool matched     = false;
      for (const auto& re : rhs_flat) {
        if (re.is_sub_bundle) {
          continue;
        }
        if (re.value.is_invalid()) {
          any_unknown = true;
          continue;
        }
        const Const eq = *le.value.eq_op(re.value);
        if (eq.is_known_true()) {
          matched = true;
          break;
        }
        if (!eq.is_known_false()) {
          any_unknown = true;
        }
      }
      if (!matched) {
        if (any_unknown) {
          outcome.reset();  // defer
          break;
        }
        outcome = false;
        break;
      }
    }
  }

  if (!outcome.has_value()) {
    return;
  }
  store_trivial(dst, Dlop::create_bool(*outcome));
}

// Fold `dst = has(l, key)`. Cursor is on the const("has") marker.
//
// Semantics for `bundle has key`:
//   - String key `'name'`: bundle must have a first-level named entry
//     matching `name`.
//   - Integer key `N`: bundle must have any first-level unnamed entry at
//     position N.
// Returns Dlop::create_bool(true) for present, Dlop::create_bool(false) for absent.
void uPass_constprop::fold_has(const std::string& dst) {
  if (!move_to_sibling()) {
    return;
  }
  std::shared_ptr<Bundle const> b;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    b = st.get_bundle(current_text());
  }
  if (!b) {
    return;
  }
  if (!move_to_sibling()) {
    return;
  }
  Const key_val;
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    key_val = Dlop::from_pyrope(current_text());
  } else if (is_type(Lnast_ntype::Lnast_ntype_ref) && st.has_trivial(current_text())) {
    key_val = st.get_trivial(current_text());
  }
  if (key_val.is_invalid()) {
    return;
  }

  auto entries = collect_first_level(b);

  bool found = false;
  if (key_val.is_string()) {
    // String literals round-trip through `'foo'` — strip the quotes to get
    // the bare name used as the canonical key.
    const std::string s = strip_pyrope_quotes(key_val.to_pyrope());
    for (const auto& e : entries) {
      if (e.name == s) {
        found = true;
        break;
      }
    }
  } else if (!key_val.is_nil() && !key_val.has_unknowns()) {
    const int target = key_val.to_i();
    for (const auto& e : entries) {
      if (e.pos == target) {
        found = true;
        break;
      }
    }
  } else {
    return;  // nil/unknown key — defer
  }

  store_trivial(dst, Dlop::create_bool(found));
}

// Fold `dst = case(l, r)`. Cursor is on the `case` marker child. Walks
// forward to read the subject (l) and pattern (r), evaluates the
// case-match predicate, and stores the result in the symbol table.
//
// Semantics for `L case R` (pyrope tuple pattern matching):
//   - Structural: every first-level key in R (named or positional) must
//     exist in L. If any R-key is absent in L → known-false (concrete).
//   - Values: at each R-key, R's value must match L's value at that key,
//     where R's `0sb?` / any has_unknowns() literal is a wildcard.
//     - All matched → known-true.
//     - Concrete mismatch on a named R-entry → nil (treated as a
//       runtime-deferred outcome). cassert/verifier counts nil as pass;
//       the if-processor treats nil as uncertain so match-expanded bodies
//       still get visited at comptime.
//     - Concrete mismatch on a purely-positional R (eq-like shape) →
//       known-false, so scalar match arms (e.g. `match sel { case 0b00
//       {…} }`) still let dead-branch elimination prune the arm.
//   - Sub-bundles or undecidable comparisons (unknowns on the L side) →
//     leave dst unfolded (undecidable this walk).
//
// Cursor is left on whichever child we last visited; the caller
// (process_func_case) restores via `move_to_parent`.
void uPass_constprop::fold_case(const std::string& dst) {
  // Resolve the operand at the current cursor to a flat-entry list. Refs to
  // tracked bundles use the existing collect_first_level helper. Const
  // literals and scalar refs degrade to a single unnamed scalar entry so
  // `sel case 0b10` (scalar vs scalar literal) folds the same way as the
  // bundle/bundle path. Returns nullopt when the value isn't decidable yet
  // (undeclared ref / no trivial value) so we can defer.
  // Out-param `b` captures the operand's bundle (for the per-field type check,
  // 1g-D); left null for scalar/const operands.
  auto resolve_flat = [this](std::shared_ptr<Bundle const>& b) -> std::optional<std::vector<Bundle_flat_entry>> {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      if (auto bun = current_ref_bundle()) {
        b = bun;
        return collect_first_level(bun);
      }
      auto name = current_text();
      if (st.has_trivial(name)) {
        Bundle_flat_entry e;
        e.pos   = 0;
        e.value = st.get_trivial(name);
        return std::vector<Bundle_flat_entry>{std::move(e)};
      }
      return std::nullopt;
    }
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      Bundle_flat_entry e;
      e.pos   = 0;
      e.value = *Dlop::from_pyrope(current_text());
      return std::vector<Bundle_flat_entry>{std::move(e)};
    }
    return std::nullopt;
  };

  std::shared_ptr<Bundle const> lhs_bundle;  // subject
  std::shared_ptr<Bundle const> rhs_bundle;  // pattern
  if (!move_to_sibling()) {
    return;
  }
  auto lhs_opt = resolve_flat(lhs_bundle);
  if (!move_to_sibling()) {
    return;
  }
  auto rhs_opt = resolve_flat(rhs_bundle);
  if (!lhs_opt || !rhs_opt) {
    return;
  }

  auto lhs_flat = std::move(*lhs_opt);
  auto rhs_flat = std::move(*rhs_opt);

  // `case` is a structural value-match. Attributes (mut/const, type
  // tags, decorations) are stripped by collect_first_level via
  // top_levels(), so only data fields participate — exactly the same
  // contract as `==`. For each rhs entry we find the matching lhs entry
  // and compare values; rhs wildcards (`0sb?`, etc.) make that entry
  // pass trivially. A concrete value mismatch at any rhs entry is a
  // definite false (no "nil softening" — that masked real failures and
  // poisoned downstream `if` cond folding).
  bool defer = false;
  for (const auto& re : rhs_flat) {
    if (re.is_sub_bundle || re.value.is_invalid()) {
      defer = true;
      continue;
    }

    const Bundle_flat_entry* lmatch = nullptr;
    if (!re.name.empty()) {
      for (const auto& le : lhs_flat) {
        if (le.name == re.name) {
          lmatch = &le;
          break;
        }
      }
    } else {
      for (const auto& le : lhs_flat) {
        if (le.name.empty() && le.pos == re.pos) {
          lmatch = &le;
          break;
        }
      }
    }

    if (lmatch == nullptr) {
      store_trivial(dst, *Dlop::create_bool(false));
      return;
    }

    // Typed `case` (1g-D): when both sides carry a declared field type, the
    // pattern type must be satisfied — `subject.field does pattern.field`. A
    // KNOWN type mismatch (e.g. subject `a:u32` vs pattern `a:bool`) is a
    // definite false even if the values would match a wildcard.
    if (lhs_bundle && rhs_bundle) {
      std::string key = re.name.empty() ? std::to_string(re.pos) : std::string(re.name);
      auto        fa  = resolve_field_operand(*lhs_bundle, key, /*declared_only=*/true);
      auto        fb  = resolve_field_operand(*rhs_bundle, key, /*declared_only=*/true);
      if (fa && fb) {
        if (auto r = compute_does(*fa, *fb); r && !*r) {
          store_trivial(dst, *Dlop::create_bool(false));
          return;
        }
      }
    }

    if (lmatch->is_sub_bundle || lmatch->value.is_invalid()) {
      defer = true;
      continue;
    }

    if (re.value.has_unknowns()) {
      continue;
    }

    if (lmatch->value.same_repr(re.value)) {
      continue;
    }

    const Const eq_result = *lmatch->value.eq_op(re.value);
    if (eq_result.is_known_true()) {
      continue;
    }
    if (eq_result.is_known_false()) {
      store_trivial(dst, *Dlop::create_bool(false));
      return;
    }
    defer = true;
  }

  if (defer) {
    return;
  }

  store_trivial(dst, *Dlop::create_bool(true));
}

std::optional<Const> uPass_constprop::resolve_current_scalar() const {
  if (is_type(Lnast_ntype::Lnast_ntype_const)) {
    return *Dlop::from_pyrope(current_text());
  }
  if (is_type(Lnast_ntype::Lnast_ntype_ref) && st.has_trivial(current_text())) {
    return st.get_trivial(current_text());
  }
  return std::nullopt;
}

std::optional<std::vector<uPass_constprop::Call_actual>> uPass_constprop::collect_call_actuals() {
  std::vector<Call_actual> actuals;

  // Helper: when the current cursor sits on a ref whose target is a
  // *non-trivial* bundle (tuple) in the symbol table, materialize it as a
  // flat key-map {field-path: scalar} so the inliner can route per-field
  // reads. Skipped for trivial-scalar bundles (key "0" only) — those route
  // through the normal scalar path and let typecast/built-in folds keep
  // working.
  auto try_collect_bundle = [&]() -> std::optional<std::unordered_map<std::string, Const>> {
    if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return std::nullopt;
    }
    auto name   = std::string(current_text());
    auto bundle = st.get_bundle(name);
    if (!bundle || bundle->is_trivial_scalar()) {
      return std::nullopt;
    }
    std::unordered_map<std::string, Const> out;
    for (const auto& [k, ep] : bundle->non_attr_entries()) {
      if (ep->trivial.is_invalid()) {
        return std::nullopt;
      }
      out.emplace(std::string(k), ep->trivial);
    }
    if (out.empty()) {
      return std::nullopt;
    }
    return out;
  };

  while (move_to_sibling()) {
    if (is_type(Lnast_ntype::Lnast_ntype_store)) {
      if (!move_to_child()) {
        continue;
      }
      Call_actual actual;
      const auto  key = current_text();
      // __ref_arg (pass-by-ref) and __ufcs_arg (task 1k UFCS receiver) are
      // POSITIONAL markers, not named arguments.
      actual.is_named = key != call_ref_arg_marker && key != call_ufcs_arg_marker;
      if (actual.is_named) {
        actual.name = std::string(key);
      }
      if (!move_to_sibling()) {
        move_to_parent();
        continue;
      }
      if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
        actual.var_name = std::string(current_text());
        // Tuple actual: try to extract a flat field-map from the caller's
        // symbol table before falling back to the scalar path.
        if (auto bundle = try_collect_bundle(); bundle.has_value()) {
          actual.is_bundle    = true;
          actual.bundle_value = std::move(*bundle);
          move_to_parent();
          actuals.emplace_back(std::move(actual));
          continue;
        }
        // Function-name actual (closure_capture, fcall6 style): when the
        // referenced name resolves to a known function in the registry,
        // surface the qualified function name as a string Const so the
        // callee's body can dispatch through it via inner_fname lookup.
        std::string qualified = std::string(lm->get_top_module_name()) + "." + actual.var_name;
        if (function_registry.count(qualified)) {
          actual.value = *Dlop::from_string(qualified);
          move_to_parent();
          actuals.emplace_back(std::move(actual));
          continue;
        }
      }
      auto value = resolve_current_scalar();
      move_to_parent();
      if (!value.has_value() || value->is_invalid()) {
        // Unresolved actual: still emit a slot so positional binding stays
        // consistent. Body stmts that don't read this param will still fold;
        // ones that do will defer/abort.
        actual.is_unresolved = true;
        actuals.emplace_back(std::move(actual));
        continue;
      }
      actual.value = *value;
      actuals.emplace_back(std::move(actual));
      continue;
    }

    std::string var_name;
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      var_name = std::string(current_text());
      if (auto bundle = try_collect_bundle(); bundle.has_value()) {
        Call_actual actual;
        actual.is_named     = false;
        actual.var_name     = std::move(var_name);
        actual.is_bundle    = true;
        actual.bundle_value = std::move(*bundle);
        actuals.emplace_back(std::move(actual));
        continue;
      }
      // Function-name actual.
      std::string qualified = std::string(lm->get_top_module_name()) + "." + var_name;
      if (function_registry.count(qualified)) {
        Call_actual actual;
        actual.is_named = false;
        actual.var_name = std::move(var_name);
        actual.value    = *Dlop::from_string(qualified);
        actuals.emplace_back(std::move(actual));
        continue;
      }
    }
    auto value = resolve_current_scalar();
    if (!value.has_value() || value->is_invalid()) {
      Call_actual unres;
      unres.is_named      = false;
      unres.var_name      = std::move(var_name);
      unres.is_unresolved = true;
      actuals.emplace_back(std::move(unres));
      continue;
    }
    actuals.emplace_back(Call_actual{.is_named = false, .name = {}, .value = *value, .var_name = std::move(var_name)});
  }

  return actuals;
}

// Marker pseudo-function handlers. The runner has already moved the cursor
// onto the func_<name> node; each fold_* helper expects the cursor to be on
// a sibling whose next-sibling is the first argument, which `move_to_child`
// (landing on dst-ref) satisfies.
void uPass_constprop::process_func_does() {
  move_to_child();
  std::string dst(current_text());
  fold_does(dst);
  move_to_parent();
}

void uPass_constprop::process_func_equals() {
  // `a equals b` ≡ `(a does b) and (b does a)`, types included (1g). For two
  // tuples this reduces to structural_equals (same set of top-level keys);
  // for scalars it means identical kind+envelope. Undecidable on either side
  // leaves the result unresolved.
  move_to_child();
  std::string dst(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto a = resolve_does_operand();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  auto b = resolve_does_operand();
  move_to_parent();
  if (!a || !b) {
    return;
  }
  auto ab = compute_does(*a, *b);
  auto ba = compute_does(*b, *a);
  if (ab && ba) {
    store_trivial(dst, *Dlop::create_bool(*ab && *ba));
  }
}

void uPass_constprop::process_func_in() {
  move_to_child();
  std::string dst(current_text());
  fold_in(dst);
  move_to_parent();
}

void uPass_constprop::process_func_has() {
  move_to_child();
  std::string dst(current_text());
  fold_has(dst);
  move_to_parent();
}

void uPass_constprop::process_func_case() {
  move_to_child();
  std::string dst(current_text());
  fold_case(dst);
  move_to_parent();
}

// Append a tuple/bundle pin's entries to `out` in positional field order
// (bundle_value is keyed by the flat index "0","1",…). Returns false if a key
// is not a plain index or a value isn't a foldable scalar.
static bool ordered_bundle_scalars(const std::unordered_map<std::string, Const>& bv, std::vector<Const>& out) {
  std::vector<std::pair<int, const Const*>> ordered;
  ordered.reserve(bv.size());
  for (const auto& [k, v] : bv) {
    int         idx = 0;
    const auto* b   = k.data();
    const auto* e   = b + k.size();
    auto [p, ec]    = std::from_chars(b, e, idx);
    if (ec != std::errc{} || p != e || v.is_invalid() || v.is_string() || v.has_unknowns()) {
      return false;
    }
    ordered.emplace_back(idx, &v);
  }
  std::sort(ordered.begin(), ordered.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
  for (const auto& [idx, v] : ordered) {
    out.emplace_back(*v);
  }
  return true;
}

bool uPass_constprop::try_eval_sum_cell_call(std::string_view dst, const std::vector<Call_actual>& actuals) {
  // Sum cell is not a plain positional add: pin a (pid 0) adds, pin b (pid 1)
  // subtracts (`__sum(a=10, b=3)` == 7). Each pin may carry a tuple of values
  // (`__sum(a=(1,2))` == 3). Collect the two pin groups and delegate to
  // Dlop::sum_op (add-all-a, subtract-all-b), which owns the cell semantics.
  std::vector<spool_ptr<Dlop>> a_vals, b_vals;
  for (std::size_t i = 0; i < actuals.size(); ++i) {
    const auto&   a   = actuals[i];
    hhds::Port_id pid = a.is_named ? Ntype::get_sink_pid(Ntype_op::Sum, a.name) : static_cast<hhds::Port_id>(i);
    if (pid != 0 && pid != 1) {
      return false;  // Sum only has the add (a) and subtract (b) pins
    }
    auto&              group = (pid == 0) ? a_vals : b_vals;
    std::vector<Const> vals;
    if (a.is_bundle) {
      if (!ordered_bundle_scalars(a.bundle_value, vals)) {
        return false;
      }
    } else if (a.value.is_invalid() || a.value.is_string() || a.value.has_unknowns()) {
      return false;
    } else {
      vals.push_back(a.value);
    }
    for (const auto& v : vals) {
      group.emplace_back(Dlop::clone(v));
    }
  }

  auto folded = Dlop::sum_op(std::span<const spool_ptr<Dlop>>(a_vals), std::span<const spool_ptr<Dlop>>(b_vals));
  if (!folded || folded->is_invalid()) {
    return false;
  }
  store_trivial(dst, folded);
  return true;
}

bool uPass_constprop::try_eval_mux_cell_call(std::string_view dst, std::string_view op, const std::vector<Call_actual>& actuals) {
  // Unlimited-sink multiplexer / LUT cells. Pins are named (`s` = selector for
  // mux/hotmux, `p1`..`pN` = ordered values; `p0`/`p1` = table/addr for lut)
  // or positional. Map every actual onto its pid, then delegate to the
  // matching Dlop kernel — which itself handles unknown selector/address bits
  // (three-valued ternary merge), so unknowns are passed through rather than
  // pre-filtered.
  const Ntype_op nop = Ntype::get_op(op);

  std::vector<const Const*> by_pid;  // indexed by sink pid (pid 0 = sel/table)
  for (std::size_t i = 0; i < actuals.size(); ++i) {
    const auto& a = actuals[i];
    if (a.is_bundle || a.value.is_invalid() || a.value.is_string()) {
      return false;  // unresolved / non-scalar actual: defer the fold
    }
    hhds::Port_id pid;
    if (a.is_named) {
      pid = Ntype::get_sink_pid(nop, a.name);
      if (pid == livehd::Port_invalid) {
        return false;
      }
    } else {
      pid = static_cast<hhds::Port_id>(i);
    }
    if (pid >= by_pid.size()) {
      by_pid.resize(pid + 1, nullptr);
    }
    by_pid[pid] = &a.value;
  }

  // Every pid slot must be filled (contiguous from 0).
  for (const auto* p : by_pid) {
    if (p == nullptr) {
      return false;
    }
  }

  spool_ptr<Dlop> folded;
  if (op == "lut") {
    if (by_pid.size() != 2) {
      return false;
    }
    folded = Dlop::lut_op(*by_pid[0], *by_pid[1]);
  } else {
    // mux / hotmux: pid 0 is the selector, pid 1..N the ordered values.
    if (by_pid.size() < 2) {
      return false;
    }
    std::vector<spool_ptr<Dlop>> values;
    values.reserve(by_pid.size() - 1);
    for (std::size_t i = 1; i < by_pid.size(); ++i) {
      values.emplace_back(Dlop::clone(*by_pid[i]));
    }
    std::span<const spool_ptr<Dlop>> vspan(values);
    folded = (op == "hotmux") ? Dlop::hotmux_op(*by_pid[0], vspan) : Dlop::mux_op(*by_pid[0], vspan);
  }

  if (!folded || folded->is_invalid()) {
    return false;
  }
  store_trivial(dst, folded);
  return true;
}

bool uPass_constprop::try_eval_cell_call(std::string_view dst, std::string_view fname, const std::vector<Call_actual>& actuals) {
  // `__name(...)` direct cell-op call. Strip the `__` prefix and dispatch
  // against the Ntype_op kernel set. Operands are positional and follow the
  // pin order from cell.cpp (see also docs/contracts/lnast2lgraph.md).
  if (fname.size() < 3 || fname[0] != '_' || fname[1] != '_') {
    return false;
  }
  std::string_view op(fname.data() + 2, fname.size() - 2);

  // Multiplexer / LUT cells (unlimited-sink) are addressed by pin name
  // (`s`, `p1`, `p2`, …) rather than positionally, and their Dlop kernels
  // fold even with unknown bits in the selector/address (three-valued ternary
  // merge). Route them through a dedicated handler that maps named pins to pid
  // positions and delegates to the matching Dlop static op.
  if (op == "mux" || op == "hotmux" || op == "lut") {
    return try_eval_mux_cell_call(dst, op, actuals);
  }
  // Sum needs pin-aware folding (a adds, b subtracts) — handled separately.
  if (op == "sum") {
    return try_eval_sum_cell_call(dst, actuals);
  }

  // Build the positional operand list the per-op kernels below expect.
  // Actuals arrive either named (`__div(a=…, b=…)`) or as a single repeated
  // pin carrying a tuple (`__mult(a=(7,1))`). Inputs must be foldable (no
  // unknown bits) — the per-op kernels assume known operands; the
  // mux/hotmux/lut path above is the one that folds unknowns.
  const Ntype_op     nop = Ntype::get_op(op);
  std::vector<Const> args;
  if (actuals.size() == 1 && actuals[0].is_bundle) {
    // Repeated single-pin tuple (`__mult(a=(7,1))`, `__and(a=(x,y))`, …): the
    // tuple's ordered entries are the operands.
    if (!ordered_bundle_scalars(actuals[0].bundle_value, args)) {
      return false;
    }
  } else {
    // Scalar pins, named or positional. Order by sink pid (cell.cpp pin
    // order) and pack densely — get_mask/set_mask number their pins with gaps
    // (a=0, mask=2, value=4), but the kernels take a compact (a, mask[,
    // value]) list, which the pid sort reproduces.
    std::vector<std::pair<hhds::Port_id, const Const*>> slots;
    slots.reserve(actuals.size());
    for (std::size_t i = 0; i < actuals.size(); ++i) {
      const auto& a = actuals[i];
      if (a.is_bundle || a.value.is_invalid() || a.value.is_string() || a.value.has_unknowns()) {
        return false;
      }
      hhds::Port_id pid;
      if (a.is_named) {
        if (nop == Ntype_op::Invalid) {
          return false;
        }
        pid = Ntype::get_sink_pid(nop, a.name);
        if (pid == livehd::Port_invalid) {
          return false;
        }
      } else {
        pid = static_cast<hhds::Port_id>(i);
      }
      slots.emplace_back(pid, &a.value);
    }
    std::stable_sort(slots.begin(), slots.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
    for (const auto& [pid, v] : slots) {
      args.emplace_back(*v);
    }
  }

  auto need_n   = [&](std::size_t n) -> bool { return args.size() == n; };
  auto need_min = [&](std::size_t n) -> bool { return args.size() >= n; };

  Const result;
  bool  matched = false;

  if (op == "mult") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.mult_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "div") {
    if (need_n(2)) {
      result  = *args[0].div_op(args[1]);
      matched = true;
    }
  } else if (op == "mod") {
    if (need_n(2)) {
      result  = *args[0].mod_op(args[1]);
      matched = true;
    }
  } else if (op == "and") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.and_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "or") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.or_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "xor") {
    if (need_min(1)) {
      Const r = args[0];
      for (std::size_t i = 1; i < args.size(); ++i) {
        r = r.xor_op(args[i]);
      }
      result  = r;
      matched = true;
    }
  } else if (op == "not") {
    if (need_n(1)) {
      result  = args[0].not_op();
      matched = true;
    }
  } else if (op == "ror") {
    if (need_n(1)) {
      result  = *args[0].ror_op();
      matched = true;
    }
  } else if (op == "rand") {
    if (need_n(1)) {
      result  = *args[0].rand_op();
      matched = true;
    }
  } else if (op == "rxor") {
    if (need_n(1)) {
      result  = *args[0].rxor_op();
      matched = true;
    }
  } else if (op == "sext") {
    // Pin a: value, pin b: sign-bit position (passed through as a Const).
    if (need_n(2)) {
      result  = args[0].sext_op(args[1]);
      matched = true;
    }
  } else if (op == "get_mask") {
    if (need_n(2)) {
      result  = *args[0].get_mask_op(args[1]);
      matched = true;
    }
  } else if (op == "set_mask") {
    if (need_n(3)) {
      result  = *args[0].set_mask_op(args[1], args[2]);
      matched = true;
    }
  } else if (op == "lt") {
    if (need_n(2)) {
      result  = *args[0].lt_op(args[1]);
      matched = true;
    }
  } else if (op == "gt") {
    if (need_n(2)) {
      result  = *args[0].gt_op(args[1]);
      matched = true;
    }
  } else if (op == "le") {
    if (need_n(2)) {
      result  = *args[0].le_op(args[1]);
      matched = true;
    }
  } else if (op == "ge") {
    if (need_n(2)) {
      result  = *args[0].ge_op(args[1]);
      matched = true;
    }
  } else if (op == "eq") {
    if (need_n(2)) {
      result  = *args[0].eq_op(args[1]);
      matched = true;
    }
  } else if (op == "ne") {
    if (need_n(2)) {
      auto eq = args[0].eq_op(args[1]);
      result  = eq->not_op();
      matched = true;
    }
  } else if (op == "shl") {
    if (need_n(2)) {
      result  = *args[0].shl_op(args[1]);
      matched = true;
    }
  } else if (op == "sra") {
    if (need_n(2)) {
      result  = *args[0].sra_op(args[1]);
      matched = true;
    }
  } else {
    // mux / hotmux / lut handled earlier in try_eval_mux_cell_call.
    return false;
  }

  if (!matched || result.is_invalid()) {
    return false;
  }
  store_trivial(dst, result);
  return true;
}

// Task 1m — resolve a live `import("…")` call against the function registry
// (docs/contracts/task_1m_plan.md §1/§2). The cursor sits on the const
// "import" callee; the next sibling is the const import string.
//
//   import("unit")        — the whole pub tuple: bind `dst` to a namespace
//                           bundle (value leaves + lambda tree-name strings,
//                           each lambda field carrying a `__pub` kind attr).
//   import("ln:u.tree")   — direct tree ref: bind `dst` to the tree-name
//                           string 'u.tree' (the fcall-ref-const lambda-value
//                           form; `equals` compares these strings — tree-url
//                           identity).
//   import("lg:graph")    — black-box graph ref (task 1m-E lowers the call
//                           sites at tolg); bound as the string 'lg:graph'.
//
// Resolution consults only the registry (completed in-invocation units +
// loaded ln: forests). A miss records a pending import — pass.upass either
// errors (standalone) or defers the file to the kernel's iterate loop.
void uPass_constprop::process_import_call(const std::string& dst) {
  if (!move_to_sibling()) {
    return;  // malformed call (no argument) — leave unfolded
  }
  std::string text(lm->current_raw_text());
  if (text.size() >= 2 && (text.front() == '\'' || text.front() == '"') && text.back() == text.front()) {
    text = text.substr(1, text.size() - 2);
  }
  if (text.empty()) {
    return;
  }
  auto pend = [&]() { pending_imports_.push_back({std::string(lm->get_top_module_name()), text}); };
  auto str_const = [](std::string_view s) { return *Dlop::from_pyrope(absl::StrCat("'", s, "'")); };

  if (text.starts_with("ln:")) {
    // Whole remaining string = the tree name, verbatim (may contain dots).
    const std::string tree = text.substr(3);
    if (!function_registry.count(tree)) {
      pend();
      return;
    }
    store_trivial(dst, str_const(tree));
    return;
  }
  if (text.starts_with("lg:")) {
    // Bind the url itself; the call sites lower to Sub instances at tolg
    // (task 1m-E wires the GraphLibrary lookup + missing-graph diagnostics).
    store_trivial(dst, str_const(text));
    return;
  }

  // Tuple form — the unit's whole pub namespace.
  auto build_namespace = [&](std::string_view                                        unit,
                             const std::vector<std::pair<std::string, std::string>>& values,
                             const std::vector<Lnast_pub_entry>&                     pubs) {
    auto b = std::make_shared<Bundle>(dst);
    for (const auto& [path, val_text] : values) {
      b->set(path, *Dlop::from_pyrope(val_text));
    }
    for (const auto& p : pubs) {
      if (p.kind == "value") {
        continue;
      }
      b->set(p.name, str_const(absl::StrCat(unit, ".", p.name)));
      b->set_attr(p.name, "pub", str_const(p.kind));
    }
    // Marks "import namespace" (the runner's UFCS check exempts these; the
    // read-only guarantee rides the importing `const` declaration).
    b->set_attr("pub_unit", str_const(unit));
    st.set(dst, b);
  };

  // Cross-invocation: a loaded `<unit>.__pub` wrapper tree (the durable pub
  // list). Walk its synthesized shape: store(ref path, const text) leaves +
  // attr_set(ref name, '__pub', '<kind>') lambda markers.
  if (auto wit = function_registry.find(text + ".__pub"); wit != function_registry.end()) {
    const auto&                                      w = wit->second;
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<Lnast_pub_entry>                     pubs;
    auto                                             root = w->get_root();
    for (auto stmts : w->children(root)) {
      if (!Lnast_ntype::is_stmts(w->get_type(stmts))) {
        continue;
      }
      for (auto stmt : w->children(stmts)) {
        auto c0 = w->get_first_child(stmt);
        if (c0.is_invalid()) {
          continue;
        }
        auto c1 = w->get_sibling_next(c0);
        if (c1.is_invalid()) {
          continue;
        }
        if (Lnast_ntype::is_attr_set(w->get_type(stmt)) && w->get_name(c1) == "__pub") {
          auto c2 = w->get_sibling_next(c1);
          if (!c2.is_invalid()) {
            std::string kind(w->get_name(c2));
            if (kind.size() >= 2 && kind.front() == '\'' && kind.back() == '\'') {
              kind = kind.substr(1, kind.size() - 2);
            }
            pubs.push_back({std::string(w->get_name(c0)), kind});
          }
          continue;
        }
        if (Lnast_ntype::is_store(w->get_type(stmt))) {
          std::string val(w->get_name(c1));
          // A lambda url leaf ('ln:u.tree') is re-bound by the pubs loop
          // (tree-name string); skip it here.
          if (val.starts_with("'ln:")) {
            continue;
          }
          values.emplace_back(std::string(w->get_name(c0)), std::move(val));
        }
      }
    }
    build_namespace(text, values, pubs);
    return;
  }

  // Same-invocation: the source unit publishes through its Lnast side
  // channels — pub_list_ from the parse, pub_values_ stamped by THIS pass
  // when the unit's file-scope walk completed. Values still pending →
  // defer (whole-file retry once the exporter finishes).
  if (auto uit = function_registry.find(text); uit != function_registry.end() && uit->second->get_lambda_kind().empty()) {
    const auto& src  = uit->second;
    const auto& pubs = src->get_pub_list();
    bool        needs_values = false;
    for (const auto& p : pubs) {
      if (p.kind == "value") {
        needs_values = true;
        break;
      }
    }
    if (needs_values && src->get_pub_values().empty()) {
      pend();  // exporter not yet completed this invocation
      return;
    }
    build_namespace(text, src->get_pub_values(), pubs);
    return;
  }

  pend();  // no matching unit among the explicit inputs
}

void uPass_constprop::process_func_call() {
  // Layout: ref(dst), ref(func_name), (const|ref)(arg)...
  // Now strictly the ref-form (built-in typecast callables and user funcs).
  // Pseudo-functions `does/in/has/case/break/continue/return` arrive as
  // dedicated ntypes (process_func_does/in/has/...). The remaining const-form
  // shapes (e.g. `import`) fall through and are left unfolded.
  move_to_child();
  std::string dst(current_text());
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  if (!is_type(Lnast_ntype::Lnast_ntype_ref)) {
    // Const-form callee (`import`, `step`, `implies` — see prp2lnast's
    // make_call).
    //
    // Task 1m — a live `import("…")` resolves here (dead branches never
    // dispatch, so liveness is exactly "we got here"): the tuple form binds
    // a pub-namespace bundle, the `ln:` url form a lambda tree-name string.
    // Unresolved → recorded for pass.upass (error or kernel defer).
    if (lm->current_raw_text() == "import") {
      process_import_call(dst);
      move_to_parent();
      return;
    }
    // `a..=b step s` has no dedicated LNAST op yet; its step
    // amount must be a positive integer (ranges only ascend — see the
    // descending-range check in process_range), so diagnose a comptime
    // non-positive step here even though the call itself stays unfolded.
    // Layout: ref(dst), const("step"), (const|ref)(range), (const|ref)(amount).
    if (lm->current_raw_text() == "step" && lm->get_top_module_name().find('.') == std::string_view::npos) {
      if (move_to_sibling() && move_to_sibling()) {
        Const amount = current_prim_value();
        // is_integer() first: nil/string satisfy is_i() (width-only check).
        if (amount.is_integer() && amount.is_i() && amount.to_i() <= 0) {
          livehd::diag::sink().emit(livehd::diag::Diagnostic{
              .severity = livehd::diag::Severity::error,
              .code     = "invalid-range-step",
              .category = "type",
              .pass     = "upass.constprop",
              .message  = std::format("range step must be a positive integer (got {})", amount.to_i()),
              .hint     = "ranges only ascend; use a positive step, e.g. `0..=10 step 2`",
          });
        }
      }
    }
    move_to_parent();
    return;
  }
  // Read the callee identifier RAW. It is a global function name (a builtin
  // typecast like `string`/`int`/`u8`, a cell op `__sum`, or a registry comb),
  // not a local variable, so it must not pick up the inline-frame rename:
  // current_text() would turn `string` into `inlN_string` while folding an
  // inlined body, and none of the casts/cell-ops below would match — leaving
  // the result tmp unfolded (e.g. an inlined `cputs("…{x}")`'s string() arg).
  std::string fname(lm->current_raw_text());

  // `optimize(<bool>)` — synthesis hint, parse-and-discard for now. Drop
  // the call by binding dst to a constant true so the statement can be
  // eliminated (no downstream consumers expect a meaningful value).
  if (fname == "optimize") {
    store_trivial(dst, Dlop::create_bool(true));
    move_to_parent();
    return;
  }

  // Task 1t — `wrap`/`sat` narrowing call: copy the `v=` arg value through to
  // the dst tmp. The following `store(lhs, dst)` then carries it to lhs. When
  // narrowing actually changes the value, attributes publishes the narrowed
  // result via runner_fold_fn (which current_prim_value consults first), so
  // this copy-through only matters for the no-op-narrowing case. bitwidth
  // exempts lhs from the overflow check. Codegen (T6) emits get_mask / mux.
  if (fname == "wrap" || fname == "sat" || fname == "saturate") {
    if (auto acts = collect_call_actuals()) {
      for (const auto& a : *acts) {
        if (a.is_named && a.name == "v" && !a.value.is_invalid()) {
          store_trivial(dst, a.value);
          break;
        }
      }
    }
    move_to_parent();
    return;
  }

  auto actuals = collect_call_actuals();

  // String-interpolation format directive `__fmt(value, 'spec')`: render
  // `value` per a std::format-style presentation spec (b/o/x/X/d) into a
  // string Const. Emitted by prp2lnast for `"{expr:spec}"` chunks and then
  // concatenated by the enclosing `string(...)` call. Defer (leave dst unset)
  // when the value isn't comptime-known yet.
  if (fname == "__fmt") {
    if (actuals.has_value() && actuals->size() == 2 && !(*actuals)[0].is_named && !(*actuals)[1].is_named) {
      const Const& val  = (*actuals)[0].value;
      const Const& spec = (*actuals)[1].value;
      if (!val.is_invalid() && !val.has_unknowns() && spec.is_string()) {
        store_trivial(dst, *Dlop::from_string(format_interp_value(val, spec.to_string())));
      }
    }
    move_to_parent();
    return;
  }

  // Direct cell-op call: `__sum(a, b)`, `__hotmux(sel, a, b, …)`, … —
  // every Ntype_op cell can surface in Pyrope as `__name(...)` and gets
  // folded here when all actuals are comptime-known. See cell.hpp for the
  // canonical names.
  if (actuals.has_value() && try_eval_cell_call(dst, fname, *actuals)) {
    move_to_parent();
    return;
  }

  // Enumerate known typecast dispatch.
  enum class Cast { none, to_int, to_uint, to_string, to_sized };
  Cast kind       = Cast::none;
  bool sized_sig  = false;  // true for sN, false for uN
  int  sized_bits = 0;
  if (fname == "int") {
    kind = Cast::to_int;
  } else if (fname == "uint" || fname == "unsigned") {
    kind = Cast::to_uint;
  } else if (fname == "string") {
    kind = Cast::to_string;
  } else if (fname.size() >= 2 && (fname[0] == 'u' || fname[0] == 's' || fname[0] == 'i')) {
    // u32 / s32 / u8 ... — size suffix is decimal digits.
    bool all_digits = true;
    for (size_t i = 1; i < fname.size(); ++i) {
      if (fname[i] < '0' || fname[i] > '9') {
        all_digits = false;
        break;
      }
    }
    if (all_digits && fname.size() > 1) {
      kind       = Cast::to_sized;
      sized_sig  = (fname[0] == 's' || fname[0] == 'i');
      sized_bits = std::stoi(fname.substr(1));
    }
  }
  if (kind == Cast::none) {
    move_to_parent();
    return;
  }

  std::vector<Const> args;
  if (!actuals.has_value()) {
    move_to_parent();
    return;
  }
  args.reserve(actuals->size());
  for (const auto& actual : *actuals) {
    if (actual.is_named) {
      move_to_parent();
      return;
    }
    // An enum-value actual stringifies as its identity name ("Color.Red"),
    // never its raw encoding — `string(E3.l1)` and `"{y}"` interpolation.
    if (kind == Cast::to_string && !actual.var_name.empty()) {
      if (const auto* id = enum_identity_of(st.get_bundle(actual.var_name)); id != nullptr) {
        args.push_back(*id);
        continue;
      }
    }
    if (actual.value.is_invalid()) {
      move_to_parent();
      return;
    }
    args.push_back(actual.value);
  }
  move_to_parent();

  // Parse a scalar from either a string (re-parse its textual content) or an
  // already-numeric Const. Returns invalid on parse failure.
  // `to_pyrope()` on a string renders as `'content'`; strip the single-quote
  // wrappers before re-parsing so `Dlop::from_pyrope("3")` (an int) is
  // produced rather than `Dlop::from_pyrope("'3'")` (a string round-trip).
  auto to_scalar = [](const Const& a) -> Const {
    if (!a.is_string()) {
      return a;
    }
    try {
      return *Dlop::from_pyrope(strip_pyrope_quotes(a.to_pyrope()));
    } catch (...) {
      return *Dlop::invalid();
    }
  };

  Const result;
  if (kind == Cast::to_string) {
    auto stringified = stringify_concat_trivials(args);
    if (!stringified.has_value()) {
      return;  // unknown bits in some arg — leave dst unset (unresolved this walk)
    }
    result = *stringified;
  } else {
    if (args.size() != 1) {
      return;
    }  // unsupported arity
    Const v = to_scalar(args.front());
    if (v.is_invalid()) {
      return;
    }
    if (kind == Cast::to_uint) {
      if (v.is_string() || v.is_negative()) {
        // Cast failure (non-numeric string or negative) → pyrope `nil`.
        v = Dlop::nil();
      }
      result = v;
    } else if (kind == Cast::to_int) {
      if (v.is_string()) {
        v = Dlop::nil();
      } else if (v.is_bool() && !v.has_unknowns()) {
        // `int(true)` is 1, never the bool's all-ones payload (~0 == -1).
        v = *Dlop::from_pyrope(v.is_known_true() ? "1" : "0");
      }
      result = v;
    } else {
      // sized: fold only when the value fits. Signed/unsigned range check is
      // deferred; the current test set just stores small positives.
      if (v.is_string()) {
        v = Dlop::nil();
      }
      (void)sized_sig;
      (void)sized_bits;
      result = v;
    }
  }

  store_trivial(dst, result);
}

void uPass_constprop::process_range() {
  // Layout: ref(dst), (const|ref)(start), (const|ref)(end)
  // Resolve start/end and stash in range_map keyed by dst. When either side
  // is unknown, leave the entry absent (unresolved this walk). For
  // `x[a..]` / `x[..]`, prp2lnast emits the open end as the literal pyrope
  // `nil`, which round-trips as a string Const — process_tuple_get treats
  // that sentinel as "to source's last index".
  //
  // For closed `lo..=hi` ranges with concrete integer bounds, also
  // materialize a positional tuple bundle so `cassert (2..=4) == (2,3,4)`
  // folds via compare_bundles_eq.
  move_to_child();
  auto dst = std::string(current_text());

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const start = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const end = current_prim_value();
  move_to_parent();

  if (start.is_invalid() || end.is_invalid()) {
    return;
  }

  // Pyrope ranges must ascend (04-variables.md): a descending range is a
  // compile error ("5 never reaches 0" — `5..=0` can't count up to its end).
  // `..<` / `..+` lower to an inclusive end before this node, so every
  // descending source form lands here as end < start. is_integer() is the
  // load-bearing gate: the open-end sentinel (`x[a..]` → nil end) and string
  // bounds satisfy is_i() (it only checks width), and nil reads as 0 — which
  // would flag every `a..` with a > 0. Same top-module gate as the
  // negative-shift diagnostic above: inside an EXTRACTED parametric body
  // ("<top>.<fn>") unbound params can fold a bogus descending range as an
  // optimization artifact, not a user mistake — the genuine error still
  // surfaces via the body's inlined copy under the real (dot-less) top.
  if (start.is_integer() && end.is_integer() && start.is_i() && end.is_i() && end.to_i() < start.to_i()) {
    if (lm->get_top_module_name().find('.') == std::string_view::npos) {
      livehd::diag::Span span;
      if (const auto& ln = lm->get_lnast()) {
        const auto loc = ln->get_loc(lm->get_current_nid());
        const auto fn  = ln->get_fname(lm->get_current_nid());
        if (!fn.empty()) {
          span.file = std::string{fn};
        }
        if (loc.line != 0) {
          span.start_line = loc.line;
        }
      }
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "invalid-descending-range",
          .category = "type",
          .pass     = "upass.constprop",
          .message  = std::format("invalid descending range: {} never reaches {} (only ascending ranges are allowed)",
                                  start.to_i(),
                                  end.to_i()),
          .span     = std::move(span),
          .hint     = "swap the bounds so the range ascends, e.g. `0..=5`",
      });
    }
    return;  // do not register the bounds — downstream folds would be nonsense
  }

  auto it = range_map.find(dst);
  if (it == range_map.end()) {
    range_map.emplace(dst, std::make_pair(start, end));
  } else if (!it->second.first.same_repr(start) || !it->second.second.same_repr(end)) {
    it->second = {start, end};
  }

  // Materialize a tuple bundle for closed integer ranges so eq/tuple_get can
  // operate on the concrete sequence. Skip for open-ended (nil) or negative
  // spans, and bound the size so a pathological span can't blow up memory.
  if (start.is_i() && end.is_i()) {
    const auto lo = start.to_i();
    const auto hi = end.to_i();
    if (hi >= lo && (hi - lo) < 4096) {
      auto bundle = std::make_shared<Bundle>(dst);
      int  pos    = 0;
      for (int64_t v = lo; v <= hi; ++v, ++pos) {
        bundle->set(std::to_string(pos), *Dlop::create_integer(v));
      }
      st.set(dst, bundle);
    }
  }
}

void uPass_constprop::process_tuple_get() {
  // Read: dst = src[field]
  // Build the symbol-table key as "src.field" and propagate the stored value.
  move_to_child();
  auto dst = std::string(current_text());

  move_to_sibling();
  auto src = std::string(current_text());  // source tuple variable

  if (!move_to_sibling()) {
    move_to_parent();
    return;  // no field — nothing to propagate
  }

  // Range-indexed tuple_get on a string folds inline (`x[1..=2]` / `x[1..]`).
  // Same path also handles integer source for bit-slicing (`b[0..<4]`):
  // detect: exactly one field operand which is a ref bound by a prior
  // `range` LNAST node.
  if (is_type(Lnast_ntype::Lnast_ntype_ref) && is_last_child()) {
    auto it = range_map.find(std::string(current_text()));
    if (it != range_map.end() && st.has_trivial(src)) {
      const auto& src_val = st.get_trivial(src);
      const auto& start   = it->second.first;
      const auto& end_lc  = it->second.second;
      if (src_val.is_string() && start.is_i()) {
        const auto  start_idx = static_cast<size_t>(start.to_i());
        std::string body      = strip_pyrope_quotes(src_val.to_pyrope());
        size_t      len       = body.size();
        bool        open      = end_lc.is_nil();  // open-end sentinel for `x[a..]`
        if (!open && !end_lc.is_i()) {
          move_to_parent();
          return;
        }
        size_t end_i = open ? (len == 0 ? 0 : len - 1) : static_cast<size_t>(end_lc.to_i());
        if (start_idx <= len && end_i + 1 <= len && start_idx <= end_i + 1) {
          store_trivial(dst, Dlop::from_string(body.substr(start_idx, end_i - start_idx + 1)));
          move_to_parent();
          return;
        }
      }
      // Integer bit-slice via the same range→value synthesis used by get_mask.
      // Delegate to Dlop (get_mask_op): unknown source bits are sliced
      // bit-precisely, so only non-values (invalid/string) are skipped.
      if (is_numeric(src_val)) {
        const Const result = apply_range_mask(src_val, start, end_lc);
        if (!result.is_invalid()) {
          store_trivial(dst, result);
          move_to_parent();
          return;
        }
      }
    }
  }

  // Accumulate field path: each child after src appends ".field" to the key.
  // LNAST const text is the pyrope-syntactic form (`"0"`, `"'b'"`, `"\"hi\""`),
  // not the field name. Parse and use Const::to_field() so integers render
  // as their decimal and strings drop their surrounding quotes — making
  // `t[0]`, `t['b']` and `t["foo"]` resolve uniformly to the bare-name
  // stored key in the bundle.
  std::string key = src;
  std::string first_seg;               // first (top-level) field segment, canonicalized
  bool        first_is_index = false;  // numeric (positional) vs named
  bool        first_captured = false;
  do {
    std::string seg;
    if (is_type(Lnast_ntype::Lnast_ntype_const)) {
      auto v = Dlop::from_pyrope(current_text());
      if (!v || v->is_invalid()) {
        move_to_parent();
        return;
      }
      seg = v->to_field();
    } else if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      // Runtime index: must be a known constant to fold statically.
      const auto idx = st.get_trivial(current_text());
      if (idx.is_invalid()) {
        move_to_parent();
        return;  // can't fold unknown index — leave runtime accesses unchecked
      }
      seg = std::to_string(idx.to_i());
    } else {
      move_to_parent();
      return;  // unhandled field type
    }
    // Positional iff the canonical key is a decimal (`t[0]` → "0"); a name like
    // `t.b` canonicalizes to "b" (NOT a digit) even though a 1-char literal
    // parses as a char-integer. The bundle key is the discriminator.
    const bool seg_is_index = !seg.empty() && seg.find_first_not_of("0123456789") == std::string::npos;
    if (!first_captured) {
      first_seg      = seg;
      first_is_index = seg_is_index;
      first_captured = true;
    }
    key += '.';
    key += seg;
  } while (!is_last_child() && move_to_sibling());

  move_to_parent();

  // Bundle-access check: the (comptime-known) top-level key must be a valid
  // index/field of `src`'s bundle. Only the first segment is checked here;
  // nested-level checks are a later phase.
  if (first_captured) {
    check_tuple_access(src, first_seg, first_is_index);
  }

  // For a NAMED access, capture src's bundle so we can tell "absent named field
  // on a resolved multi-field tuple → nil" apart from the single-output-callee
  // scalar fallback further below.
  const auto src_bundle = (first_captured && !first_is_index) ? st.get_bundle(src) : nullptr;
  const bool named_field_absent
      = src_bundle && !src_bundle->is_empty() && !src_bundle->is_scalar() && !src_bundle->has_top_named(first_seg);

  // Enum entry read (`Mammal.rat`, `Color.Red`): alias the CARRIER bundle —
  // its `__enumentry` identity attr is what enum-aware `in` / `string()` /
  // interpolation read. The trivial path below would copy only the raw
  // encoding and lose the identity (one-hot values collide across enums).
  if (st.has_bundle(key)) {
    if (auto sub_bundle = st.get_bundle(key); enum_identity_of(sub_bundle) != nullptr) {
      bool local_changed = !st.has_bundle(dst) || st.get_bundle(dst) != sub_bundle;
      if (local_changed) {
        st.set(dst, sub_bundle);
      }
      return;
    }
  }

  // Propagate trivial value if available; fall back to bundle propagation.
  if (st.has_trivial(key)) {
    store_trivial(dst, st.get_trivial(key));
  } else if (st.has_bundle(key)) {
    auto sub_bundle = st.get_bundle(key);
    if (sub_bundle) {
      bool local_changed = !st.has_bundle(dst) || st.get_bundle(dst) != sub_bundle;
      if (local_changed) {
        st.set(dst, sub_bundle);
      }
    }
  } else if (named_field_absent) {
    // Named access of an absent field on a resolved multi-field tuple reads as
    // nil (a legal existence probe, e.g. `tup0['y'] == nil` once tup0 became
    // positional `(55,66)`). Must precede the single-output-callee fallback,
    // which would otherwise hand back src's position-0 scalar for the missing
    // named field.
    store_trivial(dst, *Dlop::nil());
  } else if (st.has_trivial(src)) {
    // Single-output callee fallback: an inliner result like
    // `comb f(...) -> (res:T) { res = ... }` is stored as a trivial scalar
    // on the caller's dst (so `x == 2` works). The dotted form `x.res`
    // then asks for `x.res` which doesn't exist as a key; fall back to
    // the source's trivial value so the named-field reader and the bare
    // scalar reader converge on the same answer.
    store_trivial(dst, st.get_trivial(src));
  }
}

void uPass_constprop::check_tuple_access(const std::string& base, const std::string& seg, bool is_index) {
  // Only POSITIONAL access is checked. A NAMED access of an absent field reads
  // as `nil` (a legal existence probe — e.g. `tup0['y'] == nil`), and typed
  // tuples name positional fields through their type (not the raw shape), so a
  // missing-named-field read is NOT an error here.
  if (!is_index) {
    return;
  }
  auto base_b = st.get_bundle(base);
  // Skip unless the base shape is resolved: a null/empty bundle means the
  // shape isn't built yet at this point in the walk (flagging now would be a
  // false positive); a bare scalar is `x[0]` sugar / a single-output fallback.
  if (!base_b || base_b->is_empty() || base_b->is_scalar()) {
    return;
  }
  const size_t n_unnamed = base_b->unnamed_top_count();
  const size_t n_named   = base_b->named_top_count();
  if (n_unnamed == 0 && n_named == 0) {
    return;  // no resolved top-level fields — nothing to check
  }
  int idx      = 0;
  auto [p, ec] = std::from_chars(seg.data(), seg.data() + seg.size(), idx);
  if (ec != std::errc{}) {
    return;  // unparseable index — skip
  }
  // Positional access is valid ONLY for unnamed entries: a named tuple is
  // name-access only (`(b=1, c=2)[0]` is an error — use `.b`).
  if (n_unnamed == 0) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "index-out-of-bounds",
        .category = "type",
        .pass     = "upass.constprop",
        .message  = std::format("tuple `{}` is name-access only; positional index `{}` is not allowed", base, idx),
        .hint     = "access named fields by name (e.g. `t.field`)",
    });
    return;
  }
  if (!base_b->has_top_unnamed(idx)) {
    livehd::diag::sink().emit(livehd::diag::Diagnostic{
        .severity = livehd::diag::Severity::error,
        .code     = "index-out-of-bounds",
        .category = "type",
        .pass     = "upass.constprop",
        .message  = std::format("out of bounds access: index {} on tuple `{}` of size {}", idx, base, n_unnamed),
        .hint     = std::format("valid index range is [0, {}]", n_unnamed - 1),
    });
  }
}

void uPass_constprop::process_tuple_set() {
  // Update a single field inside an existing tuple bundle.
  // Layout: ref(tuple), field_path..., value
  // We handle the simple one-field case: tuple.field = value.
  //
  // IMPORTANT: Attribute assignments (e.g. x["__bits"] = 2, x["__signed"] = 1)
  // must be skipped.  Bundle::set() interns attribute keys as "0.__attr" while
  // Symbol_table::has_trivial() does a literal match, so the round-trip never
  // resolves.  Attribute annotations are not values that constprop propagates.
  move_to_child();
  auto tuple_var = std::string(current_text());

  if (is_last_child()) {
    move_to_parent();
    return;
  }

  // `tup[idx] = v` promotes a scalar to a 1-element tuple in Pyrope. Mark the
  // target so a downstream tuple_concat reads `tup` via bundle mode rather
  // than mis-classifying a single-entry bundle as a scalar wrapper. Mirrors
  // the propagation done by process_tuple_add for its destination.
  tuple_typed_names.insert(tuple_var);

  // Collect all children after the tuple ref into a (text, is_ref) list.
  // The last child is the value; everything before it is the field path.
  // Capturing `is_ref` matters for the value: a ref's text is a variable name
  // (look it up in the symbol table), not a literal — `Dlop::from_pyrope`
  // happily accepts unparseable text as a string, so without the type tag we
  // would store the raw ref name (e.g. `___3`) as a string Const.
  struct Child {
    std::string text;
    bool        is_ref;
  };
  std::vector<Child> path_and_val;
  while (move_to_sibling()) {
    path_and_val.push_back({std::string(current_text()), is_type(Lnast_ntype::Lnast_ntype_ref)});
  }
  if (path_and_val.size() < 2) {
    // Need at least one field and one value.
    move_to_parent();
    return;
  }

  // `__enumentry` is the parse-time enum identity tag (prp2lnast's
  // lower_enum_def): land it as a real attr leaf on the carrier bundle so
  // enum-aware `in` / `string()` / interpolation read it back. All OTHER
  // `__attr` writes stay skipped below (they belong to the attributes pass).
  if (path_and_val.size() == 2 && path_and_val[0].text == "__enumentry" && !path_and_val[1].is_ref) {
    auto bundle = tuple_var.find('.') == std::string::npos ? st.get_bundle_for_write(tuple_var) : st.get_bundle(tuple_var);
    if (!bundle) {
      bundle = std::make_shared<Bundle>(tuple_var);
      st.set(tuple_var, bundle);
    }
    bundle->set("__enumentry", *Dlop::from_pyrope(path_and_val[1].text));
    move_to_parent();
    return;
  }

  // Skip attribute assignments (e.g. x["__bits"] = 2).
  // Any field component that begins with "__" and whose third char is not '_'
  // is a Pyrope bitwidth/signed attribute — not a propagatable value.
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    const auto& f = path_and_val[i].text;
    if (f.size() > 2 && f[0] == '_' && f[1] == '_' && f[2] != '_') {
      move_to_parent();
      return;
    }
  }

  // Resolve each path element to its final field text. A path element may be:
  //   - const: literal field text (e.g. `0`, `a`) — use as-is.
  //   - ref:   variable whose value names the field (`sing_tup[key] = e`).
  //            Resolve through the symbol table; a trivial string yields its
  //            content (no quotes), a trivial int its decimal text. If the
  //            ref has no trivial yet, fall back to the variable name (it
  //            stays unresolved as a literal path element for this walk).
  std::vector<std::string> path;
  path.reserve(path_and_val.size() - 1);
  for (std::size_t i = 0; i + 1 < path_and_val.size(); ++i) {
    std::string elem = path_and_val[i].text;
    if (path_and_val[i].is_ref && st.has_trivial(elem)) {
      // to_field() unwraps a string trivial to its content (no quotes) and
      // renders an int trivial as its decimal text — exactly the field-name
      // shape we want for `tuple[ref] = …`.
      elem = st.get_trivial(elem).to_field();
    }
    path.push_back(std::move(elem));
  }

  // Decide between the flat-key path (numeric or dotted positional) and
  // the named path (single non-numeric name → bare-name key so downstream
  // Bundle::concat treats this entry as a named slot, matching what
  // tuple_add emits for `(name=val, …)`).
  auto is_decimal = [](const std::string& s) {
    if (s.empty()) {
      return false;
    }
    for (char c : s) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        return false;
      }
    }
    return true;
  };

  bool use_named_positional = path.size() == 1 && !is_decimal(path[0]) && !path[0].empty();

  const auto& val_child     = path_and_val.back();
  auto        resolve_value = [&]() -> std::optional<Const> {
    if (val_child.is_ref) {
      if (st.has_trivial(val_child.text)) {
        return st.get_trivial(val_child.text);
      }
      return std::nullopt;
    }
    Const v = *Dlop::from_pyrope(val_child.text);
    if (v.is_invalid()) {
      return std::nullopt;
    }
    return v;
  };

  if (use_named_positional) {
    // Place the entry into tuple_var's bundle under the bare name. Named
    // and unnamed slots live in separate key spaces after the bundle_sorted
    // refactor — no position prefix to compute or reuse. Mutated in place
    // below, so take the COW (un-shared) slot — a plain get_bundle would
    // leak the write into whole-bundle-assignment aliases (`p2 = p1`).
    auto bundle = tuple_var.find('.') == std::string::npos ? st.get_bundle_for_write(tuple_var) : st.get_bundle(tuple_var);
    if (!bundle) {
      bundle = std::make_shared<Bundle>(tuple_var);
      st.set(tuple_var, bundle);
    }
    const std::string& name = path[0];

    auto v = resolve_value();
    if (v) {
      // Update bundle in place; scalar values are propagated by tuple_get.
      bundle->set(name, *v);
    } else if (val_child.is_ref && st.has_bundle(val_child.text)) {
      auto sub = st.get_bundle(val_child.text);
      if (sub) {
        bundle->set(name, sub);
      }
    }
    move_to_parent();
    return;
  }

  std::string field;
  for (const auto& p : path) {
    field += '.';
    field += p;
  }
  auto key = tuple_var + field;

  if (val_child.is_ref) {
    if (st.has_trivial(val_child.text)) {
      store_trivial(key, st.get_trivial(val_child.text));
    } else if (st.has_bundle(val_child.text)) {
      auto b = st.get_bundle(val_child.text);
      if (b) {
        st.set(key, b);  // bundle pointer store (not a scalar value)
      }
    }
  } else {
    Const val = *Dlop::from_pyrope(val_child.text);
    if (!val.is_invalid()) {
      store_trivial(key, val);
    }
  }
  move_to_parent();
}

// ── Bitwidth Insensitive Reduce ──────────────────────────────────────────────
//
// Each reduction op reads its single operand and stores the result of the
// matching Dlop op. The Dlop op already encodes the right unknown handling:
// rand_op/rxor_op return a 1-bit unknown when the input has unknowns, and
// ror_op (binary form, called with v on both sides) folds true if any bit is
// set — which is what red_or wants for the unknown-friendly case too.
template <typename F>
void uPass_constprop::process_reduction(F op) {
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto input = current_prim_value();
  move_to_parent();
  if (input.is_invalid() || input.is_string()) {
    return;
  }
  Const r = op(input);
  if (!r.is_invalid()) {
    store_trivial(var, r);
  }
}

void uPass_constprop::process_red_or() {
  process_reduction([](const Const& v) -> Const { return *v.ror_op(); });
}

void uPass_constprop::process_red_and() {
  process_reduction([](const Const& v) -> Const { return *v.rand_op(); });
}

void uPass_constprop::process_red_xor() {
  process_reduction([](const Const& v) -> Const { return *v.rxor_op(); });
}

// popcount (`a#+[..]`): number of set bits, returned as an integer Const.
// Const::popcount_op handles the unknown-bit and negative/unknown-sign cases.
void uPass_constprop::process_popcount() {
  process_reduction([](const Const& v) -> Const { return *v.popcount_op(); });
}

// ── Bit Manipulation ─────────────────────────────────────────────────────────

// ── Emit classification (Slice 1 drop + fold rules) ─────────────────────────
//
// See upass.md §2.4. Called by the runner AFTER process_* has populated the
// symbol table for this node, so the LHS entry — if any — reflects the
// value this statement just computed.
//
// Rule: drop this statement iff
//   - LHS (first child) is a ref, and
//   - the symbol table holds a concrete Const for LHS (known, no unknowns).
// Otherwise emit.
upass::Emit_decision uPass_constprop::classify_statement() {
  // Peek at the first child (LHS/dst) without disturbing cursor state.
  // cassert is dispatched through the same path but has no dst — its
  // single child is an operand; always emit so it reaches the verifier.
  if (is_type(Lnast_ntype::Lnast_ntype_cassert)) {
    return upass::Emit_decision::emit_node();
  }

  bool        got_child  = move_to_child();
  bool        lhs_is_ref = got_child && is_type(Lnast_ntype::Lnast_ntype_ref);
  std::string lhs_text{lhs_is_ref ? current_text() : std::string_view{}};
  move_to_parent();

  if (!lhs_is_ref || lhs_text.empty()) {
    return upass::Emit_decision::emit_node();
  }

  // 2d-reg — writes to reg-declared names are next-state din writes; the
  // write itself is the payload (process_assign never binds them, so the ST
  // can't hold a value — this guard is belt-and-braces against stale state).
  if (reg_decl_names_.contains(lhs_text)) {
    return upass::Emit_decision::emit_node();
  }

  // Drop iff the ST holds a fully-known value. is_known_const() encapsulates
  // has_trivial + is_invalid + has_unknowns in one call.
  if (!st.is_known_const(lhs_text)) {
    return upass::Emit_decision::emit_node();
  }

  // RHS-aware guard: keep a 2-child `store(lhs, rhs)` whose RHS is a ref that
  // is NOT a genuine folded constant — i.e. either has no comptime value (a
  // runtime producer) or holds `nil` (an unset/poison marker). process_assign
  // cannot propagate such a RHS, so `lhs` keeps whatever it held before, and
  // the is_known_const check above may be true only because of a *stale* value
  // this very store was meant to overwrite. Dropping the store then loses the
  // write. This is the 1i comb-inliner bug: the inliner seeds a scalar output
  // `<tag>out = nil`, the body assigns it a runtime value (`store(<tag>out,
  // ___mult)`, ___mult = `a*b` over module inputs), so both stay nil; the seed
  // is then aliased down the epilogue chain (`___ret = <tag>out`) and into the
  // caller's `store(out, ___ret)` (out a reg/boundary write). is_nil() passes
  // is_known_const (it is neither invalid nor unknown, to_pyrope() `0`), so
  // every link looked "known" and was dropped — leaving the output undriven
  // (`out = 0`). Keeping the store wherever the RHS is nil-flavored or
  // runtime preserves the chain so the real value lands. A genuine non-nil
  // folded RHS is faithfully recovered by fold_ref at the consumer, so
  // dropping it stays safe (and comptime nil==nil folding is untouched: that
  // happens in fold_ref, not here).
  {
    const auto here = lm->save_cursor();
    bool       keep = false;
    if (lm->current_num_children() == 2 && move_to_child() && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
      const auto rhs = current_text();
      keep           = !st.is_known_const(rhs) || (st.has_trivial(rhs) && st.get_trivial(rhs).is_nil());
    }
    lm->restore_cursor(here);
    if (keep) {
      return upass::Emit_decision::emit_node();
    }
  }

  // Bundle-shape guard. is_known_const returns true as soon as the bundle's
  // position-0 entry is a concrete Const, but that's also true for:
  //   - multi-entry tuples: `(1,2)` — two non-attribute entries
  //   - single-entry named tuples: `(c=2)` — one entry but keyed `c`,
  //     not `0`; inlining as a scalar loses the name
  // fold_ref returns only the position-0 trivial, so dropping the producer
  // and substituting via fold_ref would silently truncate `(1,2)` → `1`
  // or convert `(c=2)` → `2`. Keep the stmt unless the bundle is a
  // *trivial* scalar (exactly one anonymous `0` entry), where fold_ref's
  // inline is faithful.
  if (auto b = st.get_bundle(lhs_text); b && !b->is_trivial_scalar()) {
    return upass::Emit_decision::emit_node();
  }

  return upass::Emit_decision::drop();
}

std::optional<Const> uPass_constprop::fold_ref(std::string_view name) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (!st.is_known_const(name)) {
    return std::nullopt;
  }
  // is_known_const returns true once the bundle's position-0 entry is a
  // concrete Const, but that's also true for multi-entry tuples like
  // `(1,2)` and single-entry named tuples like `(c=2)`. Inlining the
  // position-0 trivial would silently truncate `(1,2)` → `1` or strip
  // the name from `(c=2)` → `2`. Only inline when the bundle is a
  // trivial scalar (exactly one anonymous `0` entry); for everything
  // else, return nullopt so the consumer keeps the ref and reads the
  // full bundle from the symbol table.
  if (auto b = st.get_bundle(name); b && !b->is_trivial_scalar()) {
    return std::nullopt;
  }
  return st.get_trivial(name);
}

std::optional<std::vector<std::pair<std::string, Const>>> uPass_constprop::provide_bundle_fields(std::string_view name) {
  // 1i Phase E shared-ST read: hand the runner the flat comptime-const fields
  // of `name`'s bundle so it can bind/propagate tuple actuals it can't reach
  // through scalar fold_ref. Returns nullopt when `name` is not a bundle; a
  // field whose value isn't a concrete Const is skipped (caller treats a
  // partial result as "not fully comptime").
  auto b = st.get_bundle(std::string(name));
  if (!b) {
    return std::nullopt;
  }
  std::vector<std::pair<std::string, Const>> out;
  for (const auto& [k, ep] : b->non_attr_entries()) {
    if (ep && !ep->trivial.is_invalid()) {
      out.emplace_back(k, ep->trivial);
    }
  }
  return out;
}

std::string uPass_constprop::provide_typename(std::string_view name) {
  auto it = typename_of_var.find(std::string(name));
  return it != typename_of_var.end() ? it->second : std::string{};
}

std::optional<std::pair<Const, Const>> uPass_constprop::provide_range(std::string_view name) {
  // Comptime for-loop unroll (runner): a `for i in lo..hi` iterable lowers to a
  // `range` tmp that process_range recorded as (start, end_inclusive). Hand the
  // runner those folded bounds so it can iterate. The runner checks both are
  // concrete integers before unrolling.
  auto it = range_map.find(std::string(name));
  if (it == range_map.end()) {
    return std::nullopt;
  }
  return it->second;
}

void uPass_constprop::process_sext() {
  // Sign-extend: [sext: ref(dst), ref_or_const(src), const(nbits)]
  // sext_op(ebits) interprets bit (ebits-1) of src as the sign bit.
  // Delegate to Dlop: src may carry unknown bits (sext_op sign-extends the
  // unknown sign too); skip only non-values. nbits is a host int, so it must
  // be a concrete integer.
  move_to_child();
  auto var = current_text();
  move_to_sibling();
  const auto src = current_prim_value();
  move_to_sibling();
  const auto nbits_lc = current_prim_value();
  move_to_parent();
  if (is_numeric(src) && nbits_lc.is_integer() && !nbits_lc.has_unknowns()) {
    store_trivial(var, src.sext_op(nbits_lc));
  }
}

void uPass_constprop::process_get_mask() {
  // Layout: ref(dst), ref(value), (const|ref)(mask)
  // The mask operand may be:
  //   - a constant integer / known scalar (treated as a bitmask),
  //   - a `range` ref previously bound in range_map (`b#[lo..]`,
  //     `b#[lo..=hi]`, `b#[..=hi]`, etc.).
  // Range refs lower via apply_range_mask, which routes open-ended `lo..`
  // through sra_op (skipping get_mask_op's broken negative-mask path) and
  // closed `lo..=hi` through get_mask_op with the equivalent positive mask.
  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Const value = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  bool  is_range = false;
  Const range_start;
  Const range_end;
  Const mask;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    if (auto rit = range_map.find(std::string(current_text())); rit != range_map.end()) {
      is_range    = true;
      range_start = rit->second.first;
      range_end   = rit->second.second;
    } else {
      mask = current_prim_value();
    }
  } else {
    mask = current_prim_value();
  }

  move_to_parent();

  // Delegate to Dlop: a value with unknown bits is sliced bit-precisely by
  // get_mask_op, so only non-values (invalid/string) are skipped here.
  if (!is_numeric(value)) {
    return;
  }

  if (is_range) {
    const Const result = apply_range_mask(value, range_start, range_end);
    if (!result.is_invalid()) {
      store_trivial(var, result);
    }
    return;
  }

  // The mask may itself carry unknown bits — get_mask_op handles that
  // (returns a sound bounded-width unknown), so don't pre-filter it.
  if (!is_numeric(mask)) {
    return;
  }
  // Trust Dlop::get_mask_op — including the single-bit-mask → Bool rule. We
  // store whatever it returns (invalid included): the fold is real and
  // downstream code shouldn't silently drop it.
  store_trivial(var, *value.get_mask_op(mask));
}

void uPass_constprop::process_set_mask() {
  // Layout: ref(dst), ref(input), (const|ref)(mask), (const|ref)(value)
  // Mirrors process_get_mask but writes back via set_mask_op. The mask
  // operand can be a constant integer (treated as a bitmask) or a `range`
  // ref previously bound in range_map (lo..hi closed; `lo..` open).
  move_to_child();
  auto var = std::string(current_text());
  move_to_sibling();
  Const input_val = current_prim_value();
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  bool  is_range = false;
  Const range_start;
  Const range_end;
  Const mask;
  if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
    if (auto rit = range_map.find(std::string(current_text())); rit != range_map.end()) {
      is_range    = true;
      range_start = rit->second.first;
      range_end   = rit->second.second;
    } else {
      mask = current_prim_value();
    }
  } else {
    mask = current_prim_value();
  }

  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }
  Const new_val = current_prim_value();
  move_to_parent();

  // Delegate to Dlop: both the input being written into and the value being
  // written may carry unknown bits — set_mask_op tracks them bit-precisely.
  // Only the *mask* (which bits to write) must be concrete (see below).
  if (!is_numeric(input_val) || !is_numeric(new_val)) {
    return;
  }

  Const final_mask;
  if (is_range) {
    if (range_end.is_nil()) {
      // Open-ended `lo..`: bits lo and above. For set_mask we need a concrete
      // bitmask, but the upper bound isn't fixed. Skip — without a pinned
      // width there's no concrete mask to emit.
      return;
    }
    // mask = ((1 << (end - start + 1)) - 1) << start — all Const arithmetic,
    // no to_i / width / range guards (task 1g; mirrors apply_range_mask).
    auto one   = Dlop::create_integer(1);
    auto width = range_end.sub_op(range_start)->add_op(*one);
    final_mask = *one->shl_op(*width)->sub_op(*one)->shl_op(range_start);
  } else {
    // The mask selects *which* bits to overwrite; Dlop::set_mask_op requires
    // it concrete (asserts on an unknown mask, unlike get_mask_op). This is a
    // Dlop precondition on the bit-selection, not a value pre-filter — the
    // data operands (input_val/new_val) above already pass unknowns through.
    if (!foldable(mask)) {
      return;
    }
    final_mask = mask;
  }

  store_trivial(var, *input_val.set_mask_op(final_mask, new_val));
}
