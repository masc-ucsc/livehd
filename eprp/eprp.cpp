//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <ctype.h>
#include <algorithm>

#include "eprp.hpp"

void Eprp::eat_comments() {
  while (scan_is_token(Token_id_comment) && !scan_is_end()) scan_next();
}

// rule_path = (\. | alnum | / | "asdad.." | \,)+
bool Eprp::rule_path(std::string &path) {
  assert(!scan_is_end());

  if (!(scan_is_token(Token_id_dot) || scan_is_token(Token_id_alnum) || scan_is_token(Token_id_string) ||
        scan_is_token(Token_id_div)))
    return false;

  do {
    scan_append(path);  // Just get the raw text

    ast->add(Eprp_rule_path, scan_token());

    bool ok = scan_next();
    if (!ok) break;
    eat_comments();

  } while (scan_is_token(Token_id_dot) || scan_is_token(Token_id_alnum) || scan_is_token(Token_id_string) ||
           scan_is_token(Token_id_comma) || scan_is_token(Token_id_div));

  return true;
}

// rule_label_path = label path
bool Eprp::rule_label_path(const std::string &cmd_line, Eprp_var &next_var) {
  if (!scan_is_token(Token_id_label)) return false;

  std::string label = scan_text();
  ast->add(Eprp_rule_label_path, scan_token());

  scan_next();  // Skip LABEL token
  eat_comments();

  if (scan_is_end()) {
    scan_error("the {} field in {} command has no argument", label, cmd_line);
    return false;
  }

  std::string path;
  ast->down();
  bool ok = rule_path(path);
  ast->up(Eprp_rule_label_path);
  if (!ok) {
    if (scan_is_token(Token_id_register)) {
      scan_error("could not pass a register {} to a method {}", scan_text(), cmd_line);
    } else {
      scan_error("field {} with invalid value in {} command", label, cmd_line);
    }
    return false;
  }

  next_var.add(label, path);

  return true;
}

// rule_reg = reg+
bool Eprp::rule_reg(bool first) {
  if (!scan_is_token(Token_id_register)) return false;

  std::string var = scan_text();
  ast->add(Eprp_rule_reg, scan_token());
  if (first) {  // First in line @a |> ...
    if (variables.find(var) == variables.end()) {
      scan_error("variable {} is empty", var);
      return false;
    }
    last_cmd_var = variables[var];
  } else {
    variables[var] = last_cmd_var;
  }

  scan_next();
  eat_comments();

  return true;
}

// rule_cmd_line = alnum (dot alnum)*
bool Eprp::rule_cmd_line(std::string &path) {
  if (scan_is_end()) return false;

  if (!scan_is_token(Token_id_alnum)) return false;

  do {
    scan_append(path);  // Just get the raw text
    ast->add(Eprp_rule_cmd_line, scan_token());

    bool ok = scan_next();
    if (!ok) break;
    eat_comments();

  } while (scan_is_token(Token_id_dot) || scan_is_token(Token_id_alnum));

  return true;
}

// rule_cmd_full =rule_cmd_line rule_label_path*
bool Eprp::rule_cmd_full() {
  std::string cmd_line;

  Eprp_var next_var;

  ast->down();
  bool cmd_found = rule_cmd_line(cmd_line);
  ast->up(Eprp_rule_cmd_full);
  if (!cmd_found) return false;

  bool path_found;
  do {
    ast->down();
    path_found = rule_label_path(cmd_line, next_var);
    ast->up(Eprp_rule_cmd_full);
  } while (path_found);

  ast->down();
  run_cmd(cmd_line, next_var);
  ast->up(Eprp_rule_cmd_full);

  return true;
}

// rule_pipe = |> rule_cmd_or_reg
bool Eprp::rule_pipe() {
  if (scan_is_end()) return false;

  if (!scan_is_token(Token_id_pipe)) return false;

  scan_next();
  eat_comments();

  ast->down();
  bool try_either = rule_cmd_or_reg(false);
  ast->up(Eprp_rule_pipe);
  if (!try_either) {
    scan_error("after a pipe there should be a register or a command");
    return false;
  }

  return true;
}

