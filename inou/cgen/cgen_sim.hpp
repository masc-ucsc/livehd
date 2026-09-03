// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "file_output.hpp"
#include "hhds/graph.hpp"
#include "hhds/index.hpp"
#include "latch_contract.hpp"  // Design_clocks — the shared clock-role analysis

namespace livehd::sim {
class Color_plan;
}

// Cgen_sim — lower one hhds::Graph to a C++ Slop<N> struct over the ../hlop
// library (inou.cgen.sim). Structural twin of Cgen_verilog (same
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
  // word holds IS the exact mathematical value, correctly sign-extended.
  //
  // Two populations qualify. (1) Combinational temps we emit ourselves -- their
  // producing op ran under the literal-width range invariant, so the result
  // fits. (2) UNSIGNED boundary storage under sim.slop_u: a `Slop_u<W>` member
  // is canonical by its own type invariant, which is the whole point of the
  // carrier. mark_slop_u_binding() inserts those here, from IO inputs, flop q
  // pins, memory douts / read_all and Sub outputs alike.
  //
  // SIGNED boundary values still do NOT qualify, and must keep the
  // declared-width re-interpretation `operand()` applies: a module input is
  // filled from a testbench string (an s8 port can arrive holding 200, not
  // -56), and memory reads / sub outputs are materialized elsewhere. Reading
  // those bare made signed compares wrong -- caught by prp-simeq-rt_sat_s.
  //
  // Do NOT "restore" a blanket boundary exclusion here: it would silently
  // re-sign every unsigned register and port, with no compile error, because
  // Slop_u converts implicitly in both directions.
  absl::flat_hash_set<pin_key_t>              canonical_;
  // Canonical pins whose materialized C++ object is Slop_u<W>, rather than
  // its Slop<W+1> carrier. Mixed HLOP operations accept these objects directly;
  // operand() only unwraps them when a concrete Slop carrier is required.
  absl::flat_hash_set<pin_key_t>              slop_u_values_;
  // Occurrence-local color temporaries can be rebound through cloned pins whose
  // width/sign metadata is stale. Track their emitted C++ type by expression so
  // raw_operand can still account for Slop_u<W>'s W+1-bit physical carrier.
  absl::flat_hash_map<std::string, int>        slop_u_binding_width_;
  // Get_mask nodes whose occurrence input was already narrowed to their exact
  // constant lane by the color ABI. For that occurrence the cell is an
  // identity; the set is rebuilt per emitted member because Class_index is
  // definition-local and can repeat across hierarchy occurrences.
  absl::flat_hash_set<hhds::Class_index>      preextracted_get_masks_;

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
  bool slop_u_expr_    = false;

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
  // <odir>/gen_digests.json — a matching digest with every recorded artifact
  // still on disk skips the module's C++ emission entirely (the abc_cache
  // region-digest idea applied to sim; hashing is one cheap traversal,
  // emission is many).
  //
  // TWO properties make that sound, and both were added together because
  // neither is safe without the other:
  //
  //  * the digest is HIERARCHICAL (hier_graph_digest). A module's own body
  //    hash folds only a child's NAME, but the code emitted for a parent
  //    genuinely depends on the child's subtree — clock-guard forwarding at
  //    call sites, Moore/state-only classification through a Memory, and, for
  //    the occurrence-wide color root, every node in the cone. Folding each
  //    child's digest is the merkle discipline semdiff already uses, and it is
  //    what lets the color root participate at all: its plan spans the whole
  //    occurrence tree, so a leaf edit MUST move the root's key.
  //
  //  * the record carries the COMPLETE artifact set the emission produced, not
  //    a hard-coded {hpp,cpp,iface.json} triple. The root also owns the color
  //    runtime/kernel headers, one TU per canonical kernel, the evaluator and
  //    state-commit shards, and (LLVM backend) one relocatable object per
  //    color. One missing artifact is a MISS — never a partial restore.
  struct Gen_record {
    std::string              digest;
    std::vector<std::string> files;  // basenames under odir, sorted
  };
  uint64_t                                     sim_graph_digest(hhds::Graph* g);
  uint64_t                                     hier_graph_digest(hhds::Graph* g);
  // The full generation key: the hierarchical body hash folded with every
  // option that changes what is emitted. Deliberately does NOT fold the color
  // plan — the plan is DERIVED from exactly these inputs, and its report()
  // degenerates to summary counts past 100k version sites, so it is both
  // redundant and too weak to key on.
  std::string                                  generation_key(hhds::Graph* g, bool color_root);
  void                                         load_gen_digests();
  void                                         save_gen_digests();
  absl::flat_hash_map<std::string, Gen_record> gen_digests_;
  absl::flat_hash_map<hhds::Gid, uint64_t>     hier_digest_memo_;
  absl::flat_hash_map<hhds::Gid, uint64_t>*    shared_digest_memo_ = nullptr;
  std::string                                  gen_key_;  // this module's key, recorded on a clean emission
  bool                                         gen_digests_loaded_ = false;

  // Every generated file this module owns, in emission order. `open_out` is
  // the only way the emitter creates one, so the manifest cannot drift from
  // what was actually written; `note_emitted` covers the LLVM objects, which
  // are produced by the codegen backend rather than by File_output.
  std::vector<std::string>     emitted_files_;
  std::shared_ptr<File_output> open_out(std::string_view basename);
  void                         note_emitted(std::string_view basename);

  // Per-graph liveness (backward reach from IO/state/Sub/Memory sinks): only
  // live comb nodes emit — dead trees stranded by graph passes (e.g.
  // split_packed_selfref_wire) produce no code.
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
  // The stored carrier with no signed/unsigned boundary canonicalization. This
  // is only for an operation that masks the source itself (currently fused
  // low-bit Get_mask), so bits outside the declared source width cannot leak.
  std::string stored_operand(const hhds::Pin_class& dpin, int fallback_bits);
  // The RHS Slop<wbits> expression for one combinational node.
  bool        proven_unsigned_result(const hhds::Node_class& node, const hhds::Pin_class& output) const;
  bool        proven_canonical_unsigned_result(const hhds::Node_class& node, const hhds::Pin_class& output) const;
  bool        raw_width_adjust_ok(const hhds::Pin_class& drv, int wbits);
  std::string node_expr(const hhds::Node_class& node, int wbits);

  std::string vcd_file;            // --set compile.sim.vcd=FILE ("" = no VCD)
  std::string top;                 // --top: only this module bakes the VCD path (avoids file collisions)
  bool        vcd_fakedelay;       // --set compile.sim.vcdfakedelay: data settles at edge+3 with an X window (default);
                                   // false = plain edge-aligned updates (no X, no delay)
  bool        observation_on;      // compile-time hierarchical value instrumentation (VCD/probe/query)
  bool        runtime_support_on;  // checkpoint/probe/query state-walk methods; false for lean performance builds
  // Node count of a def's body. Memoized: the same def is reached once per
  // instantiation site and counting walks the whole graph.
  int         graph_node_count(hhds::Graph* g);

  absl::flat_hash_map<hhds::Graph*, int> node_count_memo_;

