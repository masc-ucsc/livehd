// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <ostream>

#include "design_scan.hpp"

namespace lean_export {
enum class Operation {
  Const,
  Sum,
  Mult,
  UDiv,
  SDiv,
  And,
  Or,
  Xor,
  Ror,
  EQ,
  Not,
  SLT,
  ULT,
  SGT,
  UGT,
  SHL,
  SRA,
  MuxBool,
  MuxN,
  Sext,
  GetMask,
  SetMask,
  MemRead,
  MemWrite,
  MemWriteBE
};
struct Op {
  Operation   kind      = Operation::Or;
  uint32_t    parameter = 0;  // Sum add count or MemWriteBE lane width
  std::string value;          // Const decimal integer
};
enum class SourceKind { Input, Const, Flop, MemImage, RomConst };
struct Source {
  uint32_t                 id    = 0;
  SourceKind               kind  = SourceKind::Input;
  uint32_t                 width = 0, addr_w = 0;
  std::string              const_int;
  uint32_t                 ordinal          = 0;
  bool                     async_reset      = false;
  uint32_t                 reset_input      = 0;
  std::string              reset_value      = "0";
  bool                     reset_active_low = false;
  std::vector<std::string> rom_contents;
  bool                     implicit_enable = false;  // always-enabled raw read of a synchronous memory
};
struct CertNode {
  uint32_t              id = 0;
  Op                    op;
  uint32_t              width = 0;
  std::vector<uint32_t> deps;
};
struct Output {
  uint32_t id = 0, width = 0;
};
struct FlopDriver {
  // Origin is diagnostic/schema provenance, never a second semantic ID.
  uint32_t                origin = 0;
  std::optional<size_t>   read_port;
  uint32_t                width = 0, din = 0;
  std::optional<uint32_t> enable, reset_pin;
  std::string             reset_value      = "0";
  bool                    reset_active_low = false;
  // Resolved by the SCAN (DesignScan::clocks); carried, never recomputed.
  uint32_t                clock = 0;
  // An asynchronous reset acts on both the visible Q and the committed state
  // with no edge, so it is a property of the commit and must reach the
  // certificate. A synchronous reset instead holds while its clock is quiet,
  // which the edge-sensitive commit already expresses.
  bool                    async_reset = false;
};
struct MemoryDriver {
  uint32_t addr_w = 0, data_w = 0, next_img = 0;
  uint32_t origin = 0;
  uint32_t clock  = 0;
};
// One emitted clock. Mirrors DesignScan::ClockDomain, minus the structural key:
// the key is the scan's grouping identity and has no meaning in the certificate,
// where a domain IS its ordinal.
struct CertClock {
  std::string name;
};
struct CertificateIR {
  std::vector<Source>          sources;
  std::vector<CertNode>        nodes;
  std::vector<Output>          outputs;
  std::vector<FlopDriver>      flops;
  std::vector<MemoryDriver>    memories;
  std::map<uint32_t, uint32_t> slot_of;
  // Always non-empty, so every `clock` ordinal above is in range and the
  // emitted `_clock_bounds` obligation is discharged against a real table.
  std::vector<CertClock>       clocks{{"clock"}};
};
struct CertificateOptions {};
CertificateIR build_certificate(const DesignScan& design, const CertificateOptions& options);
// Validate identity and dependency order and assign dense source/node slots.
void          index_certificate(CertificateIR& certificate);
}  // namespace lean_export
