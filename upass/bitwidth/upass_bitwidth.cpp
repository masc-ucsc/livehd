//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bitwidth.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "array_dim.hpp"
#include "decl_facts.hpp"
#include "diag.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "range_bits.hpp"

// ── Plugin registration ───────────────────────────────────────────────────────
static upass::uPass_plugin plugin_bitwidth("bitwidth", upass::uPass_wrapper<uPass_bitwidth>::get_upass, {"constprop"});

static constexpr std::string_view call_ref_arg_marker = "__ref_arg";

namespace {
std::string_view ssa_base_name(std::string_view name) {
  const auto pos = name.find("___ssa_");
  if (pos == std::string_view::npos) {
    return name;
  }
  return name.substr(0, pos);
}

int64_t storage_bits_for_env(const Lnast_range& env) {
  if (env.is_unbounded()) {
    return 64;
  }
  if (env.min >= 0) {
    return env.max == 0 ? 0 : static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(env.max)));
  }
  return env.get_sbits();
}

}  // namespace

// ── Constructor ──────────────────────────────────────────────────────────────

uPass_bitwidth::uPass_bitwidth(std::shared_ptr<upass::Lnast_manager>& _lm) : upass::uPass(_lm) {}

// ── Lnast_range ↔ bundle-Entry conversion ────────────────────────────────────

std::optional<int64_t> uPass_bitwidth::const_to_i64(const Dlop& v) {
  // Every value of the widest integers this pass ranges, u62 and s62 (wider
  // is "wide", see name_is_wide): 62 magnitude bits when non-negative (u62's
  // max 2^62-1), 62 signed bits when negative (s62's min). A plain 62-signed-
  // bit cut read back any u62 range with bit 61 set (and the `1 << 61` mask
  // that sets it) as unbounded, so a bit-set chain on a u62 failed its own
  // declared type. An s63's min stays out, so an s63 is still wide.
  if (v.is_invalid() || !v.is_integer() || v.has_unknowns()
      || (v.is_negative() ? v.get_signed_bits() > 62 : v.get_payload_bits() > 62)) {
    return std::nullopt;
  }
  // A u62 needs 63 signed bits, so `to_just_i64()` (<= 62 signed bits) would
  // assert on it; the checks above already bound the value to one int64 word.
  return v.base()[0];
}

Lnast_range uPass_bitwidth::range_from_entry(const Dlop& maxc, const Dlop& minc) {
  const auto mx = const_to_i64(maxc);
  const auto mn = const_to_i64(minc);
  if (!mx || !mn) {
    return Lnast_range::make_unbounded();
  }
  Lnast_range r;
  r.min       = *mn;
  r.max       = *mx;
  r.unbounded = false;
  return r;
}

Lnast_range uPass_bitwidth::range_of_operand(const upass::Operand& o) const {
  // CONST literals read their value off the throwaway bundle. REF operands
  // deliberately do NOT read the (constprop-folded) trivial: this pass does
  // not participate in const-folding — its ranges come from its OWN
  // derivation chain (Entry.bw_*), preserving the documented force/reinterpret
  // semantics (e.g. an unsigned slice stored to a signed var) that the value
  // chain would mis-flag as overflow.
  if (o.name.empty()) {
    if (o.pattern) {
      return Lnast_range::make_unbounded();  // bit-pattern literal: force semantics, no value range
    }
    if (auto v = o.bundle->scalar(); v) {
      if (auto i = const_to_i64(*v)) {
        return Lnast_range::constant(*i);
      }
      return Lnast_range::make_unbounded();  // non-integer / unknown-bits / too wide
    }
    return Lnast_range::make_unbounded();
  }
  // A comptime value (not a bit-pattern force, see force_names_) is exact: a
  // derived range that does not even contain it described an earlier binding
  // of the name (a `for v in ref t` pick rebinds `v` each iteration).
  std::optional<int64_t> value;
  if (runner_st != nullptr && !force_names_.contains(o.name)) {
    if (const auto v = runner_st->comptime_scalar(o.name)) {
      value = const_to_i64(*v);
    }
  }
  const auto current = [&](const Lnast_range& derived) {
    return !value || derived.contains(Lnast_range::constant(*value)) ? derived : Lnast_range::constant(*value);
  };
  // The bundle's derived range facts.
  const auto& e = o.bundle->get_entry(bundle_path::of_string("0"));
  auto        r = range_from_entry(e.bw_max, e.bw_min);
  if (!r.is_unbounded()) {
    return current(r);
  }
  // Cross-invocation persistence: a previous pass.upass run left ranges in
  // bw_meta (this walk's bundles start empty).
  r = read_range(o.name);
  if (!r.is_unbounded()) {
    return current(r);
  }
  // A copy of a Sub instance's output (`const q = inst.o`, a destructured
  // output): the port bounds it exactly as it bounds the field-read temp
  // (process_tuple_get). A runner scratch emit (a cast's value-preserving
  // bind) reads through here, where the unit's bw_meta is not the active one.
  if (runner_st != nullptr) {
    if (const auto origin = runner_st->tget_origin.find(std::string(o.name)); origin != runner_st->tget_origin.end()) {
      if (const auto* port = runner_st->sub_output_range(origin->second)) {
        return current(range_from_entry(port->second, port->first));
      }
    }
  }
  // A comptime value no derivation reached (including a generic tuple-field
  // bind) is its own range. Its declared envelope is only the fallback for
  // runtime values; widening a known constant here can invent an overflow.
  if (value) {
    return Lnast_range::constant(*value);
  }
  // Nothing derived (an input port, a register read, a write merged over an
  // uncertain if): a TYPED name still holds a value inside its declared type,
  // because every write into it is held to that type (check_declared_fit
  // rejects any write it cannot prove fits), so the type is a sound may-hold
  // range -- unless some write could not be judged (unchecked_typed). An
  // untyped name's envelope only rode in on some earlier value and bounds
  // nothing.
  if (auto env = declared_type_of(o.name);
      env && (runner_st == nullptr || !runner_st->unchecked_typed.contains(ssa_base_name(o.name)))) {
    return *env;
  }
  // A compiler temp has ONE definition, and the envelope its producer stamped
  // is structural: a bit-select's width, a cast's target type, the declared
  // type of the tuple field or array element it read. (A comptime temp is
  // judged by its value, which the runner's comptime check owns.)
  if (Lnast::is_tmp(o.name) && (runner_st == nullptr || !runner_st->comptime_scalar(o.name))) {
    if (auto env = decl_envelope_of(o.name)) {
      return *env;
    }
  }
  // A single-output Sub instance read through its handle (`s1 + s2`): the
  // output port bounds it.
  if (const auto* port = single_output_port(o.name)) {
    return range_from_entry(port->second, port->first);
  }
  return Lnast_range::make_unbounded();
}

const Symbol_table::Port_range* uPass_bitwidth::single_output_port(std::string_view handle) const {
  if (runner_st == nullptr) {
    return nullptr;
  }
  const auto it = runner_st->sub_output_ranges.find(handle);
  return it != runner_st->sub_output_ranges.end() && it->second.size() == 1 ? &it->second.begin()->second : nullptr;
}

uPass_bitwidth::Unbounded uPass_bitwidth::why_unbounded(const upass::Operand& o) const {
  if (o.pattern) {
    return Unbounded::force;  // a bit-pattern literal: its bits are the value
  }
  if (o.name.empty()) {
    const auto v = o.bundle ? o.bundle->scalar() : std::nullopt;
    return v && v->is_integer() && !v->has_unknowns() && !const_to_i64(*v) ? Unbounded::wide : Unbounded::unknown;
  }
  // A comptime value: wide by its own width. One that fits an i64 yet has no
  // derived range came from a bit pattern: the same force, and the runner's
  // comptime value check owns it.
  if (runner_st != nullptr) {
    if (const auto v = runner_st->comptime_scalar(o.name); v && v->is_integer() && !v->has_unknowns()) {
      return const_to_i64(*v) ? Unbounded::force : Unbounded::wide;
    }
  }
  return name_is_wide(o.name) ? Unbounded::wide : Unbounded::unknown;
}

int64_t uPass_bitwidth::declared_bits_of(const upass::Operand& o) const {
  if (o.name.empty()) {
    return 0;
  }
  const auto base = ssa_base_name(o.name);
  if (const auto* io = lm->unit_lnast()->io_meta().find(base); io != nullptr) {
    return io->kind == Io_kind::integer ? io->bits : 0;
  }
  if (runner_st == nullptr) {
    return 0;
  }
  const auto f = upass::decl_facts::lookup(*runner_st, lm->get_lnast().get(), base);
  return f && (f->kind == upass::decl_facts::Num::unsigned_int || f->kind == upass::decl_facts::Num::signed_int) ? f->bits : 0;
}

bool uPass_bitwidth::is_known_operand(const upass::Operand& o) const {
  return !range_of_operand(o).is_unbounded() || why_unbounded(o) == Unbounded::wide;
}

Lnast_range uPass_bitwidth::read_range(std::string_view name) const {
  if (name.empty()) {
    return Lnast_range::make_unbounded();
  }
  if (runner_st != nullptr) {
    if (auto b = runner_st->get_bundle(name); b) {
      const auto& e = b->get_entry(bundle_path::of_string("0"));
      auto        r = range_from_entry(e.bw_max, e.bw_min);
      if (!r.is_unbounded()) {
        return r;
      }
    }
  }
  const auto& meta = lm->get_lnast()->bw_meta();
  if (auto it = meta.ranges.find(std::string(name)); it != meta.ranges.end()) {
    Lnast_range r;
    r.min       = it->second.min;
    r.max       = it->second.max;
    r.unbounded = it->second.unbounded;
    return r;
  }
  return Lnast_range::make_unbounded();
}

// ── Writes ───────────────────────────────────────────────────────────────────

