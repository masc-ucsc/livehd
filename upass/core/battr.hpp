//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <initializer_list>
#include <string_view>

// battr — the RESIDUAL Bundle attribute-key vocabulary (1b/D).
//
// The always-present pass facts (kind, declared max/min, bw_max/bw_min,
// comptime; mode/typename at the Bundle level) are typed Entry/Bundle
// FIELDS — never string-keyed attrs. Only the genuinely rare/dynamic
// attributes live in the Bundle attr map, under these bare names (no "__").
// The per-pass attribute responsibilities below are the authoritative
// registry; keep this header in sync with it.
namespace battr {

inline constexpr std::string_view enumentry{"enumentry"};  // parse-time enum identity tag
inline constexpr std::string_view pub{"pub"};              // public-unit marker
inline constexpr std::string_view pub_unit{"pub_unit"};    // owning-unit name
inline constexpr std::string_view dp_assign{"dp_assign"};  // legacy := marker (cgen)
inline constexpr std::string_view debug{"_debug"};         // canonical sticky spelling of "debug"

// Stickiness is encoded in the canonical attr name: a leading '_' on the
// attr-name segment ("_foo"; the builtin alias "debug" stores as "_debug"
// via Bundle::canon_attr). No per-entry bit, no separate sticky map — and
// since '_' sorts apart from letter-named attrs under Canonical_less, the
// sticky subset is one contiguous run per level.
inline constexpr bool is_sticky(std::string_view attr_name) { return !attr_name.empty() && attr_name.front() == '_'; }

// The canonical set of BUILT-IN attribute names (Category A/B/C). A name here is
// a `.[name]` attribute, never a tuple/struct dot-field — used both to decide
// aggregate→field attr inheritance (uPass_attributes::is_builtin_attr delegates
// here) and to reject a `x.bits`-for-`x.[bits]` dot-field mistake on a scalar
// (constprop process_tuple_get). Keep in sync with the per-pass attribute
// registry.
inline bool is_builtin_attr_name(std::string_view name) {
  static constexpr std::initializer_list<std::string_view> names = {
      // Category A — LNAST/upass attrs
      "max", "min", "bw_max", "bw_min", "bits", "wrap", "saturate", "sat", "comptime", "const", "mut", "typename",
      "private", "size", "sign", "key", "crand", "rand", "loc", "file", "type",
      // Category B — LGraph wiring attrs
      "clock", "reset", "debug", "_debug", "async", "init", "clock_pin", "din", "enable", "negreset", "posclk",
      "reset_pin", "valid", "stop", "lat", "num", "addr", "fwd", "wensize", "rdport", "defer", "inputs", "outputs",
      // Category C — synthesis hints
      "critical", "delay", "donttouch", "keep", "inp_delay", "out_delay", "max_delay", "min_delay", "max_load",
      "max_fanout", "max_cap", "left_of", "right_of", "top_of", "bottom_of", "align_with"};
  return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace battr