public:
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

  // Every STRUCTURAL rewrite the emitter makes to a body (today: the
  // clock-gate-cell fold and compact-loop realization). Idempotent:
  // the first call per graph does the work, later ones return immediately.
  // A caller that MEASURES bodies before emitting (to_cgen_sim's partition
  // pre-scan holds pin handles into callees) must drive this over the whole
  // library first, or it measures a graph the emission then rewrites.
  // Returns false when the body cannot be prepared (an unexpandable replicated
  // instance); a diagnostic is emitted and the graph must NOT be emitted.
  bool prepare_graph(const std::shared_ptr<hhds::Graph>& graph);

  void do_from_graph(const std::shared_ptr<hhds::Graph>& graph);

  // Is `g`'s generated C++ already current in `odir`? Same question
  // do_from_graph()'s internal gate answers, exposed so the CALLER can answer it
  // BEFORE building the occurrence color plan — on a large design the plan
  // discovery is the single most expensive step in the emitter (measured: ~25 s
  // of a 32 s warm XiangShan `Rob` codegen, against ~4 s of actual emission), and
  // it is a pure function of the same cone and options the key already folds, so
  // a hit has nothing to learn from re-deriving it.
  //
  // `color_root` is what the caller is ABOUT to decide: whether this graph gets
  // a plan. The instance may be constructed with a null plan; every other
  // constructor argument must match the one that would emit, or the key does
  // not describe the same generation.
  //
  // The graph must already be prepare_graph()'d — the key is over the body AS
  // EMITTED, and prepare_graph rewrites it.
  [[nodiscard]] bool generation_current(hhds::Graph* g, bool color_root);

  // Share ONE hierarchical-digest memo across every Cgen_sim of a run. Without
  // it each instance re-walks its own cone, so a hierarchy costs
  // O(modules x cone) node visits instead of O(nodes) — on a large design that
  // is seconds of pure repetition, paid twice over (once by the pre-plan probe,
  // once by the emitter). The pointed-to map must outlive every instance, and
  // the graphs must not be structurally rewritten while it is in use — which
  // holds, because prepare_graph() runs over the whole library first.
  void share_digest_memo(absl::flat_hash_map<hhds::Gid, uint64_t>* memo) { shared_digest_memo_ = memo; }
  Cgen_sim(std::string_view _odir, std::string_view _vcd, std::string_view _top, std::string_view _fakedelay,
           const livehd::sim::Color_plan* _color_plan = nullptr, bool _compact_kernel = false, bool _observation_on = false,
           bool _runtime_support_on = true, bool _slop_u = true, bool _color_dirty = true, bool _debug = false,
           bool _unknown_zero = false, bool _llvm_backend = false)
      : odir(_odir)
      , vcd_file(_vcd)
      , top(_top)
      , vcd_fakedelay(!(_fakedelay == "false" || _fakedelay == "0" || _fakedelay == "off"))
      , observation_on(_observation_on || !_vcd.empty())
      , runtime_support_on(_runtime_support_on || _observation_on || !_vcd.empty())
      , color_plan_(_color_plan)
      , compact_kernel_(_compact_kernel)
      , llvm_backend_(_llvm_backend)
      , slop_u_(_slop_u)
      , color_dirty_(_color_dirty)
      , debug_(_debug)
      , unknown_zero_(_unknown_zero) {}

