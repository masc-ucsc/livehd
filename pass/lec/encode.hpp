// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cvc5/cvc5.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

namespace livehd::lec {

// Port/signal names are matched case-sensitively (LiveHD/Pyrope name policy):
// the ref/impl IO pairing requires identical spelling, so a port `Clk` on one
// side does NOT pair with `clk` on the other.
template <typename V>
using Io_name_map = absl::flat_hash_map<std::string, V>;

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
  // Optional X/don't-care bit-plane (same width as `term`; bit i = 1 means the
  // value's bit i is unknown/don't-care). A NULL term means fully known — the
  // common case pays nothing. Only populated when the encoder runs with
  // x_dontcare (the REFERENCE side under lec.gold_x=ignore): sourced at
  // constants with '?' bits, propagated exactly through Mux arms and
  // conservatively (whole-value smear) through every other op, and consumed by
  // the query-side miters, which exclude ref-unknown bits from the compare —
  // the cvc5 analogue of yosys `miter -ignore_gold_x`.
  cvc5::Term x_mask{};  // ('undef' avoided: C-preprocessor collision risk)
};

struct Encoded {
  bool        ok = true;
  std::string error;  // first unsupported-op / structural error, when !ok

  // Graph IO, by declared port name (case-sensitive ref/impl pairing).
  Io_name_map<Val> inputs;
  Io_name_map<Val> outputs;

  // M4 memory state. Each Memory cell is cut like a Flop: its current contents
  // are an SMT array symbol (shared across the two designs by mem_state_key, so
  // corresponding memories "collapse"), and its post-cycle contents are the
  // next-state array. Read douts are ordinary BV terms in `outputs`/pin2val.
  Io_name_map<cvc5::Term> next_mem;  // key -> next-state array

  // M4 SYNC-read latency. A type==1 (registered / latency-1) read port's dout is
  // a 1-cycle REGISTERED value: the dout this cycle is a CURRENT-STATE symbol
  // (seeded from `shared_reads`), and its next-state = select(read-source, addr).
  // Keyed "<mem_state_key>:rd<N>" (per read port). Threaded across cycles by the
  // caller EXACTLY like next_mem, so the read lands one cycle after the address —
  // matching a front-end that models the same sync read as a comb array feeding an
  // external flop. type==0 (async) and type==2 (comb) reads stay latency-0 and do
  // NOT appear here (they tie their dout within the cycle via `equalities`).
  Io_name_map<cvc5::Term> next_read;  // "<key>:rd<N>" -> next-state read value

  // Side constraints the caller must assert (EQUAL lhs rhs). Used to tie an
  // async read dout (a fresh symbol seeded before the combinational loop so
  // downstream logic can consume it) to its select(array, addr) once the read
  // address has been computed — the same fresh-var deferral the BMC loop uses
  // for flop state, applied inside one encode() for memory reads.
  std::vector<std::pair<cvc5::Term, cvc5::Term>> equalities;

  // F7 witness source-map: flop correspondence key (the same key used for the
  // `\x01nxt:` next-state outputs and the wit_state cuts) -> "file:line" of the
  // flop's declaration, resolved from the node's attrs::srcid via the graph's
  // Source_locator. Populated only when the node carries a source id (a Verilog
  // node resolves back through the cgen ECMA-426 sourcemap). Lets the BMC witness
  // render the first diverging state cut as `s (cpu.prp:554)` and stamp the same
  // location into the generated lecfail.prp testbench. Empty entry = unknown.
  Io_name_map<std::string> src_of_key;
};

