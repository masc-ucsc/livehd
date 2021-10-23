//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "eprp.hpp"
#include "lbench.hpp"

#include <ctype.h>

#include <algorithm>

void Eprp::eat_comments() {
  while (scan_is_token(Token_id_comment) && !scan_is_end()) scan_next();
}

// rule_path = (\. | alnum | / | "asdad.." | \,)+
std::pair<bool,mmap_lib::str> Eprp::rule_path() {
  assert(!scan_is_end());

  if (!(scan_is_token(Token_id_dot) || scan_is_token(Token_id_alnum) || scan_is_token(Token_id_string)
        || scan_is_token(Token_id_div)))
    return std::make_pair(false, mmap_lib::str());

  mmap_lib::str path;
  do {
    path = path.append(scan_text());

    ast->add(Eprp_rule_path, scan_token_entry());

    bool ok = scan_next();
    if (!ok)
      break;
    eat_comments();

    if (scan_is_next_token(1, Token_id_colon))
      break;  // stop if file:foo is the next argument list

  } while (scan_is_token(Token_id_dot) || scan_is_token(Token_id_alnum) || scan_is_token(Token_id_string)
           || scan_is_token(Token_id_comma) || scan_is_token(Token_id_div));

  return std::make_pair(true, path);
}

// rule_label_path = label path
bool Eprp::rule_label_path(const mmap_lib::str &cmd_line, Eprp_var &next_var) {
  if (!(scan_is_token(Token_id_alnum) && scan_is_next_token(1, Token_id_colon)))
    return false;

  auto label = scan_text();

  ast->add(Eprp_rule_label_path, scan_token_entry());

  scan_next();  // Skip alnum token
  scan_next();  // Skip colon token

  eat_comments();

  if (scan_is_end()) {
    throw scan_error(*this, "the {} field in {} command has no argument", label, cmd_line);
  }

  ast->down();
  auto [ok, path] = rule_path();
  ast->up(Eprp_rule_label_path);
  if (!ok) {
    if (scan_is_token(Token_id_register)) {
      throw scan_error(*this, "could not pass a register {} to a method {}", scan_text(), cmd_line);
    } else {
      throw scan_error(*this, "field {} with invalid value in {} command", label, cmd_line);
    }
  }

  next_var.add(label, path);

  return true;
}

// rule_reg = reg+
bool Eprp::rule_reg(bool /* first */) {
  if (!scan_is_token(Token_id_register))
    return false;

  throw scan_error(*this, "variables are deprecated (multithreaded)");
}

// rule_cmd_line = alnum (dot alnum)*
std::pair<bool, mmap_lib::str> Eprp::rule_cmd_line() {
  if (scan_is_end())
    return std::make_pair(false, mmap_lib::str());

  if (!scan_is_token(Token_id_alnum))
    return std::make_pair(false, mmap_lib::str());

  mmap_lib::str path;
  do {
    path = path.append(scan_text());
    ast->add(Eprp_rule_cmd_line, scan_token_entry());

    bool ok1 = scan_next();
    if (!ok1)
      break;

    if (!scan_is_token(Token_id_dot))
      break;

    path = mmap_lib::str::concat(path, scan_text());
    ast->add(Eprp_rule_cmd_line, scan_token_entry());

    bool ok2 = scan_next();
    if (!ok2)
      break;

  } while (scan_is_token(Token_id_alnum));

  eat_comments();

  return std::make_pair(true, path);
}

// rule_cmd_full =rule_cmd_line rule_label_path*
bool Eprp::rule_cmd_full() {

  ast->down();
  auto [cmd_found, cmd_line] = rule_cmd_line();
  ast->up(Eprp_rule_cmd_full);
  if (!cmd_found)
    return false;

  Eprp_var cmd_var_fields;

  bool path_found;
  do {
    ast->down();
    path_found = rule_label_path(cmd_line, cmd_var_fields);
    ast->up(Eprp_rule_cmd_full);
  } while (path_found);

  ast->down();
  run_cmd(cmd_line, cmd_var_fields);
  ast->up(Eprp_rule_cmd_full);

  return true;
}

