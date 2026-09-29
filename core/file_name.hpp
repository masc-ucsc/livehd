// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace livehd {

// The ONE long-name policy for every generated per-unit file: Verilog (`.v`
// plus its `.v.map` sidecar), Pyrope (`.prp`), LNAST dumps (`.lnast`) and the
// simulation sources, plus every manifest that lists them and every reader
// that opens one back.
//
// Generated names are not bounded by anything the user typed: a parameter
// specialization is spelled `mod_<param>_<param>...` and CIRCT/Chisel output
// routinely runs past 250 characters, while NAME_MAX is 255 bytes on every
// supported filesystem -- so the emit either has to shorten or die with
// ENAMETOOLONG halfway through a design.
//
// `name` is one file-name COMPONENT: directory separators must already be
// collapsed by the caller (they carry meaning the mapping cannot preserve).
// A name of at most kMaxFileNameBytes passes through untouched -- the common
// case stays readable and greppable. A longer one becomes
//
//     <head>_<64 lowercase hex characters of SHA-256(name)><.ext>
//
// exactly kMaxFileNameBytes bytes long, where `.ext` is the input's trailing
// extension when that is a `.` followed by 1-4 characters (`.v`, `.prp`,
// `.core`) and empty otherwise -- a Pyrope import spelled `../lib/core.core`
// keeps reading as a `.core` file. The prefix never splits a UTF-8 code
// point, so the result is still valid text (that trim can leave the name a
// few bytes SHORTER than the cap).
//
// Callers append their own suffix (".v", ".v.map", ".prp", ".lnast") AFTER
// this, so one unit always maps to one stem and its sidecars stay beside it.
// kMaxFileNameBytes leaves 55 bytes of headroom under NAME_MAX for those.
inline constexpr size_t kMaxFileNameBytes = 200;

[[nodiscard]] std::string shorten_file_name(std::string_view name);

// `shorten_file_name` after collapsing '/' and '\\' to '_'. This is the full
// graph-name -> file-name-stem mapping; use it wherever a generated file is
// named after a graph or an LNAST unit.
//
// NOTE: the mapping is not injective -- `a/b.c` and `a_b.c` share a stem.
// Two co-emitted units spelled that way silently overwrite each other.
[[nodiscard]] std::string unit_file_stem(std::string_view name);

}  // namespace livehd
