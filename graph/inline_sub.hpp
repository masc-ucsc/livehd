// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Structurally inline ONE Sub instance into the body that holds it, in place.
//
// The child's nodes are cloned into `parent` (node/wire names prefixed with the
// instance's logical prefix, `alu.foo`; an unnamed or `__flat___*` instance is
// hierarchy-transparent and adds no component -- see logical_instance_prefix in
// node_util.hpp), every edge crossing the boundary is rewired to what
// is on the other side of it, and the Sub node itself is deleted. The child def
// is untouched -- it stays in the library for its other instantiation sites, and
// the caller decides when (or whether) it becomes garbage.
//
// This is deliberately SINGLE-LEVEL. It does not recurse into the child's own
// sub-instances: those stay as Sub nodes in the cloned body. A caller wanting a
// deep inline walks the def hierarchy children-first, so that by the time a def
// is inlined anywhere its own children are already part of its body. That
// ordering is what keeps this a flat transform with no instantiation-context
// bookkeeping (contrast pass/partition/flatten.cpp, which inlines EVERYTHING at
// once and therefore has to carry a per-instance context tree).
//
// The boundary itself survives as what it does to the value: a connection
// whose driver does not fit its port (driver_fits_port below) is spelled in
// `parent` as the Get_mask/Sext the port performs -- an input as the callee
// declares it (only when the callee reads it), an output as the callee's port
// and then the parent net the instance output drives (its stamp). The child
// def is never edited for it, and a well-formed graph gets no new node. A fit
// node takes the instance color like any clone (`inherit_color`), and a
// feed-through cycle that closes through a fit is still the `inline-cycle`
// error below, not a silent combinational self-loop.
//
// `inst` must be a Sub whose target def has a materialized body; a body-less
// black box (liberty cell, external IP, an fproperty/lgassert marker) has nothing
// to inline and is rejected. Returns false after emitting a diagnostic, with
// `parent` untouched: every refusal is decided before the first edit. A shape it
// cannot resolve once the splice has begun (today: a combinational feed-through
// cycle through the boundary) is a FATAL `inline-cycle` diagnostic, which throws
// and may leave a partially inlined body behind, exactly like flatten.
// `def` overrides the body lookup for an instance whose def is not in the
// parent's own graph library — a `lec --lib` cell model lives in a SIDE library,
// so `Node_class::get_subnode_graph()` is null for it even though the instance's
// subnode gid resolves in that side map. nullptr = resolve the ordinary way.
// An override is bound by port NAME through the instance's own declared IO,
// never by port id: two libraries of one cell (the `--lib` gensim graph, and
// the copy a Verilog netlist was elaborated with) may number the same ports
// differently, and an id binding silently rewires the edges onto other ports
// (a clock gate's GCLK reader matched no output and every latch it enabled read
// as always-transparent). When `sub_def_port_mismatch` reports a mismatch the
// call emits an `unsupported` diagnostic and returns false with `parent`
// untouched.
// `name_state` names an UNNAMED spliced state node (a cell model's internal
// flop) after the INSTANCE, so a flop-cut correspondence key survives the
// inline. Without it the cut is keyed on a synthesized net name that has no
// counterpart on the other design. Only an instance with a non-transparent name
// (a non-empty logical prefix) can name the flop.
// `inherit_color` stamps cloned nodes with the parent instance color when
// inlining an already-colored private backend graph.
// `prefix_instance` preserves the ordinary `instance.child` hierarchy names.
// An unnamed instance already gets an empty prefix, so false only matters for a
// NAMED instance: it drops that instance's component too.
// `state_name`, when set, replaces the name every spliced STATE node (flop,
// latch) would carry from the body: a non-empty value is prefixed like any
// name, an empty one leaves the node unnamed (then `name_state` still names an
// unnamed flop after the instance). `lec` splices a `--lib` cell instance from
// the design's own copy of the cell but names its one state element the way
// the `--lib` model names it, so a correspondence key does not depend on which
// copy of the cell carried the body (pass/single_edge/proof_prep.cpp).
[[nodiscard]] bool inline_sub_instance(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass,
                                       hhds::Graph* def = nullptr, bool name_state = false, bool prefix_instance = true,
                                       bool inherit_color = false, const std::optional<std::string>& state_name = std::nullopt);

namespace detail {
struct Host_state;  // inline_sub.cpp
}

