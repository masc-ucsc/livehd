// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cvc5/cvc5.h>

#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::lec {

// L0 encoder: turns one *combinational* LiveHD LGraph (hhds::Graph) into cvc5
// bit-vector terms, mirroring inou/cgen's process_simple_node semantics. It is
// deterministic and side-effect free on the graph.
//
// A signal value is a bit-vector `term` of `width` bits (the real bus width,
// `is_unsign(pin) ? bits_of(pin)-1 : bits_of(pin)`), plus the `is_signed`
// flag that drives width-extension and signed/unsigned comparison & shift.
struct Val {
  cvc5::Term term;
  int        width     = 0;
  bool       is_signed = false;
};

struct Encoded {
  bool        ok = true;
  std::string error;  // first unsupported-op / structural error, when !ok

  // Graph IO, by declared port name.
  absl::flat_hash_map<std::string, Val> inputs;
  absl::flat_hash_map<std::string, Val> outputs;

  // M4 memory state. Each Memory cell is cut like a Flop: its current contents
  // are an SMT array symbol (shared across the two designs by mem_state_key, so
  // corresponding memories "collapse"), and its post-cycle contents are the
  // next-state array. Read douts are ordinary BV terms in `outputs`/pin2val.
  absl::flat_hash_map<std::string, cvc5::Term> next_mem;  // key -> next-state array

  // Side constraints the caller must assert (EQUAL lhs rhs). Used to tie an
  // async read dout (a fresh symbol seeded before the combinational loop so
  // downstream logic can consume it) to its select(array, addr) once the read
  // address has been computed — the same fresh-var deferral the BMC loop uses
  // for flop state, applied inside one encode() for memory reads.
  std::vector<std::pair<cvc5::Term, cvc5::Term>> equalities;
};

// Reader-invariant signature of a Memory cell (the same RTL array read through
// two front-ends yields the same signature). Drives both the shared-array sort
// in query.cpp and the cut key here.
struct Mem_sig {
  int size   = 0;  // entries
  int bits   = 0;  // element width
  int addr_w = 1;  // index width = clog2(size)
  int n_rd   = 0;  // read ports
  int n_wr   = 0;  // write ports
};

// Decode the size/bits/port-count signature of a Memory node from its config
// pins (mirrors inou/cgen's port decode). occ is supplied by the caller as the
// running count of prior same-signature memories in forward_class() order, so
// the key is stable and identical across the two front-ends.
Mem_sig     read_mem_sig(const hhds::Node_class& node);
std::string mem_state_key(const Mem_sig& sig, int occ);

// Real bus width of a pin (signed magnitude+1 count; unsigned drops the spare
// sign bit). 0 means "unknown / no bits attribute". See lec.md "Bit-width trap".
int real_width(const hhds::Pin_class& pin);
int real_width_io(const hhds::Pin_class& pin, const hhds::GraphIO& gio, std::string_view name);

// Stable cross-design / cross-front-end correspondence key for a Flop state
// cell (source span preferred, then pin name). Used by both the encoder (to
// emit current/next state under one key) and prove_equal (to share the
// current-state symbol across the two designs). See M2 in lec.md.
std::string flop_state_key(const hhds::Graph& g, const hhds::Node_class& node);

// Concrete reset value of a flop (its constant `initial` pin), as a width-bit
// BV; nullopt for a reset-less flop. Used by the BMC engine to seed the
// reachable-from-reset initial state. See M2/BMC in lec.md.
std::optional<Val> flop_initial(cvc5::TermManager& tm, const hhds::Node_class& node, int width);

// Extend (sign/zero per v.is_signed) or truncate `v` to exactly `width` bits.
cvc5::Term fit_to(cvc5::TermManager& tm, const Val& v, int width);

class Encoder {
public:
  explicit Encoder(cvc5::TermManager& tm) : tm_(tm) {}

  // Resolution library for Sub (instance) flattening (M5): maps a sub-graph's
  // name-hash Gid to its definition graph. When set, a `Sub` node whose
  // `get_subnode_gid()` resolves to a *combinational* def is flattened inline
  // (the def is encoded with its inputs bound to the instance's input Vals and
  // its outputs wired to the instance's output pins). Unset, or a def that is
  // missing / contains state / nests too deep, keeps the sound `Sub -> fail`.
  // Typically the gensim cell-model library behind an ABC standard-cell netlist;
  // it also flips Get_mask/Set_mask to raw bit-vector widths (a mapped netlist's
  // unsigned nets carry no spare sign bit, unlike front-end RTL).
  void set_sub_lib(const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* lib) { sub_lib_ = lib; }

  // Shared output symbols for BLACKBOX Sub instances (missing/unresolved defs —
  // e.g. the XiangShan SRAM macros `array_*_ext`, or any module read with its
  // children left empty). A blackbox is "collapsed": each of its outputs becomes
  // a fresh symbol SHARED across the two designs (keyed "<defname>#<occ>:<port>"
  // here), and each of its inputs is emitted as a miter comparison output
  // ("\x02bbin:<defname>#<occ>:<port>"). Sound when BOTH designs instantiate the
  // corresponding blackbox (matched by module name + occurrence order) — the
  // surrounding logic must drive identical inputs, and identical inputs yield the
  // identical (shared) outputs. Built once in query.cpp and reused by both encodes.
  void set_shared_bbox(const absl::flat_hash_map<std::string, Val>* bb) { shared_bbox_ = bb; }

  // Encode the combinational logic of `g`.
  //
  // `shared_inputs` (optional): a map from input-port name to an already-built
  // bit-vector term. Inputs whose name is present reuse that term (this is how
  // a miter across two designs shares one symbol per primary input); inputs not
  // present get a fresh `mkConst` named "<prefix><name>".
  //
  // On any unsupported op or sequential element (Flop/Memory/Sub), the returned
  // Encoded has ok=false and a populated `error` (the encoder never silently
  // produces a wrong term).
  // `shared_mems` (optional): a map from mem_state_key to an already-built SMT
  // array term for the memory's CURRENT contents. Memories whose key is present
  // reuse that array (this is how corresponding memories collapse across two
  // designs); memories not present get a fresh array `mkConst`.
  Encoded encode(hhds::Graph* g, const absl::flat_hash_map<std::string, Val>* shared_inputs = nullptr,
                 std::string_view prefix = "", const absl::flat_hash_map<std::string, cvc5::Term>* shared_mems = nullptr);

private:
  // Fit `v` to exactly `width` bits: sign/zero-extend (per v.is_signed) when
  // widening, low-bit Extract when narrowing.
  cvc5::Term fit(const Val& v, int width);

  // Bit-vector sort of `width` bits (clamped to >= 1).
  cvc5::Sort bv(int width);

  cvc5::TermManager&                                  tm_;
  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib_     = nullptr;
  const absl::flat_hash_map<std::string, Val>*        shared_bbox_ = nullptr;
  int                                                 sub_depth_   = 0;  // Sub flattening recursion guard
};

}  // namespace livehd::lec
