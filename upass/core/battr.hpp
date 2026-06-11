//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

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
inline constexpr std::string_view pub{"pub"};              // task 1m public-unit marker
inline constexpr std::string_view pub_unit{"pub_unit"};    // task 1m owning-unit name
inline constexpr std::string_view dp_assign{"dp_assign"};  // legacy := marker (cgen)
inline constexpr std::string_view debug{"_debug"};         // canonical sticky spelling of "debug"

// Stickiness is encoded in the canonical attr name: a leading '_' on the
// attr-name segment ("_foo"; the builtin alias "debug" stores as "_debug"
// via Bundle::canon_attr). No per-entry bit, no separate sticky map — and
// since '_' sorts apart from letter-named attrs under Canonical_less, the
// sticky subset is one contiguous run per level.
inline constexpr bool is_sticky(std::string_view attr_name) { return !attr_name.empty() && attr_name.front() == '_'; }

}  // namespace battr
