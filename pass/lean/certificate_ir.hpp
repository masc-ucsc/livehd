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
};
struct MemoryDriver {
  uint32_t addr_w = 0, data_w = 0, next_img = 0;
  uint32_t origin = 0;
};
struct CertificateIR {
  std::vector<Source>          sources;
  std::vector<CertNode>        nodes;
  std::vector<Output>          outputs;
  std::vector<FlopDriver>      flops;
  std::vector<MemoryDriver>    memories;
  std::map<uint32_t, uint32_t> slot_of;
};
struct CertificateOptions {};
CertificateIR build_certificate(const DesignScan& design, const CertificateOptions& options);
// Validate identity and dependency order and assign dense source/node slots.
void          index_certificate(CertificateIR& certificate);
}  // namespace lean_export
