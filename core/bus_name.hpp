//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// LiveHD bus-expansion naming standard.
//
// Whenever a pass splits ONE named multi-bit object (a register, a latch, a
// net, a memory) into several named pieces, the pieces are named the way a
// Verilog bit-select / yosys `splitnets` names them:
//
//   bit i of bus `x`                 x[i]          (`id_q` bit 3 -> `id_q[3]`)
//   entry i of memory/array `m`      m[i]
//   bit j of entry i of `m`          m[i][j]       (outermost dimension first)
//   a piece of an already-bracketed  x[2][i]       (append one more dimension;
//   name `x[2]`                                     never rewrite the base)
//
// * The index is the plain decimal bit/entry ordinal, LSB/entry 0 = `[0]`, no
//   sign, no leading zeros, no ranges.
// * The hierarchy separator is unchanged: bit 2 of `a.b.q` is `a.b.q[2]`.
// * A one-bit object keeps its plain name -- nothing was expanded.
// * In emitted Verilog the name is an escaped identifier (`\a.b.q[2] `): the
//   brackets are part of ONE identifier, not a select. cgen's get_scaped_name
//   already escapes any name with a non-[A-Za-z0-9_] character.
// * A collision uniquifier (`__dup<N>`, `_cgen<N>`) is not an index and is
//   never spelled with brackets.
//
// Readers (semdiff state pairing, the LEC bit-blast bridge) reconstruct the
// bus from these names with parse_bus_piece(). A standard-cell netlist that
// is read back WITH its cell models inlined carries the model's internal state
// element one level down (`x[3].flop_16`, `x[3].IQ`): parse_bus_piece accepts
// exactly one such trailing hierarchical segment when asked to. The
// reconstructed correspondence is a hint; every consumer re-verifies it.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace livehd::bus_name {

// `base[index]` -- the name of bit (or entry) `index` of `base`.
[[nodiscard]] std::string piece(std::string_view base, int64_t index);

// Aliases that document intent at the minting site.
[[nodiscard]] inline std::string bit(std::string_view base, int64_t index) { return piece(base, index); }
[[nodiscard]] inline std::string entry(std::string_view mem, int64_t index) { return piece(mem, index); }
[[nodiscard]] inline std::string entry_bit(std::string_view mem, int64_t entry_index, int64_t bit_index) {
  return piece(piece(mem, entry_index), bit_index);
}

struct Piece {
  std::string_view base;    // everything before the LAST `[<digits>]` group (may itself end in `]`)
  int64_t          index = 0;
  std::string_view suffix;  // the trailing cell-model state segment WITHOUT its '.', empty when none
};

// Split a standard per-bit name. `name` must already be free of reader quoting
// (backticks / Verilog's leading backslash). Returns nullopt unless `name` is
// `<base>[<digits>]` (or, with allow_model_suffix, `<base>[<digits>].<seg>`
// where <seg> is a single non-empty segment containing none of `.[]`).
// `separator` is the hierarchy separator in `name`: '.' for a hierarchical
// name, '_' for a key that already flattened '.' to '_' (LEC's
// canon_flop_name, where `x[3].flop_16` reads `x[3]_flop_16`). Either way the
// model segment is everything after the separator that follows the LAST `]`.
[[nodiscard]] std::optional<Piece> parse_bus_piece(std::string_view name, bool allow_model_suffix = false, char separator = '.');

// The register a read-back cell model's state belongs to, for a register that
// was NOT split (a one-bit register keeps its plain name, so its cell reads
// back as `r.flop_16` / `r.IQ`). Returns the owner `r` (a prefix of `name`),
// or nullopt unless `name` is `<owner>.<seg>` with a single non-empty <seg>
// free of `.[]`. When the emitted instance collided with a net of the same
// name (typically the output port the register drives), cgen uniquified it as
// `r_cgen<N>`; that emission-only suffix is removed as well.
[[nodiscard]] std::optional<std::string_view> cell_state_owner(std::string_view name);

}  // namespace livehd::bus_name