// rule_pipe = |> rule_cmd_or_reg
bool Eprp::rule_pipe() {
  if (scan_is_end())
    return false;

  if (!scan_is_token(Token_id_pipe))
    return false;

  scan_next();
  eat_comments();

  ast->down();
  bool try_either = rule_cmd_or_reg(false);
  ast->up(Eprp_rule_pipe);
  if (!try_either) {
    throw scan_error(*this, "after a pipe there should be a register or a command");
  }

  return true;
}

// rule_cmd_or_reg = rule_reg | rule_cmd_full
bool Eprp::rule_cmd_or_reg(bool first) {
  ast->down();
  bool try_reg_rule = rule_reg(first);
  ast->up(Eprp_rule_cmd_or_reg);
  if (try_reg_rule)
    return true;

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
    throw scan_error(*this, "statements start with a register or a command");
  }

  // tree.add_lazy_child(1);

  bool try_pipe = rule_pipe();
  if (!try_pipe) {
    if (scan_is_token(Token_id_or)) {
      throw scan_error(*this, "eprp pipe is |> not |");
    } else if (scan_is_end()) {
      return true;
    } else {
      throw scan_error(*this, "invalid command");
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
  pipe.clear();

  ast = std::make_unique<Ast_parser>(get_memblock(), Eprp_rule);
  ast->down();

  while (!scan_is_end()) {
    eat_comments();
    if (scan_is_end())
      return;

    bool cmd = rule_top();
    if (!cmd)
      return;
  }

  ast->up(Eprp_rule);

  // process_ast();

  ast = nullptr;

  pipe.run();
};

void Eprp::process_ast_handler(const mmap_lib::Tree_index &self, const Ast_parser_node &node) {
  auto txt = scan_text(node.token_entry);
  fmt::print("level:{} pos:{} te:{} rid:{} txt:{}\n", self.level, self.pos, node.token_entry, node.rule_id, txt);

  if (node.rule_id == Eprp_rule_cmd_or_reg) {
    mmap_lib::str children_txt;

    // HERE: Children should iterate FAST, over all the children recursively
    // HERE: Move this iterate over children as a handle_command

    for (const auto &ti : ast->children(self)) {
      auto txt2 = scan_text(ast->get_data(ti).token_entry);
      if (ast->get_data(ti).rule_id == Eprp_rule_label_path)
        children_txt = mmap_lib::str::concat(children_txt, " ", txt2, ":");
      else
        children_txt = mmap_lib::str::concat(children_txt, txt2);
    }

    fmt::print("  children: {}\n", children_txt);
  }
}

void Eprp::process_ast() {
  for (const auto &ti : ast->depth_preorder()) {
    fmt::print("ti.level:{} ti.pos:{}\n", ti.level, ti.pos);
  }

  ast->each_bottom_up_fast(std::bind(&Eprp::process_ast_handler, this, std::placeholders::_1, std::placeholders::_2));
}

void Eprp::run_cmd(const mmap_lib::str &cmd, const Eprp_var &cmd_var_fields) {
  const auto &it = methods.find(cmd);
  if (it == methods.end()) {
    throw parser_error(*this, "method {} not registered", cmd);
  }

  pipe.add_command(it->second, cmd_var_fields);
}

mmap_lib::str Eprp::get_command_help(const mmap_lib::str &cmd) const {
  const auto &it = methods.find(cmd);
  if (it == methods.end()) {
    return mmap_lib::str();
  }

  return it->second.help;
}

void Eprp::get_commands(const std::function<void(const mmap_lib::str &, const mmap_lib::str &)> &fn) const {
  for (const auto &v : methods) {
    fn(v.first, v.second.help);
  }
}

void Eprp::get_labels(const mmap_lib::str                                                                  &cmd,
                      const std::function<void(const mmap_lib::str &, const mmap_lib::str &, bool required)> &fn) const {
  const auto &it = methods.find(cmd);
  if (it == methods.end())
    return;

  for (const auto &v : it->second.labels) {
    fn(v.first, v.second.help, v.second.required);
  }
}

Eprp::Eprp() {}
