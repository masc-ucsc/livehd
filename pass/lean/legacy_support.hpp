// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "graph_access.hpp"
namespace lean_pass {
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


} // namespace lean_pass
