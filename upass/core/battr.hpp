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
// (constprop process_tuple_get).
//
// THIS LIST IS THE ONE ATTRIBUTE VOCABULARY. The Pyrope front end
// (Prp2lnast::reject_common_mistakes_attr_name) used to keep a hand-copied
// second array because inou/prp cannot link the upass/core plugin; the two
// drifted apart in both directions (`inputs`/`outputs` accepted but dead here,
// `ubits`/`sbits` accepted but removed there, `defer` outliving the feature).
// The copy is gone: inou/prp depends on the header-only //upass/core:battr_hdr
// target and calls this predicate, so a name added here is known to BOTH the
// parser's did-you-mean diagnostics and the upass attribute machinery.
//
// A name here is ALSO refused as a plain dot-field on a scalar, so a spelling
// that is a common tuple FIELD name (`name`, `key` on a user struct) must not
// be added lightly — see the `name` note in upass/tolg (reg `name=` override),
// which is deliberately NOT registered here for exactly that reason.
inline bool is_builtin_attr_name(std::string_view name) {
  static constexpr std::initializer_list<std::string_view> names
      = {// Category A — LNAST/upass attrs
         "max",
         "min",
         "bw_max",
         "bw_min",
         "bits",
         "wrap",
         "sat",
         "comptime",
         "const",
         "mut",
         "typename",
         "private",
         "size",
         "sign",
         "key",
         "id",
         "fields",
         "inp",
         "out",
         "crand",
         "rand",
         "type",
         // Category B — LGraph wiring attrs
         "clock",
         "reset",
         "debug",
         "_debug",
         "async",
         "initial",
         "clock_pin",
         "din",
         "enable",
         "negreset",
         "posclk",
         // `enable_high` is the LATCH spelling of `posclk` (2f-latch M2): on a Latch
         // pid 6 is the enable POLARITY, not a clock edge.
         "enable_high",
         // `latch` is a DECLARATION-MODE marker (`reg x:T:[latch=true]` — the
         // grammar has no `latch` keyword, prp2lnast.cpp:1900). It is consumed
         // at declaration and is NOT readable back as `.[latch]`; listed so the
         // vocabulary is complete.
         "latch",
         "reset_pin",
         "valid",
         "stop",
         "lat",
         "num",
         "addr",
         "fwd",
         "wensize",
         "rdport",
         // Memory same-cycle read/write ordering
         // ("program"|"fwd"|"old"|"none"); `fwd` above is its deprecated
         // low-level (explicit-matrix) predecessor.
         "ordering",
         // Category C — synthesis hints
         "critical",
         "delay",
         "donttouch",
         "keep",
         "inp_delay",
         "out_delay",
         "max_delay",
         "min_delay",
         "max_load",
         "max_fanout",
         "max_cap",
         "left_of",
         "right_of",
         "top_of",
         "bottom_of",
         "align_with"};
  return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace battr