// Splices MANY instances into ONE host, one after another: the same graph as
// calling inline_sub_instance (default options) on each in turn, at a cost
// linear in the total spliced size.
//
// What a one-at-a-time caller pays per splice and this does not:
//   * finding the next instance. A splice only ever ADDS Sub nodes by cloning
//     the callee's own sub-instances, and hhds node ids are append-only (a
//     deleted id is never reused), so `splice` reports exactly those clones,
//     in ascending id order, through `new_subs`. A caller that keeps a
//     worklist in node-id order visits instances in the order a re-scan of
//     the host for "the first remaining candidate" would, without re-scanning
//     (an O(host) walk per splice was quadratic on a netlist whose top absorbs
//     thousands of region instances). A handle collected from the host stays
//     valid across other splices: only the spliced instance is deleted.
//   * merging the callee's `simulation_init` program into the host's. The
//     host's program is decoded once, gains one child per spliced callee that
//     carries one, and is re-encoded once, by `flush()` (or the destructor),
//     instead of a decode+encode of the whole accumulated program per splice.
//     Nothing a splice does reads the host's program, so the deferral is
//     invisible until then; call `flush()` before reading it.
//   * indexing a wide neighbor's IO declarations again: the boundary fit
//     check reads an unstamped Sub output's width from its definition's decl
//     and a graph input's width under its name, both of which hhds only finds
//     by walking every declaration; each graph is indexed once per batch.
// Every refusal and diagnostic is inline_sub_instance's own.
//
// A `deadline` (a run's wall budget, e.g. lec's formal.timeout) is also checked
// INSIDE a splice, every few thousand cloned nodes and wired sinks, so one huge
// definition cannot overrun it by its whole splice. A splice that reaches it
// stops where it is and returns false with timed_out() set, leaving the host
// PARTIALLY spliced (the instance and a fraction of its clones): the caller
// must abandon the host -- report TIMEOUT-time, never read or query it. An
// unset deadline (the default) never stops a splice.
class Host_inliner {
public:
  Host_inliner(hhds::Graph* host, std::string_view from_pass,
               std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point{});
  ~Host_inliner();
  Host_inliner(const Host_inliner&)            = delete;
  Host_inliner& operator=(const Host_inliner&) = delete;

  // Splice `inst` (a Sub in the host). `new_subs`, when non-null, receives
  // (appended) the Sub clones the splice created, ascending node id.
  [[nodiscard]] bool splice(const hhds::Node_class& inst, std::vector<hhds::Node_class>* new_subs = nullptr);

  // A splice stopped at the deadline (see above): the host is partial.
  [[nodiscard]] bool timed_out() const;

  // Write the deferred simulation_init merge into the host (idempotent).
  void flush();

private:
  hhds::Graph*                        host_;
  std::string_view                    from_pass_;
  std::unique_ptr<detail::Host_state> state_;
};

// ---------------------------------------------------------------------------
// Sub port boundary: the reinterpretation a connection performs
// ---------------------------------------------------------------------------
// Both code generators realize a Sub port at its DECLARED width and sign:
// cgen_verilog declares `sub_port_width` bits (an unset width is a scalar) and
// `signed` exactly when the GraphIO decl is not unsign, and cgen_sim types the
// port from the same declaration. So a connection whose driver range does not
// fit the port's range is reinterpreted AT the boundary: a wider driver is
// truncated, a signed one read unsigned and an unsigned one read signed. Any
// transform that dissolves the boundary (inline_sub_instance, a LEC occurrence
// view) must keep that step, or it proves a design the emitted Verilog is not.
// A front end fits every connection itself, so a well-formed graph has nothing
// to fit; a malformed one (a width-less carry-in fed by a 32-bit carry-out) is
// exactly where dropping the step turned into a false PROVEN.
//
// An output takes two steps, as cgen spells it: the callee's port, then the
// parent net the instance output drives. inline_sub_instance applies both per
// instance; the LEC occurrence view (pass/lec/query.cpp) applies the port step
// once per definition and the net step per instance. Both yield the same value:
// a net no wider than the port keeps only low bits the port step leaves alone.

// The width a Sub port is realized at: `port` is the callee's own graph IO pin
// for `name`, whose stamped width wins over the decl, and an unset width is 1.
// (Not graph_util::ge_detail::sub_port_bits, which sums every declared port.)
[[nodiscard]] int sub_port_width(const hhds::Pin_class& port, const hhds::GraphIO& io, std::string_view name);

// Does `driver`'s value range fit a `bits`-wide port of the given sign? A
// driver of unknown width (an unstamped pin, a nil constant) fits: `bits` is an
// upper bound, so an unstamped pin says nothing about what it can carry.
[[nodiscard]] bool driver_fits_port(const hhds::Pin_class& driver, int bits, bool is_signed);

// The same test for a value already known to be `dbits` wide with the given
// sign (a port's own range feeding the next declaration).
[[nodiscard]] bool range_fits_port(int dbits, bool dsigned, int bits, bool is_signed);

// The driver pin, created in `graph`, of `driver` reinterpreted at a
// `bits`-wide port: a Get_mask for an unsigned port, a Sext for a signed one
// (whose `b` operand is the KEPT bit count, `bits`, not the sign position).
[[nodiscard]] hhds::Pin_class fit_to_port(hhds::Graph& graph, const hhds::Pin_class& driver, int bits, bool is_signed);

// Why `def` cannot stand in for the definition of Sub `inst` when bound by port
// NAME, or "" when it can (or when it IS the instance's own definition). Every
// port the instance declares must exist in `def` with the same direction, and
// every input `def` reads must be declared by the instance; a width stated on
// both sides must agree (0 = unspecified). A `def`-only output is allowed:
// nothing on the instance reads it.
[[nodiscard]] std::string sub_def_port_mismatch(const hhds::Node_class& inst, hhds::Graph* def);

}  // namespace livehd::graph_util