void uPass_bitwidth::write_bw(std::string_view name, Bundle& dst, Lnast_range r, bool replace, Unbounded why) {
  if (name.empty() || name.find('.') != std::string_view::npos) {
    return;  // scalar names only (per-field ranges are a follow-up)
  }
  // A write that does not fit is reported once; the destination then holds
  // what its declared storage can, so readers do not cascade the same
  // overflow into every later assignment.
  if (const auto env = check_declared_fit(name, r, why)) {
    r = *env;
  }
  note_pre_if_range(name);

  // Narrow-vs-replace against what the bundle already holds.
  const auto& e0  = dst.get_entry(bundle_path::of_string("0"));
  const auto  cur = range_from_entry(e0.bw_max, e0.bw_min);
  if (!replace && !cur.is_unbounded() && !r.is_narrower_than(cur)) {
    return;
  }
  if (cur.min == r.min && cur.max == r.max && cur.is_unbounded() == r.is_unbounded()) {
    // bw fields unchanged; still write through to bw_meta below on first sight.
  } else if (dst.is_empty() || dst.has_trivial(bundle_path::of_string("0"))) {
    Bundle::Entry e = dst.get_entry(bundle_path::of_string("0"));
    e.immutable     = false;
    if (r.is_unbounded()) {
      e.bw_max = Bundle::invalid_lconst;
      e.bw_min = Bundle::invalid_lconst;
    } else {
      e.bw_max = *Dlop::create_integer(r.max);
      e.bw_min = *Dlop::create_integer(r.min);
    }
    dst.set(bundle_path::of_string("0"), std::move(e));
  }

  // Feed the if-arm merge: this is what `name` is assigned on this arm's path
  // (the last committed write wins). Consumed by notify_if_merge_end.
  record_arm_write(name, r);

  // A bit-pattern force keeps its force semantics through copies.
  if (r.is_unbounded() && why == Unbounded::force) {
    force_names_.emplace(name);
  } else if (!force_names_.empty()) {
    force_names_.erase(std::string(name));
  }

  // Write-through to lnast->bw_meta() — the tolg/LSP interface, and the
  // cross-invocation persistence store (replaces the old end_run flush).
  publish_range(name, r, why == Unbounded::wide);
}

void uPass_bitwidth::publish_range(std::string_view name, const Lnast_range& r, bool wide) {
  // A value too WIDE for an i64 range stays an overflow risk for its readers
  // (this walk's operands, the runner's argument check): unlike a range
  // nothing derived, it may not fit any narrower typed destination.
  if (runner_st != nullptr) {
    if (wide) {
      runner_st->wide_values.emplace(name);
    } else {
      runner_st->wide_values.erase(std::string(name));
    }
  }
  auto&         meta = lm->get_lnast()->bw_meta();
  BitwidthEntry me;
  me.min                         = r.min;
  me.max                         = r.max;
  me.unbounded                   = r.is_unbounded();
  meta.ranges[std::string(name)] = me;
}

bool uPass_bitwidth::name_is_wide(std::string_view name) const {
  if (runner_st != nullptr && runner_st->wide_values.contains(name)) {
    return true;
  }
  if (const auto* port = single_output_port(name)) {
    return range_from_entry(port->second, port->first).is_unbounded();  // a >62-bit output port
  }
  // A declared integer wider than this pass's i64 ranges (a u64 port or local).
  const auto base = ssa_base_name(name);
  if (const auto* io = lm->unit_lnast()->io_meta().find(base); io != nullptr) {
    return io->kind == Io_kind::integer && (io->bits > 62 || (io->wide_range_min && io->wide_range_max));
  }
  if (runner_st == nullptr) {
    return false;
  }
  const auto f = upass::decl_facts::lookup(*runner_st, lm->get_lnast().get(), base);
  return f && (f->kind == upass::decl_facts::Num::unsigned_int || f->kind == upass::decl_facts::Num::signed_int) && f->bits > 62;
}

void uPass_bitwidth::clear_range(std::string_view name) {
  if (name.empty()) {
    return;
  }
  if (runner_st != nullptr) {
    if (auto b = runner_st->get_bundle_for_write(name); b && (b->is_empty() || b->has_trivial(bundle_path::of_string("0")))) {
      Bundle::Entry e = b->get_entry(bundle_path::of_string("0"));
      e.immutable     = false;
      e.bw_max        = Bundle::invalid_lconst;
      e.bw_min        = Bundle::invalid_lconst;
      b->set(bundle_path::of_string("0"), std::move(e));
    }
  }
  lm->get_lnast()->bw_meta().ranges.erase(std::string(name));
}

// ── If-arm value-range merge ─────────────────────────────────────────────────

void uPass_bitwidth::record_arm_write(std::string_view name, const Lnast_range& r) {
  if (arm_write_stack_.empty()) {
    return;  // not inside an uncertain if-arm
  }
  arm_write_stack_.back()[std::string(name)] = r;  // latest write in this arm wins
}

void uPass_bitwidth::note_pre_if_range(std::string_view name) {
  if (arm_write_stack_.empty()) {
    return;  // not inside an uncertain if-arm
  }
  // bw_meta still holds the last committed range from before the enclosing
  // if: write_bw writes through on every write, and this is the first write
  // of `name` that if has seen.
  std::optional<Lnast_range> before;
  for (auto& f : if_merge_stack_) {
    if (f.pre_if.contains(name)) {
      continue;
    }
    if (!before) {
      before           = Lnast_range::make_unbounded();
      const auto& meta = lm->get_lnast()->bw_meta();
      if (auto it = meta.ranges.find(std::string(name)); it != meta.ranges.end() && !it->second.unbounded) {
        before->min       = it->second.min;
        before->max       = it->second.max;
        before->unbounded = false;
      }
    }
    f.pre_if.emplace(std::string(name), *before);
    if (before->is_unbounded() && name_is_wide(name)) {
      f.pre_if_wide.emplace(name);
    }
  }
}

void uPass_bitwidth::commit_merged(std::string_view name, const Lnast_range& r, bool wide) {
  // The per-name wide flag is last-writer state: after the arms it holds only
  // the LAST arm's write, so the merge passes what every path left.
  set_range(name, r, r.is_unbounded() && wide);
  // If this if is nested inside another uncertain arm, the merged value is
  // `name`'s contribution to that outer arm's path.
  record_arm_write(name, r);
}

void uPass_bitwidth::set_range(std::string_view name, const Lnast_range& r, bool wide) {
  // Refresh the scalar "0" entry DIRECTLY: the arm's leave_scope invalidated
  // the trivial, so write_bw's `is_empty() || has_trivial` guard would skip
  // the bundle write and leave the stale narrow bw fields that range_of_operand
  // reads first (bw_meta is only the fallback).
  if (auto b = (runner_st != nullptr) ? runner_st->get_bundle_for_write(name) : nullptr; b) {
    Bundle::Entry e = b->get_entry(bundle_path::of_string("0"));
    e.immutable     = false;
    if (r.is_unbounded()) {
      e.bw_max = Bundle::invalid_lconst;
      e.bw_min = Bundle::invalid_lconst;
    } else {
      e.bw_max = *Dlop::create_integer(r.max);
      e.bw_min = *Dlop::create_integer(r.min);
    }
    b->set(bundle_path::of_string("0"), std::move(e));
  }
  // Write-through to bw_meta (LSP hover + tolg + cross-invocation persistence).
  publish_range(name, r, wide);
}

void uPass_bitwidth::notify_if_merge_begin() { if_merge_stack_.emplace_back(); }

void uPass_bitwidth::notify_uncertain_arm_begin() {
  if (if_merge_stack_.empty()) {
    return;  // an uncertain arm with no bracketing merge frame (shouldn't happen)
  }
  // A later arm starts from the values the if was entered with, never from
  // what an earlier arm wrote: where a name is not SSA-renamed per arm (a
  // loop body, the lifted body of a rolled loop) both arms share its binding.
  auto& f = if_merge_stack_.back();
  if (f.uncertain_arms > 0) {
    for (const auto& [var, pre] : f.pre_if) {
      set_range(var, pre, f.pre_if_wide.contains(var));
    }
  }
  arm_write_stack_.emplace_back();
  ++f.uncertain_arms;
}

void uPass_bitwidth::notify_uncertain_arm_end() {
  if (if_merge_stack_.empty() || arm_write_stack_.empty()) {
    return;
  }
  const auto arm = std::move(arm_write_stack_.back());
  arm_write_stack_.pop_back();
  auto& f = if_merge_stack_.back();
  for (const auto& [var, r] : arm) {
    const auto it    = f.arm_union.find(var);
    f.arm_union[var] = (it == f.arm_union.end()) ? r : it->second.join(r);  // join = [min,max] over paths
    ++f.arm_writes[var];
    if (r.is_unbounded() && name_is_wide(var)) {
      f.arm_wide.emplace(var);  // before a later arm's bounded write resets the flag
    }
  }
}

void uPass_bitwidth::notify_if_merge_end(bool all_paths_covered) {
  if (if_merge_stack_.empty()) {
    return;
  }
  const auto f = std::move(if_merge_stack_.back());
  if_merge_stack_.pop_back();
  for (const auto& [var, arm_union] : f.arm_union) {
    // The arms' union alone is sound ONLY when every runtime path assigns
    // `var`: a fully-covered if (real else, no comptime-decided arm) where
    // `var` was written in ALL arms. Otherwise `var` may keep its pre-if value
    // on some path, so the union also takes that value's range — unbounded
    // when it is unknown, which renders as the declared type envelope (or
    // `int`), never a stale narrow / spurious-constant range.
    const bool covered = all_paths_covered && f.arm_writes.at(var) == f.uncertain_arms;
    if (covered) {
      commit_merged(var, arm_union, f.arm_wide.contains(var));
      continue;
    }
    const auto it = f.pre_if.find(var);
    commit_merged(var,
                  it == f.pre_if.end() ? Lnast_range::make_unbounded() : arm_union.join(it->second),
                  f.arm_wide.contains(var) || f.pre_if_wide.contains(var));
  }
}

// ── Declared envelope + fit check (at the offending node) ───────────────────

std::optional<Lnast_range> uPass_bitwidth::port_envelope_of(std::string_view base, const Lnast_tree_io& ios) const {
  const auto* io = ios.find(base);
  if (io == nullptr || io->kind != Io_kind::integer || io->array_size > 0) {
    return std::nullopt;  // an array port's lanes are held to its element type (check_array_elem_fit)
  }
  // Prefer the EXACT declared int(min,max) when the port pins both bounds —
  // `int(0,99)` admits [0,99], not the [0,127] bits window (cat 1).
  if (io->has_range) {
    Lnast_range r;
    r.min       = io->range_min;
    r.max       = io->range_max;
    r.unbounded = false;
    return r;
  }
  if (io->bits <= 0 || io->bits > 62) {
    return std::nullopt;
  }
  if (io->is_signed) {
    return Lnast_range::sext_to(io->bits - 1);
  }
  Lnast_range r;
  r.min       = 0;
  r.max       = (int64_t{1} << io->bits) - 1;
  r.unbounded = false;
  return r;
}

