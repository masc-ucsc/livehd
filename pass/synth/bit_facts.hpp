// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <vector>

#include "hhds/graph.hpp"

// Bit-level facts about word-level values, used where a module boundary would
// otherwise hide them from the mapper (a lowered memory, an extracted ware
// shifter): the parent knows that part of an input is provably 0, the module
// body does not.
namespace livehd::synth::bit_facts {

// Which bits of the value `pin` drives are provably constant 0 (true = known
// zero), for a value read `width` bits wide. Structural and best effort: it
// looks through constants, Mux/Hotmux arms, And/Or, Concat lane layouts, a
// constant Get_mask window, a constant left shift, the low zeros of a sum or of a
// product with a constant (`idx * 32 + 32`) and the `cond * K` that cprop spells
// `cond ? K : 0` as; every other shape is unknown (false), so a miss costs
// nothing but a missed fold.
[[nodiscard]] std::vector<bool> known_zero_bits(const hhds::Pin_class& pin, int width, int depth = 0);

// `driver & mask` as a new And cell in `g`, where bit i of the constant mask is
// `keep[i]`; stamped with the width and sign of `driver`. Every kept bit equals
// the driver's; each cleared bit reads 0, so the mapper's bit-level dead-logic
// drop removes whatever only fed it.
// `color` (>= 0) puts the cell in that synthesis region, so it is mapped together with the logic it feeds.
[[nodiscard]] hhds::Pin_class mask_pin(hhds::Graph& g, const hhds::Pin_class& driver, const std::vector<bool>& keep, int color = -1);

// The text of `keep` as a hex string, for a module descriptor.
[[nodiscard]] std::string mask_text(const std::vector<bool>& keep);

}  // namespace livehd::synth::bit_facts
