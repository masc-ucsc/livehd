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
  // `op == ScanOp::Memory` ONLY: which READ PORT of memory `id` this entry
  // schedules, as an ordinal into `Memory::read_ports`. One memory contributes
  // one entry per read port whose output is consumed, each at its own place in
  // the order, because two read ports of one array are two independent values
  // over one shared image -- a dependency from one to the other's address is an
  // ordinary DAG edge, not a cycle. The image itself, and the next-state write
  // chain, stay ONE identity per memory.
  uint32_t             memory_read = 0;
};
struct Port {
  std::string           name;
  uint32_t              id    = 0;
  uint32_t              width = 0;
  std::optional<PinRef> driver;
};
// An OWNED clock domain. The scan resolves every committing state element to one
// of these and stores only the resolved result -- no graph handle, no pin, no
// reconstruction downstream. The emitter must be able to format a certificate
// from a DesignScan alone (R13.3: "the emitter must not consult HHDS or
// reconstruct domains").
//
// `key` is `Commit_class::key()`: the structural (root-net, edge) identity from
// graph/latch_contract.hpp. It is derived structurally rather than from a nid or
// traversal order, so two scans of the same design agree. It is the grouping
// identity; `name` is only for humans and for the emitted `ClockDesc.name`.
//
// `key` does NOT discriminate bus BITS. `net_key()` names a graph input by its
// NAME, and `control_root` follows every Get_mask to the whole input, so
// `clks[0]` and `clks[1]` produce the SAME key. R13.3 requires that a bus
// clock's bit either survive into the ordinal or be refused, never merged --
// two 180-degrees-apart domains that compare equal are exactly the
// clock-blindness false-PROVEN class the latch contract guards against. The
// scanner therefore REFUSES a multi-bit clock root outright
// (`clock_root_bit_refusal`); preserving the bit is the stronger option and is
// not implemented here.
struct ClockDomain {
  std::string key;                // Commit_class::key() -- structural identity
  std::string name;               // human-facing; emitted as ClockDesc.name
  bool        rising   = true;    // commits on the rise of `key`'s net
  bool        implicit = false;   // the module's implicit clock (`reg x = 0`)
};

struct Flop {
  uint32_t                id    = 0;
  uint32_t                width = 0;
  std::optional<PinRef>   din, enable, reset, clock;
  // Index into DesignScan::clocks. Defaulted to 0 so a scan that has not run
  // domain resolution still describes the single-domain design it used to.
  uint32_t                clock_ordinal = 0;
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
  // Resolved domain of THIS port. A port that does not commit (an asynchronous
  // read) carries no obligation and keeps `has_domain == false`.
  uint32_t              clock_ordinal = 0;
  bool                  has_domain    = false;
};
struct Memory {
  uint32_t                 id         = 0;
  uint32_t                 bits       = 0;
  uint32_t                 addr_width = 1;
  uint64_t                 size       = 0;
  uint32_t                 wensize    = 1;
  int64_t                  type = 0, posclk = 1;
  // Read-during-write forwarding, as PER-READ ROWS: `fwd_rows[r][w]` means read
  // ordinal r sees write ordinal w's new data on a same-address collision.
  // Rows rather than one integer because the matrix is reads x writes, and a
  // wide memory's does not fit any fixed-width window.  Plain `bool` rows keep
  // this struct free of `Dlop` and so free of HHDS, which is the contract
  // directly below.
  std::vector<std::vector<bool>> fwd_rows;
  // The ONE domain this memory commits on. `DesignCert` carries a single
  // `MemoryDesc.clock`, which is a CERTIFICATE RESTRICTION, not a hardware
  // guarantee: cells have a per-port `clock_pin` and one global `posclk`, and a
  // shared polarity does not imply a shared root. The scan therefore enforces
  // its own common-domain policy across every committing port and refuses
  // distinct roots rather than silently picking one.
  uint32_t                 clock_ordinal = 0;
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
  // Owned clock table, in deterministic ordinal order. Always non-empty: a
  // design with no resolved domain still gets the single default entry, so
  // `clock_ordinal == 0` is always in range and `clocks.size()` is the bound
  // every emitted `_clock_bounds` obligation is checked against.
  std::vector<ClockDomain>   clocks;
};
DesignScan scan_design(hhds::Graph& graph, const ScanOptions& options);
}  // namespace lean_export
