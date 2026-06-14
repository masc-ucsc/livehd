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
  const Bundle::Entry& e     = (b && !(field.empty() && multi)) ? b->get_entry(field.empty() ? std::string_view{"0"} : field)
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
      ti.bits = ti.range_max->is_known_zero() ? 0 : static_cast<uint32_t>(ti.range_max->get_bits() - 1);
    } else {
      ti.bits = static_cast<uint32_t>(std::max<int64_t>(ti.range_max->get_bits(), ti.range_min->get_bits()));
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
  const bool  bare_tmp = field.empty() && root.size() >= 3 && root.substr(0, 3) == "___";
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
          ti.bits = ti.range_max->is_known_zero() ? 0 : static_cast<uint32_t>(ti.range_max->get_bits() - 1);
        } else {
          ti.bits = static_cast<uint32_t>(std::max<int64_t>(ti.range_max->get_bits(), ti.range_min->get_bits()));
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
  // range when the port pins both bounds (has_range). Carrying the range — not
  // just `bits` — lets downstream max/min derivation and overload range-fit use
  // the precise bounds instead of a power-of-two `bits` window (review cat 1 R1).
  if (ti.kind == Num::none && ti.bits == 0 && ln != nullptr) {
    const auto& io         = ln->io_meta();
    auto        merge_port = [&](const Lnast_io_entry& pe) {
      if (pe.name != var) {
        return false;
      }
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
        }
      }
      any = true;
      return true;
    };
    bool hit = false;
    for (const auto& pe : io.inputs) {
      if (merge_port(pe)) {
        hit = true;
        break;
      }
    }
    if (!hit) {
      for (const auto& pe : io.outputs) {
        if (merge_port(pe)) {
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

}  // namespace upass::decl_facts
