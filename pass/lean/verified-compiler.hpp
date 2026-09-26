// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Declarations shared between the LEGACY per-design emitter (`pass_lean.cpp`)
// and the VERIFIED-COMPILER exporter (`verified-compiler.cpp`).
//
// The split exists because the two emit different artifacts from the same walk.
// The legacy path emits a fast model, a certificate and per-design bridge proofs;
// the verified-compiler path emits ONLY `<Top>_designCert` and lets
// `Compiler.compileDesign` -- proved once, for every design it accepts -- supply
// the model and its correctness.  Everything here is the walk's shared state;
// nothing in this header is specific to either artifact.

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace lean_pass {

using Node     = hhds::Node_class;
using Node_pin = hhds::Pin_class;
using Edge     = hhds::Edge_class;


struct Emit_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Trivial graph accessors, inline so both translation units share one definition.
inline uint32_t node_id(const Node& node) { return static_cast<uint32_t>(node.get_debug_nid()); }
inline Node     pin_node(const Node_pin& pin) { return pin.get_master_node(); }
inline Ntype_op node_op(const Node& node) { return livehd::graph_util::type_op_of(node); }
inline bool     node_is_flop(const Node& node) { return livehd::graph_util::is_type_flop(node); }
inline bool     node_is_memory(const Node& node) { return node_op(node) == Ntype_op::Memory; }
inline bool     pin_is_input(const Node_pin& pin) { return livehd::graph_util::is_graph_input_pin(pin); }
inline bool     pin_is_const(const Node_pin& pin) { return livehd::graph_util::is_const_pin(pin); }

inline Dlop pin_const_value(const Node_pin& pin) { return livehd::graph_util::hydrate_const(pin); }

// Input edges in a deterministic order: by sink port id, then driver index.
// Both emitters depend on this order -- a node's operand positions ARE its
// certificate dependency order.
inline livehd::graph_util::Edge_vec inp_edges_ordered(const Node& node) {
  auto edges = node.inp_edges();
  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    const auto ap = a.sink.get_port_id();
    const auto bp = b.sink.get_port_id();
    if (ap != bp) {
      return ap < bp;
    }
    return a.driver.get_class_index().value < b.driver.get_class_index().value;
  });
  return edges;
}

struct Memory_port_info {
  size_t   port_id = 0;
  bool     rdport  = false;
  Node_pin addr;
  Node_pin din;
  Node_pin enable;
  Node_pin clock;
  uint32_t driver_pid = 0;  // read-output driver pin id, valid only for rdport
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
  bool                          sync = false;                   // type == 1 (registered read data)
  std::map<size_t, std::string> read_reg_field;                 // port_id -> st_ read-data register field (sync only)

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

[[noreturn]] void fatal(const LeanCtx& ctx, const std::string& msg);

// Defined in pass_lean.cpp; used by both emitters.
std::string input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin);
Node_pin    resolve_resize_chain(const Node_pin& start);

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
// The hand-off from the shared walk to the verified-compiler exporter.
//
// These are `emit_for_graph`'s locals, filled by the topological walk before
// either emitter runs.  Passed by reference in one struct rather than as a
// dozen parameters, so adding a certificate field does not re-thread a
// signature through two files.
// ---------------------------------------------------------------------------
struct VerifiedCompilerInputs {
  const std::vector<CertNodeInfo>&              cert_infos;
  const std::vector<uint32_t>&                  source_ids;
  const std::map<std::string, uint32_t>&        output_cert_ids;
  const std::map<uint32_t, uint32_t>&           flop_din_cert_ids;
  const std::map<uint32_t, uint32_t>&           flop_enable_cert_ids;
  const std::map<uint32_t, uint32_t>&           flop_reset_cert_ids;
  const std::map<uint32_t, Node_pin>&           flop_reset;  // nid -> reset net (async check)
  const std::map<uint32_t, std::string>&        flop_initial;
  const std::map<uint32_t, Node_pin>&           flop_negreset;
  const std::set<uint32_t>&                     flop_active_low;
  const std::set<uint32_t>&                     flop_async;
  const std::map<uint32_t, MemCertIds>&         mem_cert_ids;
};

// Emit `<Top>_designCert` and nothing else.  Called only under
// `formal.lean.mode=verified_compiler`; the legacy emitter continues past the
// call site when the mode is off.
void emit_verified_compiler(const LeanCtx& ctx, const CertBuild& cert_build,
                            const VerifiedCompilerInputs& in, const std::string& raw_name,
                            const std::string& base_name, const std::string& lean_path);

}  // namespace lean_pass
