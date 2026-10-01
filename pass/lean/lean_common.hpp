//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Graph -> Lean CERTIFICATE machinery, shared by every direction that consumes
// it.  This is deliberately NOT "Direction 2's half of pass.lean": the pieces
// below (the Memory_info parse, LeanCtx, the CertBuild id space) are used by the
// legacy fast-model emission too.  What it draws is the line BELOW which a
// direction's emitter sits -- `IR_Semantics_pass.cpp` for Direction 2's
// `DesignCert`, and pass_lean.cpp's legacy path -- so that adding or changing
// one direction cannot reach into another.
//
// Measured seam: Direction 2's emitter calls exactly four functions from here
// (`fatal`, `node_id`, `pin_is_input`, `resolve_resize_chain`) and otherwise
// only READS what `Design_scan` carries.  Everything else it touches is already
// a proper header (latch_contract, node_util, design_cert_export).

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace livehd::lean_ir {

using Node     = hhds::Node_class;
using Node_pin = hhds::Pin_class;
using Edge     = hhds::Edge_class;

struct Emit_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Graph accessors.  Thin, but they are the ONLY spelling used below, so a
// front-end change lands in one place.
// ---------------------------------------------------------------------------
uint32_t node_id(const Node& node);
Node     pin_node(const Node_pin& pin);
Ntype_op node_op(const Node& node);
bool     node_is_flop(const Node& node);
bool     node_is_memory(const Node& node);
bool     pin_is_input(const Node_pin& pin);
bool     pin_is_const(const Node_pin& pin);

livehd::graph_util::Edge_vec inp_edges_ordered(const Node& node);

std::string sink_pin_name(const Edge& edge);
uint32_t    raw_pin_width(const Node_pin& pin);
uint32_t    raw_node_width(const Node& node);
Dlop        pin_const_value(const Node_pin& pin);
Dlop        node_const_value(const Node& node);
bool        node_output_is_signed(const Node& node);

// A memory port's role AND, for a read port, its TIMING.
//
// This was a `bool rdport`, which could not distinguish an asynchronous read
// from a synchronous one -- so read timing had to come from the cell-global
// `type`, and a memory whose read ports DIFFER has no scalar answer there. The
// importer now records timing per port (Ntype::Memory_rdport_*); these are the
// same values, named.
//
// Deliberately NOT convertible to bool: `if (p.rdport)` was true for every read
// port regardless of timing, and the whole point here is that the two differ.
// Ask `is_read()` or `is_sync_read()`.
enum class Port_timing : uint8_t {
  Write      = 0,
  AsyncRead  = 1,
  SyncRead   = 2,
};

struct Memory_port_info {
  size_t      port_id = 0;
  Port_timing timing  = Port_timing::Write;
  Node_pin    addr;
  Node_pin    din;
  Node_pin    enable;
  Node_pin    clock;
  uint32_t    driver_pid = 0;  // read-output driver pin id, valid only for a read port

  [[nodiscard]] bool is_read() const { return timing != Port_timing::Write; }
  [[nodiscard]] bool is_sync_read() const { return timing == Port_timing::SyncRead; }
  [[nodiscard]] bool is_async_read() const { return timing == Port_timing::AsyncRead; }
};

struct Memory_info {
  Node                          node;
  uint32_t                      nid        = 0;
  std::string                   field;
  std::string                   raw_name;
  uint32_t                      bits       = 0;
  uint32_t                      addr_width = 1;
  uint64_t                      size       = 0;
  uint32_t                      wensize    = 0;
  int64_t                       type       = 0;
  // Read-during-write forwarding MATRIX (graph/cell.cpp pid 5), NOT a bool: bit
  // (r * n_write_ports + w) set => read ordinal r sees write ordinal w's new data
  // on a same-cycle same-address collision.  `fwd_known` is false when the pin is
  // non-constant or the matrix does not fit an i64; the emitter then refuses any
  // memory where the policy is observable (>=1 read and >=1 write port) rather
  // than silently assuming read-first.
  int64_t                       fwd        = 0;
  bool                          fwd_known  = true;
  int64_t                       posclk     = 1;
  // ordering="none" (graph/cell.cpp pid 15): is ANY (read,write) collision
  // window undefined? Kept as a bool, not the matrix — the emitter refuses the
  // whole memory on the first set bit, and a wide matrix does not fit an i64.
  bool                          undef      = false;
  std::vector<Memory_port_info> ports;
  std::vector<size_t>           read_ports;
  std::vector<size_t>           write_ports;
  // EXISTENCE only: "does this memory have at least one synchronous read
  // port?". NEVER a per-port answer -- a mixed memory has both, so every
  // output / next-state / state-field decision must consult the PORT's own
  // `timing`. Derived from the ports, not from the cell-global `type`.
  bool                          any_sync_read = false;
  std::map<size_t, std::string> read_reg_field;                 // port_id -> st_ read-data register field (SYNC read ports only)

  // Initialization / whole-array pins (graph/cell.cpp pids 11..14).  RECORDED
  // during the pin walk and CLASSIFIED after it: the strict ROM test needs the
  // read/write port split, and `memory_policy_summary` needs it too -- reporting
  // from inside the walk printed `rdports=0 wrports=0 addr_width=1` (struct
  // defaults) for memories that in fact have a read port, because a ROM's port
  // pins sit at raw pids 0/2/4/10, all BELOW `init` at 11.
  Node_pin init_pin;
  Node_pin update_pin;
  Node_pin update_enable_pin;
  Node_pin bulk_reset_pin;

  // Immutable ROM: `init` is a constant AND no write port is active AND `update`
  // is not driven AND the type is one whose read semantics we model.  A WRITABLE
  // initialized memory is RAM-with-initial-contents, which is a different thing
  // and is refused rather than silently treated as constant.
  bool                     is_rom = false;
  std::vector<std::string> rom_contents;  // `size` entries, each `bits` wide, entry 0 first
};

struct LeanCtx {
  hhds::Graph* g = nullptr;
  std::string  top_name;
  std::string  base_name;
  bool         strict = true;
  // `formal.lean.mode=verified_compiler`.  Read by parse_memory_info: features
  // whose Lean counterpart exists ONLY in the verified-compiler model (ROM
  // contents, async reset, a nonzero reset value) are accepted there and still
  // refused on the legacy path, where the corresponding state is unconstrained.
  bool         verified_compiler = false;
  size_t       max_width = 1024;
  // Refuse a combinational cone larger than this many nodes.  Spelled through
  // `parse_max_width`, so "0"/"unlimited"/"inf"/"none" all arrive here as
  // SIZE_MAX -- which is the DEFAULT, deliberately: a node budget that fires out
  // of the box could refuse a design that emits fine today.  The traversal's own
  // push ceiling is the automatic guard; this is the one an operator sets.
  size_t       max_nodes = std::numeric_limits<size_t>::max();

  absl::flat_hash_set<std::string> used_fields;

  std::map<std::string, std::string> input_field;
  std::map<std::string, uint32_t>    input_width;
  std::map<std::string, uint32_t>    input_source_id;

  std::map<std::string, std::string> output_field;
  std::map<std::string, uint32_t>    output_width;

  std::map<uint32_t, std::string> flop_field;
  std::map<uint32_t, uint32_t>    flop_width;

  std::map<uint32_t, Memory_info> memory_info;
  // (memory nid << 32 | read driver_pid) -> certificate id of that read port's
  // Op_MemRead node.  Populated by cert_memory_expand; read by driver_expr, which
  // in bridge mode must name the factored `fv` def (there is no let-chain to bind
  // `n_<mem>_p<pid>`).
  std::map<uint64_t, uint32_t>    mem_read_fv;

  // Fast-view bridge (step 5) emission: when bridge_fv_mode is set, driver_expr
  // references an internal node's value as a factored top-level def
  // `<base>_fv<id><bridge_fv_args>` (e.g. " i" or " i s") instead of the local
  // let name `n_<id>`, so φ and the per-node have-chain can name each value.
  bool         bridge_fv_mode = false;
  std::string  bridge_fv_args;
};

// A diagnosed refusal.  `[[noreturn]]`: every caller may treat the design as
// gone after this, and the thrown `Emit_error` becomes a named
// `[ERROR] pass.lean:` diagnostic at the pass boundary.
[[noreturn]] void fatal(const LeanCtx& ctx, const std::string& msg);

// The declared port name of a graph-input pin, or "" when the pin is not one.
std::string input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin);