std::optional<Lnast_range> uPass_bitwidth::decl_envelope_of(std::string_view name) const {
  const std::string_view base = ssa_base_name(name);
  if (base.find('.') != std::string_view::npos || runner_st == nullptr) {
    return std::nullopt;
  }
  // The unit's own ports skip the declare bake; their declared type rides
  // io_meta (SSA), and a value copied into an output must not re-type it.
  if (const auto& io = lm->unit_lnast()->io_meta(); io.find(base) != nullptr) {
    return port_envelope_of(base, io);
  }
  const auto b = runner_st->get_bundle(base);
  if (!b) {
    return std::nullopt;
  }
  const auto& e = b->get_entry(bundle_path::of_string("0"));
  auto        r = range_from_entry(e.decl_max, e.decl_min);
  if (r.is_unbounded()) {
    return std::nullopt;
  }
  return r;
}

std::string_view uPass_bitwidth::display_name(std::string_view name) const {
  const auto user  = lm->user_name(name);
  const auto param = Lnast_io_entry::default_value_param(user);
  return param.empty() ? user : param;
}

std::optional<Lnast_range> uPass_bitwidth::declared_type_of(std::string_view name) const {
  const std::string_view base = ssa_base_name(name);
  if (base.empty() || base.front() == '%') {
    return std::nullopt;  // a compiler temp is never declared
  }
  // A defaulted input's default-value local becomes that input at every call
  // that omits it, so the default must fit the input's declared type.
  // (Its unit is the definition being walked, or inlined: the active tree.)
  if (const auto param = Lnast_io_entry::default_value_param(lm->user_name(base)); !param.empty()) {
    const auto& ios = lm->get_lnast()->io_meta();
    const auto* io  = ios.find(param);
    return io != nullptr && io->has_default ? port_envelope_of(param, ios) : std::nullopt;
  }
  if (!typed_names_.contains(base) && lm->unit_lnast()->io_meta().find(base) == nullptr) {
    return std::nullopt;
  }
  // Flattened tuple-port reads are scalar operands too. Their declared
  // envelope lives on the leaf, not on the tuple's scalar slot zero.
  if (base.find('.') != std::string_view::npos) {
    return declared_field_type_of(base);
  }
  return decl_envelope_of(base);
}

std::optional<int64_t> uPass_bitwidth::declared_floor_of(std::string_view base) const {
  if (base.empty() || base.front() == '%' || runner_st == nullptr) {
    return std::nullopt;
  }
  // A port: an integer with no width and no exact range that is unsigned is the
  // bare `Unsigned` (floor 0).
  if (const auto* io = lm->unit_lnast()->io_meta().find(base); io != nullptr) {
    if (io->kind == Io_kind::integer && io->array_size == 0 && !io->is_signed && io->bits <= 0 && !io->has_range
        && !io->wide_range_min && !io->has_deferred_bound()) {
      return 0;
    }
    return std::nullopt;
  }
  if (!typed_names_.contains(base)) {
    return std::nullopt;
  }
  const auto f = upass::decl_facts::lookup(*runner_st, lm->get_lnast().get(), base);
  if (f && f->range_min && !f->range_max && f->range_min->is_just_i64()) {
    return f->range_min->to_just_i64();
  }
  return std::nullopt;
}

std::optional<Lnast_range> uPass_bitwidth::check_declared_fit(std::string_view name, const Lnast_range& r, Unbounded why) {
  if (name.empty()) {
    return std::nullopt;
  }
  const std::string_view base = ssa_base_name(name);
  if (wrap_sat_exempt_.erase(name) != 0 || (base != name && wrap_sat_exempt_.erase(base) != 0)) {
    return std::nullopt;
  }
  // Only a DECLARED type is a promise to hold. An SSA-version / compiler temp
  // (`%x_0`) or an untyped name takes the range of its value: any envelope it
  // carries rode in on an earlier value (e.g. a bit-select force's `uW`
  // typespec on one if-arm), and a sibling arm's wider legal write must not be
  // judged against it. The BASE name's own check still runs at the merged write.
  const auto env = base.find('.') == std::string_view::npos ? declared_type_of(base) : declared_field_type_of(base);
  if (!env) {
    // No two-sided envelope; a one-sided floor (`Unsigned` == `Signed(min=0)`,
    // `Signed(min=-5)`) is still a promise: "above `max` or below `min`" is a
    // compile error (docs 07-typesystem "Bitwidth").
    if (const auto floor = base.find('.') == std::string_view::npos ? declared_floor_of(base) : std::nullopt;
        floor && !r.is_unbounded() && r.min < *floor) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "bitwidth-overflow",
          .category = "bitwidth",
          .pass     = "upass.bitwidth",
          .message  = std::format("`{}` ({}) {} not fit its declared range [{}, +inf)",
                                  display_name(base),
                                  r.is_constant() ? std::format("value {}", r.min) : std::format("range [{}, {}]", r.min, r.max),
                                  r.max < *floor ? "does" : "may",
                                  *floor),
          .span     = lm->current_span(),
          .hint     = "widen the declared type, force the bits with a bit-select (e.g. `x#[0..]`), or "
                      "apply a wrap/saturate policy",
      });
    }
    return std::nullopt;
  }
  // A bit-PATTERN literal is a force (the runner's comptime value check judges
  // one wider than its destination). A range nothing derived (a concat still
  // awaiting its lane widths, an opaque call result) cannot be judged, but the
  // declared type no longer bounds what `name` holds either: it must not become
  // its read range (a `wrap` of it would lower to a plain alias).
  if (r.is_unbounded() && why != Unbounded::wide) {
    if (why == Unbounded::unknown && runner_st != nullptr) {
      runner_st->unchecked_typed.emplace(base);
    }
    return std::nullopt;
  }
  // User ruling 2026-09-27: a typed destination must CONTAIN every value the
  // right-hand side may take (its derived range, which falls back to each
  // operand's declared envelope). A value that only MAY overflow is the same
  // compile error as one that always does, and so is a value too wide for any
  // derived range (`x << n`, a u64); the fix is an explicit `wrap`/`sat`
  // (exempted above) or a bit-select.
  if (!r.is_unbounded() && env->contains(r)) {
    return std::nullopt;
  }
  record_overflow(base, r, *env);
  return env;
}

std::optional<Lnast_range> uPass_bitwidth::declared_field_type_of(std::string_view name) const {
  // A typed tuple FIELD: a tuple port's flattened leaf (`o.a`), or a typed
  // tuple the detupler split into per-field declares (`t.x` of
  // `mut t:(x:u4, y:u4)`). The field's own type, exactly as a scalar's.
  if (runner_st == nullptr) {
    return std::nullopt;
  }
  const auto& io = lm->unit_lnast()->io_meta();
  if (io.find(name) != nullptr) {
    return port_envelope_of(name, io);
  }
  if (!typed_names_.contains(name)) {
    return std::nullopt;
  }
  if (const auto it = declared_field_envs_.find(name); it != declared_field_envs_.end()) {
    return it->second;
  }
  const auto f = upass::decl_facts::lookup(*runner_st, lm->get_lnast().get(), name);
  if (!f || !f->range_max || !f->range_min) {
    return std::nullopt;
  }
  const auto r = range_from_entry(*f->range_max, *f->range_min);
  return r.is_unbounded() ? std::nullopt : std::optional<Lnast_range>(r);
}

std::optional<Lnast_range> uPass_bitwidth::array_elem_envelope_of(std::string_view name) const {
  if (runner_st == nullptr) {
    return std::nullopt;
  }
  // An array PORT of the unit (`v:[4]u8`): its element type rides io_meta. A
  // multi-dimensional port's elem_bits is one packed row: the element, like a
  // body array's (`[4][8]u8 -> u8`), is that row split over the inner dims.
  const std::string_view base = ssa_base_name(name);
  if (const auto* io = lm->unit_lnast()->io_meta().find(base); io != nullptr) {
    int64_t row = 1;
    for (const auto d : io->inner_dims) {
      row *= d;
    }
    const int64_t bits = row > 0 && io->elem_bits % row == 0 ? io->elem_bits / row : 0;
    if (io->array_size <= 0 || bits <= 0 || bits > 62) {
      return std::nullopt;
    }
    if (io->elem_signed) {
      return Lnast_range::sext_to(bits - 1);
    }
    Lnast_range r;
    r.min       = 0;
    r.max       = (int64_t{1} << bits) - 1;
    r.unbounded = false;
    return r;
  }
  // A declared body array: the element envelope rides the root bundle's
  // internal __elem_max/__elem_min attrs (baked by the runner's declare
  // pre-step; [4][8]u8 -> u8).
  for (const auto n : {name, base}) {
    if (const auto b = runner_st->get_bundle(n)) {
      const auto env = range_from_entry(b->get_attr("__elem_max"), b->get_attr("__elem_min"));
      if (!env.is_unbounded()) {
        return env;
      }
    }
  }
  return std::nullopt;
}

bool uPass_bitwidth::is_array_name(std::string_view name) const {
  if (runner_st == nullptr) {
    return false;
  }
  const std::string_view base = ssa_base_name(name);
  if (const auto* io = lm->unit_lnast()->io_meta().find(base); io != nullptr) {
    return io->array_size > 0;
  }
  const auto b = runner_st->get_bundle(base);
  return b && (!b->get_attr("__elem_max").is_invalid() || !b->get_attr("__array_size").is_invalid());
}

