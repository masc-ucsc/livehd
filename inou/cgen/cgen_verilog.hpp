// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "file_output.hpp"
#include "hhds/graph.hpp"
#include "hhds/index.hpp"
#include "hhds/sourcemap_emit.hpp"

class Cgen_verilog {
private:
  const bool        verbose;
  std::string_view  odir;
  const bool        srcmap;  // emit an ECMA-426 .map sidecar
  static inline int trace_module_cnt = 0;

  // Re-keyed onto hhds::Class_index — the migration moved cgen off
  // Lgraph::Node_pin::Compact_class / Node::Compact_class.
  using pin_key_t  = hhds::Class_index;
  using node_key_t = hhds::Class_index;

  struct Expr {
    Expr(std::string_view v, bool n) : var(v), needs_parenthesis(n) {}
    std::string var;
    bool        needs_parenthesis;
  };

  absl::flat_hash_map<pin_key_t, Expr>         pin2expr;
  absl::flat_hash_map<pin_key_t, std::string>  pin2var;
  absl::flat_hash_map<node_key_t, std::string> mux2vector;

  bool first_array_block;

  // Per-module set of cgen_memory_* wrapper module names already emitted into
  // the current file (whether `include`d or generated inline), so two memories
  // of the same shape do not re-define the module.
  absl::flat_hash_set<std::string> mem_wrappers_emitted_;

  std::atomic<int>                               nrunning;
  inline static std::mutex                       lgs_mutex;  // setup of static reserved_keyword
  inline static absl::flat_hash_set<std::string> reserved_keyword;

  // Helper: name for a wire, preferring user-assigned name; falls back to a
  // synthesised name from node + port_id. Graph-IO pins resolve to their
  // declared name from GraphIO.
  static std::string pin_wire_name(const hhds::Pin_class& pin);
  std::string        get_wire_or_const(const hhds::Pin_class& dpin) const;
  static std::string get_scaped_name(std::string_view name);
  static std::string get_append_to_name(std::string_view name, std::string_view ext);
  std::string        get_expression(const hhds::Pin_class& dpin) const;
  std::string        add_expression(std::string_view txt_seq, std::string_view txt_op, const hhds::Pin_class& dpin) const;

  // Resolve the "driver of this sink pin": walk inp_edges and return the
  // first edge's driver. Returns an invalid Pin_class if not connected.
  static hhds::Pin_class get_driver(const hhds::Pin_class& sink);

  // Sink pin lookup by name (e.g. "a", "din", "clock_pin"). Uses Ntype to
  // translate sink names to port_ids and find_or-not-create lookup on the
  // shadow.
  static hhds::Pin_class find_sink_pin(const hhds::Node_class& node, std::string_view name);

  void process_flop(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_latch(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  // Generate a cgen_memory_[multiclock_]<R>rd_<W>wr module body for a (R,W,clock)
  // shape that ware/rtl does not ship, mirroring the static wrapper templates.
  static std::string gen_mem_wrapper(const std::string& mod_name, int n_rd, int n_wr, bool single_clock);
  void process_memory(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_mux(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_hotmux(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_simple_node(std::shared_ptr<File_output> fout, const hhds::Node_class& node);

  void create_module_io(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_memories(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_subs(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_combinational(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_outputs(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_registers(std::shared_ptr<File_output> fout, hhds::Graph* graph);

  void add_to_pin2var(std::shared_ptr<File_output> fout, const hhds::Pin_class& dpin, std::string_view name, bool out_unsigned);
  void create_locals(std::shared_ptr<File_output> fout, hhds::Graph* graph);

  // ── ECMA-426 egress ──────────────────────────────────────────
  // One Segment per emitted statement whose node carries a SourceId: the
  // generated position is the line the statement is about to land on.
  // Resolution to file/line:col happens in hhds::sourcemap::to_json at write
  // time. note_src is a no-op unless `srcmap` is set.
  std::vector<hhds::sourcemap::Segment> map_segments_;
  void                                  note_src(const std::shared_ptr<File_output>& fout, const hhds::Node_class& node);
  void write_srcmap(const std::shared_ptr<File_output>& fout, const std::string& filename, const hhds::Source_locator& sl);

  // Module-level anchor (a graph io node, stamped by tolg with the
  // `mod`/`comb` declaration's SourceId): structural lines with no cell of
  // their own — module header, ports, always_comb begin/end, endmodule — map
  // to the unit's declaration instead of staying unmapped.
  hhds::Node_class module_anchor_;
  void             note_module(const std::shared_ptr<File_output>& fout);

public:
  void do_from_graph(const std::shared_ptr<hhds::Graph>& graph);

  Cgen_verilog(bool _verbose, std::string_view _odir, bool _srcmap = false);
};