// State-aware black-box for a SEQUENTIAL collapse (lec.html "sequential
// black-box"). A combinational box models outputs as a pure function of inputs —
// sound only for a stateless leaf. To collapse a STATEFUL proven leaf (a register
// file, a pipeline-stage register) soundly, the box becomes state-aware:
//   outputs    = UF_out(state)            (MOORE — see below)
//   next_state = UF_next(inputs, state)
// with ONE state cut per leaf instance corresponding on both designs (shared UF
// symbols + a shared state symbol) and a matched-reset shared init. Then equal
// corresponding state => equal outputs, and equal inputs + state => equal
// next-state, by congruence — sound in the inductive (assume-equal-state) frame
// directly, and under BMC-from-reset the threaded state makes the leaf output VARY
// per cycle (a constant combinational box would false-prove a timing difference).
//
// The OUTPUT is MOORE (state ONLY, NOT the current inputs): a Mealy output adds a
// combinational input->output path that closes a FALSE combinational cycle when a
// collapsed stage register's output feeds glue back to its own stall/enable input.
// Output-from-state matches a registered output and stays sound — divergent leaf
// inputs are still mitered via the bbin compare points. (The encoder also emits the
// outputs BEFORE resolving the inputs, so that feedback cannot deadlock its fixpoint.)
//
// The state SYMBOL is not stored here: it threads through the ordinary flop-cut
// machinery under the key "\x01leafstate:<defname>#<tag>" (a shared current-state
// in query.cpp's `shared`, next-state emitted with the "\x01nxt:" prefix), so the
// inductive miter corresponds it and the BMC unroll threads it. Only the shared
// UF symbols + domain widths live here; built once in query.cpp, applied by both
// encodes. Keyed "<defname>#<tag>" — the box correspondence key from query.cpp's
// builder (name-first instance pairing, occurrence fallback; see set_box_keys).
struct State_box {
  int                                      in_w    = 0;  // concatenated input width (UF domain)
  int                                      state_w = 0;  // state width (UF domain + next codomain)
  // NAME-SORTED (port, width) input concat layout, unioned across the two
  // designs (max declared width per port). The encoder MUST build the UF_next
  // input concat from this layout — NOT from its own side's decl order: two
  // front-ends can declare the same ports in different orders (slang keeps the
  // .v port list, Pyrope appends implicit clock/reset), and a permuted concat
  // feeds the shared UF structurally different values for EQUAL inputs, so the
  // threaded states spuriously diverge. A port a side does not connect (or
  // does not even declare) contributes 0.
  std::vector<std::pair<std::string, int>> in_ports;
  cvc5::Term                                   next_fn;  // UF (inputs, state) -> state
  absl::flat_hash_map<std::string, cvc5::Term> out_fn;   // output port -> UF (state) -> out  [Moore]
  absl::flat_hash_map<std::string, int>        out_w;    // output port -> width
};

// PAIRING-FREE collapse of a STATELESS proven leaf. The legacy stateless box
// (one free output symbol SHARED between the two designs' corresponding
// instances, plus the bbin input compare obligations) needs a bijective
// instance pairing — and with several interchangeable instances of the same
// def, occurrence-order pairing can associate physically different instances
// and falsely refute an equivalent pair (tests/equiv/instance_collapse_order).
// Instead, each output of a stateless collapsed leaf becomes
//   out = UF_<def>_<port>(concatenated inputs)
// with ONE uninterpreted function per (def, output port) shared across BOTH
// designs and ALL instances. The solver's congruence closure then pairs
// instances dynamically: any two applications over equal inputs yield equal
// outputs, regardless of declaration order, instance count, or names — so no
// correspondence key exists to get wrong, and no bbin obligations are emitted.
// Sound: a Proven verdict holds for ALL interpretations of the UF, in
// particular the real (already-proven-equivalent) leaf function.
//
// The encoder still emits each instance's outputs as per-instance FRESH
// symbols on the FIRST visit (before the inputs resolve) and ties them with
// `out_sym == UF(inputs)` equalities once the inputs land (Encoded::equalities)
// — the same two-phase trick the Moore box uses, so a collapsed leaf whose
// output feeds glue back to its own input cannot deadlock the fixpoint.
// NOTE the fresh symbols are NOT shared across the designs (sharing without
// the bbin obligations would be an unjustified equality assumption).
//
// The input layout is NAME-SORTED (not decl-order) so both front-ends build
// the identical concat; a port unconnected on an instance contributes 0.
// in_w == 0 (a no-input leaf = a constant): cvc5 UFs need a non-empty domain,
// so the outputs are instead ONE shared free symbol per (def, port) in
// `out_const` — same def + no inputs => same outputs, trivially congruent.
struct Comb_box {
  int                                            in_w = 0;   // concatenated input width (UF domain)
  std::vector<std::pair<std::string, int>>       in_ports;   // NAME-SORTED (port, width) concat layout
  absl::flat_hash_map<std::string, cvc5::Term>   out_fn;     // port -> UF (inputs) -> out (in_w > 0)
  absl::flat_hash_map<std::string, Val>          out_const;  // port -> shared symbol (in_w == 0)
  absl::flat_hash_map<std::string, int>          out_w;      // port -> width (max across designs)
  absl::flat_hash_map<std::string, bool>         out_sgn;    // port -> signedness (of the widest side)
};

// Structural identity of a node inside one design's hierarchical walk
// (graph id : hierarchy position : debug nid). Stable across repeated walks of
// the same in-memory design, so query.cpp's box-correspondence builder and the
// encoder agree on which instance is which without sharing traversal order.
std::string box_node_key(const hhds::Node_class& n);

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