// rule_cmd_or_reg = rule_reg | rule_cmd_full
bool Eprp::rule_cmd_or_reg(bool first) {
  ast->down();
  bool try_reg_rule = rule_reg(first);
  ast->up(Eprp_rule_cmd_or_reg);
  if (try_reg_rule) return true;

  ast->down();
  bool cmd_found = rule_cmd_full();
  ast->up(Eprp_rule_cmd_or_reg);
  return cmd_found;
}

// rule_top = rule_cmd_or_reg(first) rule_pipe*
bool Eprp::rule_top() {
  ast->down();
  bool try_either = rule_cmd_or_reg(true);
  ast->up(Eprp_rule_top);
  if (!try_either) {
    scan_error("statements start with a register or a command");
    return false;
  }

  // tree.add_lazy_child(1);

  bool try_pipe = rule_pipe();
  if (!try_pipe) {
    if (scan_is_token(Token_id_or)) {
      scan_error("eprp pipe is |> not |");
      return false;
    } else if (scan_is_end()) {
      return true;
    } else {
      scan_error("invalid command");
      return false;
    }
  }

  // tree.add_lazy_child(1);

  while (rule_pipe()) {
    // tree.add_lazy_child(1);
    ;
  }

  return true;
}

// top = parse_top+
void Eprp::elaborate() {
  ast = std::make_unique<Ast_parser>(get_buffer());
  ast->down();

  while (!scan_is_end()) {
    eat_comments();
    if (scan_is_end()) return;

    bool cmd = rule_top();
    if (!cmd) return;
  }

  ast->up(Eprp_rule);

  // process_ast();

  ast = nullptr;
  last_cmd_var.clear();
};

void Eprp::process_ast_handler(const Tree_index &parent, const Tree_index &self, const Ast_parser_node &node) {
  auto txt = scan_text(node.token_entry);
  fmt::print("level:{} pos:{} te:{} rid:{} txt:{}\n", self.level, self.pos, node.token_entry, node.rule_id, txt);

  if (node.rule_id == Eprp_rule_cmd_or_reg) {
    std::string children_txt;

    // HERE: Children should iterate FAST, over all the children recursively
    // HERE: Move this iterate over children as a handle_command

    for (const auto &ti : ast->get_children(self)) {
      auto txt2 = scan_text(ast->get_data(ti).token_entry);
      if (ast->get_data(ti).rule_id == Eprp_rule_label_path)
        absl::StrAppend(&children_txt, " ", txt2, ":");
      else
        absl::StrAppend(&children_txt, txt2);
    }

    fmt::print("  children: {}\n", children_txt);
  }
}

void Eprp::process_ast() {
  // ast->each_depth_first

  ast->each_bottom_first_fast(
      std::bind(&Eprp::process_ast_handler, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void Eprp::run_cmd(const std::string &cmd, Eprp_var &var) {
  const auto &it = methods.find(cmd);
  if (it == methods.end()) {
    parser_error("method {} not registered", cmd);
    return;
  }

  const auto &m = it->second;

  last_cmd_var.add(var);

  std::string err_msg;
  bool        err = m.check_labels(last_cmd_var, err_msg);
  if (err) {
    parser_error(err_msg);
    return;
  }

#if 0
  for(const auto v:var.dict) {
    if (!m.has_label(v.first)) {
      parser_warn("method {} does not have passed label {}", cmd, v.first);
    }
  }
#endif

  for (const auto &label : m.labels) {
    if (!label.second.default_value.empty() && !last_cmd_var.has_label(label.first))
      last_cmd_var.add(label.first, label.second.default_value);
  }

  m.method(last_cmd_var);
}

const std::string &Eprp::get_command_help(const std::string &cmd) const {
  const auto &it = methods.find(cmd);
  if (it == methods.end()) {
    static const std::string empty = "";
    return empty;
  }

  return it->second.help;
}

void Eprp::get_commands(std::function<void(const std::string &, const std::string &)> fn) const {
  for (const auto v : methods) {
    fn(v.first, v.second.help);
  }
}

void Eprp::get_labels(const std::string &                                                          cmd,
                      std::function<void(const std::string &, const std::string &, bool required)> fn) const {
  const auto &it = methods.find(cmd);
  if (it == methods.end()) return;

  for (const auto v : it->second.labels) {
    fn(v.first, v.second.help, v.second.required);
  }
}

Eprp::Eprp() {}
