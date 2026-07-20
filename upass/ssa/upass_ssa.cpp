//  This file is distributed under the BSD 3-Clause License. See LICENSE for
//  details.

#include "upass_ssa.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "hlop/dlop.hpp"
#include "lnast_ntype.hpp"
#include "range_bits.hpp"
#include "str_tools.hpp"

namespace {
// Returns true for LNAST statement types whose first child is the write
// destination (LHS) and whose remaining children are read operands (RHS).
// This range covers: dp_assign, declare, store, delay_assign, all arithmetic/
// logic/comparison/shift/bit-manipulation ops up to `ge` inclusive.
// (`assign` was deleted; `dp_assign` is now the low anchor. store/declare are
// in range; the caller excludes declare + store-as-tuple_set from versioning.)
// Excluded: if, for, while, func_*, stmts, io, top, ref, const, tuple_*,
// attr_*, cassert, type_*, and all type-leaf nodes.
constexpr bool stmt_has_dest(Lnast_ntype::Lnast_ntype_int t) {
  return t >= Lnast_ntype::Lnast_ntype_dp_assign &&
         t <= Lnast_ntype::Lnast_ntype_ge;
}

// Parse the bits/is_signed from a prim_type_uint/prim_type_sint subtree
// (or any other type ntype). Returns {bits=0, is_signed=true} on miss.
struct Type_info {
  int32_t bits = 0;
  bool is_signed = true;
  Io_kind kind = Io_kind::none;
  bool has_range =
      false; // explicit `int(min,max)` bounds (both known, fit i64)
  int64_t range_min = 0;
  int64_t range_max = 0;
};
Type_info type_info_from(const std::shared_ptr<Lnast> &lnast,
                         Lnast_nid type_nid) {
  Type_info ti;
  if (type_nid.is_invalid()) {
    return ti;
  }
  const auto tty = lnast->get_type(type_nid);
  if (Lnast_ntype::is_prim_type_bool(tty)) {
    ti.kind = Io_kind::boolean;
    return ti;
  }
  if (Lnast_ntype::is_prim_type_string(tty)) {
    ti.kind = Io_kind::string;
    return ti;
  }
  if (Lnast_ntype::is_prim_type_int(tty)) {
    ti.kind = Io_kind::integer;
    // Canonical integer: derive bits/signed from the (max,min)
    // range children ("nil" = unbounded). signed ⇐ min<0; bits ⇐ both known.
    auto max_nid = lnast->get_first_child(type_nid);
    std::optional<Dlop> max_v;
    std::optional<Dlop> min_v;
    if (!max_nid.is_invalid() &&
        Lnast_ntype::is_const(lnast->get_type(max_nid))) {
      auto v = Dlop::from_pyrope(lnast->get_name(max_nid));
      if (v->is_integer()) {
        max_v = *v;
      }
      auto min_nid = lnast->get_sibling_next(max_nid);
      if (!min_nid.is_invalid() &&
          Lnast_ntype::is_const(lnast->get_type(min_nid))) {
        auto mv = Dlop::from_pyrope(lnast->get_name(min_nid));
        if (mv->is_integer()) {
          min_v = *mv;
        }
      }
    }
    ti.is_signed =
        !(min_v && !min_v->is_negative()); // unsigned only when min known ≥ 0
    // bits from the bound Consts via get_bits() (signed width; drop the sign
    // bit when unsigned) — no to_i, handles >64-bit bounds.
    if (max_v && min_v && max_v->is_integer() && min_v->is_integer()) {
      if (!min_v->is_negative()) {
        ti.bits = max_v->is_known_zero()
                      ? 0
                      : static_cast<int32_t>(max_v->get_bits() - 1);
      } else {
        ti.bits = static_cast<int32_t>(
            std::max<int64_t>(max_v->get_bits(), min_v->get_bits()));
      }
      // Keep the EXACT declared bounds for range-precise overload dispatch (the
      // `bits` window above only approximates `int(min,max)`).
      if (max_v->is_just_i64() && min_v->is_just_i64()) {
        ti.has_range = true;
        ti.range_min = min_v->to_just_i64();
        ti.range_max = max_v->to_just_i64();
      }
    }
  }
  return ti;
}
} // namespace

// ── is_user_var ──────────────────────────────────────────────────────────────
bool uPass_ssa::is_user_var(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  // Compiler-generated temporaries (`%`-prefixed) — already unique per
  // func_extract, and never re-versioned.
  if (Lnast::is_tmp(name)) {
    return false;
  }
  return true;
}