void uPass_bitwidth::check_array_elem_fit(std::string_view name, const Lnast_range& r, Unbounded why) {
  // Element stores into a declared array ([4][8]u8 → each element u8, or an
  // array port's element type). Scalars/tuples have no element envelope --
  // no-op there. An UNKNOWN range is not judged: a bit-range update of one
  // element (`mem[a]#[(w*8)..+8] = d`) is an element-wide read-modify-write
  // whose range this pass does not derive. A value too WIDE for any derived
  // range is judged like a scalar's (check_declared_fit).
  if (name.empty() || (r.is_unbounded() && why != Unbounded::wide)) {
    return;
  }
  const std::string_view base = ssa_base_name(name);
  if (wrap_sat_exempt_.erase(name) != 0 || (base != name && wrap_sat_exempt_.erase(base) != 0)) {
    return;
  }
  const auto env = array_elem_envelope_of(name);
  if (!env) {
    return;
  }
  // Same containment judgement as check_declared_fit (user ruling 16): an
  // element must CONTAIN every value the stored expression may take. An
  // indexed entry cannot carry `wrap`/`sat` (04b-attributes.md), so the fix is
  // an intermediate `wrap`/`sat` variable or a bit-select.
  bool over = r.is_unbounded() || !env->contains(r);
  if (over && env->is_signed() && !r.is_unbounded() && lm->get_lnast()->is_verilog_origin()) {
    // A SIGNED element declared W bits of a VERILOG memory also holds any
    // W-bit PATTERN: the reader models array storage as RAW BITS and
    // sign-extends on read (`q[i]#sext[0..=W-1]`), so an element write carries
    // the UNSIGNED reinterpretation of the same storage. Without this, EVERY
    // signed memory (`reg signed [16:0] q [3:0]` read at a dynamic index) died
    // on a self-contradictory "`q` (value 0) does not fit its declared range
    // [-65536, 65535]". Pyrope code has no such force: a u4 value into an s4
    // element may not fit, like the scalar store `mut m:s4 = a`.
    const int64_t sb = storage_bits_for_env(*env);
    if (sb > 0 && sb < 63 && r.min >= 0 && r.max <= ((int64_t{1} << sb) - 1)) {
      over = false;
    }
  }
  if (over) {
    record_overflow(base, r, *env, /*element=*/true);
  }
}

void uPass_bitwidth::record_overflow(std::string_view name, const Lnast_range& value, const Lnast_range& env, bool element) {
  // The diagnostic is emitted AT the offending node: the cursor is on
  // the store/op during dispatch, so its SourceId (resolved through the
  // owning Lnast's locator) is the span. An error-severity diag
  // fails the compile; no end_run throw, no deferral.
  livehd::diag::Span              span = lm->current_span();
  std::vector<livehd::diag::Note> notes;
  if (const auto& ln = lm->get_lnast()) {
    notes = ln->notes_of(lm->get_current_nid(), "reached via this site");
  }
  // A single value either fits or not; a range that only partly leaves the
  // envelope (or that nothing bounds) MAY overflow.
  const bool        disjoint = !value.is_unbounded() && (value.min > env.max || value.max < env.min);
  const std::string what     = value.is_unbounded()  ? std::string("unbounded range")
                               : value.is_constant() ? std::format("value {}", value.min)
                                                     : std::format("range [{}, {}]", value.min, value.max);
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = "bitwidth-overflow",
      .category = "bitwidth",
      .pass     = "upass.bitwidth",
      .message  = std::format("{}`{}` ({}) {} not fit its declared range [{}, {}]",
                              element ? "element of " : "",
                              display_name(name),
                              what,
                              disjoint ? "does" : "may",
                              env.min,
                              env.max),
      .span     = std::move(span),
      .hint     = element ? "widen the declared type of the elements, force the bits with a bit-select (e.g. `x#[0..]`), "
                            "or apply a wrap/saturate policy through a typed variable first (`wrap v = …` then store `v`): "
                            "an entry picked with an index cannot carry `wrap`/`sat`"
                          : "widen the declared type, force the bits with a bit-select (e.g. `x#[0..]`), or "
                            "apply a wrap/saturate policy",
  });
}

Lnast_range uPass_bitwidth::envelope_of_operand(const upass::Operand& o) const {
  // A never-written name (e.g. an input param) has no derived value range,
  // only a declared envelope — good enough to judge a shift amount's sign
  // (`n: s4` admits [-8, 7]).
  if (o.name.empty()) {
    return Lnast_range::make_unbounded();
  }
  if (auto env = decl_envelope_of(o.name)) {
    return *env;
  }
  return Lnast_range::make_unbounded();
}

void uPass_bitwidth::check_shift_amount(const Lnast_range& amt) {
  if (amt.is_unbounded() || amt.min >= 0) {
    return;
  }
  // Skip deferred-template bodies (Lnast::is_template): unbound params fold
  // nil-derived placeholder values, so a negative amount there is an
  // optimization artifact — the realized copy at a real call site re-checks.
  if (const auto& ln = lm->get_lnast(); ln && ln->is_template()) {
    return;
  }
  const bool         always_negative = amt.max < 0;
  livehd::diag::Span span            = lm->current_span();
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = always_negative ? livehd::diag::Severity::error : livehd::diag::Severity::warning,
      .code     = "negative-shift",
      .category = "bitwidth",
      .pass     = "upass.bitwidth",
      .message  = always_negative ? std::format("shift amount is always negative (range [{}, {}])", amt.min, amt.max)
                                  : std::format("shift amount may be negative (range [{}, {}])", amt.min, amt.max),
      .span     = std::move(span),
      .hint     = "a shift / bit-select count must be >= 0",
  });
}

void uPass_bitwidth::check_index_nonneg(const Lnast_range& idx, std::string_view idx_name) {
  // An array index must be >= 0: any `bw_min < 0` in an index is a compile
  // error. The runner has already rebased the index of an array declared over
  // an index range (`[-4..<4]`), so here the lower bound is always 0. Mirrors
  // check_shift_amount's template-skip.
  if (idx.is_unbounded() || idx.min >= 0) {
    return;
  }
  if (const auto& ln = lm->get_lnast(); ln && ln->is_template()) {
    return;
  }
  livehd::diag::Span span    = lm->current_span();
  const bool         rebased = idx_name.starts_with(upass::kRebasedIndexPrefix);
  livehd::diag::sink().emit(livehd::diag::Diagnostic{
      .severity = livehd::diag::Severity::error,
      .code     = "negative-index",
      .category = "bitwidth",
      .pass     = "upass.bitwidth",
      .message  = rebased ? std::format("array index may be below the first index of the array's index range (index minus "
                                        "the first index has range [{}, {}])",
                                        idx.min,
                                        idx.max)
                          : std::format("array index is negative (range [{}, {}])", idx.min, idx.max),
      .span     = std::move(span),
      .hint     = rebased ? "an index into an array declared `[lo..<hi]` must be >= lo: narrow the index's type"
                          : "an array index must be >= 0",
  });
}

// ── Process hooks (push form) ────────────────────────────────────────────────

upass::Vote uPass_bitwidth::process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // Direct assignment to a mut var REPLACES the range (a stale narrow range
  // must not survive a reassignment — soundness: a binding's range must
  // contain its value).
  if (dst_name.empty() || src.empty()) {
    return Vote::keep;
  }
  if (dst_name.find('.') != std::string_view::npos) {
    // A typed tuple FIELD (`t.x = v`) is a typed destination like a scalar
    // (user ruling 16); its range is not tracked (per-field ranges are a
    // follow-up), only the fit is judged.
    if (src.size() == 1) {
      (void)check_declared_fit(dst_name, range_of_operand(src.front()), why_unbounded(src.front()));
    }
    return Vote::keep;
  }
  if (src.size() == 1) {
    check_array_whole_fit(dst_name, src.front());
    note_inferred_whole(dst_name, src.front());
    write_bw(dst_name, dst, range_of_operand(src.front()), /*replace=*/true, why_unbounded(src.front()));
    return Vote::keep;
  }
  // Field-path store (selectors + value). The VALUE must fit a declared
  // array's element envelope (b:[4][8]u8 → b[i][j] = 300 errors). `x[0] = v`
  // on a SCALAR binding is the scalar itself — stamp the value's range
  // exactly. Any other path invalidates the root's scalar range (per-field
  // ranges are a follow-up; an unbounded fallback is sound — the declared
  // envelope still bounds it).
  check_array_elem_fit(dst_name, range_of_operand(src.back()), why_unbounded(src.back()));
  note_inferred_access(dst_name, src.first(src.size() - 1), &src.back());
  // Every selector (index) preceding the value must be a non-negative index.
  for (std::size_t i = 0; i + 1 < src.size(); ++i) {
    check_index_nonneg(range_of_operand(src[i]), src[i].name);
  }
  // (Never on an array: lane 0 of `r:[2]s8` is an element, judged above, not
  // the packed port.)
  const auto& sel   = src.front();
  const bool  slot0 = sel.name.empty() && sel.bundle && !sel.bundle->lone_trivial().is_invalid()
                      && sel.bundle->lone_trivial().is_known_zero() && !is_array_name(dst_name);
  if (src.size() == 2 && slot0) {
    write_bw(dst_name, dst, range_of_operand(src.back()), /*replace=*/true, why_unbounded(src.back()));
  } else {
    write_bw(dst_name, dst, Lnast_range::make_unbounded(), /*replace=*/true, Unbounded::unknown);
  }
  return Vote::keep;
}

// `dst = x + c` / `dst = x - c` (sign -1) over a compiler temp: record dst as
// `base(x) + offset` (see affine_temps_).
void uPass_bitwidth::note_affine(std::string_view dst_name, upass::Src_span src, int64_t sign) {
  if (src.size() != 2 || !Lnast::is_tmp(dst_name)) {
    return;
  }
  for (int var = 0; var < 2; ++var) {
    const auto& x = src[var];
    const auto  c = range_of_operand(src[1 - var]);
    if (x.name.empty() || !src[1 - var].name.empty() || !c.is_constant() || (sign < 0 && var != 0)) {
      continue;
    }
    auto a    = affine_of(x.name);
    a.offset += sign * c.min;
    affine_temps_.insert_or_assign(std::string(dst_name), std::move(a));
    return;
  }
}

uPass_bitwidth::Affine uPass_bitwidth::affine_of(std::string_view name) const {
  if (const auto it = affine_temps_.find(name); it != affine_temps_.end()) {
    return it->second;
  }
  return Affine{std::string(name), 0};
}

