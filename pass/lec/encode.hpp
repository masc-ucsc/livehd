// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "smt-switch/smt.h"

namespace livehd::lec {

// L0 encoder: turns one *combinational* LiveHD LGraph (hhds::Graph) into
// smt-switch bit-vector terms, mirroring inou/cgen's process_simple_node
// semantics. It is deterministic and side-effect free on the graph.
//
// A signal value is a bit-vector `term` of `width` bits (the real bus width,
// `is_unsign(pin) ? bits_of(pin)-1 : bits_of(pin)`), plus the `is_signed`
// flag that drives width-extension and signed/unsigned comparison & shift.
struct Val {
  smt::Term term;
  int       width     = 0;
  bool      is_signed = false;
};

struct Encoded {
  bool        ok = true;
  std::string error;  // first unsupported-op / structural error, when !ok

  // Graph IO, by declared port name.
  absl::flat_hash_map<std::string, Val> inputs;
  absl::flat_hash_map<std::string, Val> outputs;
};

// Real bus width of a pin (signed magnitude+1 count; unsigned drops the spare
// sign bit). 0 means "unknown / no bits attribute". See lec.md "Bit-width trap".
int real_width(const hhds::Pin_class& pin);
int real_width_io(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view name);

// Extend (sign/zero per v.is_signed) or truncate `v` to exactly `width` bits.
smt::Term fit_to(const smt::SmtSolver& solver, const Val& v, int width);

class Encoder {
public:
  explicit Encoder(const smt::SmtSolver& solver) : solver_(solver) {}

  // Encode the combinational logic of `g`.
  //
  // `shared_inputs` (optional): a map from input-port name to an already-built
  // bit-vector term. Inputs whose name is present reuse that term (this is how
  // a miter across two designs shares one symbol per primary input); inputs not
  // present get a fresh `make_symbol` named "<prefix><name>".
  //
  // On any unsupported op or sequential element (Flop/Memory/Sub), the returned
  // Encoded has ok=false and a populated `error` (the encoder never silently
  // produces a wrong term).
  Encoded encode(hhds::Graph* g, const absl::flat_hash_map<std::string, Val>* shared_inputs = nullptr,
                 std::string_view prefix = "");

private:
  // Fit `v` to exactly `width` bits: sign/zero-extend (per v.is_signed) when
  // widening, low-bit Extract when narrowing.
  smt::Term fit(const Val& v, int width);

  // Common working width for combining a set of operands with an output pin.
  smt::Sort bv(int width);

  smt::SmtSolver solver_;
};

}  // namespace livehd::lec
