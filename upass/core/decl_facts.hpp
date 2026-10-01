#pragma once
// Declared-fact derivation over the runner-owned Symbol_table.
//
// The single source of truth for "what was `name` declared as": typed Entry
// fields written by the runner's declare/type_spec bake (+ per-field rides
// and back-flows), per-field "fmode"/"fcomptime" residual attrs (bundle-
// valued fields have no Entry), the pending dotted-decl stash (a type_spec
// that precedes the field's first value write), io_meta (typed ports are
// never table-backed values), and the explicit `[bits=N]` attr value.
//
// uPass_attributes::lookup_type_info_bundle delegates here, and the runner /
// constprop consume it directly — replacing the provide_decl_type /
// provide_field_type / provide_decl_storage / runner_type_query_fn pull
// seams (deleted — facts flow through the table, nothing pulls).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "hlop/dlop.hpp"
#include "kind.hpp"
#include "lnast.hpp"
#include "symbol_table.hpp"

namespace upass::decl_facts {

enum class Num : uint8_t { none, unsigned_int, signed_int, boolean, string };

// Canonical decl_facts::Num -> Io_kind projection. Several passes mapped this by
// hand (runner try_field_type, constprop scalar_type_query_of, …); share one so
// they agree. `range`-only facts (no explicit Num) are integers — pass
// has_range to fold that in.
inline Io_kind io_kind_from_num(Num n, bool has_range = false) {
  switch (n) {
    case Num::unsigned_int:
    case Num::signed_int  : return Io_kind::integer;
    case Num::boolean     : return Io_kind::boolean;
    case Num::string      : return Io_kind::string;
    case Num::none        : return has_range ? Io_kind::integer : Io_kind::none;
  }
  return Io_kind::none;
}

struct Facts {
  upass::Mode          mode{upass::Mode::unknown};
  Num                  kind{Num::none};
  uint32_t             bits{0};
  bool                 is_comptime{false};
  bool                 has_type_spec{false};
  std::optional<Dlop> range_max;
  std::optional<Dlop> range_min;
};

// Returns nullopt when nothing was declared for `var`. `ln` may be null
// (no io_meta merge then).
inline std::optional<Facts> lookup(const Symbol_table& st, const Lnast* ln, std::string_view var) {
  if (var.empty()) {
    return std::nullopt;
  }
  const auto root  = Bundle::get_first_level(var);
  const auto field = Bundle::get_all_but_first_level(var);
  const auto b     = st.get_bundle(root);

  static const Bundle::Entry kEmpty_entry;
  // A multi-shaped bundle (named top / >1 positional) has no scalar self:
  // its "0" entry is FIELD ZERO, not the variable's own facts.
  const bool           multi = b && (b->has_named_top() || b->unnamed_top_count() > 1);
  const Bundle::Entry& e     = (b && !(field.empty() && multi))
                                   ? b->get_entry(bundle_path::of_string(field.empty() ? std::string_view{"0"} : field))
                                   : kEmpty_entry;

  Facts ti;
  bool  any = false;
  switch (e.kind) {
    case upass::Kind::boolean:
      ti.kind          = Num::boolean;
      ti.has_type_spec = true;
      any              = true;
      break;
    case upass::Kind::string:
      ti.kind          = Num::string;
      ti.has_type_spec = true;
      any              = true;
      break;
    case upass::Kind::integer:
      ti.has_type_spec = true;  // `:int` annotated; signedness derives from range_min below
      any              = true;
      break;
    default: break;
  }
  if (!e.decl_max.is_invalid()) {
    ti.range_max     = e.decl_max;
    ti.has_type_spec = true;
    any              = true;
  }
  if (!e.decl_min.is_invalid()) {
    ti.range_min     = e.decl_min;
    ti.has_type_spec = true;
    any              = true;
  }
  // Mirror the legacy scalar-type read: signedness from range_min; bits from
  // the bound widths (signed width; drop the sign bit for unsigned).
  if (ti.range_min && ti.kind != Num::boolean && ti.kind != Num::string) {
    ti.kind = ti.range_min->is_negative() ? Num::signed_int : Num::unsigned_int;
  }
  if (ti.range_max && ti.range_min && ti.range_max->is_integer() && ti.range_min->is_integer()) {
    if (!ti.range_min->is_negative()) {
      ti.bits = static_cast<uint32_t>(ti.range_max->get_payload_bits());
    } else {
      ti.bits = static_cast<uint32_t>(std::max<int64_t>(ti.range_max->get_signed_bits(), ti.range_min->get_signed_bits()));
    }
  }
  if (e.comptime) {
    ti.is_comptime = true;
    any            = true;
  }
  // Per-field Entry.mode first; binding-level mode for bare names; bundle-
  // valued fields carry mode/comptime as per-field attrs (no entry). A bare
  // ___ tmp is never a DECLARED name — mode riding its binding via value
  // copies / carrier sharing must not read back as a storage class.
  // A bare compiler temp (`%`-prefixed; see Lnast::is_tmp).
  const bool  bare_tmp = field.empty() && !root.empty() && root[0] == '%';
  upass::Mode m        = bare_tmp ? upass::Mode::unknown : e.mode;
  if (b && m == upass::Mode::unknown && field.empty() && !bare_tmp) {
    m = b->get_mode();
  }
  if (b && !field.empty()) {
    if (m == upass::Mode::unknown) {
      if (const auto& fm = b->get_attr(field, "fmode"); !fm.is_invalid() && fm.is_just_i64()) {
        m = static_cast<upass::Mode>(fm.to_just_i64());
      }
    }
    if (!ti.is_comptime) {
      if (const auto& fc = b->get_attr(field, "fcomptime"); !fc.is_invalid() && !fc.is_known_false()) {
        ti.is_comptime = true;
        any            = true;
      }
    }
  }
  if (m != upass::Mode::unknown) {
    ti.mode = m;
    any     = true;
  }
  // Not-yet-applied dotted decl facts (dotted type_spec before the field's
  // first value write — e.g. the inliner's typed tuple-param prologue).
  if (!any && !field.empty()) {
    if (auto pit = st.pending_decl_facts.find(std::string(var)); pit != st.pending_decl_facts.end()) {
      const auto& pf = pit->second;
      switch (pf.kind) {
        case upass::Kind::boolean: ti.kind = Num::boolean; ti.has_type_spec = true; any = true; break;
        case upass::Kind::string : ti.kind = Num::string; ti.has_type_spec = true; any = true; break;
        case upass::Kind::integer: ti.has_type_spec = true; any = true; break;
        default: break;
      }
      if (!pf.decl_max.is_invalid()) {
        ti.range_max     = pf.decl_max;
        ti.has_type_spec = true;
        any              = true;
      }
      if (!pf.decl_min.is_invalid()) {
        ti.range_min     = pf.decl_min;
        ti.has_type_spec = true;
        any              = true;
      }
      if (ti.range_min && ti.kind != Num::boolean && ti.kind != Num::string) {
        ti.kind = ti.range_min->is_negative() ? Num::signed_int : Num::unsigned_int;
      }
      if (ti.range_max && ti.range_min && ti.range_max->is_integer() && ti.range_min->is_integer()) {
        if (!ti.range_min->is_negative()) {
          ti.bits = static_cast<uint32_t>(ti.range_max->get_payload_bits());
        } else {
          ti.bits = static_cast<uint32_t>(std::max<int64_t>(ti.range_max->get_signed_bits(), ti.range_min->get_signed_bits()));
        }
      }
      if (pf.comptime) {
        ti.is_comptime = true;
        any            = true;
      }
      switch (pf.mode) {
        case upass::Mode::unknown: break;
        default                  : ti.mode = pf.mode; any = true; break;
      }
    }
  }
  // Explicit `[bits=N]` attr (no signedness) — field attr for dotted names,
  // whole-bundle attr for bare ones (builtin attrs never inherit root→field).
  if (ti.bits == 0 && b) {
    const auto& bv = field.empty() ? b->get_attr("bits") : b->get_attr(field, "bits");
    if (!bv.is_invalid() && bv.is_just_i64()) {
      ti.bits          = static_cast<uint32_t>(bv.to_just_i64());
      ti.has_type_spec = true;
      any              = true;
    }
  }
  // io PORT facts come from io_meta: kind+bits, plus the EXACT int(min,max)
  // range when the port pins both bounds (has_range, or wide_range_min/max past
  // an i64). Carrying the range — not
  // just `bits` — lets downstream max/min derivation and overload range-fit use
  // the precise bounds instead of a power-of-two `bits` window (review cat 1 R1).
  if (ti.kind == Num::none && ti.bits == 0 && ln != nullptr) {
    auto        merge_port = [&](const Lnast_io_entry& pe) {
      if (pe.bits == 0 && pe.kind != Io_kind::boolean && !pe.has_range) {
        return false;  // unbounded/untyped (template port) — nothing to pin
      }
      ti.has_type_spec = true;
      if (pe.kind == Io_kind::boolean) {
        ti.kind = Num::boolean;
        ti.bits = 1;
      } else {
        ti.kind = pe.is_signed ? Num::signed_int : Num::unsigned_int;
        ti.bits = static_cast<uint32_t>(pe.bits);
        if (pe.has_range) {
          ti.range_min = *Dlop::create_integer(pe.range_min);
          ti.range_max = *Dlop::create_integer(pe.range_max);
          ti.kind      = pe.range_min < 0 ? Num::signed_int : Num::unsigned_int;
        } else if (pe.wide_range_min && pe.wide_range_max) {
          // The exact bounds of a port past an i64 (a 62+-bit unsigned or
          // 63+-bit signed one), never its `bits` window.
          ti.range_min = *pe.wide_range_min;
          ti.range_max = *pe.wide_range_max;
          ti.kind      = pe.wide_range_min->is_negative() ? Num::signed_int : Num::unsigned_int;
        }
      }
      any = true;
      return true;
    };
    // The index resolves inputs before outputs, matching the pre-index scan.
    // That scan also FELL THROUGH to the output list when the name-matching
    // input was itself untyped/unbounded, so keep that second chance: a leaf
    // that is an untyped input and a typed output must still pin its range.
    const auto* pe = ln->io_meta().find(var);
    if (pe != nullptr && !merge_port(*pe)) {
      for (const auto& oe : ln->io_meta().outputs) {
        if (oe.name == var && merge_port(oe)) {
          break;
        }
      }
    }
  }
  if (!any) {
    return std::nullopt;
  }
  return ti;
}

// The kind a symbol-table value carries. Shape FIRST: a multi-entry /
// named-field bundle is a tuple regardless of what field 0's entry-kind says
// (constprop copies field entries wholesale, so `(a<b, a>b)`'s slot 0 carries
// the lt tmp's boolean kind -- that describes the FIELD, not the bundle). Then
// the bundle-level value kind (producer-stamped; covers 0/1-entry tuples the
// shape can't express). Last, the "0" Entry's kind (declared by the runner's
// bake or preserved through value writes). Shared by typecheck and the
// runner's call-argument kind check so the two agree.
inline upass::Kind bundle_kind(const Bundle& b) {
  if (b.has_named_top() || b.unnamed_top_count() > 1) {
    return upass::Kind::tuple;
  }
  if (b.get_value_kind() != upass::Kind::unknown) {
    return b.get_value_kind();
  }
  return b.get_entry(bundle_path::of_string("0")).kind;
}

// The declared facts of an OPERAND: `name` itself, or, for a tuple_get temp
// (`child.o`, `t.f`, which has no type of its own), the field it was read
// from; a bare single-output Sub instance handle (`const t = nz(a=x)`, or a
// chained call's result temp) IS its one output and reads that port's facts.
inline std::optional<Facts> lookup_operand(const Symbol_table& st, const Lnast* ln, std::string_view name) {
  auto f = lookup(st, ln, name);
  // A user variable with a declared type of its own (`mut m:s8 = b`) holds its
  // value converted to that type, declared once on the base name and read
  // through any SSA version; only an untyped name reads through its origin.
  if (!Lnast::is_tmp(name) && !name.starts_with("___")) {
    const auto typed = [](const std::optional<Facts>& t) { return t && t->has_type_spec && t->kind != Num::none; };
    if (typed(f)) {
      return f;
    }
    if (const auto p = name.find("___ssa_"); p != std::string_view::npos) {
      if (auto bf = lookup(st, ln, name.substr(0, p)); typed(bf)) {
        return bf;
      }
    }
  }
  if (const auto origin = st.tget_origin.find(name); origin != st.tget_origin.end()) {
    if (auto of = lookup(st, ln, origin->second); of && of->kind != Num::none) {
      return of;
    }
  }
  if (!f || f->kind == Num::none) {
    if (const auto so = st.single_output_port.find(name); so != st.single_output_port.end()) {
      if (auto of = lookup(st, ln, absl::StrCat(name, ".", so->second)); of && of->kind != Num::none) {
        return of;
      }
    }
  }
  return f;
}

// The DECLARED kind of an operand whose value carries none: an IO port (its
// type rides io_meta, never the table) or a Sub instance output (the callee's
// declared port, stashed by the runner). A signed instance output is a range
// only (Symbol_table::sub_output_ranges), still an integer. `unknown` when
// nothing declared it.
inline upass::Kind operand_kind(const Symbol_table& st, const Lnast* ln, std::string_view name) {
  if (ln != nullptr) {
    if (const auto* pe = ln->io_meta().find(name); pe != nullptr && pe->array_size != 0) {
      return upass::Kind::unknown;  // an ARRAY port: its io kind is the element's, not the port's
    }
  }
  if (const auto f = lookup_operand(st, ln, name)) {
    switch (io_kind_from_num(f->kind, f->range_max || f->range_min)) {
      case Io_kind::boolean: return upass::Kind::boolean;
      case Io_kind::string : return upass::Kind::string;
      case Io_kind::integer: return upass::Kind::integer;
      case Io_kind::none   : break;
    }
  }
  if (const auto origin = st.tget_origin.find(name);
      origin != st.tget_origin.end() && st.sub_output_range(origin->second) != nullptr) {
    return upass::Kind::integer;
  }
  if (const auto h = st.sub_output_ranges.find(name);
      h != st.sub_output_ranges.end() && h->second.size() == 1 && st.single_output_port.contains(name)) {
    return upass::Kind::integer;
  }
  return upass::Kind::unknown;
}

}  // namespace upass::decl_facts