// Follow width-only reshaping back to the pin that really drives a signal.
//
// After yosys + cprop a top-level port does not reach its consumers directly: it
// arrives through resize nodes -- an arity-1 Or, or a Get_mask against an
// all-ones constant mask (get_mask(a,-1) == zext(a), per the LiveHD spec).  Both
// preserve the value, so for the purpose of asking "is this signal a primary
// input?" they are transparent.  Testing the immediate driver instead reports
// "computed inside the design" for what is plainly a port.
//
// Only single-input reshaping is followed; anything else stops the walk, and a
// bounded loop count guards against a malformed graph.
Node_pin resolve_resize_chain(const Node_pin& start);

struct CertBuild {
  std::set<uint32_t> source_ids;
  std::map<uint32_t, std::string> source_exprs;
  // Fast-view bridge (step 5): per-source `bvenc`-able BitVec leaf + kind, so the
  // emitter can generate `<base>_src<id> : sourceEnv id = bvenc <leaf>` facts.
  std::map<uint32_t, std::string> source_leaf;  // BitVec expr: i.f / s.f / BitVec.ofInt w c
  std::map<uint32_t, int>         source_kind;  // 0 = input, 1 = const, 2 = flop
  std::map<uint32_t, uint32_t>    source_width; // BitVec width of the source leaf
  // Verified-compiler exporter: the constant's Lean `Int` text, kept verbatim so
  // `SourceDesc.const` need not re-parse it out of `source_exprs`.
  std::map<uint32_t, std::string> source_const_int;
  // ROM tables, by array-source id: `size` entries, each `bits` wide, entry 0
  // first.  Source kind 4 (an IMMUTABLE table) as opposed to kind 3 (a mutable
  // array image carried in RuntimeState).
  std::map<uint32_t, std::vector<std::string>> source_rom_contents;
  // Synthetic read-data registers of SYNC memories, by their source id ->
  // (width, cert id of the next value).  A sync memory is not stateless even
  // when its table is immutable: the registered read port IS state, and the
  // certificate already models it as `if read_enable then table[addr] else old`
  // (an Op_MuxBool).  These become ordinary FlopDescs in the DesignCert.
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> sync_read_regs;
  // ...and the memory each one belongs to (source id -> memory nid), so the
  // register inherits its memory's CLOCK ordinal in the certificate.
  std::map<uint32_t, uint32_t> sync_read_owner;
  uint32_t next_synth_id = 1000000000;

