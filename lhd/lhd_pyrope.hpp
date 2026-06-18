//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// `lhd pyrope <sub>` — the Pyrope developer tools that live next to the
// language rather than the compile/lec/synth flows:
//   lhd pyrope lsp        the Pyrope LSP server (JSON-RPC over stdio)
//   lhd pyrope fmt FILE…  the Pyrope source formatter (prpfmt), clang-format-like
//
// These run before the pass/inou engine is initialized: the LSP owns stdio for
// the JSON-RPC protocol, and `fmt` is a pure source->source transform that
// needs no graph engine. Neither writes the result envelope.

namespace lhd {
struct Options;
}

namespace livehd::pyrope {

// Dispatch a `pyrope` sub-command. Returns the process exit code.
int run(const lhd::Options& opts);

}  // namespace livehd::pyrope
