// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Internal HHDS access and memory-policy validation shared with the compatibility
// implementation. Public scanner results are owned values in design_scan.hpp.

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
  uint32_t                      nid = 0;
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
  bool                          sync = false;    // type == 1 (registered read data)
  std::map<size_t, std::string> read_reg_field;  // port_id -> st_ read-data register field (sync only)

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
  bool         strict            = true;
  // `formal.lean.mode=verified_compiler`.  Read by parse_memory_info: features
  // whose Lean counterpart exists ONLY in the verified-compiler model (ROM
  // contents, async reset, a nonzero reset value) are accepted there and still
  // refused on the legacy path, where the corresponding state is unconstrained.
  bool         verified_compiler = false;
  size_t       max_width         = 1024;

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
  bool        bridge_fv_mode = false;
  std::string bridge_fv_args;
};

[[noreturn]] void fatal(const LeanCtx& ctx, const std::string& msg);

std::string        input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin);
Node_pin           resolve_resize_chain(const Node_pin& start);
std::string        sink_pin_name(const Edge& edge);
uint32_t           raw_pin_width(const Node_pin& pin);
uint32_t           raw_node_width(const Node& node);
Dlop               node_const_value(const Node& node);
bool               node_output_is_signed(const Node& node);
void               check_width(const LeanCtx& ctx, const Node& node, uint32_t w, std::string_view what);
uint32_t           intrinsic_const_width(const Dlop& v);
uint32_t           pin_width(const LeanCtx& ctx, const Node_pin& pin, const Node& owner);
uint32_t           data_dep_width(const LeanCtx& ctx, const Node_pin& pin, const Node& owner, uint32_t op_w);
uint32_t           node_width(const LeanCtx& ctx, const Node& node);
uint32_t           ceil_log2_u64(uint64_t v);
int64_t            const_pin_int(const LeanCtx& ctx, const Node_pin& pin, const Node& owner, std::string_view field);
int64_t            const_pin_int_or(const Node_pin& pin, int64_t dflt);
std::string        memory_policy_summary(const Memory_info& mi);
Memory_info        parse_memory_info(LeanCtx& ctx, const Node& node);
const Memory_info& memory_info_for(const LeanCtx& ctx, const Node& node);
uint32_t           minimal_unsigned_const_width(const Dlop& v);
size_t             memory_read_ordinal(const Memory_info& mi, size_t port_idx);
bool               memory_fwd_bit(const Memory_info& mi, size_t r_ord, size_t w_ord);

}  // namespace lean_pass
