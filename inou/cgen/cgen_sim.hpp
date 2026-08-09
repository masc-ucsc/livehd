// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "file_output.hpp"
#include "hhds/graph.hpp"
#include "hhds/index.hpp"
#include "latch_contract.hpp"  // Design_clocks — the shared clock-role analysis

// Cgen_sim — lower one hhds::Graph to a C++ Slop<N> struct over the ../hlop
// library (TODO 3d, inou.cgen.sim). Structural twin of Cgen_verilog (same
// body().nodes(hhds::Node_order::forward) walk + Ntype_op dispatch via livehd::graph_util), but emits a
// functional `Out cycle(In)` struct instead of inlined Verilog: one flat SSA
// binding (Slop<W> v = a.op(b);) per node, registers as struct members. Each
// module is split into <name>.hpp (the interface: data members, In/Out, method
// declarations — what an instantiating module #includes) and <name>.cpp (the
// bodies, "the slop") so a body edit recompiles one .o and a module appears once
// however many times it is instantiated. The standalone Bazel module scaffold
// (MODULE.bazel / BUILD / manifest) is written by the kernel's emit_sim_outputs.
class Cgen_sim {
private:
  std::string_view odir;

  using pin_key_t = hhds::Class_index;
  // driver pin -> the C++ expression naming its current value: an input field
  // ("in.a"), a flop member ("q"), or a combinational temp ("cg_3").
  absl::flat_hash_map<pin_key_t, std::string> pin2var;
  int                                         tmp_cnt = 0;
  // Pins whose C++ variable is CANONICAL at its declared width: the value the
  // word holds IS the exact mathematical value, correctly sign-extended. Only
  // the combinational temps we emit ourselves qualify -- their producing op ran
  // under the capacity invariant (bits = magnitude+1), so the result fits.
  //
  // BOUNDARY values do NOT qualify and must keep the declared-width
  // re-interpretation `operand()` applies: a module input is filled from a
  // testbench string (an s8 port can arrive holding 200, not -56), and memory
  // reads / sub outputs are materialized elsewhere. Reading those bare made
  // signed compares wrong -- caught by prp-simeq-rt_sat_s.
  absl::flat_hash_set<pin_key_t>              canonical_;

  // Pins whose C++ name is REWRITTEN by the sequential section, mid-stream:
  // a latency-1 memory read register (`<mem>_q<n>`), which the memory block
  // slop_update()s before the LATER memories and the flops are emitted.
  //
  // Single-use forestation pastes a comb node's expression at its consumer
  // instead of freezing it in a temp, which is only sound while every name in
  // that expression means the same thing at the paste point. A cone reading a
  // read register does not qualify -- pasted into a following memory's
  // address/data or into a flop `_din`, it would sample the just-updated value
  // and the design would read/write one cycle early -- so bind_comb
  // MATERIALIZES those cones in the combinational section, exactly as it did
  // before forestation. Materializing also ENDS the taint: the temp is frozen,
  // so its own consumers may forest freely.
  absl::flat_hash_set<pin_key_t> seq_volatile_;

  // Set by node_expr() when the expression it returns is NARROWER than the
  // width it was asked for -- only the Get_mask raw pass-through does that, and
  // only for an unsigned canonical source (top stored bit clear, so a
  // sign-extending landing equals the zext the mask would have produced).
  // bind_comb reads it to pick brace-init for exactly those declarations and
  // keep `= expr` -- i.e. the EXPLICIT cross-width ctor's build-time width
  // check -- everywhere else.
  bool sub_width_expr_ = false;

  // Stage 0 combinational-loop safety net. A sim module is ONE sequential
  // `cycle()` schedule, so a combinational cycle -- a real loop, or a FALSE loop
  // through an atomic Sub call (sub output feeds back through parent comb logic
  // into one of the sub's own inputs) -- has no valid emission order. operand()
  // would otherwise silently substitute `create_integer(0)` for an unschedulable
  // back-edge sink, producing a WRONG simulation with no diagnostic. These flags
  // turn that into a loud, located build failure.
  bool        cycle_unresolved_ = false;  // hit an unschedulable comb-cycle back-edge this graph
  bool        cycle_reported_   = false;  // a located error was already emitted for this graph
  std::string cycle_first_label_;         // first offending value (for the generic message)

  static std::string     cpp_id(std::string_view name);  // sanitize to a valid C++ identifier
  // C++ access path of a PORT. A tuple/struct-packed port flattens to dotted
  // leaves ("io_in.pc"), which are mirrored as a nested struct, so the dot is
  // KEPT and only the segments are sanitized. Every producer and consumer of a
  // port name (this module's In/Out structs, and a parent driving or reading a
  // sub-instance) must agree on this, or the parent names a field the child
  // never emitted.
  static std::string     cpp_port_path(std::string_view name);
  static hhds::Pin_class get_driver(const hhds::Pin_class& sink);
  static hhds::Pin_class find_sink_pin(const hhds::Node_class& node, std::string_view name);
  static hhds::Pin_class find_driver_pin(const hhds::Node_class& node, std::string_view name);