  // ---- Memory decomposition (step 5 memory path) --------------------------
  // A Memory node is multi-output (N read-data values plus the array next state)
  // while NodeCert carries ONE width and ONE value, so a memory is decomposed
  // into single-valued cert nodes -- see cert_memory_expand.  These maps let a
  // consumer's cert_dep_id resolve a memory read-data pin, and let the bridge
  // codegen tell a `.mem`-valued id from a `.bv`-valued one.
  std::set<uint32_t>              mem_valued;   // cert ids whose CertVal is `.mem`
  std::set<uint32_t>              mem_raw_reads; // Op_MemRead ids with a literal enable (sync raw read)
  std::map<uint64_t, uint32_t>    mem_read_id;  // (mem nid<<32 | driver_pid) -> cert id
  // Emitted-text side of the decomposition, keyed by cert id: the fast-model
  // expression each synthetic node's `fv` def must carry.
  std::map<uint32_t, std::string> synth_fv_expr;
  std::map<uint32_t, std::string> synth_fv_type;
};

// Certificate ids of one memory node's decomposition.
struct MemCertIds {
  uint32_t                   array_src  = 0;  // source id: the committed array image
  uint32_t                   next_chain = 0;  // all-writes chain tail (== array_src if write-less)
  std::map<size_t, uint32_t> read_out;        // port_id -> id whose value is the port's read DATA
  std::map<size_t, uint32_t> rdreg_src;       // port_id -> read-data register source id (sync only)
  std::map<size_t, uint32_t> rdreg_one;       // port_id -> the literal-1 read-enable source of the raw read
  std::map<size_t, uint32_t> raw_read;        // port_id -> cert id of the UNGATED Op_MemRead (sync only)
  std::map<size_t, uint32_t> rdreg_next;      // port_id -> cert id of that register's next value
};

// Structured view of one emitted node certificate, captured for bridge codegen.
struct CertNodeInfo {
  uint32_t              nid = 0;
  std::string           op_expr;   // e.g. "LGraphOp.Op_And"
  uint32_t              width = 0;
  std::vector<uint32_t> deps;
};

// ---------------------------------------------------------------------------
// The certificate DAG, and its one topological order.
//
// A certificate node's dependencies are the ids in `CertNodeInfo::deps`, and
// those are the ONLY dependencies the Lean model has.  That is a finer relation
// than the LGraph's, and the difference is the whole point:
//
//   * a Memory CELL is one LGraph node with every port's pins on it, so
//     read-output -> logic -> write-input looks like a self-dependency;
//   * the certificate decomposes it per port.  A read sees the write chain of
//     the write ports FORWARDED to it (`memory_fwd_bit`) and no others, and a
//     SYNCHRONOUS read sees a register source, which depends on nothing in the
//     current cycle.
//
// So the order is decided HERE, over the combined DAG (ordinary nodes plus the
// synthetic nodes a memory decomposes into), not over the LGraph node vector.
// A cycle that survives this IS a cycle in the model -- a read forwarded from a
// write whose data it feeds -- and must be refused.
// ---------------------------------------------------------------------------

