// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>
#include <vector>

#include "hhds/graph.hpp"

// Bit-level facts about word-level values, used where a boundary would
// otherwise hide them: pass.specialize carries them across instance boundaries
// for every flow (compile, sim, synth), and synthesis across its colors, a
// lowered memory and an extracted ware shifter. The parent knows that part of
// an input is provably constant, the body on the other side does not.
// (Namespace kept from its first home in pass/synth.)
namespace livehd::synth::bit_facts {

// Which bits of the value `pin` drives are provably constant 0 (true = known
// zero), for a value read `width` bits wide. Structural and best effort: it
// looks through constants, Mux/Hotmux arms, And/Or, Concat lane layouts, a
// constant Get_mask window, a constant left shift, the low zeros of a sum or of a
// product with a constant (`idx * 32 + 32`) and the `cond * K` that cprop spells
// `cond ? K : 0` as; every other shape is unknown (false), so a miss costs
// nothing but a missed fold. A query inspects a bounded number of pins (the walk is not
// memoized), after which the remaining shapes are unknown too.
[[nodiscard]] std::vector<bool> known_zero_bits(const hhds::Pin_class& pin, int width);

// Both polarities: `zero` as above, `one` the bits provably 1 (never both). Ones are claimed only for an unsigned
// value, inside its declared width, through constants, And/Or, Mux arms, Concat lanes, a constant left shift and
// constant Get_mask / Set_mask windows.
struct Known {
  std::vector<bool> zero, one;
};
[[nodiscard]] Known known_bits(const hhds::Pin_class& pin, int width);

// `driver` with its known bits spelled out: `(driver & keep) | ones`, where keep clears every known bit; the plain
// constant when every bit is known. `known` must be `known_bits(driver, w)` for the w the readers see, and the driver
// unsigned when it has known ones. `color` as in mask_pin.
[[nodiscard]] hhds::Pin_class known_pin(hhds::Graph& g, const hhds::Pin_class& driver, const Known& known, int color = -1);

// `driver & mask` as a new And cell in `g`, where bit i of the constant mask is
// `keep[i]`; stamped with the width and sign of `driver`. Every kept bit equals
// the driver's; each cleared bit reads 0, so the mapper's bit-level dead-logic
// drop removes whatever only fed it.
// `color` (>= 0) puts the cell in that synthesis region, so it is mapped together with the logic it feeds.
[[nodiscard]] hhds::Pin_class mask_pin(hhds::Graph& g, const hhds::Pin_class& driver, const std::vector<bool>& keep,
                                       int color = -1);

// The text of `keep` as a hex string, for a module descriptor.
[[nodiscard]] std::string mask_text(const std::vector<bool>& keep);

}  // namespace livehd::synth::bit_facts