// Canonicalize a flop's hierarchical name so a HIERARCHICAL stage register on
// one side pairs with a FLATTENED reimplementation on the other. CIRCT/firtool
// emits each single-field pipeline register as instance "<inst>.reg_<field>";
// a hand-flattened design names the same state "<inst>_<field>". This collapses
// ".reg_" → "_" and then flattens any remaining instance separator "." → "_", so
// both converge to one key. Deterministic and applied identically to both designs
// (names that already agree stay equal), so it never breaks an existing match.
// Used by the encoder (current/next-state keys) and prove_equal (shared symbols).
std::string canon_flop_name(std::string_view hier_name);

// Concrete reset value of a flop (its constant `initial` pin), as a width-bit
// BV; nullopt for a reset-less flop. Used by the BMC engine to seed the
// reachable-from-reset initial state. See M2/BMC in lec.md.
std::optional<Val> flop_initial(cvc5::TermManager& tm, const hhds::Node_class& node, int width, bool x_as_undefined = false);

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
  // a fresh symbol SHARED across the two designs (keyed "<defname>#<tag>:<port>"
  // here), and each of its inputs is emitted as a miter comparison output
  // ("\x02bbin:<defname>#<tag>:<port>"). Sound when BOTH designs instantiate the
  // corresponding blackbox — matched by module name + the box correspondence key
  // (name-first instance pairing, occurrence fallback; see set_box_keys) — the
  // surrounding logic must drive identical inputs, and identical inputs yield the
  // identical (shared) outputs. Built once in query.cpp and reused by both encodes.
  void set_shared_bbox(const Io_name_map<Val>* bb) { shared_bbox_ = bb; }

  // Explicit state-correspondence alias (lec.match): maps a CANONICALIZED flop
  // name (canon_flop_name of this design's hier name) to the shared cut key it
  // collapses onto. Applied to both the current-state lookup and the next-state
  // output key, so a flop the user paired with a differently-named flop on the
  // other design shares one symbol. Unset / key-absent ⇒ the canon name is used.
  void set_name_alias(const Io_name_map<std::string>* a) { name_alias_ = a; }

  // Proven-module black-box collapse set (lec.collapse): def-NAMES (case-sensitive)
  // that must be FORCED to the blackbox path even when sub_lib_ has a combinational
  // def that could be flattened inline. A def the driver already proved equivalent
  // becomes a sound UF(inputs) box, so the parent stops re-solving its internals.
  // query.cpp's shared-bbox builder must apply the SAME predicate so the forced
  // box's output symbols are pre-built and shared across the two designs.
  void set_collapse_defs(const Io_name_map<bool>* c) { collapse_defs_ = c; }

  // Shared state-aware boxes for STATEFUL collapsed leaves (keyed by the box
  // correspondence key from set_box_keys). When a collapsed leaf has an entry
  // here, its outputs/next-state are encoded as UF(inputs, state) instead of
  // the stateless shared-symbol box — see State_box. Built once in query.cpp
  // so both designs share the UF symbols.
  void set_state_boxes(const absl::flat_hash_map<std::string, State_box>* s) { state_boxes_ = s; }

  // Pairing-free boxes for STATELESS collapsed leaves, keyed by module DEF
  // name (no instance key needed — see Comb_box). Built once in query.cpp so
  // both designs and all instances share the per-(def,port) UF symbols.
  void set_comb_boxes(const absl::flat_hash_map<std::string, Comb_box>* c) { comb_boxes_ = c; }

  // Box-instance correspondence: box_node_key(node) -> the cross-design
  // correspondence key ("<defname>#<tag>") computed ONCE by query.cpp's
  // builder for THIS design (name-first pairing, occurrence fallback — set
  // separately before encoding each side). Replaces the encoder-local
  // traversal-order occurrence counters, whose order could disagree both with
  // the other design's encode and with query.cpp's own builders (the latter
  // silently degraded a stateful box to a stateless one — a false-PASS
  // hazard). A node absent from the map gets a per-encode "?" key: its
  // outputs stay UNSHARED and its obligations one-sided, which the miters
  // gate to an incomplete correspondence (never a false verdict).
  void set_box_keys(const Io_name_map<std::string>* k) { box_keys_ = k; }

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
  // `shared_reads` (optional): a map from "<mem_state_key>:rd<N>" to an already-
  // built BV term for a type==1 (sync) read port's CURRENT registered dout. Read
  // ports whose key is present reuse that term (the sync-read latency-1 state cut,
  // threaded across cycles like shared_mems); absent keys get a fresh symbol.
  Encoded encode(hhds::Graph* g, const Io_name_map<Val>* shared_inputs = nullptr,
                 std::string_view prefix = "", const Io_name_map<cvc5::Term>* shared_mems = nullptr,
                 const Io_name_map<cvc5::Term>* shared_reads = nullptr);

  // lec.gold_x=ignore: while true, constants with unknown ('?') bits source an
  // undef bit-plane on every Val (see Val::undef) instead of being silently
  // masked to 0. Toggled ON only while encoding the REFERENCE design.
  void set_x_dontcare(bool on) { x_dontcare_ = on; }

  // 2f-verify: while true, each `fproperty` Sub (a user assert/assume/
  // assert_always materialized by tolg — see graph_util::fproperty_module_name)
  // emits its resolved cond as a synthetic output keyed
  //   "\x04prop:<occ>\x1f<kind>\x1f<loc>\x1f<msg>"
  // (occ = deterministic walk-order counter, so the SAME property carries the
  // SAME key on every per-cycle encode of one design). Default OFF: the
  // equivalence miters never see property outputs (an fproperty has no data
  // output, and a one-sided \x04 output would gate lec to Unknown).
  void set_emit_props(bool on) { emit_props_ = on; }

  // 2f-verify submodule port taps: for every Sub instance whose HIER name is in
  // `insts`, emit each resolvable input/output port value as a synthetic output
  //   "\x05tap:<inst_hier>.<port>"
  // so a formal-block monitor can bind a SUBMODULE port exactly like a top-level
  // output (Monitor::Bind::Src::output with the tap key). Values are fitted to
  // the port DECLARATION's width (the same width the CLI types the monitor input
  // with). Default nullptr: no taps, zero cost. The set must outlive the encoder.
  void set_port_taps(const absl::flat_hash_set<std::string>* insts) { port_taps_ = insts; }

  // Budget-aware encode (2f-lec): a wall-clock budget, in SECONDS, bounding each
  // top-level encode() call. encode() is where deep/late hierarchical parents can
  // spend minutes flattening the instance tree BEFORE cvc5 is ever called (the
  // `while(progress)` fixpoint), so the per-checkSat `tlimit-per` never bounds it.
  // Each top-level encode() call (sub_depth_==0) gets a fresh `seconds`-long
  // budget; the recursive Sub-flatten re-entry inherits the parent's deadline
  // (so a slow subtree still counts against its parent, not a fresh clock). On
  // overrun the encode bails via the same ok=false/error path a structural
  // failure uses, which the query layer maps to a sound Verdict::Unknown (a
  // budget-out is never a wrong verdict). 0 (the default) = no encode budget.
  // Mirrors Lec_options::timeout, so encode and each checkSat share one knob.
  void set_encode_budget(int seconds) { budget_seconds_ = seconds; }