private:
  const livehd::sim::Color_plan* color_plan_     = nullptr;  // non-null only while emitting the selected hierarchy root
  bool                           compact_kernel_ = false;    // definition still called by a native compact-loop wrapper
  bool                           llvm_backend_   = false;    // direct native object lowering requested for supported colors
  // sim.slop_u — materialize every value whose LGraph driver pin is proven
  // unsigned in the CANONICAL-unsigned Slop_u<n> (one mask at the write),
  // instead of the lazily-masked Slop<n> (one mask at every read).
  //
  // UNIFORM: combinational temps, IO ports, memories, registers/flops/latches
  // and color boundary slots all follow the SAME rule -- there is no exemption
  // for state, for reset-free state, or for module boundaries.
  // false = everything is Slop<n>.
  bool                           slop_u_         = true;
  bool                           color_dirty_    = true;  // cross-cycle color activation and boundary change tracking
  // sim.debug keeps the materializing Slop_u landing in generated code so an
  // unsigned-proof mistake remains visible while debugging. The normal path
  // uses from_proven(), whose width check is compile-time and whose runtime
  // work is only a carrier copy.
  bool                           debug_          = false;
  // sim.unknown_zero — Slop carries no runtime X, so every `?` bit of a literal
  // must become a concrete 0 or 1. false (the default) DRAWS it from hlop's
  // seeded PRNG (hlop_set_random_seed, the driver's --seed) once per literal, so
  // an unspecified bit cannot be silently relied on while a run stays
  // reproducible. The draw is once, not per cycle: a constant stays constant, so
  // the value never flickers and Color_plan::runtime_random (per-period random
  // work, which defeats quiescence detection) stays false for it. true forces
  // zero, which also lets the literal fold at C++ compile time.
  bool                           unknown_zero_   = false;

public:
  // The C++ TYPE a stored unsigned value of `bits` literal LiveHD bits is
  // declared as. Signed storage is never Slop_u -- it has a real sign bit and
  // the lazy contract is correct for it.
  [[nodiscard]] std::string stored_type(int bits, bool unsign) const {
    if (slop_u_ && unsign && bits > 0) {
      return absl::StrCat("Slop_u<", bits, ">");
    }
    return absl::StrCat("Slop<", bits, ">");
  }
  [[nodiscard]] bool slop_u_enabled() const { return slop_u_; }
};
