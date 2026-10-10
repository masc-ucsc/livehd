// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hhds {
class Graph;
}
namespace lean_export {

struct ScanOptions {
  bool   strict    = true;
  size_t max_width = 1024;
};

// Decimal integers are owned data, never Lean expressions.
enum class PinKind { Node, Input, Constant, Flop, Memory };
struct PinRef {
  PinKind     kind            = PinKind::Node;
  uint32_t    id              = 0;
  uint32_t    port            = 0;
  uint32_t    width           = 0;
  uint32_t    intrinsic_width = 0;
  std::string value;
  uint32_t    minimum_shift_width = 0;
};
enum class ScanOp { Constant, Sum, Mult, Div, And, Or, Xor, Ror, EQ, Not, LT, GT, SHL, SRA, Mux, Sext, GetMask, SetMask, Memory };
struct Operand {
  // The sink pid this operand arrived on. Under ONE DRIVER PER SINK PIN a cell
  // spends one pid per operand, so the pid is the operand's SLOT: Mux option k
  // is pid k, and a lowering that needs contiguous control/value pairs reads
  // this field.
  uint32_t port;
  // The operand BANK (`Ntype::sink_bank` of `port`), which is the operand's
  // ROLE. For a banked cell the role rides on pid PARITY, not on the pid: a
  // Sum's addends are the even pids and its subtrahends the odd ones, so three
  // adds and one subtract occupy {0, 2, 4} and {1}. Selecting a role on the raw
  // pid would keep only the first slot of each bank and silently DROP the rest.
  // For an unbanked cell `sink_bank` is the identity, so `bank == port` and a
  // consumer may compare against it unconditionally.
  uint32_t bank;
  PinRef   driver;
};
struct DesignNode {
  uint32_t             id        = 0;
  ScanOp               op        = ScanOp::Or;
  uint32_t             width     = 0;
  bool                 is_signed = false;
  std::vector<Operand> operands;
};
struct Port {
  std::string           name;
  uint32_t              id    = 0;
  uint32_t              width = 0;
  std::optional<PinRef> driver;
};
struct Flop {
  uint32_t                id    = 0;
  uint32_t                width = 0;
  std::optional<PinRef>   din, enable, reset, clock;
  bool                    asynchronous = false;
  bool                    active_low   = false;
  std::string             initial      = "0";
  std::optional<uint32_t> reset_input;
  // When the reset cone does NOT reach a primary input, what the transparent
  // walk stopped on: `<op> n_<id>`. An asynchronous flop whose reset cannot be
  // resolved is refused, and the refusal is only actionable if it names the
  // node that blocked it -- "not driven by a primary input" alone does not say
  // whether the cone holds an unhandled resize wrapper or real logic.
  std::string             reset_block;
  std::string             raw_name;
};
struct MemoryPort {
  size_t                port_id    = 0;
  uint32_t              driver_pid = 0;
  PinRef                addr, din, enable;
  std::optional<PinRef> clock;
};
struct Memory {
  uint32_t                 id         = 0;
  uint32_t                 bits       = 0;
  uint32_t                 addr_width = 1;
  uint64_t                 size       = 0;
  uint32_t                 wensize    = 1;
  int64_t                  type = 0, fwd = 0, posclk = 1;
  bool                     sync = false, is_rom = false;
  std::vector<std::string> rom_contents;
  std::vector<MemoryPort>  ports;
  std::vector<size_t>      read_ports, write_ports;
  std::string              raw_name;
};
// No graph handles or formatting state survive scanning. Safe to use after the
// graph is closed, and reusable by consumers that do not link HHDS or EPRP.
struct DesignScan {
  std::string                name;
  ScanOptions                policy;
  std::vector<Port>          inputs, outputs;
  std::vector<Flop>          flops;
  std::map<uint32_t, Memory> memories;
  std::vector<PinRef>        roots;
  std::vector<DesignNode>    nodes;  // reachable topological order
};
DesignScan scan_design(hhds::Graph& graph, const ScanOptions& options);
}  // namespace lean_export