void uPass_bitwidth::process_range() {
  if (!move_to_child()) {
    return;
  }
  const std::string dst{current_text()};
  std::string       ends[2];
  for (auto& end : ends) {
    if (!move_to_sibling() || !Lnast_ntype::is_ref(get_raw_ntype())) {
      move_to_parent();
      return;
    }
    end = std::string{current_text()};
  }
  move_to_parent();
  const auto lo = affine_of(ends[0]);
  const auto hi = affine_of(ends[1]);
  if (Lnast::is_tmp(dst) && lo.base == hi.base && hi.offset >= lo.offset) {
    runtime_lane_bits_.insert_or_assign(dst, hi.offset - lo.offset + 1);
  }
}

// clang-format off
upass::Vote uPass_bitwidth::process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  note_affine(dst_name, src, 1);
  if (src.empty()) { return stamp_arith(dst_name, dst, Lnast_range::make_unbounded(), src); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.add(range_of_operand(src[i])); }
  return stamp_arith(dst_name, dst, result, src);
}

upass::Vote uPass_bitwidth::process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  note_affine(dst_name, src, -1);
  if (src.size() < 2) { return stamp_arith(dst_name, dst, Lnast_range::make_unbounded(), src); }
  return stamp_arith(dst_name, dst, range_of_operand(src[0]).sub(range_of_operand(src[1])), src);
}

upass::Vote uPass_bitwidth::process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp_arith(dst_name, dst, Lnast_range::make_unbounded(), src); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.mul(range_of_operand(src[i])); }
  return stamp_arith(dst_name, dst, result, src);
}

upass::Vote uPass_bitwidth::process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // |a / d| <= |a| for any integer |d| >= 1.
  if (src.size() < 2) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  // An input divisor has no derived range, only its declared envelope. That is
  // enough to know its SIGN -- the one fact div() takes from the divisor when
  // the dividend is non-negative -- exactly as the shift checks use it. It only
  // TIGHTENS the stamp ([-|a|,|a|] -> [0,a.max]): a `u25 / u15` quotient
  // otherwise failed its own declared unsigned range (fixme_hier_test's leaf2).
  auto divisor = range_of_operand(src[1]);
  if (divisor.is_unbounded()) {
    if (const auto env = envelope_of_operand(src[1]); !env.is_unbounded() && env.min >= 0) {
      divisor = env;
    }
  }
  return stamp_carry(dst_name, dst, range_of_operand(src[0]).div(divisor), src);
}

upass::Vote uPass_bitwidth::process_mod(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // |a % d| < |d| and <= |a|; sign follows the dividend.
  if (src.size() < 2) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  return stamp_carry(dst_name, dst, range_of_operand(src[0]).mod(range_of_operand(src[1])), src);
}

upass::Vote uPass_bitwidth::process_shl(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) { return stamp_arith(dst_name, dst, Lnast_range::make_unbounded(), src); }
  if (const auto one = range_of_operand(src[0]); Lnast::is_tmp(dst_name) && src[0].name.empty() && one.is_constant() && one.min == 1) {
    runtime_lane_bits_.insert_or_assign(std::string(dst_name), 1);  // `1 << i`: the one-hot mask of a bit write
  }
  const auto amt = range_of_operand(src[1]);
  check_shift_amount(amt.is_unbounded() ? envelope_of_operand(src[1]) : amt);
  return stamp_arith(dst_name, dst, range_of_operand(src[0]).shl(amt), src);
}

upass::Vote uPass_bitwidth::process_sra(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  const auto amt = range_of_operand(src[1]);
  check_shift_amount(amt.is_unbounded() ? envelope_of_operand(src[1]) : amt);
  return stamp_carry(dst_name, dst, range_of_operand(src[0]).sra(amt), src);
}

// Bitwise ops — conservative: join of operand ranges (not tight).
upass::Vote uPass_bitwidth::process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.band(range_of_operand(src[i])); }
  return stamp_carry(dst_name, dst, result, src);
}

upass::Vote uPass_bitwidth::process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.bor(range_of_operand(src[i])); }
  return stamp_carry(dst_name, dst, result, src);
}

upass::Vote uPass_bitwidth::process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  Lnast_range result = range_of_operand(src[0]);
  for (std::size_t i = 1; i < src.size(); ++i) { result = result.bxor(range_of_operand(src[i])); }
  return stamp_carry(dst_name, dst, result, src);
}

upass::Vote uPass_bitwidth::process_bit_not(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // ~x = -x - 1, exactly -- unless the node is the TYPED form bit_not(x, N) the
  // runner issues for an unsigned-typed operand (user ruling 26): then only the
  // N bits flip, a uN result.
  if (src.empty()) { return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src); }
  if (src.size() > 1) {
    const auto n = range_of_operand(src[1]);
    const auto r = n.is_constant() ? range_of_operand(src[0]).bnot_bits(n.min) : Lnast_range::make_unbounded();
    // Past an i64 the result is still exactly N bits wide: WIDE, not unknown.
    write_bw(dst_name, dst, r, /*replace=*/true, r.is_unbounded() && n.is_constant() && n.min >= 63 ? Unbounded::wide : Unbounded::unknown);
    return upass::Vote::keep;
  }
  return stamp_carry(dst_name, dst, range_of_operand(src[0]).bnot(), src);
}

// Logical ops / reductions / comparisons — result is always boolean.
upass::Vote uPass_bitwidth::process_log_and(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_log_or(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_log_not(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_red_or(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::unsigned_bit()); }
upass::Vote uPass_bitwidth::process_red_and(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::unsigned_bit()); }
upass::Vote uPass_bitwidth::process_red_xor(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::unsigned_bit()); }
upass::Vote uPass_bitwidth::process_ne(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_eq(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_lt(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_le(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_gt(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
upass::Vote uPass_bitwidth::process_ge(std::string_view dst_name, Bundle& dst, upass::Src_span) { return stamp(dst_name, dst, Lnast_range::boolean()); }
// clang-format on

// popcount (`a#+[..]`) — set-bit count, not boolean. Always in
// [0, sbits(input)]: an n-bit value has at most n set bits.
upass::Vote uPass_bitwidth::process_popcount(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.empty()) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  const auto in = range_of_operand(src[0]);
  // A >64-bit input collapses Lnast_range to `unbounded` (its int64 bounds can't
  // hold the value), and get_sbits() then reports 64 — but the popcount of a
  // 128-bit all-ones value is 128, which would violate the bw-soundness check
  // against a [0,64] range. Leave the result unbounded for a wide input; the
  // concrete count is supplied by hlop (Dlop::popcount_op). (2f-bignum: no const
  // op — or its range — is computed in LiveHD for values hlop owns.)
  if (in.unbounded) {
    // A wide input still has its declared width.
    if (const auto bits = declared_bits_of(src[0]); bits > 0) {
      Lnast_range r;
      r.min       = 0;
      r.max       = bits;
      r.unbounded = false;
      return stamp(dst_name, dst, r);
    }
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  // A non-negative input has at most bit_width(max) set bits; a possibly
  // negative one counts its sign-extended width.
  Lnast_range result;
  result.min       = 0;
  result.max       = in.min >= 0 ? static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(in.max))) : in.get_sbits();
  result.unbounded = false;
  return stamp(dst_name, dst, result);
}

upass::Vote uPass_bitwidth::process_sext(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // sext(value, sign_bit_pos): result in [-2^p, 2^p - 1] for a constant
  // position p. src[1] is the sign-bit POSITION (0-indexed).
  if (src.size() < 2) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  const auto pos = range_of_operand(src[1]);
  if (!pos.is_constant()) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  return stamp(dst_name, dst, Lnast_range::sext_to(pos.min));
}

// get_mask(base, mask) — the selected bits packed LSB-first as an UNSIGNED
// value (this is the default zext "force" operator). A window of w bits lies
// in [0, 2^w − 1] — including the single-bit case w==1, which is the unsigned
// [0, 1] (a set bit reads as 1, never -1; only the explicit `#sext` form,
// lowered to a separate sext node, may be negative). The window is a
// non-negative constant mask, or a `x#[lo..=hi]` range object with comptime
// bounds. A non-negative base also bounds it: packing never moves a bit up,
// so the result is at most `base >> lo`, and a low window at least as wide as
// the base is the base itself (`x#[0..=7]` over a u3 is a zero extension, not
// a fresh 8-bit value). Negative (carve-out) or runtime windows stay
// unbounded here; the runner stamps a runtime window's static width as the
// temp's envelope, which reads fall back to.
upass::Vote uPass_bitwidth::process_get_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 2) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  const auto low  = range_of_operand(src[1]);
  const auto high = src.size() > 2 ? range_of_operand(src[2]) : low.add(Lnast_range::constant(1));
  if (!low.is_constant() || !high.is_constant() || low.min < 0 || high.min < low.min) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  const int64_t lo = low.min;
  const int64_t w  = high.min - lo;
  if (w == 0) {
    return stamp(dst_name, dst, Lnast_range::constant(0));
  }
  if (w >= 63) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  Lnast_range r;
  r.min       = 0;
  r.max       = (int64_t{1} << w) - 1;
  r.unbounded = false;
  if (const auto base = range_of_operand(src[0]); !base.is_unbounded() && base.min >= 0) {
    r.max = std::min(r.max, lo >= 63 ? int64_t{0} : base.max >> lo);
    if (lo == 0 && base.max <= r.max) {
      r.min = base.min;
    }
  }
  return stamp(dst_name, dst, r);
}

upass::Vote uPass_bitwidth::process_set_mask(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  if (src.size() < 3) {
    return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src);
  }
  const auto low  = range_of_operand(src[2]);
  const auto high = src.size() > 3 ? range_of_operand(src[3]) : low.add(Lnast_range::constant(1));
  if (!src[0].name.empty() && high.is_constant()) {
    const auto base = ssa_base_name(src[0].name);
    if (auto env = decl_envelope_of(base); env && high.min > storage_bits_for_env(*env)) {
      livehd::diag::sink().emit(livehd::diag::Diagnostic{
          .severity = livehd::diag::Severity::error,
          .code     = "bit-range-overflow",
          .category = "bitwidth",
          .pass     = "upass.bitwidth",
          .message
          = std::format("bit range ending at {} does not fit the {}-bit destination", high.min, storage_bits_for_env(*env)),
          .span = lm->current_span(),
          .hint = "keep the selection within the destination storage",
      });
    }
  }
  {
    std::optional<int64_t> lane_bits;
    if (low.is_constant() && high.is_constant() && low.min >= 0 && high.min >= low.min) {
      lane_bits = high.min - low.min;
    } else if (src.size() == 3) {
      lane_bits = 1;
    } else if (!src[2].name.empty() && !src[3].name.empty()) {
      const auto l = affine_of(src[2].name);
      const auto h = affine_of(src[3].name);
      if (l.base == h.base && h.offset > l.offset) {
        lane_bits = h.offset - l.offset;
      }
    }
    if (lane_bits && *lane_bits > 0) {
      // Prefer the declared envelope: `b:u8` remains an eight-bit source even
      // if an earlier optimization happened to learn a narrower current value.
      // Compiler temporaries (arithmetic, literals) fall back to their derived
      // range when they have no declaration.
      auto value_env = envelope_of_operand(src[1]);
      if (value_env.is_unbounded()) {
        value_env = range_of_operand(src[1]);
      }
      if (!value_env.is_unbounded()) {
        const int64_t value_bits = storage_bits_for_env(value_env);
        if (value_bits > *lane_bits) {
          livehd::diag::sink().emit(livehd::diag::Diagnostic{
              .severity = livehd::diag::Severity::error,
              .code     = "bit-range-overflow",
              .category = "bitwidth",
              .pass     = "upass.bitwidth",
              .message  = std::format("{}-bit value does not fit the {}-bit destination slice", value_bits, *lane_bits),
              .span     = lm->current_span(),
              .hint = std::format("select {} bits on the right-hand side, or apply an explicit wrap/saturate policy", *lane_bits),
          });
        }
      }
    }
  }
  // The result is the base with the selected bits replaced: every set bit is
  // the base's or the mask's, whatever the inserted value was. So a
  // non-negative base with a known non-negative window stays within the ones
  // cover of both; otherwise it stays in the base's declared storage (a mask
  // reaching past it is the error above). A SIGNED variable's bits read back
  // as its own type, and the written bit may be its sign bit: its result is
  // the declared envelope, never the unsigned cover (`mut v:s8 = 0; v#[7] =
  // e` otherwise "overflowed" v's own type on the store back).
  if (!src.empty() && !src[0].name.empty()) {
    if (const auto env = decl_envelope_of(src[0].name); env && !env->is_unbounded() && env->min < 0) {
      return stamp_carry(dst_name, dst, *env, src);
    }
  }
  {
    std::optional<int64_t> mask_bits;
    if (low.is_constant() && high.is_constant() && low.min >= 0 && high.min > low.min && high.min < 63) {
      mask_bits = (int64_t{1} << high.min) - (int64_t{1} << low.min);
    }
    if (const auto base = range_of_operand(src[0]); mask_bits && !base.is_unbounded() && base.min >= 0) {
      Lnast_range r;
      r.min       = 0;
      r.max       = Lnast_range::ones_cover(std::max(base.max, *mask_bits));
      r.unbounded = false;
      return stamp_carry(dst_name, dst, r, src);
    }
  }
  if (!src.empty() && !src[0].name.empty()) {
    if (const auto env = decl_envelope_of(src[0].name)) {
      return stamp_carry(dst_name, dst, *env, src);
    }
  }
  return stamp_carry(dst_name, dst, Lnast_range::make_unbounded(), src);
}

