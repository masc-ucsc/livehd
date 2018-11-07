#ifndef EPRP_H
#define EPRP_H

#include "elab_scanner.hpp"
#include "eprp_var.hpp"
#include "eprp_method.hpp"

class Eprp : public Elab_scanner {
protected:
  std::map<std::string, Eprp_method, eprp_casecmp_str> methods;

  std::map<std::string, Eprp_var, eprp_casecmp_str> variables;

  Eprp_var last_cmd_var;

  void elaborate() final;

  void eat_comments();

  bool rule_path(std::string &path);
  bool rule_label_path(const std::string &cmd_line, Eprp_var &next_var);
  bool rule_reg(bool first);
  bool rule_cmd_line(std::string &path);
  bool rule_cmd_full();
  bool rule_pipe();
  bool rule_cmd_or_reg(bool first);
  bool rule_top();

public:
  Eprp();

  void register_method(const Eprp_method &method) {
    assert(methods.find(method.get_name()) == methods.end());
    methods.insert({method.get_name(),method});
  }

  bool has_method(const std::string &cmd) const {
    return methods.find(cmd) != methods.end();
  }

  void run_cmd(const std::string &cmd, Eprp_var &var);
  void set_variable(const std::string &name, const Eprp_var &var) {
    variables[name] = var;
  }

  bool readline(const char *line);

  const std::string &get_command_help(const std::string &cmd) const;

  void get_commands(std::function<void(const std::string &, const std::string &)> fn) const;
  void get_labels(const std::string &cmd, std::function<void(const std::string &, const std::string &, bool required)> fn) const;
};

#endif