// ── copy_subtree ─────────────────────────────────────────────────────────────
void uPass_ssa::copy_subtree(const std::shared_ptr<Lnast> &src,
                             const Lnast_nid &src_nid,
                             const std::shared_ptr<Lnast> &dst,
                             const Lnast_nid &dst_parent) {
  auto type = src->get_type(src_nid);
  Lnast_nid new_nid;
  if (Lnast_ntype::is_ref(type)) {
    new_nid = dst->add_child(dst_parent,
                             Lnast_node::create_ref(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_const(type)) {
    new_nid = dst->add_child(dst_parent,
                             Lnast_node::create_const(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_invalid(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_invalid());
  } else {
    new_nid = dst->add_child(dst_parent, type);
  }
  // Carry (same-locator: the staging body replaces src's own body).
  if (Lnast::srcid_carries(type)) {
    dst->set_srcid(new_nid, src->get_srcid(src_nid));
  }
  for (auto child : src->children(src_nid)) {
    copy_subtree(src, child, dst, new_nid);
  }
}

// ── copy_with_rename ─────────────────────────────────────────────────────────
void uPass_ssa::copy_with_rename(
    const std::shared_ptr<Lnast> &src, const Lnast_nid &src_nid,
    const std::shared_ptr<Lnast> &dst, const Lnast_nid &dst_parent,
    const absl::flat_hash_map<std::string, std::string> &rename_map) {
  auto type = src->get_type(src_nid);
  Lnast_nid new_nid;
  if (Lnast_ntype::is_ref(type)) {
    // Heterogeneous lookup: no temporary std::string allocation per ref.
    std::string_view name = src->get_name(src_nid);
    auto it = rename_map.find(name);
    new_nid = dst->add_child(
        dst_parent, Lnast_node::create_ref(it != rename_map.end()
                                               ? std::string_view{it->second}
                                               : name));
  } else if (Lnast_ntype::is_const(type)) {
    new_nid = dst->add_child(dst_parent,
                             Lnast_node::create_const(src->get_name(src_nid)));
  } else if (Lnast_ntype::is_invalid(type)) {
    new_nid = dst->add_child(dst_parent, Lnast_node::create_invalid());
  } else {
    new_nid = dst->add_child(dst_parent, type);
  }
  // Carry (same-locator: the staging body replaces src's own body).
  if (Lnast::srcid_carries(type)) {
    dst->set_srcid(new_nid, src->get_srcid(src_nid));
  }
  for (auto child : src->children(src_nid)) {
    // A named tuple-literal field `store(ref key, val)` under a tuple_add: the
    // key is a structural FIELD LABEL, not a variable read — renaming it (the
    // module also has a var of that name, e.g. a struct field `data` next to a
    // local `data`) corrupts the bundle's field name, so a later expansion
    // against the callee's flattened `port.key` leaf params (or a `t.key`
    // read) can never match. Copy the key verbatim; only the value follows the
    // rename map. (Same rule as a func_call named-arg formal, above.)
    if (Lnast_ntype::is_tuple_add(type) && Lnast_ntype::is_store(src->get_type(child))) {
      auto st = dst->add_child(new_nid, Lnast_ntype::create_store());
      if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
        dst->set_srcid(st, src->get_srcid(child));
      }
      bool first_field_child = true;
      for (auto c2 : src->children(child)) {
        if (first_field_child) {
          copy_subtree(src, c2, dst, st);  // field key — verbatim
          first_field_child = false;
        } else {
          copy_with_rename(src, c2, dst, st, rename_map);  // field value — a read
        }
      }
      continue;
    }
    copy_with_rename(src, child, dst, new_nid, rename_map);
  }
}

// ── run ──────────────────────────────────────────────────────────────────────
void uPass_ssa::run(const std::shared_ptr<Lnast> &lnast, const std::vector<std::shared_ptr<Lnast>> *all_units_) {
  auto root = lnast->get_root();

  // ── Demote stale SSA versions from a prior run ──────────────────────────
  // A reloaded forest (an `ln:` import, or a `lhd compile ln:DIR` recompile)
  // carries body names from a PRIOR SSA pass (`<base>___ssa_<N>`). The
  // `___ssa_` namespace is PRIVATE to a single SSA run: this run re-mints
  // `<base>___ssa_<N>` for the same base, so a merge-back store aliases the
  // stale version onto the freshly-minted one and collapses to a self-store
  // (`_mux_3___ssa_1 = _mux_3___ssa_1` -> the "irrelevant assignment" error
  // in the runner). A freshly-parsed body never contains `___ssa_` (only SSA
  // mints it), so any `___ssa_<digits>` seen at SSA entry is necessarily
  // stale — demote it out of the private namespace (`___ssa_<N>` -> `__w<N>`,
  // the same move pass.prp_writer makes on emit) so this run versions freely
  // without collision. A consistent rename keeps every def/use linked.
  for (auto nid : lnast->depth_preorder(root)) {
    if (nid.is_invalid()) {
      continue;
    }
    std::string_view nm  = lnast->get_name(nid);
    auto             pos = nm.rfind("___ssa_");
    if (pos == std::string_view::npos) {
      continue;
    }
    const size_t d = pos + 7;  // first char past "___ssa_"
    if (d >= nm.size()) {
      continue;  // bare "___ssa_" with no version — leave intact
    }
    bool all_digits = true;
    for (size_t i = d; i < nm.size(); ++i) {
      if (nm[i] < '0' || nm[i] > '9') {
        all_digits = false;
        break;
      }
    }
    if (!all_digits) {
      continue;  // not a pure-digit SSA suffix — not our version form
    }
    std::string demoted(nm.substr(0, pos));
    demoted += "__w";
    demoted += nm.substr(d);
    lnast->set_name(nid, demoted);  // interns the demoted name
  }

  // ── Detect post-func_extract shape: top must have an 'io' child ──────────
  bool found_io = false;
  bool found_stmts = false;
  Lnast_nid io_nid;
  Lnast_nid stmts_nid;

  for (auto child : lnast->children(root)) {
    const auto t = lnast->get_type(child);
    if (Lnast_ntype::is_io(t)) {
      io_nid = child;
      found_io = true;
    }
    if (Lnast_ntype::is_stmts(t)) {
      stmts_nid = child;
      found_stmts = true;
    }
  }
  if (!found_io || !found_stmts) {
    return; // Not a post-func_extract function LNAST — nothing to do.
  }

  // ── Walk io's two tuple_add children (inputs, outputs) ──────────────────
  //
  // Each child is a `tuple_add` whose children are arg-shape assigns:
  //   assign(ref name, const default-or-ref-marker, type-subtree?)
  // A composite tuple type has the 3rd child as a `tuple_add` of recursive
  // assigns. We flatten those into dotted leaf names (`ar.x`, `ar.y`) and
  // populate io_meta in the same order. The flat form is also re-emitted in
  // the staging tree's io node so the post-SSA dump shows the expanded
  // signature.
  Lnast_nid in_tup_nid;
  Lnast_nid out_tup_nid;
  {
    auto it = lnast->children(io_nid).begin();
    auto end = lnast->children(io_nid).end();
    if (it != end) {
      in_tup_nid = *it;
      ++it;
    }
    if (it != end) {
      out_tup_nid = *it;
    }
  }

  Lnast_tree_io &meta = lnast->io_meta();

  // Build flat (name, bits, is_signed, is_ref) tuples. Recursive helper:
  // when an assign's type slot is a `tuple_add`, expand each field with a
  // dotted-name prefix; otherwise emit one leaf entry.
  using Flat_field = Lnast_io_entry;
  std::vector<Flat_field> flat_inputs;
  std::vector<Flat_field> flat_outputs;

  // Read a `stages(min,max)` node's two const children. A `nil`
  // text (the `@[]` explicit no-check opt-out on a mod output) harvests as
  // -1: distinguishable from a declared `@[0]` (which IS a real check that a
  // mod output is a combinational feedthrough).
  auto read_stages = [&](Lnast_nid st_nid) -> std::pair<int32_t, int32_t> {
    int32_t smin = 0;
    int32_t smax = 0;
    auto c = lnast->get_first_child(st_nid);
    if (!c.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c))) {
      if (lnast->get_name(c) == "nil") {
        smin = -1;
      } else if (auto v = Dlop::from_pyrope(lnast->get_name(c));
                 v && v->is_integer() && v->is_just_i64()) {
        smin = static_cast<int32_t>(v->to_just_i64());
      }
      auto c2 = lnast->get_sibling_next(c);
      if (!c2.is_invalid() && Lnast_ntype::is_const(lnast->get_type(c2))) {
        if (lnast->get_name(c2) == "nil") {
          smax = -1;
        } else if (auto v2 = Dlop::from_pyrope(lnast->get_name(c2));
                   v2 && v2->is_integer() && v2->is_just_i64()) {
          smax = static_cast<int32_t>(v2->to_just_i64());
        }
      }
    }
    return {smin, smax};
  };

  std::function<void(Lnast_nid, const std::string &, bool,
                     std::vector<Flat_field> &, int32_t, int32_t)>
      flatten_assign;
  flatten_assign = [&](Lnast_nid assign_nid, const std::string &prefix,
                       bool collect_is_ref, std::vector<Flat_field> &out,
                       int32_t inh_smin, int32_t inh_smax) {
    if (!Lnast_ntype::is_store(lnast->get_type(assign_nid))) {
      return;
    }
    auto name_nid = lnast->get_first_child(assign_nid);
    if (name_nid.is_invalid() ||
        !Lnast_ntype::is_ref(lnast->get_type(name_nid))) {
      return;
    }
    auto leaf_name = std::string(lnast->get_name(name_nid));
    auto full = prefix.empty() ? leaf_name : prefix + "." + leaf_name;

    auto rhs_nid = lnast->get_sibling_next(name_nid);
    auto type_nid =
        rhs_nid.is_invalid() ? rhs_nid : lnast->get_sibling_next(rhs_nid);

    // The trailing stages(min,max) annotation either follows the
    // optional type child or stands in its place (untyped entry). Identify
    // by ntype, harvest, and drop it from the type slot. A composite entry's
    // stages propagates to every flattened leaf (each leaf is one output
    // flop at the same depth).
    int32_t smin = inh_smin;
    int32_t smax = inh_smax;
    if (!type_nid.is_invalid() &&
        Lnast_ntype::is_stages(lnast->get_type(type_nid))) {
      std::tie(smin, smax) = read_stages(type_nid);
      // A type may still FOLLOW the stages child: trees are append-only, so
      // the generic specializer injects `-> (r:T@[N])`'s concrete type after
      // the re-emitted stages slot (store(r, nil, stages, prim_type_int)).
      auto after = lnast->get_sibling_next(type_nid);
      if (!after.is_invalid() &&
          !Lnast_ntype::is_stages(lnast->get_type(after))) {
        type_nid = after;
      } else {
        type_nid = Lnast_nid(); // no real type child
      }
    } else if (!type_nid.is_invalid()) {
      auto st_nid = lnast->get_sibling_next(type_nid);
      if (!st_nid.is_invalid() &&
          Lnast_ntype::is_stages(lnast->get_type(st_nid))) {
        std::tie(smin, smax) = read_stages(st_nid);
      }
    }

    // Composite tuple type → recurse on each inner assign with a dotted prefix.
    if (!type_nid.is_invalid() &&
        Lnast_ntype::is_tuple_add(lnast->get_type(type_nid))) {
      // An INLINE tuple type on `self` would flatten it into dotted
      // leaves (`self.a`, `self.b`) and break the inliner's `has_self`
      // detection (io.inputs[0].name == "self"). Only named self types are
      // supported; reject with a clean error instead of mis-binding later.
      if (collect_is_ref && prefix.empty() && leaf_name == "self") {
        const auto msg =
            std::format("`self` cannot use an inline tuple type in `{}`",
                        lnast->get_top_module_name());
        livehd::diag::sink().emit(livehd::diag::Diagnostic{
            .severity = livehd::diag::Severity::error,
            .code = "self-inline-type",
            .category = "type",
            .pass = "upass.ssa",
            .message = msg,
            .span = lnast->span_of(type_nid),
            .hint = "declare the tuple as a named type and use `self:Name`"});
        throw std::runtime_error(msg);
      }
      for (auto inner : lnast->children(type_nid)) {
        flatten_assign(inner, full, collect_is_ref, out, smin, smax);
      }
      return;
    }

    bool is_ref = false;
    if (collect_is_ref && !rhs_nid.is_invalid() &&
        Lnast_ntype::is_const(lnast->get_type(rhs_nid)) &&
        lnast->get_name(rhs_nid) == "ref") {
      is_ref = true;
    }
    // Var-arg param: the default-value slot carries the `...`
    // sentinel (mirrors the `ref` marker above). Only meaningful on an input.
    bool is_vararg = false;
    if (collect_is_ref && !rhs_nid.is_invalid() &&
        Lnast_ntype::is_const(lnast->get_type(rhs_nid)) &&
        lnast->get_name(rhs_nid) == "...") {
      is_vararg = true;
    }
    // Input DEFAULT (`comb f(in1:u4, in2=3)`, todo 3g E): the slot carries the
    // `__default` sentinel; the value itself is a body-prologue store. Records
    // io_meta.has_default so the comb inliner tolerates an omitted arg (and
    // skips the prologue when the arg is provided).
    bool has_default = false;
    if (collect_is_ref && !rhs_nid.is_invalid() &&
        Lnast_ntype::is_const(lnast->get_type(rhs_nid)) &&
        lnast->get_name(rhs_nid) == "__default") {
      has_default = true;
    }
    // A NAMED type annotation (`self:t1`, `x:Point`) arrives as a
    // `ref` type child (prp2lnast emit_type_expr). Record the typename so the
    // inliner's typed-self does-check can resolve the declared fields.
    std::string type_name;
    if (!type_nid.is_invalid() &&
        Lnast_ntype::is_ref(lnast->get_type(type_nid))) {
      type_name = std::string(lnast->get_name(type_nid));
    }
    auto ti = type_info_from(lnast, type_nid);
    // An IMPORTED scalar alias port type (`cmd:pkg.VPU_FCMD_SZ_T`) resolves
    // its range off the exporting unit's pub-type face — without it the port
    // harvests width-less and the lowering breaks.
    if (ti.kind == Io_kind::none && !type_name.empty() && all_units_ != nullptr) {
      if (const auto dot = type_name.rfind('.'); dot != std::string::npos) {
        const std::string unit = type_name.substr(0, dot);
        const std::string mem  = type_name.substr(dot + 1);
        for (const auto &ex : *all_units_) {
          if (ex == nullptr || ex->get_top_module_name() != unit || !ex->get_lambda_kind().empty()) {
            continue;
          }
          std::string max_txt;
          std::string min_txt;
          if (ex->pub_type_face(mem, max_txt, min_txt)) {
            auto max_v = Dlop::from_pyrope(max_txt);
            auto min_v = Dlop::from_pyrope(min_txt);
            if (max_v && min_v && max_v->is_integer() && min_v->is_integer()) {
              ti.kind      = Io_kind::integer;
              ti.is_signed = min_v->is_negative();
              if (!min_v->is_negative()) {
                ti.bits = max_v->is_known_zero() ? 0 : static_cast<int32_t>(max_v->get_bits() - 1);
              } else {
                ti.bits = static_cast<int32_t>(std::max<int64_t>(max_v->get_bits(), min_v->get_bits()));
              }
              if (max_v->is_just_i64() && min_v->is_just_i64()) {
                ti.has_range = true;
                ti.range_min = min_v->to_just_i64();
                ti.range_max = max_v->to_just_i64();
              }
            }
          }
          break;
        }
      }
    }
    out.push_back({full, ti.bits, ti.is_signed, is_ref, is_vararg, ti.kind,
                   smin, smax, std::move(type_name)});
    out.back().has_range = ti.has_range;
    out.back().range_min = ti.range_min;
    out.back().range_max = ti.range_max;
    out.back().has_default = has_default;
  };

  if (!in_tup_nid.is_invalid()) {
    for (auto entry : lnast->children(in_tup_nid)) {
      flatten_assign(entry, std::string{}, /*collect_is_ref=*/true, flat_inputs,
                     0, 0);
    }
  }
  if (!out_tup_nid.is_invalid()) {
    for (auto entry : lnast->children(out_tup_nid)) {
      flatten_assign(entry, std::string{}, /*collect_is_ref=*/false,
                     flat_outputs, 0, 0);
    }
  }
  meta.inputs = flat_inputs;
  meta.outputs = flat_outputs;

  // ── Build staging tree with SSA renaming ─────────────────────────────────
  auto staging_body = lnast->forest()->create_tree_temp(
      std::format("ssa-{}", lnast->get_top_module_name()));
  auto staging =
      std::make_shared<Lnast>(staging_body, lnast->get_top_module_name());
  auto new_root = staging->set_root(Lnast_ntype::create_top());
  // Module anchor: replace_body swaps the whole tree — re-stamp the
  // unit-declaration id (func_extract put it on the source root; the id
  // lives in `lnast`'s locator, which survives replace_body).
  if (const auto id = lnast->get_srcid(lnast->get_root());
      id != hhds::SourceId_invalid) {
    staging->set_srcid(new_root, id);
  }

  // Re-emit the io node with flattened leaf entries (no nested tuple_add
  // type subtree). Width/signedness for each entry come from io_meta.
  auto new_io = staging->add_child(new_root, Lnast_ntype::create_io());
  auto emit_section = [&](const std::vector<Flat_field> &fields,
                          bool is_input) {
    auto tup = staging->add_child(new_io, Lnast_ntype::create_tuple_add());
    for (const auto &f : fields) {
      auto a = staging->add_child(tup, Lnast_ntype::create_store());
      staging->add_child(a, Lnast_node::create_ref(f.name));
      staging->add_child(
          a, Lnast_node::create_const(is_input && f.is_varargs ? "..."
                                      : is_input && f.is_ref   ? "ref"
                                                               : "nil"));
      if (f.bits > 0) {
        // Re-emit the canonical prim_type_int(max,min) from the
        // flat field's (bits, signed).
        auto ty = staging->add_child(a, Lnast_ntype::create_prim_type_int());
        staging->add_child(
            ty, Lnast_node::create_const(std::string(
                    upass::max_from_bits(f.bits, f.is_signed).to_pyrope())));
        staging->add_child(
            ty, Lnast_node::create_const(std::string(
                    upass::min_from_bits(f.bits, f.is_signed).to_pyrope())));
      }
      // Re-emit the trailing stages(min,max) annotation so the
      // post-SSA io tree keeps the pipe contract visible (the LN pipe upass
      // and lnast_to_slop read io_meta, but the dump stays source-of-truth).
      // A mod re-emits EVERY output's slot: `@[0]` (0,0) is a real check the
      // LG checker must see, and the `@[]` opt-out (-1) re-emits as `nil` so
      // tolg skips the pending record (1a-mem follow-up).
      if (!is_input &&
          (f.stages_min > 0 || lnast->get_lambda_kind() == "mod")) {
        auto st = staging->add_child(a, Lnast_ntype::create_stages());
        staging->add_child(
            st, Lnast_node::create_const(
                    f.stages_min < 0 ? "nil" : std::to_string(f.stages_min)));
        staging->add_child(
            st, Lnast_node::create_const(
                    f.stages_max < 0 ? "nil" : std::to_string(f.stages_max)));
      }
    }
  };
  emit_section(flat_inputs, /*is_input=*/true);
  emit_section(flat_outputs, /*is_input=*/false);

  // ── name-case-collision lint ────────────────────────────────────────────
  // LiveHD/Pyrope names are CASE-SENSITIVE. Two VALUE variables (or a variable
  // and an IO port) whose names differ ONLY in letter case used to alias under
  // the old case-insensitive policy; now they are distinct. Warn once per
  // colliding name so a program that silently relied on the fold gets flagged
  // rather than changing meaning. Type/enum/func names live in a separate
  // namespace (they never aliased a value var), so they are excluded. Runs on
  // the ORIGINAL (pre-SSA-rename) body.
  {
    auto base_of = [](std::string_view n) -> std::string_view {
      const auto dot = n.find('.');
      return dot == std::string_view::npos ? n : n.substr(0, dot);
    };

    // Names bound as a comptime entity — `declare/attr_set(name, …, mode)` with
    // a non-value mode (`type`/`enum`/`fun`, not mut/const/reg/stage). `enum E`
    // and a value `const e` are NOT a collision (different namespaces).
    absl::flat_hash_set<std::string> type_names;
    std::function<void(const Lnast_nid &)> collect_types =
        [&](const Lnast_nid &nid) {
          const auto t = lnast->get_type(nid);
          if (Lnast_ntype::is_declare(t) || Lnast_ntype::is_attr_set(t)) {
            auto c0 = lnast->get_first_child(nid);
            if (!c0.is_invalid() && Lnast_ntype::is_ref(lnast->get_type(c0))) {
              auto c1 = lnast->get_sibling_next(c0);
              auto c2 = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
              if (!c2.is_invalid() && (Lnast_ntype::is_declare(t) ||
                                       lnast->get_name(c1) == "type")) {
                const auto mode = lnast->get_name(c2);
                if (mode != "mut" && mode != "const" && mode != "reg" &&
                    mode != "stage") {
                  type_names.insert(std::string(base_of(lnast->get_name(c0))));
                }
              }
            }
          }
          for (auto c : lnast->children(nid)) {
            collect_types(c);
          }
        };
    collect_types(stmts_nid);

    absl::flat_hash_map<std::string, std::string>
        first_spelling;                      // folded base → first spelling
    absl::flat_hash_set<std::string> warned; // folds already reported

    auto note = [&](std::string_view name, const Lnast_nid &at) {
      const auto base = base_of(name);
      if (base.empty() || !is_user_var(base) || type_names.contains(base)) {
        return;
      }
      auto fold = str_tools::ascii_fold(base);
      const auto &kept = first_spelling.try_emplace(fold, base).first->second;
      if (kept == base || warned.contains(fold)) {
        return; // identical spelling, or already reported this fold
      }
      warned.insert(fold);
      const auto msg =
          std::format("variables `{}` and `{}` differ only in letter case; "
                      "Pyrope is case sensitive, but this feels wrong",
                      kept, base);
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::warning,
          .code = "name-case-collision",
          .category = "name",
          .pass = "upass.ssa",
          .message = msg,
          .span = lnast->span_of_nearest(at),  // IO-seed anchor (io_nid) may lack a srcid; walk up to one
          .hint =
              "rename one of them if they were meant to be the same variable"});
    };

    // Seed with IO port base names: an input `Foo` and a local `foo` used to
    // alias.
    for (const auto &f : flat_inputs) {
      note(f.name, io_nid);
    }
    for (const auto &f : flat_outputs) {
      note(f.name, io_nid);
    }
    // Every assignment target in the body (the LHS is the first child of a
    // dest-carrying statement; reads on the RHS are not inspected). The span
    // anchor is the STATEMENT node — a `ref` LHS carries no srcid of its own.
    std::function<void(const Lnast_nid &)> scan_collisions =
        [&](const Lnast_nid &nid) {
          if (stmt_has_dest(lnast->get_type(nid))) {
            auto lhs = lnast->get_first_child(nid);
            if (!lhs.is_invalid() &&
                Lnast_ntype::is_ref(lnast->get_type(lhs))) {
              note(lnast->get_name(lhs), nid);
            }
          }
          for (auto c : lnast->children(nid)) {
            scan_collisions(c);
          }
        };
    scan_collisions(stmts_nid);
  }

  auto new_stmts = staging->add_child(new_root, Lnast_ntype::create_stmts());

  // SSA rename state (straight-line scope only; branches copied verbatim).
  // absl::flat_hash_map<std::string, …> supports heterogeneous string_view
  // lookup — the hot inner loop walks 1M+ refs and reads these via views,
  // never paying for a std::string temporary on the read path.
  absl::flat_hash_map<std::string, std::string>
      rename_map;                                  // base → current SSA name
  absl::flat_hash_map<std::string, int> ssa_count; // version counter
  absl::flat_hash_set<std::string> seen_lhs;       // first-seen tracker

  // Reg-declared names are NEVER SSA-versioned: every read of a reg
  // sees the flop's q (Verilog `<=` semantics) regardless of program order,
  // and every store is a next-state din write to the SAME flop. Versioning
  // them would split the din writes off the declared name (a dangling flop)
  // and rewrite q reads to the comb value.
  //
  // 2c-wire — a `wire` (single-driver combinational net) is treated the same:
  // its reads are position-independent (every read sees the one resolved
  // driver, even before it appears textually), so it must NOT be versioned
  // either — a read before the driver must stay on the base name, not bind to a
  // `nil` earlier version. tolg resolves the base name to the buffered net.
  absl::flat_hash_set<std::string> reg_names;
  absl::flat_hash_set<std::string>
      reg_only_names; // true `reg` (NOT `wire`) — for set_mask din threading
                      // below
  for (const auto &nid : lnast->depth_preorder(lnast->get_root())) {
    if (nid.is_invalid() || !Lnast_ntype::is_declare(lnast->get_type(nid))) {
      continue;
    }
    auto c0 = lnast->get_first_child(nid);
    if (c0.is_invalid()) {
      continue;
    }
    auto c1 = c0.is_invalid() ? c0 : lnast->get_sibling_next(c0);
    auto c2 = c1.is_invalid() ? c1 : lnast->get_sibling_next(c1);
    if (c2.is_invalid() || !Lnast_ntype::is_const(lnast->get_type(c2))) {
      continue;
    }
    auto mode = lnast->get_name(c2);
    const bool is_reg = (mode == "reg" || mode.starts_with("reg "));
    const bool is_wire = (mode == "wire" || mode.starts_with("wire "));
    if (is_reg || is_wire) {
      reg_names.emplace(lnast->get_name(c0));
    }
    if (is_reg) {
      reg_only_names.emplace(lnast->get_name(c0));
    }
  }

  // ── Reg partial-write (set_mask RMW) din threading ─────────────────────────
  // `reg x; x#[lo..=hi] = a; x#[lo2..=hi2] = b` lowers (in prp2lnast) to a
  // chain of read-modify-writes:
  //   set_mask(___x_0, x, mask0, a) ; store(x, ___x_0)
  //   set_mask(___x_1, x, mask1, b) ; store(x, ___x_1)
  // Because reg names are NEVER SSA-versioned (every read is the flop q — see
  // above), the SECOND set_mask re-reads q instead of the accumulating
  // next-state ___x_0, so only the LAST partial write survives and every
  // earlier one is silently dropped (a `mut` dodges this via SSA versioning).
  // Fix it by threading the chain by hand on the SOURCE tree (before the
  // staging rebuild, so the verbatim branch copy below inherits it): rewrite
  // each SUBSEQUENT set_mask's BASE operand (child 1) from the reg name to the
  // prior partial-write temp. The FIRST partial write keeps the reg name (= q,
  // the hold value for the bits it does not write); plain reads `r = x` are
  // left alone and still read q. Only true `reg`s participate — a `wire` is a
  // single-driver net handled differently. A branch body inherits a COPY of the
  // pre-branch din (so a conditional partial write still composes with the
  // straight-line ones), and after a branch the reg's next-state is a mux, so
  // its pending temp is dropped (subsequent set_masks fall back to q — the
  // prior behavior, no regression).
  if (!reg_only_names.empty()) {
    auto is_temp_ref = [&](const Lnast_nid &nid) {
      return Lnast_ntype::is_ref(lnast->get_type(nid)) &&
             !is_user_var(lnast->get_name(nid));
    };
    // Reg names a subtree (re)writes via a 2-child store, recursively.
    std::function<void(const Lnast_nid &, absl::flat_hash_set<std::string> &)>
        collect_reg_writes =
            [&](const Lnast_nid &sub, absl::flat_hash_set<std::string> &out) {
              for (auto c : lnast->children(sub)) {
                if (Lnast_ntype::is_store(lnast->get_type(c))) {
                  auto d0 = lnast->get_first_child(c);
                  if (!d0.is_invalid() &&
                      Lnast_ntype::is_ref(lnast->get_type(d0)) &&
                      reg_only_names.contains(lnast->get_name(d0))) {
                    out.emplace(lnast->get_name(d0));
                  }
                }
                collect_reg_writes(c, out);
              }
            };
    std::function<void(const Lnast_nid &,
                       absl::flat_hash_map<std::string, std::string> &)>
        thread_stmts =
            [&](const Lnast_nid &sblk,
                absl::flat_hash_map<std::string, std::string> &pending) {
              for (auto c : lnast->children(sblk)) {
                const auto ct = lnast->get_type(c);
                if (Lnast_ntype::is_set_mask(ct)) {
                  // children: result, base, mask, value — rewrite base when it
                  // reads a reg that already has an in-flight next-state temp
                  // this scope.
                  auto res = lnast->get_first_child(c);
                  auto base =
                      res.is_invalid() ? res : lnast->get_sibling_next(res);
                  if (!base.is_invalid() &&
                      Lnast_ntype::is_ref(lnast->get_type(base))) {
                    auto bn = lnast->get_name(base);
                    if (reg_only_names.contains(bn)) {
                      if (auto it = pending.find(bn); it != pending.end()) {
                        lnast->set_name(base, it->second);
                      }
                    }
                  }
                } else if (Lnast_ntype::is_store(ct)) {
                  // 2-child store to a reg whose value is a temp → record the
                  // din so the NEXT set_mask chains off it. Any other store
                  // shape (const / user-var value, a tuple_set) drops the chain
                  // (reads fall to q).
                  auto d0 = lnast->get_first_child(c);
                  auto d1 = d0.is_invalid() ? d0 : lnast->get_sibling_next(d0);
                  auto d2 = d1.is_invalid() ? d1 : lnast->get_sibling_next(d1);
                  if (!d0.is_invalid() && d2.is_invalid() &&
                      Lnast_ntype::is_ref(lnast->get_type(d0))) {
                    auto nm = lnast->get_name(d0);
                    if (reg_only_names.contains(nm)) {
                      if (!d1.is_invalid() && is_temp_ref(d1)) {
                        pending[std::string(nm)] =
                            std::string(lnast->get_name(d1));
                      } else {
                        pending.erase(nm);
                      }
                    }
                  }
                } else if (Lnast_ntype::is_if_like(ct) ||
                           Lnast_ntype::is_for(ct) ||
                           Lnast_ntype::is_while(ct)) {
                  absl::flat_hash_set<std::string> rw;
                  collect_reg_writes(c, rw);
                  for (auto sub : lnast->children(c)) {
                    if (Lnast_ntype::is_stmts(lnast->get_type(sub))) {
                      auto branch_pending =
                          pending; // copy: the pre-branch din flows in
                      thread_stmts(sub, branch_pending);
                    }
                  }
                  for (const auto &r : rw) {
                    pending.erase(r); // post-branch the next-state is a mux,
                                      // not a single temp
                  }
                } else if (Lnast_ntype::is_stmts(ct)) {
                  thread_stmts(
                      c,
                      pending); // bare `{ }` block: unconditional continuation
                } else if (Lnast_ntype::is_func_def(ct)) {
                  for (auto sub : lnast->children(c)) {
                    if (Lnast_ntype::is_stmts(lnast->get_type(sub))) {
                      absl::flat_hash_map<std::string, std::string>
                          fresh; // separate name scope
                      thread_stmts(sub, fresh);
                    }
                  }
                }
              }
            };
    absl::flat_hash_map<std::string, std::string> top_pending;
    thread_stmts(stmts_nid, top_pending);
  }

  // Collect the BASE names that a branch subtree (re)writes — the scalar/wire
  // 2-child store/dp_assign dests, recursively (nested ifs included), skipping
  // regs and tuple-set stores. Used for the branch carry-in below.
  std::function<void(const Lnast_nid &, absl::flat_hash_set<std::string> &)>
      collect_branch_writes =
          [&](const Lnast_nid &sub, absl::flat_hash_set<std::string> &out) {
            for (auto c : lnast->children(sub)) {
              auto ct = lnast->get_type(c);
              if (stmt_has_dest(ct) && !Lnast_ntype::is_declare(ct)) {
                size_t nchild = 0;
                for (auto x : lnast->children(c)) {
                  (void)x;
                  ++nchild;
                }
                bool scalar_store = !Lnast_ntype::is_store(ct) || nchild <= 2;
                if (scalar_store) {
                  auto d0 = lnast->get_first_child(c);
                  if (!d0.is_invalid()) {
                    auto nm = lnast->get_name(d0);
                    if (is_user_var(nm) && !reg_names.contains(nm)) {
                      out.emplace(nm);
                    }
                  }
                }
              }
              collect_branch_writes(c, out); // recurse (nested stmts/ifs)
            }
          };

  // ── Tuple-typed PORT field-access flattening ───────────────────────────────
  // The io DECLARATION was just flattened into dotted leaves (`ar.x`, `p.q`)
  // above, but the body still talks to the WHOLE tuple port via `tuple_get`
  // (field read) and a ≥3-child field `store` (field write). Those have no
  // driver/sink once the port only exists as leaves. Rewrite them to the dotted
  // leaf names so a `comb/pipe/mod f(ar:(x,y)) -> (p:(q,r))` lowers EXACTLY
  // like its hand-flattened `f(ar.x, ar.y) -> (p.q, p.r)` twin — leaving no
  // `tuple_*` node that references a tuple port for tolg. Mirrors detuple's
  // struct reg/mem leaf rewrite (upass_detuple.cpp), but keyed on the io leaf
  // set.
  absl::flat_hash_set<std::string>
      port_in_leaf; // full input leaf names ("ar.x")
  absl::flat_hash_set<std::string>
      port_out_leaf; // full output leaf names ("p.q")
  absl::flat_hash_set<std::string>
      port_prefix; // every proper prefix ("ar"; "p","p.q" for "p.q.a")
  auto register_port_leaf = [&](const std::string &nm, bool is_in) {
    if (nm.find('.') == std::string::npos) {
      return; // scalar port — already a plain leaf, nothing to flatten
    }
    (is_in ? port_in_leaf : port_out_leaf).insert(nm);
    for (auto pos = nm.find('.'); pos != std::string::npos;
         pos = nm.find('.', pos + 1)) {
      port_prefix.insert(nm.substr(0, pos));
    }
  };
  for (const auto &f : flat_inputs) {
    register_port_leaf(f.name, /*is_in=*/true);
  }
  for (const auto &f : flat_outputs) {
    register_port_leaf(f.name, /*is_in=*/false);
  }
  const bool has_tuple_ports = !port_prefix.empty();

  // A temp that aliases a (possibly interior) tuple-port path. A nested read
  // `ar.x.a` lowers to a chain `t1 = ar['x']` (interior) ; `t2 = t1['a']`
  // (leaf): the first records t1 -> "ar.x" and emits nothing; the second
  // resolves through it to the leaf "ar.x.a".
  absl::flat_hash_map<std::string, std::string> tget_alias;

  // A whole-tuple write to a tuple OUTPUT port driven CONDITIONALLY
  // (`p = if c { (first=1,..) } else { (first=3,..) }`) lowers as: each arm
  // writes a whole-tuple temp (`___p0 = ___pN`), then a post-if
  // `store(p,___p0)`. A whole-tuple copy carries no per-field driver, so the
  // arm writes cannot be muxed per leaf. Pre-scan maps the post-if
  // whole-store's source temp -> its port prefix; the in-arm whole-tuple copy
  // is then distributed per output leaf
  // (`p.first = ___pN.first`, ...) so tolg muxes each leaf - IDENTICAL to
  // writing the fields conditionally by hand. `port_split_armed` records which
  // ports got arm-driven so the now-redundant post-if whole-store is dropped.
  absl::flat_hash_map<std::string, std::string>
      whole_port_split; // src temp -> port prefix
  absl::flat_hash_set<std::string>
      port_split_armed; // ports driven by arm distribution
  int arm_split_tmp = 0;

  // base/interior name -> its tuple-port dotted path (if it is one).
  auto resolve_port_path =
      [&](std::string_view name) -> std::optional<std::string> {
    if (port_prefix.contains(name)) {
      return std::string(name);
    }
    if (auto it = tget_alias.find(name); it != tget_alias.end()) {
      return it->second;
    }
    return std::nullopt;
  };

  // SSA-version a scalar LHS name (same rule as the has_dest LHS path below).
  // Fills pending_* for the deferred rename_map update (applied AFTER the RHS).
  auto version_lhs = [&](std::string_view lhs_view, std::string &pending_lhs,
                         std::string &pending_ssa) -> std::string {
    std::string out_name;
    if (is_user_var(lhs_view) && !reg_names.contains(lhs_view)) {
      if (auto seen_it = seen_lhs.find(lhs_view); seen_it != seen_lhs.end()) {
        int n = ssa_count[*seen_it] + 1;
        out_name.assign(lhs_view);
        out_name.append("___ssa_");
        out_name.append(std::to_string(n));
        pending_lhs.assign(lhs_view);
        pending_ssa = out_name;
      } else {
        std::string lhs_str{lhs_view};
        seen_lhs.insert(lhs_str);
        rename_map.try_emplace(lhs_str, lhs_str);
        out_name = std::move(lhs_str);
      }
    } else {
      out_name.assign(lhs_view);
    }
    return out_name;
  };

  // Rewrite `tuple_get(dst, port, 'f'[, 'g'...])` (a tuple-port field READ)
  // into a scalar `store(dst, ref 'port.f.g')`. An interior (non-leaf) read
  // records an alias and emits nothing (a deeper tuple_get resolves through
  // it).
  auto try_rewrite_port_tuple_get = [&](const Lnast_nid &child) -> bool {
    std::vector<Lnast_nid> kids;
    for (auto c : lnast->children(child)) {
      kids.push_back(c);
    }
    if (kids.size() < 3 || !Lnast_ntype::is_ref(lnast->get_type(kids[0])) ||
        !Lnast_ntype::is_ref(lnast->get_type(kids[1]))) {
      return false;
    }
    auto base = resolve_port_path(lnast->get_name(kids[1]));
    if (!base) {
      return false;
    }
    std::string path = *base;
    for (size_t i = 2; i < kids.size(); ++i) {
      if (!Lnast_ntype::is_const(lnast->get_type(kids[i]))) {
        return false; // runtime index — not a static tuple-port field access
      }
      path.push_back('.');
      path.append(lnast->get_name(kids[i]));
    }
    auto dst = std::string(lnast->get_name(kids[0]));
    if (port_in_leaf.contains(path) || port_out_leaf.contains(path)) {
      auto st = staging->add_child(new_stmts, Lnast_ntype::create_store());
      if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
        staging->set_srcid(st, lnast->get_srcid(child));
      }
      staging->add_child(st, Lnast_node::create_ref(dst));
      staging->add_child(st, Lnast_node::create_ref(path));
      tget_alias.insert_or_assign(std::move(dst), std::move(path));
      return true;
    }
    if (port_prefix.contains(path)) {
      tget_alias.insert_or_assign(
          std::move(dst),
          std::move(path)); // interior — defer to the deeper read
      return true;
    }
    return false;
  };

  // Rewrite a field `store(port, 'f'[, 'g'...], value)` (a tuple-port field
  // WRITE) into a scalar leaf `store(port.f.g, value)` (SSA-versioned LHS).
  auto try_rewrite_port_store = [&](const Lnast_nid &child) -> bool {
    std::vector<Lnast_nid> kids;
    for (auto c : lnast->children(child)) {
      kids.push_back(c);
    }
    if (kids.size() < 3 || !Lnast_ntype::is_ref(lnast->get_type(kids[0]))) {
      return false; // 2-child scalar store (or odd shape) — normal path
    }
    auto base = resolve_port_path(lnast->get_name(kids[0]));
    if (!base) {
      return false;
    }
    std::string path = *base;
    for (size_t i = 1; i + 1 < kids.size(); ++i) {
      if (!Lnast_ntype::is_const(lnast->get_type(kids[i]))) {
        return false;
      }
      path.push_back('.');
      path.append(lnast->get_name(kids[i]));
    }
    if (!port_out_leaf.contains(path)) {
      return false; // not a known tuple-output leaf
    }
    std::string pending_lhs;
    std::string pending_ssa;
    auto out_name = version_lhs(path, pending_lhs, pending_ssa);
    auto st = staging->add_child(new_stmts, Lnast_ntype::create_store());
    if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
      staging->set_srcid(st, lnast->get_srcid(child));
    }
    staging->add_child(st, Lnast_node::create_ref(out_name));
    copy_with_rename(lnast, kids.back(), staging, st, rename_map);
    if (!pending_lhs.empty()) {
      ++ssa_count[pending_lhs];
      rename_map[pending_lhs] = pending_ssa;
    }
    return true;
  };

  // Split a WHOLE-tuple write to a tuple-typed OUTPUT port — a 2-child
  // `store(p, src)` whose dest `p` is an output prefix — into one scalar store
  // per flattened output leaf: `p.first = src.first`, `p.second = src.second`,
  // … The per-leaf value is pulled from `src` with a `tuple_get` (`src` is a
  // normal tuple temp/var, e.g. a `(first=1, second=2)` literal built by
  // `tuple_add`); if `src` is itself a tuple INPUT port the leaves are bound
  // leaf-to-leaf. Without this split tolg records a driver for `p`/`src` but
  // none for the flattened leaf names, so the output finalize loop reports
  // `p.first` undriven.
  int whole_store_tmp = 0;
  auto try_rewrite_port_whole_store = [&](const Lnast_nid &child) -> bool {
    std::vector<Lnast_nid> kids;
    for (auto c : lnast->children(child)) {
      kids.push_back(c);
    }
    if (kids.size() != 2 || !Lnast_ntype::is_ref(lnast->get_type(kids[0])) ||
        !Lnast_ntype::is_ref(lnast->get_type(kids[1]))) {
      return false; // not a plain 2-child `store(dst, src)`
    }
    std::string prefix(lnast->get_name(kids[0]));
    if (port_split_armed.contains(prefix)) {
      return true; // a conditional whole-tuple write already drove the leaves
                   // per arm
    }
    if (!port_prefix.contains(prefix)) {
      return false; // dest is not a tuple-port prefix — normal scalar store
    }
    const std::string dot_prefix = prefix + ".";
    // src may need a rename (a reassigned comb net); also resolve whether src
    // is itself a tuple input port (then the leaves are read directly, not via
    // get).
    std::string src_name(lnast->get_name(kids[1]));
    if (auto it = rename_map.find(src_name); it != rename_map.end()) {
      src_name = it->second;
    }
    auto src_port = resolve_port_path(lnast->get_name(kids[1]));
    bool any = false;
    for (const auto &f : flat_outputs) {
      if (f.name.size() <= dot_prefix.size() ||
          f.name.compare(0, dot_prefix.size(), dot_prefix) != 0) {
        continue; // not a leaf under this prefix
      }
      any = true;
      const std::string sub =
          f.name.substr(dot_prefix.size()); // "first" | "s.n"
      std::string value_ref;                // the per-leaf RHS ref name
      if (src_port) {
        value_ref =
            *src_port + "." + sub; // leaf-to-leaf from a tuple input port
      } else {
        // One multi-field `tuple_get(tmp, src, 's', 'n', …)` per leaf —
        // constprop resolves a multi-segment path in a single node (a chained
        // `t1 = src['s'] ; t2 = t1['n']` left the intermediate sub-tuple temp
        // unresolved for a runtime field).
        std::string tmp = absl::StrCat("%ws_", whole_store_tmp++);
        auto tg =
            staging->add_child(new_stmts, Lnast_ntype::create_tuple_get());
        if (Lnast::srcid_carries(Lnast_ntype::create_tuple_get())) {
          staging->set_srcid(tg, lnast->get_srcid(child));
        }
        staging->add_child(tg, Lnast_node::create_ref(tmp));
        staging->add_child(tg, Lnast_node::create_ref(src_name));
        for (size_t pos = 0; pos != std::string::npos;) {
          size_t dot = sub.find('.', pos);
          std::string field = (dot == std::string::npos)
                                  ? sub.substr(pos)
                                  : sub.substr(pos, dot - pos);
          staging->add_child(tg, Lnast_node::create_const(field));
          pos = (dot == std::string::npos) ? std::string::npos : dot + 1;
        }
        value_ref = std::move(tmp);
      }
      std::string pending_lhs;
      std::string pending_ssa;
      auto out_name = version_lhs(f.name, pending_lhs, pending_ssa);
      auto st = staging->add_child(new_stmts, Lnast_ntype::create_store());
      if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
        staging->set_srcid(st, lnast->get_srcid(child));
      }
      staging->add_child(st, Lnast_node::create_ref(out_name));
      staging->add_child(st, Lnast_node::create_ref(value_ref));
      if (!pending_lhs.empty()) {
        ++ssa_count[pending_lhs];
        rename_map[pending_lhs] = pending_ssa;
      }
    }
    return any;
  };

  // Port-aware deep copy for a control-flow subtree (the arms of an `if`):
  // identical to copy_with_rename, but ALSO rewrites tuple-port field READS
  // nested inside the branch (`tuple_get(dst, ar, 'x')` → `store(dst, ref
  // 'ar.x')`) so a port read used inside an if-EXPRESSION (`res = if c { ar.x }
  // …`) lowers to the flattened leaf, exactly like the same read at the top
  // level. Interior reads (`ar.x.a` chains) record an alias and emit nothing,
  // resolved by the deeper read — matching the top-level
  // try_rewrite_port_tuple_get.
  std::function<void(const Lnast_nid &, const Lnast_nid &)>
      copy_branch_port_aware = [&](const Lnast_nid &src_nid,
                                   const Lnast_nid &dst_parent) {
        auto type = lnast->get_type(src_nid);
        if (Lnast_ntype::is_tuple_get(type)) {
          std::vector<Lnast_nid> kids;
          for (auto c : lnast->children(src_nid)) {
            kids.push_back(c);
          }
          if (kids.size() >= 3 &&
              Lnast_ntype::is_ref(lnast->get_type(kids[0])) &&
              Lnast_ntype::is_ref(lnast->get_type(kids[1]))) {
            if (auto base = resolve_port_path(lnast->get_name(kids[1]))) {
              std::string path = *base;
              bool ok = true;
              for (size_t i = 2; i < kids.size(); ++i) {
                if (!Lnast_ntype::is_const(lnast->get_type(kids[i]))) {
                  ok = false; // runtime index — not a static field access
                  break;
                }
                path.push_back('.');
                path.append(lnast->get_name(kids[i]));
              }
              if (ok) {
                auto dst = std::string(lnast->get_name(kids[0]));
                if (port_in_leaf.contains(path) ||
                    port_out_leaf.contains(path)) {
                  auto st = staging->add_child(dst_parent,
                                               Lnast_ntype::create_store());
                  if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
                    staging->set_srcid(st, lnast->get_srcid(src_nid));
                  }
                  staging->add_child(st, Lnast_node::create_ref(dst));
                  staging->add_child(st, Lnast_node::create_ref(path));
                  tget_alias.insert_or_assign(std::move(dst), std::move(path));
                  return;
                }
                if (port_prefix.contains(path)) {
                  tget_alias.insert_or_assign(std::move(dst),
                                              std::move(path)); // interior
                  return;
                }
              }
            }
          }
          // fall through: a non-port tuple_get is copied generically
        }
        if (Lnast_ntype::is_store(type)) {
          // A WHOLE-tuple copy to a temp that feeds a post-if whole-store to a
          // tuple-output port (`___p0 = ___pN`, with a later `store(p,___p0)`):
          // distribute it into one per-leaf write `store(p.first,
          // ___pN.first)`,
          // ... so each output leaf is driven in BOTH arms and tolg muxes it.
          // This is exactly the hand-written conditional per-field form. The
          // post-if whole-store is then dropped (port_split_armed, below).
          {
            std::vector<Lnast_nid> kids;
            for (auto c : lnast->children(src_nid)) {
              kids.push_back(c);
            }
            if (kids.size() == 2 &&
                Lnast_ntype::is_ref(lnast->get_type(kids[0])) &&
                Lnast_ntype::is_ref(lnast->get_type(kids[1]))) {
              if (auto pit = whole_port_split.find(
                      std::string(lnast->get_name(kids[0])));
                  pit != whole_port_split.end()) {
                const std::string &port = pit->second;
                const std::string dot_port = port + ".";
                std::string src_name(lnast->get_name(kids[1]));
                if (auto rit = rename_map.find(src_name);
                    rit != rename_map.end()) {
                  src_name = rit->second;
                }
                auto src_port = resolve_port_path(lnast->get_name(kids[1]));
                bool any = false;
                for (const auto &f : flat_outputs) {
                  if (f.name.size() <= dot_port.size() ||
                      f.name.compare(0, dot_port.size(), dot_port) != 0) {
                    continue; // not a leaf under this port
                  }
                  any = true;
                  const std::string sub =
                      f.name.substr(dot_port.size()); // "first" | "lo.hi"
                  std::string value_ref;              // the per-leaf RHS ref name
                  if (src_port) {
                    value_ref =
                        *src_port + "." + sub; // leaf-to-leaf from a tuple input port
                  } else {
                    std::string tmp = absl::StrCat("%aws_", arm_split_tmp++);
                    auto tg = staging->add_child(dst_parent,
                                                 Lnast_ntype::create_tuple_get());
                    if (Lnast::srcid_carries(Lnast_ntype::create_tuple_get())) {
                      staging->set_srcid(tg, lnast->get_srcid(src_nid));
                    }
                    staging->add_child(tg, Lnast_node::create_ref(tmp));
                    staging->add_child(tg, Lnast_node::create_ref(src_name));
                    for (size_t pos = 0; pos != std::string::npos;) {
                      size_t dot = sub.find('.', pos);
                      std::string field = (dot == std::string::npos)
                                              ? sub.substr(pos)
                                              : sub.substr(pos, dot - pos);
                      staging->add_child(tg, Lnast_node::create_const(field));
                      pos = (dot == std::string::npos) ? std::string::npos
                                                       : dot + 1;
                    }
                    value_ref = std::move(tmp);
                  }
                  auto st = staging->add_child(dst_parent,
                                               Lnast_ntype::create_store());
                  if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
                    staging->set_srcid(st, lnast->get_srcid(src_nid));
                  }
                  staging->add_child(
                      st, Lnast_node::create_ref(
                              f.name)); // BASE leaf name (branch write)
                  staging->add_child(st, Lnast_node::create_ref(value_ref));
                }
                if (any) {
                  port_split_armed.insert(port);
                  return;
                }
              }
            }
          }
          // A field WRITE to a tuple-output leaf nested in an if-arm
          // (`store(p,'first',v)`) → a 2-child `store(p.first, v)` on the BASE
          // leaf name. tolg's lower_branch merges the same base-name writes
          // from both arms into the output Mux, so (unlike a top-level field
          // write) this is NOT SSA-versioned — it stays on `p.first` exactly
          // like any other in-branch comb write.
          std::vector<Lnast_nid> kids;
          for (auto c : lnast->children(src_nid)) {
            kids.push_back(c);
          }
          if (kids.size() >= 3 &&
              Lnast_ntype::is_ref(lnast->get_type(kids[0]))) {
            if (auto base = resolve_port_path(lnast->get_name(kids[0]))) {
              std::string path = *base;
              bool ok = true;
              for (size_t i = 1; i + 1 < kids.size(); ++i) {
                if (!Lnast_ntype::is_const(lnast->get_type(kids[i]))) {
                  ok = false;
                  break;
                }
                path.push_back('.');
                path.append(lnast->get_name(kids[i]));
              }
              if (ok && port_out_leaf.contains(path)) {
                auto st =
                    staging->add_child(dst_parent, Lnast_ntype::create_store());
                if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
                  staging->set_srcid(st, lnast->get_srcid(src_nid));
                }
                staging->add_child(st, Lnast_node::create_ref(path));
                copy_branch_port_aware(kids.back(),
                                       st); // value: rename + nested port reads
                return;
              }
            }
          }
          // fall through: a non-port store is copied generically
        }
        Lnast_nid new_nid;
        if (Lnast_ntype::is_ref(type)) {
          std::string_view name = lnast->get_name(src_nid);
          auto it = rename_map.find(name);
          new_nid = staging->add_child(
              dst_parent,
              Lnast_node::create_ref(it != rename_map.end()
                                         ? std::string_view{it->second}
                                         : name));
        } else if (Lnast_ntype::is_const(type)) {
          new_nid = staging->add_child(
              dst_parent, Lnast_node::create_const(lnast->get_name(src_nid)));
        } else if (Lnast_ntype::is_invalid(type)) {
          new_nid =
              staging->add_child(dst_parent, Lnast_node::create_invalid());
        } else {
          new_nid = staging->add_child(dst_parent, type);
        }
        if (Lnast::srcid_carries(type)) {
          staging->set_srcid(new_nid, lnast->get_srcid(src_nid));
        }
        for (auto c : lnast->children(src_nid)) {
          // Named tuple-literal field key under a tuple_add: a structural
          // label, never a variable read — keep it verbatim (see
          // copy_with_rename); only the value follows the branch-aware copy.
          if (Lnast_ntype::is_tuple_add(type) &&
              Lnast_ntype::is_store(lnast->get_type(c))) {
            auto st =
                staging->add_child(new_nid, Lnast_ntype::create_store());
            if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
              staging->set_srcid(st, lnast->get_srcid(c));
            }
            bool first_field_child = true;
            for (auto c2 : lnast->children(c)) {
              if (first_field_child) {
                copy_subtree(lnast, c2, staging, st); // field key — verbatim
                first_field_child = false;
              } else {
                copy_branch_port_aware(c2, st);
              }
            }
            continue;
          }
          copy_branch_port_aware(c, new_nid);
        }
      };

  // Per-statement processing of the straight-line body.  Defined as a recursive
  // helper so a bare `{ }` lexical block (an unconditional nested `stmts`) can
  // be INLINED into the current scope — its reads then follow the live
  // rename_map and its writes version + carry out, instead of being copied
  // verbatim (which disconnected the block's outer-variable writes from the SSA
  // chain and dropped them after the block).
  std::function<void(const Lnast_nid &)> process_child = [&](const Lnast_nid
                                                                 &child) {
    auto type = lnast->get_type(child);

    // Tuple-typed PORT field accesses are flattened to their dotted leaf names
    // (see the helpers above) so they lower like the hand-flattened twin.
    if (has_tuple_ports) {
      if (Lnast_ntype::is_tuple_get(type) &&
          try_rewrite_port_tuple_get(child)) {
        return;
      }
      if (Lnast_ntype::is_store(type) && try_rewrite_port_store(child)) {
        return;
      }
      if (Lnast_ntype::is_store(type) && try_rewrite_port_whole_store(child)) {
        return;
      }
    }

    // A `store` with ≥3 children is the tuple_set form (an in-place
    // field write `t.f = v`), which must NOT be SSA-versioned: both `t.x = …`
    // and `t.y = …` mutate the same bundle version. Only a 2-child store (the
    // scalar/wire write) versions like `assign`. (tuple_set itself is excluded
    // by stmt_has_dest's enum range; store falls inside it, hence this guard.)
    bool has_dest = stmt_has_dest(type);
    if (has_dest && Lnast_ntype::is_store(type)) {
      size_t nchild = 0;
      for (auto sub : lnast->children(child)) {
        (void)sub;
        if (++nchild > 2) {
          break;
        }
      }
      has_dest = (nchild <= 2);
    }
    // A `declare(var, type, mode)` introduces a name + type but is
    // NOT a value write (the value is a separate `store`). It must not be
    // SSA-versioned, and must not mark the var as "seen" — otherwise the
    // first value store becomes a spurious reassignment (var → var___ssa_1),
    // diverging the declaration from its value. (declare falls inside
    // stmt_has_dest's enum range, hence this guard.) Copy it verbatim.
    if (has_dest && Lnast_ntype::is_declare(type)) {
      has_dest = false;
    }

    // ── Statements with a write dest as first child: apply SSA renaming ────
    if (has_dest) {
      auto stmt_node = staging->add_child(new_stmts, type);
      // Carry the statement's SourceId across the SSA rebuild.
      if (Lnast::srcid_carries(type)) {
        staging->set_srcid(stmt_node, lnast->get_srcid(child));
      }

      // Defer rename_map update until after RHS is fully copied so that a
      // self-referencing assignment like `t = t + 1` (second assignment)
      // reads the *previous* SSA version of `t` on the RHS, not the new one.
      std::string pending_lhs; // empty = no deferred update
      std::string pending_ssa;

      bool first = true;
      for (auto sub : lnast->children(child)) {
        if (first) {
          // ── LHS ────────────────────────────────────────────────────────
          // Read via string_view; only materialize std::string when we have
          // to insert into the rename/ssa maps.
          std::string_view lhs_view = lnast->get_name(sub);
          std::string out_name;

          if (is_user_var(lhs_view) && !reg_names.contains(lhs_view)) {
            if (auto seen_it = seen_lhs.find(lhs_view);
                seen_it != seen_lhs.end()) {
              // Preview next SSA version — do NOT touch rename_map yet.
              int n = ssa_count[*seen_it] + 1;
              out_name.reserve(lhs_view.size() + 6 + 6);
              out_name.assign(lhs_view);
              out_name.append("___ssa_");
              out_name.append(std::to_string(n));
              pending_lhs.assign(lhs_view);
              pending_ssa = out_name;
            } else {
              std::string lhs_str{lhs_view};
              seen_lhs.insert(lhs_str);
              // Initialise identity mapping so RHS reads work correctly
              // even before a rename is needed.
              rename_map.try_emplace(lhs_str, lhs_str);
              out_name = std::move(lhs_str);
            }
          } else {
            out_name.assign(lhs_view);
          }
          staging->add_child(stmt_node, Lnast_node::create_ref(out_name));
          first = false;
        } else {
          // ── RHS: rename_map still holds OLD mapping — correct. ──────────
          copy_with_rename(lnast, sub, staging, stmt_node, rename_map);
        }
      }

      // Apply deferred update: subsequent statements now see the new SSA name.
      if (!pending_lhs.empty()) {
        ++ssa_count[pending_lhs];
        rename_map[pending_lhs] = pending_ssa;
      }
    } else if (Lnast_ntype::is_func_call(type)) {
      // fcall(dst, callee, args...): dst and callee copy verbatim, but the
      // ARGUMENT reads must follow the rename map — an instance fed by a
      // reassigned wire reads its latest version, not the first one. A named
      // arg `store(param, value)` renames only the value (never the formal
      // parameter name).
      auto stmt_node = staging->add_child(new_stmts, type);
      if (Lnast::srcid_carries(type)) {
        staging->set_srcid(stmt_node, lnast->get_srcid(child));
      }
      int pos = 0;
      for (auto sub : lnast->children(child)) {
        if (pos < 2) {
          copy_subtree(lnast, sub, staging, stmt_node); // dst tmp + callee name
        } else if (Lnast_ntype::is_store(lnast->get_type(sub))) {
          auto arg_node =
              staging->add_child(stmt_node, Lnast_ntype::create_store());
          if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
            staging->set_srcid(arg_node, lnast->get_srcid(sub));
          }
          bool first_arg_child = true;
          for (auto a2 : lnast->children(sub)) {
            if (first_arg_child) {
              copy_subtree(lnast, a2, staging, arg_node); // formal param name
              first_arg_child = false;
            } else {
              copy_with_rename(lnast, a2, staging, arg_node, rename_map);
            }
          }
        } else {
          copy_with_rename(lnast, sub, staging, stmt_node,
                           rename_map); // positional arg
        }
        ++pos;
      }
    } else if (Lnast_ntype::is_tuple_get(type) ||
               Lnast_ntype::is_tuple_add(type) ||
               Lnast_ntype::is_attr_get(type) ||
               Lnast_ntype::is_attr_set(type)) {
      // dst/tuple_get/tuple_add/attr_get/attr_set: the first child is the
      // target (a temp/user var being read, or — for attr_set — the entity
      // being attributed) copied VERBATIM (these aren't SSA-versioned, as
      // before), but the remaining children are READS that must follow the live
      // rename_map — e.g. a `tuple_get` memory-read index, or a `tuple_add`
      // field, that reads an SSA-versioned comb net (a wire reassigned after
      // its declaration-time poison) must resolve to the live driver, not the
      // stale base. (mem_multidim_rw: `data[raddr]` read the poison `raddr`
      // base instead of `raddr___ssa_1 = {ri,rj}`.) For `attr_set` the value
      // operand is likewise a read: a `clock_pin`/`reset_pin` driven by an
      // INTERNAL wire (a gated/derived/buffered clock — `attr_set q clock_pin
      // gclk`) must resolve to the wire's live SSA driver (`gclk___ssa_1 =
      // clk`), not the declaration-time `gclk = 0ub?` poison — otherwise the
      // flop clock binds to an invalid/unknown const.
      auto stmt_node = staging->add_child(new_stmts, type);
      if (Lnast::srcid_carries(type)) {
        staging->set_srcid(stmt_node, lnast->get_srcid(child));
      }
      bool first = true;
      for (auto sub : lnast->children(child)) {
        if (first) {
          copy_subtree(lnast, sub, staging,
                       stmt_node); // write target — verbatim
          first = false;
        } else if (Lnast_ntype::is_tuple_add(type) &&
                   Lnast_ntype::is_store(lnast->get_type(sub))) {
          // Named tuple-literal field `store(ref key, val)`: the key is a
          // structural FIELD LABEL (never a variable read) — renaming it when
          // a same-named var is live (`data` field next to a `data` local)
          // corrupts the bundle field name. Key verbatim; value renamed.
          // (Mirrors the func_call named-arg formal handling above.)
          auto arg_node =
              staging->add_child(stmt_node, Lnast_ntype::create_store());
          if (Lnast::srcid_carries(Lnast_ntype::create_store())) {
            staging->set_srcid(arg_node, lnast->get_srcid(sub));
          }
          bool first_field_child = true;
          for (auto a2 : lnast->children(sub)) {
            if (first_field_child) {
              copy_subtree(lnast, a2, staging, arg_node); // field key
              first_field_child = false;
            } else {
              copy_with_rename(lnast, a2, staging, arg_node, rename_map);
            }
          }
        } else {
          copy_with_rename(lnast, sub, staging, stmt_node, rename_map); // reads
        }
      }
    } else {
      // Control-flow / structural nodes (if, func_def, always_ff body, …).
      // The lowerer's lower_branch() + Mux logic merges base-name WRITES, so a
      // branch body must keep its write targets on the BASE name. But its READS
      // must follow the live rename_map: another scope (always_ff, nested if)
      // that reads a comb net the straight-line scope already SSA-versioned
      // must see the versioned driver, not the net's declaration-time poison —
      // copying verbatim left those reads on the stale base (the bug that
      // dropped the FIFO `outDataReg <= IN_data` override: its guards
      // `empty`/`outputReady`/`doInsert` read the `0ub?` poison and folded to
      // constants). So copy WITH rename. Regs are never in rename_map (flop q
      // semantics) so reg writes stay base; for comb vars (re)written in this
      // branch we first do the carry-in below — bind the base name to its
      // current version and reset the rename to base — so those writes stay on
      // the base for the Mux merge while everything else reads the live
      // version.
      if (Lnast_ntype::is_if_like(type)) {
        absl::flat_hash_set<std::string> bwrites;
        collect_branch_writes(child, bwrites);
        for (const auto &var : bwrites) {
          auto it = rename_map.find(var);
          if (it != rename_map.end() && it->second != var) {
            auto st =
                staging->add_child(new_stmts, Lnast_ntype::create_store());
            staging->add_child(st, Lnast_node::create_ref(var));
            staging->add_child(st, Lnast_node::create_ref(it->second));
            it->second =
                var; // branch writes + post-branch reads use the merged base
          }
        }
        // Reads inside the branch follow the live rename_map (so a versioned
        // comb net read here resolves to its driver, not its poison base);
        // writes were reset to base above so the Mux merge still works; regs
        // aren't versioned. With tuple-typed ports, also rewrite tuple-port
        // field READS nested in the arms (a port read used inside an
        // if-EXPRESSION) to their flattened leaf — copy_with_rename alone would
        // leave the `tuple_get(ar,'x')` with no driver.
        if (has_tuple_ports) {
          copy_branch_port_aware(child, new_stmts);
        } else {
          copy_with_rename(lnast, child, staging, new_stmts, rename_map);
        }
      } else if (Lnast_ntype::is_stmts(type)) {
        // A bare `{ }` lexical block runs unconditionally: inline its
        // statements into the straight-line scope (same rename_map) so reads
        // see the live version and writes thread out.  (Copying verbatim left
        // the block's outer writes on the stale base name, disconnected from
        // the SSA chain — e.g. a `set_mask` bit-init/accumulate emitted inside
        // an unrolled-loop block was dropped, scrambling the result.)
        for (auto gc : lnast->children(child)) {
          process_child(gc);
        }
      } else {
        // Other structural nodes (func_def, …) own a separate name scope — the
        // outer rename_map must NOT leak in, so copy verbatim.
        copy_subtree(lnast, child, staging, new_stmts);
      }
    }
  };
  // Pre-scan: map a post-if whole-store's source temp to its tuple-OUTPUT-port
  // prefix (`store(p, ___p0)` -> ___p0 maps to "p"), so a conditional
  // whole-tuple write building ___p0 in if-arms can be distributed per output
  // leaf (see copy_branch_port_aware / port_split_armed). Only ports with
  // output leaves.
  if (has_tuple_ports) {
    for (auto child : lnast->children(stmts_nid)) {
      if (!Lnast_ntype::is_store(lnast->get_type(child))) {
        continue;
      }
      std::vector<Lnast_nid> kids;
      for (auto c : lnast->children(child)) {
        kids.push_back(c);
      }
      if (kids.size() != 2 || !Lnast_ntype::is_ref(lnast->get_type(kids[0])) ||
          !Lnast_ntype::is_ref(lnast->get_type(kids[1]))) {
        continue;
      }
      std::string_view dst_nm = lnast->get_name(kids[0]);
      if (!port_prefix.contains(dst_nm)) {
        continue;
      }
      const std::string dot_port = std::string(dst_nm) + ".";
      bool has_leaf = false;
      for (const auto &f : flat_outputs) {
        if (f.name.size() > dot_port.size() &&
            f.name.compare(0, dot_port.size(), dot_port) == 0) {
          has_leaf = true;
          break;
        }
      }
      if (has_leaf) {
        whole_port_split[std::string(lnast->get_name(kids[1]))] =
            std::string(dst_nm);
      }
    }
  }

  for (auto child : lnast->children(stmts_nid)) {
    process_child(child);
  }

  // Final-version write-back for io outputs: a top-level reassignment was
  // SSA-versioned (out -> out___ssa_N) but tolg exports the pin bound to the
  // BASE output name, so without a trailing rebind every write after the
  // first is dead and the first write wins. (Reg-mode outputs are exempt
  // from versioning above and need no rebind.)
  for (const auto &f : flat_outputs) {
    auto it = rename_map.find(f.name);
    if (it != rename_map.end() && it->second != f.name) {
      auto st = staging->add_child(new_stmts, Lnast_ntype::create_store());
      staging->add_child(st, Lnast_node::create_ref(f.name));
      staging->add_child(st, Lnast_node::create_ref(it->second));
    }
  }

  // Commit: replace the original LNAST body with the staging tree (and its
  // name pool — names are pool-relative int ids).
  lnast->replace_body(staging);
}