upass::Vote uPass_bitwidth::process_concat(std::string_view dst_name, Bundle& dst, upass::Src_span src) {
  // concat(dst, v_msb, w_msb, …, v_lsb, w_lsb) drops each lane into its OWN
  // window, so the assembled value is always NON-NEGATIVE and exactly sum(w_i)
  // bits wide: the range is [0, 2^sum − 1], whatever the lanes' own signs and
  // values are (a negative lane lands as its two's-complement pattern inside
  // its window).
  //
  // The widths are OPERANDS, not something this pass derives: a width that had
  // to be inferred from a lane's range would shrink whenever the range did, and
  // shrinking one lane shifts every lane ABOVE it -- a silent miscompile rather
  // than a lost bound. An unbound (`nil`) width therefore does not get a guess
  // here; the result is simply left unbounded, and upass.tolg owns the
  // `concat-untyped-lane` diagnostic, which can point at the offending lane.
  int64_t total = 0;
  if ((src.size() % 2) != 0 || src.empty()) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());  // malformed shape: fail closed
  }
  for (std::size_t i = 1; i < src.size(); i += 2) {  // the ODD operands are the widths
    const auto r = range_of_operand(src[i]);
    if (r.unbounded || r.min != r.max || r.min <= 0) {
      return stamp(dst_name, dst, Lnast_range::make_unbounded());  // still `nil`, or not a positive comptime width
    }
    total += r.min;
  }
  // Lnast_range's bounds are int64, so a 63-bit-or-wider bus cannot be
  // expressed (the same cutoff get_mask uses).
  if (total <= 0) {
    return stamp(dst_name, dst, Lnast_range::make_unbounded());
  }
  if (total >= 63) {
    write_bw(dst_name, dst, Lnast_range::make_unbounded(), /*replace=*/true, Unbounded::wide);
    return Vote::keep;
  }
  Lnast_range r;
  r.min       = 0;
  r.max       = (int64_t{1} << total) - 1;
  r.unbounded = false;
  return stamp(dst_name, dst, r);
}

// ── Nullary hooks ────────────────────────────────────────────────────────────

void uPass_bitwidth::process_func_call() {
  if (!move_to_child()) {
    return;
  }

  const std::string call_dst(current_text());
  clear_range(call_dst);  // call result is unknown unless another pass proves it
  if (!move_to_sibling()) {
    move_to_parent();
    return;
  }

  // A `wrap`/`sat` narrowing call. The `type=` arg names the target
  // var whose declared envelope is intentionally overflowed; exempt it from
  // the does-not-fit check at its next write.
  const auto callee      = current_text();
  const bool is_wrap_sat = callee == "wrap" || callee == "sat";
  if (is_wrap_sat && runner_st != nullptr) {
    // Attributes/constprop already computed a constant narrowing result.
    // Retain its precise range so the following assignment and .[bw_max]
    // observe the narrowed value instead of an unbounded call result.
    if (auto value = runner_st->comptime_scalar(call_dst); value) {
      if (auto number = const_to_i64(*value); number) {
        if (auto bundle = runner_st->get_bundle_for_write(call_dst); bundle) {
          write_bw(call_dst, *bundle, Lnast_range::constant(*number), /*replace=*/true, Unbounded::unknown);
        }
      }
    }
  }

  while (move_to_sibling()) {
    if (!is_type(Lnast_ntype::Lnast_ntype_store)) {
      continue;
    }
    if (!move_to_child()) {
      continue;
    }
    const bool is_ref_arg  = current_text() == call_ref_arg_marker;
    const bool is_type_arg = is_wrap_sat && current_text() == "type";
    if (is_ref_arg && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
      clear_range(current_text());
    } else if (is_type_arg && move_to_sibling() && is_type(Lnast_ntype::Lnast_ntype_ref)) {
      wrap_sat_exempt_.insert(std::string{current_text()});
    }
    move_to_parent();
  }

  move_to_parent();
}

void uPass_bitwidth::process_tuple_get() {
  if (runner_st == nullptr || !move_to_child()) {
    return;
  }
  const std::string           dst{current_text()};
  std::string                 src;
  // The positions read (an inferred array is sized by them); the names are
  // owned here, the operands view them.
  std::vector<std::string>    idx_names;
  std::vector<upass::Operand> idx;
  if (move_to_sibling()) {
    src = std::string{current_text()};
    while (live_inferred(src) != nullptr && move_to_sibling()) {
      if (Lnast_ntype::is_const(get_raw_ntype())) {
        const auto v = upass::int_literal(current_text());
        if (!v) {
          idx.clear();  // a field name, not a position
          idx_names.clear();
          break;
        }
        idx.push_back(upass::Operand{.name = {}, .bundle = Bundle::make_const(*Dlop::create_integer(*v), upass::Kind::integer)});
      } else {
        idx_names.emplace_back(current_text());
        idx.push_back(upass::Operand{.name = {}, .bundle = {}});
      }
    }
  }
  move_to_parent();
  for (size_t i = 0, n = 0; i < idx.size(); ++i) {
    if (idx[i].bundle == nullptr) {  // a ref: bind it now that idx_names is stable
      const auto& nm = idx_names[n++];
      const auto  b  = runner_st->get_bundle(nm);
      idx[i]         = upass::Operand{.name = nm, .bundle = b ? b : std::make_shared<Bundle>()};
    }
  }
  if (!idx.empty()) {
    note_inferred_access(src, idx, nullptr);
  }
  if (dst.empty() || dst.find('.') != std::string::npos) {
    return;  // scalar reads only
  }
  std::optional<Lnast_range> r;
  // A Sub instance output (`child.o`): its port type bounds it. The child's own
  // body is held to that type, and the port is the boundary.
  // (Past 62 bits the range is unbounded here, but still an integer's.)
  if (const auto origin = runner_st->tget_origin.find(dst); origin != runner_st->tget_origin.end()) {
    if (const auto* port = runner_st->sub_output_range(origin->second)) {
      r = range_from_entry(port->second, port->first);
    }
  }
  // An element of a declared array or an array port (`arr[i]`, even at a
  // runtime index): the element type bounds it, as a typed name's type bounds
  // its reads (every element store is held to it, see check_array_elem_fit).
  if (!r && !src.empty()) {
    r = array_elem_envelope_of(src);
  }
  if (!r) {
    // The read rebinds `dst` (a loop's `for v in ref t` picks each slot into
    // the same name): any range it held described the previous value.
    clear_range(dst);
    return;
  }
  // Only an unbounded port range (past 62 bits) is a WIDE value; a bounded one
  // is just its range (flagged wide, it made every value derived from it --
  // an element's bit-range update -- look too wide to judge).
  const auto why = r->is_unbounded() ? Unbounded::wide : Unbounded::unknown;
  if (auto b = runner_st->get_bundle_for_write(dst); b) {
    write_bw(dst, *b, *r, /*replace=*/true, why);
  } else {
    Bundle scratch(dst);
    write_bw(dst, scratch, *r, /*replace=*/true, why);
  }
}

