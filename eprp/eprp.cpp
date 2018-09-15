
#include <ctype.h>
#include <algorithm>

#include "eprp.hpp"

// rule_path = (\. | alnum | /)+
bool Eprp::rule_path(std::string &path) {

  assert(!scan_is_end());

  if (!(scan_is_token(TOK_DOT)
        || scan_is_token(TOK_ALNUM)
        || scan_is_token(TOK_DIV)))
    return false;

  do {
    scan_append(path); // Just get the raw text

    bool done = scan_next();
    if (!done)
      break;

  }while(scan_is_token(TOK_DOT)
        || scan_is_token(TOK_ALNUM)
        || scan_is_token(TOK_DIV));

  return true;
}

// rule_label_path = label path
bool Eprp::rule_label_path(const std::string &cmd_line, Eprp_var &next_var) {

  if (!scan_is_token(TOK_LABEL))
    return false;

  std::string label = scan_text();

  scan_next(); // Skip LABEL token

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

  return true;
}

// rule_cmd_line = alnum (dot alnum)*
bool Eprp::rule_cmd_line(std::string &path) {

  assert(!scan_is_end());

  if (!scan_is_token(TOK_ALNUM))
    return false;

  do {
    scan_append(path); // Just get the raw text

    bool done = scan_next();
    if (!done)
      break;

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

  bool try_either = rule_cmd_or_reg(false);
  if (try_either) {
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

// rule_top = rule_cmd_or_reg(first) rule_pipe+
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
    }else{
      scan_error(fmt::format("missing pipe operation. At least @foo |> @bar"));
    }
    return false;
  }

  while(rule_pipe())
    ;

  return true;
}

// top = parse_top+
void Eprp::elaborate() {

  while(!scan_is_end()) {
    bool cmd = rule_top();
    if (!cmd)
      return;
  }

};

void Eprp_var::add(const Eprp_dict &_dict) {
  for (const auto& var : _dict) {
    add(var.first, var.second);
  }
}

void Eprp_var::add(const Eprp_lgs &_lgs) {
  for (const auto& lg : _lgs) {
    add(lg);
  }
}

void Eprp_var::add(const Eprp_var &_var) {
  add(_var.lgs);
  add(_var.dict);
}

void Eprp_var::add(LGraph *lg) {
  if (std::find(lgs.begin(), lgs.end(), lg) != lgs.end())
    lgs.push_back(lg);
}

void Eprp_var::add(const std::string &name, const std::string &value) {
  if (name!="" && dict.find(name) != dict.end())
    fmt::print("WARNING: redundant {} field", name);

  dict[name] = value;
}

const std::string &Eprp_var::get(const std::string &name) const {
  const auto &elem = dict.find(name);
  if (elem == dict.end()) {
    static const std::string empty("");
    return empty;
  }
  return elem->second;
}

Eprp_var Eprp::run_cmd(const std::string &cmd, const Eprp_var &var) {

  if (methods.find(cmd) == methods.end()) {
    scan_error(fmt::format("method {} not registered", cmd));

    return var;
  }

  fmt::print("run_cmd({}...)\n",cmd);

  last_cmd_var.add(var);

  last_cmd_var = methods[cmd](last_cmd_var);

  return last_cmd_var;
}

