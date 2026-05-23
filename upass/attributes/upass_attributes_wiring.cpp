//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Category-B LGraph-wiring attribute handlers.
//
// The attribute pass's job for cat-B is "preserve / lower" rather than
// "consume". The bulk of the actual
// connection work happens at LGraph generation time (which reads
// attr_set_values as the side-map). The attribute pass does the upass-
// side bookkeeping:
//
//   * record the attribute on the side-map (already done by
//     process_attr_set's generic path),
//   * apply per-attribute validation that's only knowable while the LNAST
//     is still in scope (e.g. posclk / structural mode attrs must be
//     comptime-known).
//
// All handlers here are intentionally light: they carry per-attribute
// docstrings that future expansion can hang lowering / validation code
// off of without touching the dispatch wiring. This is the scaffold the
// spec asks for; the actual register/memory pin emission lives in the
// LNAST→LGraph pass, not here.

#include <string>
#include <string_view>

#include "upass_attributes.hpp"
#include "upass_attributes_handler.hpp"

namespace upass {
namespace attributes {

// Pin-name handler: clock_pin / reset_pin / din / addr / enable / wensize /
// rdport / fwd / lat / num. process_attr_set already stores ref values in
// attr_set_refs and constants in attr_set_values, which is what LGraph
// generation reads. The handler exists so future per-attribute checks
// (e.g. clock_pin must reference a runtime wire, not a comptime constant)
// have a registered hook.
class Pin_handler : public Attribute_handler {};

// Signal-classification handler: clock / reset. Same scaffold; LGraph
// generation interprets the classification. The sticky handler already
// covers `_*` sticky propagation; clock/reset are not sticky.
class Signal_class_handler : public Attribute_handler {};

// Mode-selection handler: posclk / async / negreset / type (memory) /
// initial. Per spec these must be comptime-known; process_attr_set's
// shared path stores them as Const values, which means a non-comptime
// expression operand becomes Dlop::invalid() and is silently dropped
// today. A future tightening can fail loudly here once the cassert
// coverage exists.
class Mode_handler : public Attribute_handler {};

}  // namespace attributes
}  // namespace upass

// Registration entry point — invoked from uPass_attributes' constructor.
void uPass_attributes_register_wiring(uPass_attributes& self) {
  using namespace upass::attributes;
  auto  pin    = std::make_shared<Pin_handler>();
  auto  signal = std::make_shared<Signal_class_handler>();
  auto  mode   = std::make_shared<Mode_handler>();
  auto& reg    = self.registry();

  // Pin-name attributes (memory / register port wiring).
  for (const char* name : {"clock_pin", "reset_pin", "din", "addr", "enable", "wensize", "rdport", "fwd", "lat", "num"}) {
    reg.register_exact(name, pin);
  }
  // Signal-classification attributes.
  for (const char* name : {"clock", "reset"}) {
    reg.register_exact(name, signal);
  }
  // Mode / structural attributes (must be comptime-known; LGraph generation
  // bakes them into the node mode / pin shape).
  for (const char* name : {"posclk", "async", "initial", "negreset", "valid", "stop", "defer"}) {
    reg.register_exact(name, mode);
  }
}