void uPass_bitwidth::process_declare() {
  // declare(var, type, mode[, value]): a typed declaration makes `var` a
  // destination the fit check holds to its type; an untyped one
  // (prim_type_none) takes the range of whatever it is assigned.
  if (!move_to_child()) {
    return;
  }
  const std::string var{current_text()};
  const bool        typed = move_to_sibling() && !Lnast_ntype::is_prim_type_none(get_raw_ntype());
  if (typed && Lnast_ntype::is_comp_type_array(get_raw_ntype())) {
    note_inferred_declare(var);
  }
  // A typed tuple FIELD the detupler declared (`declare(p.x, prim_type_int(15,
  // 0), mut)`): its envelope is kept here, because a later runtime store into
  // the field re-points the field's binding at the stored value.
  std::optional<Lnast_range> field_env;
  if (typed && var.find('.') != std::string::npos && Lnast_ntype::is_prim_type_int(get_raw_ntype()) && move_to_child()) {
    Dlop mx;
    Dlop mn;
    if (Lnast_ntype::is_const(get_raw_ntype())) {
      mx = *Dlop::from_pyrope(current_text());
    }
    if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {
      mn = *Dlop::from_pyrope(current_text());
    }
    move_to_parent();
    if (const auto r = range_from_entry(mx, mn); !r.is_unbounded()) {
      field_env = r;
    }
  }
  move_to_parent();
  if (typed && !var.empty()) {
    typed_names_.insert(var);
    check_reg_init_fit(var);
  }
  if (field_env) {
    declared_field_envs_.insert_or_assign(var, *field_env);
  }
}

namespace {

// The shape an array declaration writes (`comp_type_array` at `type`): each
// level outermost first, which of their dimensions are an open `[]`, and
// whether the innermost level has no element type (`[13]` / `[]`: its lone
// child is the dimension).
struct Array_decl_shape {
  std::vector<Lnast_nid> levels;
  std::vector<bool>      open_dim;
  bool                   open_elem = false;

  [[nodiscard]] bool open() const {
    return open_elem || std::any_of(open_dim.begin(), open_dim.end(), [](bool o) { return o; });
  }
};

Array_decl_shape array_decl_shape(const Lnast& ln, Lnast_nid type) {
  Array_decl_shape s;
  for (auto level = type; Lnast_ntype::is_comp_type_array(ln.get_type(level));) {
    const auto dim = upass::array_level_dim(ln, level);
    s.levels.push_back(level);
    s.open_dim.push_back(!dim.is_invalid() && ln.get_name(dim) == "[]");
    const auto first = ln.get_first_child(level);
    if (first.is_invalid() || ln.get_sibling_next(first).is_invalid()) {
      s.open_elem = true;
      break;
    }
    level = first;
  }
  return s;
}

// An inferred array larger than this (all dimensions together) is an error:
// an index as wide as a U32 would otherwise size a 2^32-entry memory.
constexpr int64_t kMaxInferredLanes = int64_t{1} << 16;

}  // namespace

void uPass_bitwidth::note_inferred_declare(const std::string& var) {
  // Cursor on the declare's comp_type_array.
  const auto& ln    = *lm->get_lnast();
  const auto  type  = lm->get_current_nid();
  const auto  shape = array_decl_shape(ln, type);
  if (!shape.open()) {
    inferred_live_.erase(var);  // a sized `[8]U8`: its accesses size nothing
    return;
  }
  // Sibling scopes may declare the same name again: their facts merge (a
  // larger extent or a wider element is always sound), unless the shapes
  // they leave open differ.
  const auto [it, fresh] = inferred_arrays_.try_emplace(var);
  auto& a                = it->second;
  if (fresh) {
    a.open_dim  = shape.open_dim;
    a.open_elem = shape.open_elem;
    a.extent.assign(a.open_dim.size(), 0);
    a.init_extent.assign(a.open_dim.size(), 0);
    a.span = lm->current_span();
  } else if (a.open_dim != shape.open_dim || a.open_elem != shape.open_elem) {
    a.conflict = true;
  }
  inferred_live_.insert(var);
  // A `reg` array's initializer rides its declare (a `mut` one is a store).
  const auto mode = ln.get_sibling_next(type);
  const auto init = mode.is_invalid() ? mode : ln.get_sibling_next(mode);
  if (init.is_invalid()) {
    return;
  }
  if (Lnast_ntype::is_const(ln.get_type(init))) {
    const auto v = Dlop::from_pyrope(ln.get_name(init));
    note_inferred_whole(var, upass::Operand{.name = {}, .bundle = Bundle::make_const(v ? *v : Dlop{}, upass::Kind::integer)});
  } else if (Lnast_ntype::is_ref(ln.get_type(init))) {
    const std::string name(ln.get_name(init));
    const auto        b = runner_st == nullptr ? nullptr : runner_st->get_bundle(name);
    note_inferred_whole(var, upass::Operand{.name = name, .bundle = b ? b : std::make_shared<Bundle>()});
  }
}

uPass_bitwidth::Inferred_array* uPass_bitwidth::live_inferred(std::string_view name) {
  const auto base = ssa_base_name(name);
  if (!inferred_live_.contains(base)) {
    return nullptr;
  }
  const auto it = inferred_arrays_.find(base);
  return it == inferred_arrays_.end() ? nullptr : &it->second;
}

void uPass_bitwidth::note_inferred_access(std::string_view name, upass::Src_span idx, const upass::Operand* value) {
  auto* a = live_inferred(name);
  if (a == nullptr) {
    return;
  }
  const auto bound = [this](const upass::Operand& o) {
    const auto r = range_of_operand(o);
    return r.is_unbounded() ? envelope_of_operand(o) : r;
  };
  for (size_t i = 0; i < idx.size() && i < a->extent.size(); ++i) {
    if (const auto r = bound(idx[i]); r.is_unbounded()) {
      a->unsized = a->unsized || a->open_dim[i];
    } else if (r.max >= 0) {
      a->extent[i] = std::max(a->extent[i], r.max + 1);
      if (a->open_dim[i] && r.max >= kMaxInferredLanes && a->big_index.empty()) {
        a->big_index = idx[i].name.empty() || Lnast::is_tmp(idx[i].name)
                           ? std::format("an index reaches {}", r.max)
                           : std::format("index `{}` reaches {}", upass::Lnast_manager::user_name(idx[i].name), r.max);
      }
    }
  }
  if (value == nullptr) {
    return;
  }
  if (const auto v = value->bundle->scalar(); value->name.empty() && v && (v->is_nil() || v->has_unknowns())) {
    return;  // `nil` / `0sb?` leave an entry undefined: no value to type it
  }
  const auto r = bound(*value);
  if (r.is_unbounded()) {
    a->untyped = true;
  } else {
    a->elem = a->elem ? a->elem->join(r) : r;
  }
}

void uPass_bitwidth::note_inferred_whole(std::string_view name, const upass::Operand& value) {
  auto* a = live_inferred(name);
  if (a == nullptr || value.bundle == nullptr) {
    return;
  }
  const auto& b = *value.bundle;
  if (b.has_named_top()) {
    a->untyped = true;  // a named tuple is no array value
    return;
  }
  if (b.unnamed_top_count() == 0) {
    return;  // `= nil` lowered to an empty tuple: no value at all
  }
  if (b.unnamed_top_count() == 1) {
    // One value fills every entry.
    const upass::Operand* v = &value;
    note_inferred_access(name, {}, v);
    return;
  }
  // A positional tuple (`(0, 1, …, 7)`, nested row-major for `[][]`): each
  // position sizes its dimension, each entry is a value. The tuple defines
  // only the entries it lists, so no index may size the array past it.
  std::vector<int64_t> listed(a->extent.size(), 0);
  for (const auto& [key, e] : b.non_attr_entries()) {
    std::string_view rest = key;
    for (size_t l = 0; !rest.empty() && l < listed.size(); ++l) {
      const auto seg = Bundle::get_first_level(rest);
      if (const auto pos = upass::int_literal(seg); pos && *pos >= 0) {
        listed[l] = std::max(listed[l], *pos + 1);
      }
      rest = Bundle::get_all_but_first_level(rest);
    }
    if (e.trivial.is_nil() || e.trivial.has_unknowns()) {
      continue;
    }
    if (const auto i = const_to_i64(e.trivial)) {
      a->elem = a->elem ? a->elem->join(Lnast_range::constant(*i)) : Lnast_range::constant(*i);
    } else {
      a->untyped = true;
    }
  }
  for (size_t l = 0; l < listed.size(); ++l) {
    a->extent[l]      = std::max(a->extent[l], listed[l]);
    a->init_extent[l] = a->init_extent[l] == 0 ? listed[l] : std::min(a->init_extent[l], listed[l]);
  }
}