private:
  // Fit `v` to exactly `width` bits: sign/zero-extend (per v.is_signed) when
  // widening, low-bit Extract when narrowing.
  cvc5::Term fit(const Val& v, int width);

  // Bit-vector sort of `width` bits (clamped to >= 1).
  cvc5::Sort bv(int width);

  // canon_flop_name(hier) then the lec.match alias collapse: the shared cut key
  // for a flop. Used for both its current-state lookup and its next-state output.
  std::string flop_key(std::string_view hier) const;

  cvc5::TermManager&                                  tm_;
  const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib_       = nullptr;
  const Io_name_map<Val>*                             shared_bbox_   = nullptr;
  const Io_name_map<std::string>*                     name_alias_    = nullptr;
  const Io_name_map<bool>*                            collapse_defs_ = nullptr;
  const absl::flat_hash_map<std::string, State_box>*  state_boxes_   = nullptr;
  const absl::flat_hash_map<std::string, Comb_box>*   comb_boxes_    = nullptr;
  const Io_name_map<std::string>*                     box_keys_      = nullptr;
  int                                                 sub_depth_   = 0;  // Sub flattening recursion guard
  bool                                                x_dontcare_  = false;  // ref-side X = don't-care (lec.gold_x=ignore)
  bool                                                emit_props_  = false;  // emit fproperty conds as \x04prop: outputs (2f-verify)
  const absl::flat_hash_set<std::string>*             port_taps_   = nullptr;  // sub instances whose ports get \x05tap: outputs
  int                                                 budget_seconds_ = 0;  // per-encode wall-clock budget in s (2f-lec); 0 = none
  std::optional<std::chrono::steady_clock::time_point> deadline_{};  // set at each top-level encode() entry from budget_seconds_
};

// Fit an undef bit-plane to `width` alongside its value: NULL stays NULL;
// narrowing extracts; widening follows the VALUE's extension rule — a
// sign-extension duplicates the (possibly unknown) top bit, a zero-extension
// appends known-0 bits.
cvc5::Term fit_x_mask_to(cvc5::TermManager& tm, const Val& v, int width);

}  // namespace livehd::lec