  // Raw name of the input port clocking `g` (flop clock_pin, else recursively a
  // sub-instance's clock port); "" = none. Memoized in clk_memo_.
  std::string                                   clock_input_of(hhds::Graph* g);
  absl::flat_hash_map<std::string, std::string> clk_memo_;

  // Incremental generation: one structural digest per module, persisted in
  // <odir>/gen_digests.json — a matching digest with existing outputs skips
  // the module's C++ emission entirely (the abc_cache region-digest idea
  // applied to sim; hashing is one cheap traversal, emission is many).
  uint64_t                                      sim_graph_digest(hhds::Graph* g);
  void                                          load_gen_digests();
  void                                          save_gen_digests();
  absl::flat_hash_map<std::string, std::string> gen_digests_;
  bool                                          gen_digests_loaded_ = false;

  // Per-graph liveness (backward reach from IO/state/Sub/Memory sinks): only
  // live comb nodes emit — dead trees stranded by graph passes (e.g.
  // split_packed_selfref_wires) produce no code.
  absl::flat_hash_set<hhds::Class_index> live_;

  // Resolve a driver pin to a Slop<target_bits> C++ EXPRESSION: a constant ->
  // Slop<W>::create_integer(N) when it fits an int64, else Slop<W>::from_pyrope("...");
  // otherwise the named value width-converted to W.
  // sign_mode: 0 = per is_unsign(pin), +1 = force signed (Slop<W>{...}, sext),
  // -1 = force unsigned (.zext_to<W>(), zext).
  std::string operand(const hhds::Pin_class& dpin, int target_bits, int sign_mode = 0);
  // Resolve a driver pin to a Slop expression AT ITS OWN WIDTH -- no width
  // conversion at all. For the mixed-width Slop ops (Slop<W>::add_op(x, y) and
  // friends), which accept operands of ANY width and materialize the result at
  // W: the operand widths are template parameters DEDUCED from the argument
  // types, so the emitter neither knows nor needs each variable's declared width.
  //
  // This is what makes the emission 1-to-1 -- one LGraph cell becomes one Slop
  // call, with no invented per-operand conversion. An LGraph op is an
  // unbounded-precision integer op and a pin's `bits` is derived metadata
  // (pass/bitfuzz strips it and recomputes it), so a conversion that only
  // re-states a width the value already satisfies is pure overhead.
  //
  // `fallback_bits` is used only for the shapes with no variable to name: an
  // invalid pin, a constant, or an unresolved combinational cycle.
  std::string raw_operand(const hhds::Pin_class& dpin, int fallback_bits);
  // The RHS Slop<wbits> expression for one combinational node.
  bool        raw_width_adjust_ok(const hhds::Pin_class& drv, int wbits);
  std::string node_expr(const hhds::Node_class& node, int wbits);

  std::string vcd_file;       // --set compile.sim.vcd=FILE ("" = no VCD)
  std::string top;            // --top: only this module bakes the VCD path (avoids file collisions)
  bool        vcd_fakedelay;  // --set compile.sim.vcdfakedelay: data settles at edge+3 with an X window (default);
                              // false = plain edge-aligned updates (no X, no delay)
  // --set sim.flatten=N: structurally inline a Sub whose callee body has <= N
  // nodes into its parent before emitting. 0 = off. See flatten_small_subs().
  int         flatten_budget = 0;

  // Bottom-up structural inline of every sub-instance that fits the budget.
  // Returns the number of instances inlined out of `g`.
  int flatten_small_subs(hhds::Graph* g);
  // Node count of a def's body. Memoized: the same def is reached once per
  // instantiation site and counting walks the whole graph.
  int graph_node_count(hhds::Graph* g);