// Certificate nodes as they are BUILT, before their order is known.  The
// `seed` is only the order candidates are first considered in; a synthetic
// memory id has no LGraph node and therefore no position of its own.
//
// RESERVE-THEN-FILL, and the reason is cross-memory dependency.  A memory's
// port pins may be driven by ANOTHER memory's read output, and `cert_dep_id`
// resolves such a pin through `CertBuild::mem_read_id`.  If each memory were
// allocated and filled in one go, whichever memory happened to be expanded
// first would fatal on the other's read pin -- and which one that is would
// depend on graph iteration order.  So every memory's externally visible read
// ids are RESERVED first, and only then is any port's dependency built.
//
// It also FAILS CLOSED on malformed construction.  `by_id`, `text` and `seed`
// are three views of one set, and an id silently appearing in two of them (or
// in `sources` as well) would quietly change the model: a node that is also a
// source is skipped as a leaf by the sort, so its operator would never run.
// `errors()` lists every such inconsistency and `cert_dag_order` refuses on it.
struct CertPool {
  std::map<uint32_t, CertNodeInfo> by_id;
  std::map<uint32_t, std::string>  text;    // the emitted `{ nid := ... }` record
  std::vector<uint32_t>            seed;
  std::map<uint32_t, std::string>  origin;  // id -> what to call it in a diagnostic

  // Claim `id`'s place in the order now; its deps and text arrive from `fill`.
  void reserve(uint32_t id, std::string what = {}) {
    seed.push_back(id);
    if (!what.empty()) {
      origin[id] = std::move(what);
    }
  }

  void fill(uint32_t id, std::string node_text, CertNodeInfo info) {
    by_id[id] = std::move(info);
    text[id]  = std::move(node_text);
  }

  void add(uint32_t id, std::string node_text, CertNodeInfo info, std::string what = {}) {
    reserve(id, std::move(what));
    fill(id, std::move(node_text), std::move(info));
  }

  [[nodiscard]] bool reserved(uint32_t id) const {
    return std::find(seed.begin(), seed.end(), id) != seed.end();
  }

  // Every way the three views can disagree.  Empty iff the pool is well formed.
  [[nodiscard]] std::vector<std::string> errors(const std::set<uint32_t>& sources) const;
};

struct Cert_dag_result {
  std::vector<uint32_t> order;
  bool                  ok = true;
  std::string           reason;  // empty iff ok
};

// A DFS post-order over `pool.seed`, emitting each node after its deps.
//
// STABLE: when `seed` is already a valid dependency order the result is `seed`
// unchanged, so a design with no memory is ordered exactly as before and its
// emitted text does not move.
//
// Fails closed on three things, with the id's `origin` text when it has one: a
// malformed pool (see `errors`), a dependency cycle, and a dependency that is
// neither a certificate node nor a source (a dangling reference would otherwise
// become a silently wrong model).
Cert_dag_result cert_dag_order(const CertPool& pool, const std::set<uint32_t>& sources);

// ---------------------------------------------------------------------------
// What the shared scan hands a direction's emitter.
//
// REFERENCES, not copies: these name locals of `Pass_lean::emit_for_graph`,
// which outlive the emitter call.  Const throughout except `cert_build`, whose
// synthetic-id counter an emitter may still advance.  Kept as one struct rather
// than eighteen parameters so that adding a scan result does not re-sign every
// direction.
// ---------------------------------------------------------------------------
struct Design_scan {
  const LeanCtx&     ctx;
  hhds::Graph*       g = nullptr;
  const std::string& raw_name;   // the RTL name, as reported
  const std::string& base_name;  // sanitized for Lean
  const std::string& lean_path;

  const std::vector<Node>&               flop_nodes;
  const std::map<uint32_t, Node_pin>&    flop_reset;
  const std::set<uint32_t>&              flop_async;       // ASYNCHRONOUS reset
  const std::set<uint32_t>&              flop_active_low;  // `negreset`
  const std::map<uint32_t, std::string>& flop_initial;     // nid -> Lean Int reset value
  const std::map<uint32_t, Node_pin>&    flop_negreset;

  CertBuild&                            cert_build;
  const std::vector<CertNodeInfo>&      cert_infos;
  const std::map<uint32_t, MemCertIds>& mem_cert_ids;

  const std::map<std::string, uint32_t>& output_cert_ids;
  const std::map<uint32_t, uint32_t>&    flop_din_cert_ids;
  const std::map<uint32_t, uint32_t>&    flop_reset_cert_ids;
  const std::map<uint32_t, uint32_t>&    flop_enable_cert_ids;
  const std::vector<uint32_t>&           source_ids;
};

// ---------------------------------------------------------------------------
// The directions.  One entry point each, taking the shared scan and owning
// everything below it.  Direction 2 is the only one split out so far; the
// legacy fast-model emission still lives in pass_lean.cpp.
// ---------------------------------------------------------------------------
namespace ir_semantics {
// Direction 2: emit `<Top>_designCert` (IR_Semantics_pass.cpp), or refuse via
// `fatal`.  Returning ends the pass's work on this graph.
void emit(const Design_scan& scan);
}  // namespace ir_semantics

}  // namespace livehd::lean_ir
