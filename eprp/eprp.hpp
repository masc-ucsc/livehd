#ifndef EPRP_H
#define EPRP_H

#include <strings.h>

#include <vector>
#include <map>
#include <functional>
#include <cassert>

#include <fmt/core.h>

class LGraph {
public:
  int id;

  LGraph() {
    static int conta=0;
    id = conta++;
  };

  int get_id() const { return id; };
};

struct casecmp_str : public std::binary_function<std::string, std::string, bool> {
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0 ;
    }
};

typedef std::map<std::string, std::string, casecmp_str> Eprp_dict;
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

  void join(const Eprp_dict &_dict);
  void join(const Eprp_lgs &_lgs);

  void add(LGraph *lg);
  void add(const std::string &name, const std::string &value);

  void clear() {
    dict.clear();
    lgs.clear();
  }
};

class Eprp {
protected:
  std::map<std::string, std::function<Eprp_var(Eprp_var)> , casecmp_str> methods;

  std::map<std::string, Eprp_var, casecmp_str> variables;

  Eprp_var last_cmd_var;
public:

  void register_method(const std::string &name, std::function<Eprp_var(const Eprp_var &var)> method) {
    assert(methods.find(name) == methods.end());
    methods[name] = method;
  }

  Eprp_var run_cmd(const std::string &cmd, const Eprp_dict &dict);
  void set_variable(const std::string &name, const Eprp_var &var) {
    variables[name] = var;
  }

  void readline(const char *line);
};

#endif
