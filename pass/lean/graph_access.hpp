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
using Edge     = livehd::graph_util::Sink_driver<Node_pin>;

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
inline bool     pin_is_const(const Node_pin& pin) { return pin.is_const(); }

inline Dlop pin_const_value(const Node_pin& pin) { return livehd::graph_util::const_of(pin); }

// Translate master's per-operand sink slots into the historical bank order.
// The owned IR uses semantic ports (Sum: 0=add, 1=subtract), not slot numbers.
// Sort drivers within each bank as before, keeping duplicate operands on their
// distinct slots. Fixed-port cells retain their port order.
inline auto inp_edges_ordered(const Node& node) {
  auto       edges = livehd::graph_util::inp_sink_drivers(node);
  const auto op    = node_op(node);
  std::sort(edges.begin(), edges.end(), [op](const Edge& a, const Edge& b) {
    const auto ap = Ntype::sink_bank(op, a.sink.get_port_id());
    const auto bp = Ntype::sink_bank(op, b.sink.get_port_id());
    if (ap != bp) {
      return ap < bp;
    }
    const auto ad = a.driver.get_class_index().value;
    const auto bd = b.driver.get_class_index().value;
    return ad != bd ? ad < bd : a.sink.get_port_id() < b.sink.get_port_id();
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
  bool                          sync = false;  // type == 1 (registered read data)

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
  hhds::Graph*                    g         = nullptr;
  bool                            strict    = true;
  size_t                          max_width = 1024;
  std::map<std::string, uint32_t> input_width;
  std::map<std::string, uint32_t> input_source_id;
  std::map<uint32_t, uint32_t>    flop_width;
  std::map<uint32_t, Memory_info> memory_info;
};

[[noreturn]] void fatal(const LeanCtx& ctx, const std::string& msg);

std::string        input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin);
Node_pin           resolve_resize_chain(const Node_pin& start);
std::string        sink_pin_name(const Edge& edge);
uint32_t           raw_pin_width(const Node_pin& pin);
uint32_t           raw_node_width(const Node& node);
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