  absl::flat_hash_map<hhds::Graph*, int> node_count_memo_;
  absl::flat_hash_set<hhds::Graph*>      flatten_walked_;  // defs already recursed into

public:
  // Per-output-group callee partitioning — the no-inlining answer to a false
  // combinational loop through an atomic instance (2026-08-06 ruling). A
  // definition instantiated on a word-level cycle emits, IN ITS OWN C++, one
  // `__settle_g<k>(In)` method per GROUP of outputs sharing the same comb
  // input-support; the parent calls exactly the group a demanded output needs,
  // as soon as that group's inputs are bound, and defers the single atomic
  // `cycle()` state advance to the end of the pass. No body is ever cloned:
  // eight instances of one def stay one emitted class, and the emitted TU size
  // stays proportional to the DEFINITION, not to the instance tree (the
  // inliner it replaces produced a 77MB minion_top.cpp).
  struct Split_group {
    // One OUTPUT (or output SLICE) this group computes. len == 0 means the
    // whole port; len > 0 is a bit range [lo, lo+len) with `leaf` the driver
    // of that range (a concat leaf, or — when `shifted` — a sliced callee's
    // whole-bundle output pin the range must be shifted out of). Slices are
    // the 2026-08-06 bit-level ruling: refine only under loop pressure —
    // slices sharing one support signature merge back into a single group,
    // so an un-conflicted port keeps exactly one group.
    struct Out {
      uint32_t        pid = 0;
      uint32_t        lo  = 0;
      uint32_t        len = 0;
      hhds::Pin_class leaf;
      bool            shifted = false;
    };
    std::vector<Out> outs;
    // Support atom: what the group's cones read of one input. len == 0 means
    // the whole port; len > 0 a bit range — the parent then fills only those
    // bits of the callee's In field, which is what lets two packed buses
    // exchange disjoint fields in the same cycle without a false cycle.
    struct In_atom {
      uint32_t pid = 0;
      uint32_t lo  = 0;
      uint32_t len = 0;
    };
    std::vector<In_atom> support;

    [[nodiscard]] bool covers_out(uint32_t pid) const {
      for (const auto& o : outs) {
        if (o.pid == pid) {
          return true;
        }
      }
      return false;
    }
    [[nodiscard]] bool sliced() const {
      for (const auto& o : outs) {
        if (o.len != 0) {
          return true;
        }
      }
      return false;
    }
  };
  using Split_map = absl::node_hash_map<const hhds::Graph*, std::vector<Split_group>>;

  // ICG fold: guard operands of a `<clock> & <enable>` clock cone, or empty
  // when the cone is not a foldable ICG (2f-latch M5).
  // `clocks` is the design-wide clock-role analysis (the same `Design_clocks`
  // pass/lec's phase schedule builds); it is required (nullptr = cannot fold).
  //
  // Takes the CLOCK DRIVER, not the state element: a Memory's clock arrives on
  // a per-port `clock_pin` sink rather than on the Flop-shaped one, and a
  // memory on a gated clock is the same question with the same answer.
  //
  // `fall_commit` (optional) is how a caller OPTS IN to the ACTIVE-LOW gate
  // flavour: an inverted reference (`~clk & en`) gates the FALLING edge, so
  // its guards are only foldable by an endpoint that can commit in the fall
  // sub-tick (a flop; the sim has a fall pass). When the caller passes the
  // out-param and the cone is inverted, the guards are returned with
  // *fall_commit = true — INCLUDING the enables of any inner (non-inverted)
  // gate the inverted reference rides on: the fall edge of a gated clock
  // exists only in periods where the inner gate was open, so dropping the
  // inner enable commits on periods with no pulse at all
  // (tests/sim/chained_clock_gates_mixed_phase.prp, row c5). A null
  // `fall_commit` keeps the historical refusal (memories, tick-guard words).
  static std::vector<hhds::Pin_class> icg_guards(const hhds::Pin_class& clock_driver, std::string_view clock_port,
                                                 const livehd::latch_contract::Design_clocks* clocks      = nullptr,
                                                 bool*                                        fall_commit = nullptr);

  // A clock cone that is an IDENTITY wrapper around a plain clock net (`clk & 1`,
  // `clk == 1`, a width mask) rather than a gate: no guard needed, and no
  // refusal owed. Distinguishes "not gated" from "gated in a way I cannot fold",
  // which an empty `icg_guards` result conflates.
  static bool plain_clock_cone(const hhds::Pin_class& clock_driver, const livehd::latch_contract::Design_clocks& clocks);

  // Every STRUCTURAL rewrite the emitter makes to a body (the clock-gate-cell
  // fold, the sim.flatten absorb, the packed self-ref wire split). Idempotent:
  // the first call per graph does the work, later ones return immediately.
  // A caller that MEASURES bodies before emitting (to_cgen_sim's partition
  // pre-scan holds pin handles into callees) must drive this over the whole
  // library first, or it measures a graph the emission then rewrites.
  // Returns false when the body cannot be prepared (an unexpandable replicated
  // instance); a diagnostic is emitted and the graph must NOT be emitted.
  bool prepare_graph(const std::shared_ptr<hhds::Graph>& graph);

  void do_from_graph(const std::shared_ptr<hhds::Graph>& graph);
  Cgen_sim(std::string_view _odir, std::string_view _vcd, std::string_view _top, std::string_view _fakedelay, int _flatten = 0,
           const Split_map* _splits = nullptr)
      : odir(_odir)
      , vcd_file(_vcd)
      , top(_top)
      , vcd_fakedelay(!(_fakedelay == "false" || _fakedelay == "0" || _fakedelay == "off"))
      , flatten_budget(_flatten)
      , splits_(_splits) {}

private:
  const Split_map* splits_ = nullptr;  // built once per pass run by to_cgen_sim
};
