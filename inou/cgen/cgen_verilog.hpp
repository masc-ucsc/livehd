// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <mutex>
#include <atomic>
#include <string>

#include "lgraph.hpp"
#include "file_output.hpp"

class Cgen_verilog {
private:
  const bool          verbose;
  std::string_view odir;

  using pin2str_type = absl::flat_hash_map<Node_pin::Compact_class, std::string>;

  struct Expr {
    Expr(std::string_view v, bool n) : var(v), needs_parenthesis(n) { }
    std::string var;
    bool needs_parenthesis;
  };

  absl::flat_hash_map<Node_pin::Compact_class, Expr>           pin2expr;
  absl::flat_hash_map<Node_pin::Compact_class, std::string>  pin2var;
  absl::flat_hash_map<Node    ::Compact_class, std::string>  mux2vector;

  bool first_array_block;

  std::atomic<int> nrunning;
  inline static std::mutex lgs_mutex; // just needed for the once at a time setup of static reserved_keyword
  inline static absl::flat_hash_set<std::string> reserved_keyword;

  std::string get_wire_or_const(const Node_pin &dpin) const;
  static std::string get_scaped_name(std::string_view name);

  std::string get_append_to_name(std::string_view name, std::string_view ext) const;
  std::string get_expression(const Node_pin &dpin) const;
  std::string get_expression(const Node_pin &&dpin) const { return get_expression(dpin); }
  std::string add_expression(std::string_view txt_seq, std::string_view txt_op, Node_pin &dpin) const;

  void process_flop(std::shared_ptr<File_output> fout, Node &node);
  void process_memory(std::shared_ptr<File_output> fout, Node &node);
  void process_mux(std::shared_ptr<File_output> fout, Node &node);
  void process_simple_node(std::shared_ptr<File_output> fout, Node &node);

  void create_module_io(std::shared_ptr<File_output> fout, Lgraph *lg);
  void create_memories(std::shared_ptr<File_output> fout, Lgraph *lg);
  void create_subs(std::shared_ptr<File_output> fout, Lgraph *lg);
  void create_combinational(std::shared_ptr<File_output> fout, Lgraph *lg);
  void create_outputs(std::shared_ptr<File_output> fout, Lgraph *lg);
  void create_registers(std::shared_ptr<File_output> fout, Lgraph *lg);

  void add_to_pin2var(std::shared_ptr<File_output> fout, Node_pin &dpin, std::string_view name, bool out_unsigned);
  void create_locals(std::shared_ptr<File_output> fout, Lgraph *lg);
public:
  void do_from_lgraph(Lgraph *lg_parent);

  Cgen_verilog(bool _verbose, std::string_view _odir);
};
