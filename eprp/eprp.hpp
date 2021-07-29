//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "ast.hpp"
#include "elab_scanner.hpp"
#include "eprp_method.hpp"
#include "eprp_pipe.hpp"
#include "eprp_var.hpp"

class Eprp : public Elab_scanner {
protected:
  std::map<mmap_lib::str, Eprp_method, eprp_casecmp_str> methods;

  Eprp_pipe pipe;

  std::unique_ptr<Ast_parser> ast;

  enum Eprp_rules : Rule_id {
    Eprp_invalid = 0,  // zero is not a valid Rule_id
    Eprp_rule,
    Eprp_rule_path,
    Eprp_rule_label_path,
    Eprp_rule_reg,
    Eprp_rule_cmd_line,
    Eprp_rule_cmd_full,
    Eprp_rule_pipe,
    Eprp_rule_cmd_or_reg,
    Eprp_rule_top
  };

  void elaborate() final;

  void eat_comments();

  std::pair<bool,mmap_lib::str> rule_path();
  std::pair<bool, mmap_lib::str> rule_cmd_line();

  bool rule_label_path(const mmap_lib::str &cmd_line, Eprp_var &next_var);

  bool rule_reg(bool first);
  bool rule_cmd_full();
  bool rule_pipe();
  bool rule_cmd_or_reg(bool first);
  bool rule_top();

  void process_ast_handler(const mmap_lib::Tree_index &self, const Ast_parser_node &node);
  void process_ast();

public:
  Eprp();

  void register_method(const Eprp_method &method) {
    assert(methods.find(method.get_name()) == methods.end());
    methods.insert({method.get_name(), method});
  }

  bool has_method(const mmap_lib::str &cmd) const { return methods.find(cmd) != methods.end(); }

  void run_cmd(const mmap_lib::str &cmd, const Eprp_var &cmd_var_fields);

  bool readline(const char *line);

  mmap_lib::str get_command_help(const mmap_lib::str &cmd) const;

  void get_commands(const std::function<void(const mmap_lib::str &, const mmap_lib::str &)> &fn) const;
  void get_labels(const mmap_lib::str &cmd,
                  const std::function<void(const mmap_lib::str &, const mmap_lib::str &, bool required)> &fn) const;
};