void uPass_bitwidth::walk_dest(const std::shared_ptr<Lnast>& dest) {
  if (inferred_arrays_.empty() || dest == nullptr) {
    return;
  }
  std::vector<Lnast_nid>           decls;
  std::vector<Lnast_nid>           whole_stores;  // `store(a, v)`
  absl::flat_hash_set<std::string> empty_tuples;  // `tuple_add(t)` with no entries
  for (const auto& nid : dest->depth_preorder(dest->get_root())) {
    const auto t = dest->get_type(nid);
    if (Lnast_ntype::is_declare(t)) {
      decls.push_back(nid);
    } else if (Lnast_ntype::is_store(t) || Lnast_ntype::is_tuple_add(t)) {
      const auto dst = dest->get_first_child(nid);
      if (dst.is_invalid()) {
        continue;
      }
      const auto val = dest->get_sibling_next(dst);
      if (Lnast_ntype::is_tuple_add(t) && val.is_invalid()) {
        empty_tuples.emplace(dest->get_name(dst));
      } else if (Lnast_ntype::is_store(t) && !val.is_invalid() && dest->get_sibling_next(val).is_invalid()) {
        whole_stores.push_back(nid);
      }
    }
  }
  absl::flat_hash_set<std::string> patched;
  for (const auto& decl : decls) {
    const auto name = dest->get_first_child(decl);
    const auto type = name.is_invalid() ? name : dest->get_sibling_next(name);
    if (type.is_invalid() || !Lnast_ntype::is_comp_type_array(dest->get_type(type))) {
      continue;
    }
    const std::string var(dest->get_name(name));
    const auto        it = inferred_arrays_.find(var);
    if (it == inferred_arrays_.end() || it->second.conflict) {
      continue;
    }
    auto&      a     = it->second;
    // Only a declaration that leaves exactly the recorded shape open takes the
    // facts: a sized `[8]U8` of the same name in another scope keeps its own.
    const auto shape = array_decl_shape(*dest, type);
    if (shape.open_dim != a.open_dim || shape.open_elem != a.open_elem) {
      continue;
    }
    bool    sized = !a.unsized && (!a.open_elem || (!a.untyped && a.elem));
    int64_t lanes = 1;  // every dimension together, saturated past the limit
    for (size_t l = 0; l < a.open_dim.size() && sized; ++l) {
      const auto dim = upass::array_level_dim(*dest, shape.levels[l]);
      if (dim.is_invalid()) {
        sized = false;
        break;
      }
      const auto n = a.open_dim[l] ? a.extent[l] : upass::array_dim_lanes(dest->get_name(dim)).value_or(1);
      // An index past a positional tuple initializer's entries would read an
      // entry the initializer never wrote.
      sized        = n > 0 && (!a.open_dim[l] || a.init_extent[l] == 0 || n <= a.init_extent[l]);
      lanes        = n > 0 && lanes <= kMaxInferredLanes / n ? lanes * n : kMaxInferredLanes + 1;
    }
    if (!sized) {
      continue;  // nothing settles its shape: lnast.tolg reports it
    }
    if (lanes > kMaxInferredLanes) {
      if (!a.reported) {
        a.reported = true;
        livehd::diag::sink().emit(livehd::diag::Diagnostic{
            .severity = livehd::diag::Severity::error,
            .code     = "array-infer-too-large",
            .category = "bitwidth",
            .pass     = "upass.bitwidth",
            .message  = std::format("array `{}` would infer more than {} entries from its uses{}",
                                    upass::Lnast_manager::user_name(var),
                                    kMaxInferredLanes,
                                    a.big_index.empty() ? std::string{} : std::format(" ({})", a.big_index)),
            .span     = a.span,
            .hint     = "declare its size (`[N]T`) or narrow the index type",
        });
      }
      continue;
    }
    patched.emplace(var);
    for (size_t l = 0; l < shape.levels.size(); ++l) {
      const auto level = shape.levels[l];
      const auto dim   = upass::array_level_dim(*dest, level);
      const auto text  = a.open_dim[l] ? std::format("[{}]", a.extent[l]) : std::string(dest->get_name(dim));
      if (a.open_elem && l + 1 == shape.levels.size()) {
        // An element-less level: its lone dimension child becomes the element
        // type, and the dimension moves behind it (`comp_type_array(elem, [N])`).
        const bool is_signed = a.elem->min < 0;
        const auto bits      = static_cast<uint32_t>(
            std::max<int64_t>(1, upass::range_bits(*Dlop::create_integer(a.elem->max), *Dlop::create_integer(a.elem->min))));
        dest->set_type(dim, Lnast_ntype::create_prim_type_int());
        dest->set_name(dim, "");
        dest->add_child(dim, Lnast_node::create_const(upass::max_from_bits(bits, is_signed).to_pyrope()));
        dest->add_child(dim, Lnast_node::create_const(upass::min_from_bits(bits, is_signed).to_pyrope()));
        dest->add_child(level, Lnast_node::create_const(text));
      } else {
        dest->set_name(dim, text);
      }
    }
  }
  // `mut a:[] = nil` seeds the unsized array with an EMPTY tuple (a later
  // splice may grow it); once its indices size it, that seed is no contents
  // at all -- a sized `[N]T = nil`.
  for (const auto& st : whole_stores) {
    const auto dst = dest->get_first_child(st);
    const auto val = dest->get_sibling_next(dst);
    if (patched.contains(dest->get_name(dst)) && Lnast_ntype::is_ref(dest->get_type(val))
        && empty_tuples.contains(dest->get_name(val))) {
      dest->set_type(val, Lnast_ntype::create_const());
      dest->set_name(val, "nil");
    }
  }
}

void uPass_bitwidth::check_array_whole_fit(std::string_view name, const upass::Operand& value) {
  // A whole-array store of known entries (`mut m:[4]u4 = (1, 2, 3, 20)`, or a
  // scalar fill) writes every element: each entry must fit the element type.
  if (!value.bundle || !is_array_name(name)) {
    return;
  }
  const auto env = array_elem_envelope_of(name);
  if (!env) {
    return;
  }
  const auto judge = [&](const Dlop& v) {
    if (const auto i = const_to_i64(v); i && !env->contains(Lnast_range::constant(*i))) {
      record_overflow(ssa_base_name(name), Lnast_range::constant(*i), *env, /*element=*/true);
      return false;
    }
    return true;
  };
  if (value.bundle->has_named_top() || value.bundle->unnamed_top_count() > 1) {
    for (const auto& [key, e] : value.bundle->non_attr_entries()) {
      if (!judge(e.trivial)) {
        return;
      }
    }
  } else if (value.name.empty()) {
    if (const auto v = value.bundle->scalar()) {
      (void)judge(*v);
    }
  }
}

void uPass_bitwidth::check_reg_init_fit(std::string_view var) {
  // declare(var, type, 'reg…', init): a register's initial (= reset) value is
  // a write into its declared type (user ruling 16), and a `reg` array's is a
  // write into every element. Unlike a `mut`, it rides the declare instead of
  // a store, so no store check sees it. A Verilog reader unit keeps its own
  // (raw bit pattern) initializers.
  const auto& ln = lm->get_lnast();
  if (runner_st == nullptr || !ln || ln->is_verilog_origin()) {
    return;
  }
  const auto decl = lm->get_current_nid();
  const auto vn   = ln->get_first_child(decl);
  const auto ty   = vn.is_invalid() ? vn : ln->get_sibling_next(vn);
  const auto mode = ty.is_invalid() ? ty : ln->get_sibling_next(ty);
  const auto init = mode.is_invalid() ? mode : ln->get_sibling_next(mode);
  if (init.is_invalid() || !Lnast_ntype::is_const(ln->get_type(mode)) || ln->get_name(mode).find("reg") == std::string_view::npos) {
    return;
  }
  std::vector<Dlop> values;
  if (Lnast_ntype::is_const(ln->get_type(init))) {
    values.push_back(*Dlop::from_pyrope(ln->get_name(init)));
  } else if (Lnast_ntype::is_ref(ln->get_type(init))) {
    const auto name = ln->get_name(init);
    if (const auto b = runner_st->get_bundle(name); b && (b->has_named_top() || b->unnamed_top_count() > 1)) {
      for (const auto& [key, e] : b->non_attr_entries()) {
        values.push_back(e.trivial);  // a per-entry initializer (a comptime tuple)
      }
    } else if (const auto v = runner_st->comptime_scalar(name)) {
      values.push_back(*v);
    }
  }
  const bool array = Lnast_ntype::is_comp_type_array(ln->get_type(ty));
  const auto env   = array ? array_elem_envelope_of(var) : declared_type_of(var);
  if (!env) {
    return;
  }
  for (const auto& v : values) {
    if (const auto i = const_to_i64(v); i && !env->contains(Lnast_range::constant(*i))) {
      record_overflow(var, Lnast_range::constant(*i), *env, array);
      return;
    }
  }
}

void uPass_bitwidth::process_type_spec() {
  // type_spec(ref(var), prim_type_int(max,min)) — the runner's declare
  // pre-step bakes envelopes only into EXISTING bindings; a bare type_spec
  // target (a tmp) is unbound in this pass's standalone runner (no constprop
  // creates it), so ensure the binding and bake the envelope here.
  if (runner_st == nullptr || !move_to_child()) {
    return;
  }
  const std::string var{current_text()};
  // A named target (not a `%` temp, e.g. an inlined comb's `inl1_a` param or
  // `inl1_o` output) is declared by this type.
  if (!var.empty() && var.front() != '%' && var.find("___ssa_") == std::string::npos) {
    typed_names_.insert(var);
  }
  std::optional<Dlop> dmax;
  std::optional<Dlop> dmin;
  if (move_to_sibling() && Lnast_ntype::is_prim_type_int(get_raw_ntype())) {
    if (move_to_child()) {
      if (Lnast_ntype::is_const(get_raw_ntype())) {
        if (auto v = Dlop::from_pyrope(current_text()); v->is_integer()) {
          dmax = *v;
        }
      }
      if (move_to_sibling() && Lnast_ntype::is_const(get_raw_ntype())) {
        if (auto v = Dlop::from_pyrope(current_text()); v->is_integer()) {
          dmin = *v;
        }
      }
      move_to_parent();
    }
  }
  move_to_parent();

  if (var.empty() || var.find('.') != std::string::npos || !dmax || !dmin) {
    return;
  }
  if (!runner_st->has_known(var)) {
    (void)runner_st->set(var, std::make_shared<Bundle>(var));
  }
  auto b = runner_st->get_bundle_for_write(var);
  if (!b || (!b->is_empty() && !b->has_trivial(bundle_path::of_string("0")))) {
    return;
  }
  Bundle::Entry e = b->get_entry(bundle_path::of_string("0"));
  e.immutable     = false;
  // WIDEN-ONLY against an existing declared envelope: a per-version inline
  // typespec (the `x = v#[lo..=hi]` bit-select force stamps the SSA version's
  // width) must not NARROW an explicitly declared/init envelope — `mut x =
  // 0ub????` is [0,15]; one if-arm writing `x = v#[0..=2]` would otherwise
  // restamp [0,7] and a sibling arm's legal wider write then fails the
  // declared-fit check.
  const auto old  = range_from_entry(e.decl_max, e.decl_min);
  if (!old.is_unbounded()) {
    if (auto nmax = const_to_i64(*dmax); nmax && old.max > *nmax) {
      dmax = *Dlop::from_pyrope(std::to_string(old.max));
    }
    if (auto nmin = const_to_i64(*dmin); nmin && old.min < *nmin) {
      dmin = *Dlop::from_pyrope(std::to_string(old.min));
    }
  }
  e.decl_max = *dmax;
  e.decl_min = *dmin;
  b->set(bundle_path::of_string("0"), std::move(e));
}
