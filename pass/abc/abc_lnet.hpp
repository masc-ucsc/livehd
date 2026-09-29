// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Lnet -> ABC (abc_cleanup.md section 4): the ABC netlist of an Lnet, built
// object for object in the Lnet's creation order (REPLAY), so the network ABC
// sees is the one the blaster would have built directly: the same object ids,
// names and fanin order. Every latch input is connected at the end.
#include <string_view>

#include "lnet.hpp"

namespace livehd::abc {

enum class Lnet_names : uint8_t {
  // A mapped region: an input's net takes its Lnet name (none when empty), an
  // output is a PO behind a uniquely named net alias of its driver (ABC's
  // netlist checker requires unique CO net names).
  region,
  // A proof: an input's net is named `i<pi object id>`, an output is a PO
  // named by the Lnet reading its driver net directly.
  proof,
};

// A new ABC_NTK_NETLIST / ABC_FUNC_AIG network (Abc_Ntk_t*, opaque here) named
// `name`, not yet finalized. A LUT with other than the RAW gates' tables is
// built as a Shannon expansion over its fanins.
[[nodiscard]] void* lnet_to_abc(const synth::Lnet& net, std::string_view name, Lnet_names names = Lnet_names::region);

// A new ABC_NTK_LOGIC / ABC_FUNC_SOP network over `skeleton`'s CIs and COs
// (Abc_Ntk_t*, its names, latches and timing kept): Lnet input i is CI i,
// latch k is CI inputs+k, and CO j reads combinational output j (outputs,
// then latch inputs). A LUT is one SOP node -- its Lnet::sop when it has one,
// else its on-set minterms -- an inverter an ABC inverter, a buffer a wire.
// Null when the boundaries differ.
[[nodiscard]] void* lnet_into_logic(const synth::Lnet& net, void* skeleton);

}  // namespace livehd::abc
