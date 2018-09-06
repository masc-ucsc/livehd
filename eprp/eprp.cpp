
#include <ctype.h>
#include <algorithm>

#include "eprp.hpp"


void Eprp_var::join(const Eprp_dict &_dict) {
  for (const auto& var : _dict) {
    add(var.first, var.second);
  }
}

void Eprp_var::join(const Eprp_lgs &_lgs) {
  for (const auto& lg : _lgs) {
    add(lg);
  }
}

void Eprp_var::add(LGraph *lg) {
  if (std::find(lgs.begin(), lgs.end(), lg) != lgs.end())
    lgs.push_back(lg);
}

void Eprp_var::add(const std::string &name, const std::string &value) {
  if (dict.find(name) != dict.end())
    fmt::print("ERROR: redundant {} field", name);
  else
    dict[name] = value;
}

Eprp_var Eprp::run_cmd(const std::string &cmd, const Eprp_dict &dict) {

  Eprp_var var;

  if (methods.find(cmd) == methods.end()) {
    fmt::print("ERROR: method {} not registered\n", cmd);

    return var;
  }

  var = methods[cmd](dict);

  last_cmd_var = var;
  return var;
}

void Eprp::readline(const char *line) {

  int len=0;
  const char *stop_at=0;


  bool is_string = false;

  bool cmd_finished  = false;
  bool name_finished = false;

  bool last_space = true;
  bool in_quotes  = false;

  char cmd[1024];
  int  pos = 0;
  char name[1024];
  char value[1024];

  Eprp_dict dict;

  while(*line) {
    if (pos>=1024) {
      fmt::print("ERROR: {} name/variable is too long\n",line);
      exit(-3);
    }

    if (in_quotes) {
      is_string = true;
      if (*line == '"' || *line == '\'') {
        in_quotes = false;
      }else{
        if (!cmd_finished) {
          cmd[pos++] = *line;
        }else if (!name_finished) {
          name[pos++] = *line;
        }else{
          value[pos++] = *line;
        }
      }
    }else if (*line == '"' || *line == '\'') {
      in_quotes = true;
      is_string = true;
    }else if (*line == '|' || *line == '\n') {
      if (!cmd_finished) {
        cmd[pos] = 0;
      }else if (!name_finished) {
        name[pos] = 0;
        if (pos>0) {
          std::string name_str;
          if (is_string)
            name_str = "." + std::string(name);
          else
            name_str = name;

          dict[""] = name_str; // Only if name makes sense
        }
      }else{
        value[pos] = 0;
        if (pos>0) {
          std::string value_str;
          if (is_string)
            value_str = "." + std::string(value);
          else
            value_str = value;
          dict[name] = value_str;
        }else{
          std::string name_str;
          if (is_string)
            name_str = "." + std::string(name);
          else
            name_str = name;
          dict[""] = name_str; // Only if name makes sense
        }
      }
      run_cmd(cmd,dict);

      dict.clear();
      cmd_finished = false;
      name_finished = false;
      is_string    = false;
      if (*line == '\n')
        last_cmd_var.clear();
      pos = 0;

      last_space = true;
    }else if (isspace(*line) || *line == ':') {
      if (!last_space) {
        if (!cmd_finished) {
          cmd[pos] = 0;
          cmd_finished = true;
        }else if (!name_finished) {
          name[pos] = 0;
          name_finished = true;
          is_string = false;
        }else{
          value[pos] = 0;

          std::string value_str;
          if (is_string)
            value_str = "." + std::string(value);
          else
            value_str = value;

          fmt::print("add1 {} {} {}\n",name,value_str, is_string);
          dict[name] = value_str;
          name_finished = false;
          is_string = false;
        }
        pos = 0;
      }

      last_space = true;
    }else{
      if (!cmd_finished) {
        cmd[pos++] = *line;
      }else if (!name_finished) {
        name[pos++] = *line;
      }else{
        value[pos++] = *line;
      }

      last_space = false;
    }

    line++;
  }

  if (pos>=1) {
    if (!cmd_finished) {
      cmd[pos] = 0;
    }else if (!name_finished) {
      name[pos] = 0;
      std::string name_str;
      if (is_string)
        name_str = "." + std::string(name);
      else
        name_str = name;

      dict[""] = name_str; // Only if name makes sense
      fmt::print("add2 0 {}\n",name_str);
    }else{
      value[pos] = 0;
      std::string value_str;
      if (is_string)
        value_str = "." + std::string(value);
      else
        value_str = value;

      dict[name] = value_str;
      fmt::print("add3 {} {}\n",name,value_str);
    }
    run_cmd(cmd,dict);
  }
  last_space = true;
}

