#ifndef EPRP_H
#define EPRP_H

#include <strings.h>

#include <vector>
#include <map>
#include <functional>
#include <cassert>
#include <string>

#include "spdlog/spdlog.h"

#include "eprp_scanner.hpp"

class LGraph {
public:
  int id;

  LGraph() {
    static int conta=0;
    id = conta++;
  };

  int get_id() const { return id; };
};

struct casecmp_str : public std::binary_function<const std::string, const std::string, bool> {
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0 ;
    }
};

typedef std::map<const std::string, std::string, casecmp_str> Eprp_dict;
typedef std::vector<LGraph *> Eprp_lgs;

class Eprp_var {
private:

public:
  Eprp_dict dict;
  Eprp_lgs  lgs;

  Eprp_var() {
    dict.clear();
    lgs.clear();
  }
  Eprp_var(const Eprp_dict &_dict) {
    dict = _dict;
    lgs.clear();
  }
  Eprp_var(const Eprp_lgs _lgs) {
    dict.clear();
    lgs = _lgs;
  }

  void add(const Eprp_dict &_dict);
  void add(const Eprp_lgs &_lgs);
  void add(const Eprp_var &_var);

  void add(LGraph *lg);
  void add(const std::string &name, const std::string &value);
  const std::string &get(const std::string &name) const;

  void clear() {
    dict.clear();
    lgs.clear();
  }

  bool empty() const { return dict.empty() && lgs.empty(); }
};

class Eprp : public Eprp_scanner {
protected:
  std::map<std::string, std::function<Eprp_var(Eprp_var)> , casecmp_str> methods;

  std::map<std::string, Eprp_var, casecmp_str> variables;

  Eprp_var last_cmd_var;

  void elaborate() final;

  bool rule_path(std::string &path);
  bool rule_label_path(const std::string &cmd_line, Eprp_var &next_var);
  bool rule_reg(bool first);
  bool rule_cmd_line(std::string &path);
  bool rule_cmd_full();
  bool rule_pipe();
  bool rule_cmd_or_reg(bool first);
  bool rule_top();

public:

  void register_method(const std::string &name, std::function<Eprp_var(const Eprp_var &var)> method) {
    assert(methods.find(name) == methods.end());
    methods[name] = method;
  }

  Eprp_var run_cmd(const std::string &cmd, const Eprp_var &var);
  void set_variable(const std::string &name, const Eprp_var &var) {
    variables[name] = var;
  }

  bool readline(const char *line);
};

#endif
