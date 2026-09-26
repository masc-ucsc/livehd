// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// One region's bit-level translation, before any backend sees it: a RAW Lnet
// (lnet.hpp; region inputs and black-box outputs as inputs, crossing registers
// as latches, region outputs and black-box inputs as outputs) plus everything
// the read-back needs to stitch a mapped netlist into the region body again.
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "blast.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "lnet.hpp"
#include "pass_partition.hpp"

namespace livehd::synth {

// --- sequential: each region Flop -> N 1-bit ABC latches (seq=true only) ---
// The latch output (Q) seeds bitnet so the combinational cells read it as a
// source; the latch input (D) is wired to the folded next-state cone AFTER the
// comb loop (it may depend on logic that has not been bit-blasted yet). On
// read-back a latch becomes a plain Liberty DFF cell (register=true) or a
// native flop; either way the latch is what lets ABC optimize across the
// register boundary.
struct Seq_flop {
  hhds::Node_class        node;
  std::string             root;
  int                     bits = 0;
  hhds::Pin_class         q_pin;
  hhds::Pin_class         din_drv, en_drv, rst_drv, rval_drv, clk_drv;
  bool                    neg_reset = false;
  bool                    neg_clock = false;
  // The register has a SYNCHRONOUS reset (`reset_pin` driven, `async` not
  // asserted): the reset is folded into the latch's D cone as
  // `rst ? rval : (en ? din : Q)` and `initial` is the RESET value, not a
  // power-on one. Snapshotted like has_init (the read-back decides per bit
  // whether an init must keep a native flop, and `rst_drv` is a source-side
  // handle the rewritten region no longer resolves).
  bool                    has_reset = false;
  // The reset is ASYNCHRONOUS and the Liberty has a clear/preset flop cell for
  // every bit's reset value: the register still crosses as a latch, but the
  // reset stays OUT of the D cone (D = en ? din : Q) and the read-back drives
  // the cell's asynchronous pin from `arst_src` instead. has_reset is set too
  // (the `initial` is the reset value, never a power-on one).
  bool                    async_reset = false;
  // The region-input driver the async reset traces to (through 1-bit
  // Get_mask/Sext identities and Nots), a source-side handle compared against
  // the region's input ports like clk_drv; `arst_low` = it asserts the reset
  // at 0 (negreset XOR the Nots peeled on the way). Invalid when the reset is
  // computed inside the region (a reset synchronizer's flop, a scan-mode mux):
  // the reset then crosses as an ABC PO at the level the cell pin wants,
  // `arst_po[pin_low]` (an index into the Lnet outputs, after the region and
  // black-box POs), so the mapped logic drives the pin -- exactly how a
  // Liberty flow maps the same logic in front of a reset pin.
  hhds::Pin_class         arst_src;
  bool                    arst_low  = false;
  int32_t                 arst_po[2] = {-1, -1};
  bool                    neg_reset_hint = false;  // the `negreset` flavour, read while classifying
  // Per bit, the value the async reset loads (the `initial` bit; 0 without
  // one; an unknown bit takes whichever cell the library has).
  std::vector<bool>       arst_val;
  // Index into Region_blast::icgs when the register's clock is a recognized
  // latch+AND clock gate: it still crosses as a latch, and the read-back clocks
  // its cell from that gate's ICG cell output (clk_drv is then the gate's
  // source-side output pin, not a region input). -1 otherwise.
  int32_t                 icg = -1;
  // The `initial` (power-on / reset) value, SNAPSHOT at crossing time. The
  // read-back below runs after map_region has rewritten the region, so the
  // source const node behind `rval_drv` may already be gone -- re-reading the
  // pin there silently answers "no init" and refines an init-carrying flop
  // into a plain DFF cell (measured: abc_flat_names lost `a.r`/`b.r`).
  bool                    has_init  = false;
  Dlop                    init_val;
  // The latch carries ~next_state and the read-back wires the DFF cell's QN
  // pin as Q. Set only for an init-less register mapped to a QN-only cell
  // under the BUILT-IN flow: the inversion the QN cell needs then lands in
  // the mapper's own phase assignment (`&nf` costs both phases of every
  // node), which is where it is cheapest in aggregate -- +52 um^2 of comb
  // over the 10-test asap7 set (mixed per test: br_credit_sender -6.8,
  // br_arb_rr +3.3, br_ram_flops +28) against ~560 um^2 of flop savings,
  // where the flow-independent read-back absorption (pass 1b twin swap /
  // D inverter) costs ~0.03 per flop (~+230 um^2). The encoding is exact only
  // under combinational transformations (the machine ABC sees is
  // BO' = ~F(BO, x)), hence the flow gate; every other latch keeps the honest
  // next state and takes the read-back path.
  bool                    d_inverted = false;
  std::vector<uint32_t>   latch;     // per-bit Lnet latch index
  std::vector<Lid>        qbits;     // this stage, including hidden pipeline stages
  std::vector<Lid>        previous;  // preceding stage; empty for the din stage
};

struct Bbox_out {
  hhds::Pin_class src_pin;
  int             port_id;
  int             bits;
  bool            sign;
  bool            abc_bits;
};
struct Bbox_in {
  int             port_id;
  hhds::Pin_class drv;
  int             bits;
  bool            sign;  // operand signedness — load-bearing for a Div boundary (the LEC fit()s its operands by sign)
};
struct Bbox {
  hhds::Node_class                             node;
  Ntype_op                                     op;
  std::vector<Bbox_out>                        outs;
  std::vector<Bbox_in>                         ins;
  std::vector<std::pair<int, hhds::Pin_class>> const_ins;   // (port_id, const driver)
  std::vector<std::pair<int, hhds::Pin_class>> native_ins;  // flop boundary: (port_id, region-input driver) reconnected directly
  std::vector<std::tuple<int, hhds::Pin_class, int, bool>> fit_native_ins;  // direct driver, truncated to a Concat lane
};

// How a PI came to exist: a demanded bit of a region input, of a native
// boundary (black box) output, or a recognized clock gate's output (index =
// Region_blast::icgs).
enum class Pi_kind : uint8_t { region_input, bbox_output, icg_output };
struct Pi_origin {
  Pi_kind kind;
  size_t  index;
};
struct Bbox_po_target {
  int bx;
  int input;
  int bit;
};

// A recognized latch-based clock gate, `gclk = clk & L` with `L` a latch
// transparent while `clk` is low holding `en` -- mapped onto the Liberty's
// integrated clock-gate cell. Its latch and AND (and the 1-bit identities
// between them) are absorbed: neither bit-blasted nor kept native. `en`
// crosses ABC as an extra PO (`en_po`, after the async-reset POs) so the
// mapped logic drives the cell's enable pin; the gated clock is a PI of the
// mapped logic (Pi_kind::icg_output, index = this gate) read back from the
// cell's output; the cell's clock pin is the region input `clk_src`, or the
// `parent` gate's cell output for a gate chain.
struct Icg_gate {
  hhds::Pin_class gclk;         // source-side AND output: the gated clock the registers' clk_drv resolve to
  hhds::Pin_class clk_src;      // source-side reference clock: a region-input driver, or the parent gate's gclk
  int32_t         parent = -1;  // Region_blast::icgs index of the gate driving clk_src, -1: a region input
  hhds::Pin_class en_drv;       // source-side driver of the latched enable (bit 0 is latched)
  int32_t         en_po  = -1;
  std::string     name;         // the enable latch's name, for the cell instance
  int             fanout = 0;   // crossed register bits it clocks (the drive-ladder pick)
};

struct Region_blast {
  enum class Status { blasted, refused, over_budget };
  Status                                            status = Status::blasted;
  Lnet                                              lnet;
  std::vector<Seq_flop>                             flops;  // crossing registers, one per pipeline stage
  std::vector<Bbox>                                 bboxes;
  absl::flat_hash_set<hhds::Node_class>             region;             // rb.nodes
  absl::flat_hash_set<hhds::Node_class>             native_comb_logic;  // a combinational-cycle remainder kept native
  absl::flat_hash_map<hhds::Pin_class, std::string> region_in_name;     // region-input driver -> port name
  std::vector<std::pair<size_t, int>>               pi_order;           // region-input PIs: (input port, bit)
  std::vector<Pi_origin>                            all_pi_order;       // every PI, in creation order
  std::vector<std::tuple<int, int, int>>            bbox_pi;            // black-box PIs: (bbox, output, bit)
  std::vector<std::pair<size_t, int>>               po_order;           // region-output POs: (output port, bit)
  std::vector<std::vector<Bbox_po_target>>          bbox_po;            // black-box input POs, after po_order
  std::vector<bool>                                 direct_native_output;
  bool                                              has_dummy_po = false;  // a sentinel PO no read-back target reads
  size_t                                            blast_total  = 0;      // nodes scheduled for blasting
  size_t                                            arst_pos     = 0;      // internal async-reset POs, after bbox_po
  std::vector<Icg_gate>                             icgs;                  // recognized clock gates (Seq_flop::icg)
  size_t                                            icg_pos      = 0;      // ICG enable POs, after the async-reset POs
  uint64_t                                          rss_before   = 0;      // the admission baseline (0: no admission)
};

struct Blast_hooks {
  // Memory admission while blasting: true stops the translation (the region
  // does not fit). Null disables sampling (allow_oversize).
  std::function<bool(uint64_t rss_before, size_t blasted, size_t total, size_t net_nodes)> over_budget;
  // Stage trace: verbose timing and peak sampling.
  std::function<void(std::string_view)> stage;
  // Region wall time so far, for verbose progress lines.
  std::function<double()> elapsed_ms;
  // Rewrites the trivially convertible remainders of the source graph, once
  // per graph, right before the black-box scan.
  std::function<void()> rewrite_rems;
};

// Translate one region. `options.map_register` is the region's effective
// register mode; refusals are reported as diagnostics (status refused).
Region_blast blast_region(const livehd::partition::Region_body& rb, const Blast_options& options, const Blast_hooks& hooks);

// A region that is one constant shift of a region input (or a constant) is pure
// bus wiring: rebuild the typed shift in rb.body. True when it did.
bool rewrite_single_shift(const livehd::partition::Region_body& rb);

// `a % 2^k` over a non-negative dividend becomes `a & (2^k - 1)`, in place.
void rewrite_trivial_rems(hhds::Graph* g);

// Per-node diagnostic provenance shared by the translation and the read-back.
[[nodiscard]] int                pipeline_depth(const hhds::Node_class& node);
[[nodiscard]] livehd::diag::Span node_span(const livehd::partition::Region_body& rb, const hhds::Node_class& n);
[[nodiscard]] std::string        node_identity(const hhds::Node_class& n);
[[nodiscard]] std::string        const_brief(const Dlop& v);

}  // namespace livehd::synth
