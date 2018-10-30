
#include <ctype.h>
#include <algorithm>

#include "spdlog/spdlog.h"
#include "eprp.hpp"


void Eprp::eat_comments() {
  while(scan_is_token(TOK_COMMENT) && !scan_is_end())
    scan_next();
}

// rule_path = (\. | alnum | / | "asdad.." | \,)+
bool Eprp::rule_path(std::string &path) {


  assert(!scan_is_end());

  if (!(scan_is_token(TOK_DOT)
        || scan_is_token(TOK_ALNUM)
        || scan_is_token(TOK_STRING)
        || scan_is_token(TOK_DIV)))
    return false;

  do {
    scan_append(path); // Just get the raw text

    bool ok = scan_next();
    if (!ok)
      break;
    eat_comments();

  }while(scan_is_token(TOK_DOT)
        || scan_is_token(TOK_ALNUM)
        || scan_is_token(TOK_STRING)
        || scan_is_token(TOK_COMMA)
        || scan_is_token(TOK_DIV));

  return true;
}

// rule_label_path = label path
bool Eprp::rule_label_path(const std::string &cmd_line, Eprp_var &next_var) {


  if (!scan_is_token(TOK_LABEL))
    return false;

  std::string label = scan_text();

  scan_next(); // Skip LABEL token
  eat_comments();

  if (scan_is_end()) {
    scan_error(fmt::format("the {} field in {} command has no argument", label, cmd_line));
    return false;
  }

  std::string path;
  bool ok = rule_path(path);
  if (!ok) {
    if (scan_is_token(TOK_REGISTER)) {
      scan_error(fmt::format("could not pass a register {} to a method {}", scan_text(), cmd_line));
    }else{
      scan_error(fmt::format("field {} with invalid value in {} command", label, cmd_line));
    }
    return false;
  }

  next_var.add(label,path);

  return true;
}

// rule_reg = reg+
bool Eprp::rule_reg(bool first) {
  if (!scan_is_token(TOK_REGISTER))
    return false;

  std::string var = scan_text();
  if (first) { // First in line @a |> ...
    if (variables.find(var) == variables.end()) {
      scan_error(fmt::format("variable {} is empty", var));
      return false;
    }
    last_cmd_var = variables[var];
  }else{
    variables[var] = last_cmd_var;
  }

  scan_next();
  eat_comments();

  return true;
}

// rule_cmd_line = alnum (dot alnum)*
bool Eprp::rule_cmd_line(std::string &path) {

  if(scan_is_end())
    return false;

  if (!scan_is_token(TOK_ALNUM))
    return false;

  do {
    scan_append(path); // Just get the raw text

    bool ok = scan_next();
    if (!ok)
      break;
    eat_comments();

  }while(scan_is_token(TOK_DOT) || scan_is_token(TOK_ALNUM));

  return true;
}

// rule_cmd_full =rule_cmd_line rule_label_path*
bool Eprp::rule_cmd_full() {

  std::string cmd_line;

  Eprp_var next_var;

  bool cmd_found = rule_cmd_line(cmd_line);
  if (!cmd_found)
    return false;

  while(rule_label_path(cmd_line, next_var))
    ;

  run_cmd(cmd_line, next_var);

  return true;
}

// rule_pipe = |> rule_cmd_or_reg
bool Eprp::rule_pipe() {

  if (scan_is_end())
    return false;

  if (!scan_is_token(TOK_PIPE))
    return false;

  scan_next();
  eat_comments();

  bool try_either = rule_cmd_or_reg(false);
  if (!try_either) {
    scan_error(fmt::format("after a pipe there should be a register or a command"));
    return false;
  }

  return true;
}

// rule_cmd_or_reg = rule_reg | rule_cmd_full
bool Eprp::rule_cmd_or_reg(bool first) {

  bool try_reg_rule = rule_reg(first);
  if (try_reg_rule)
    return true;

  return rule_cmd_full();
}

// rule_top = rule_cmd_or_reg(first) rule_pipe*
bool Eprp::rule_top() {

  bool try_either = rule_cmd_or_reg(true);
  if (!try_either) {
    scan_error(fmt::format("statements start with a register or a command"));
    return false;
  }

  bool try_pipe = rule_pipe();
  if (!try_pipe) {
    if (scan_is_token(TOK_OR)) {
      scan_error(fmt::format("eprp pipe is |> not |"));
      return false;
    }else if (scan_is_end()) {
      return true;
    }else{
      scan_error(fmt::format("invalid command"));
      return false;
    }
  }

  while(rule_pipe())
    ;

  return true;
}

// top = parse_top+
void Eprp::elaborate() {

  while(!scan_is_end()) {
    eat_comments();
    if (scan_is_end())
      return;
    bool cmd = rule_top();
    if (!cmd)
      return;
  }

  last_cmd_var.clear();
};

void Eprp::run_cmd(const std::string &cmd, Eprp_var &var) {

  const auto &it = methods.find(cmd);
  if (it == methods.end()) {
    parser_error(fmt::format("method {} not registered", cmd));
    return ;
  }

  const auto &m = it->second;

  last_cmd_var.add(var);

  std::string err_msg = m.check_labels(last_cmd_var);
  if (!err_msg.empty()) {
    parser_error(err_msg);
    return;
  }

#if 0
  for(const auto v:var.dict) {
    if (!m.has_label(v.first)) {
      parser_warn(fmt::format("method {} does not have passed label {}", cmd, v.first));
    }
  }
#endif

  for (const auto & label : m.labels) {
    if (!label.second.default_value.empty() &&
        !last_cmd_var.has_label(label.first))
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
  for(const auto v:methods) {
    fn(v.first,v.second.help);
  }
}

void Eprp::get_labels(const std::string &cmd, std::function<void(const std::string &, const std::string &, bool required)> fn) const {
  const auto &it = methods.find(cmd);
  if (it == methods.end())
    return;

  for(const auto v:it->second.labels) {
    fn(v.first, v.second.help, v.second.required);
  }
}

Eprp::Eprp() {
}
