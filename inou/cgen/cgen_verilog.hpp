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

class Cgen_verilog {
private:
  const bool        verbose;
  std::string_view  odir;
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

  std::atomic<int>                               nrunning;
  inline static std::mutex                       lgs_mutex;  // setup of static reserved_keyword
  inline static absl::flat_hash_set<std::string> reserved_keyword;

  // Helper: name for a wire, preferring user-assigned name; falls back to a
  // synthesised name from node + port_id. Graph-IO pins resolve to their
  // declared name from GraphIO.
  static std::string         pin_wire_name(const hhds::Pin_class& pin);
  std::string                get_wire_or_const(const hhds::Pin_class& dpin) const;
  static std::string         get_scaped_name(std::string_view name);
  static std::string         get_append_to_name(std::string_view name, std::string_view ext);
  std::string                get_expression(const hhds::Pin_class& dpin) const;
  std::string                add_expression(std::string_view txt_seq, std::string_view txt_op, const hhds::Pin_class& dpin) const;

  // Resolve the "driver of this sink pin": walk inp_edges and return the
  // first edge's driver. Returns an invalid Pin_class if not connected.
  static hhds::Pin_class get_driver(const hhds::Pin_class& sink);
  static bool            is_connected(const hhds::Pin_class& pin);

  // Sink pin lookup by name (e.g. "a", "din", "clock_pin"). Uses Ntype to
  // translate sink names to port_ids and find_or-not-create lookup on the
  // shadow.
  static hhds::Pin_class find_sink_pin(const hhds::Node_class& node, std::string_view name);

  void process_flop(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_memory(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_mux(std::shared_ptr<File_output> fout, const hhds::Node_class& node);
  void process_simple_node(std::shared_ptr<File_output> fout, const hhds::Node_class& node);

  void create_module_io(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_memories(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_subs(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_combinational(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_outputs(std::shared_ptr<File_output> fout, hhds::Graph* graph);
  void create_registers(std::shared_ptr<File_output> fout, hhds::Graph* graph);

  void add_to_pin2var(std::shared_ptr<File_output> fout, const hhds::Pin_class& dpin, std::string_view name, bool out_unsigned);
  void create_locals(std::shared_ptr<File_output> fout, hhds::Graph* graph);

public:
  void do_from_graph(const std::shared_ptr<hhds::Graph>& graph);

  Cgen_verilog(bool _verbose, std::string_view _odir);
};
