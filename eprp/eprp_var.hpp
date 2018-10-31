#ifndef EPRP_VAR_H
#define EPRP_VAR_H

#include <strings.h>
#include <vector>
#include <map>
#include <functional>
#include <cassert>
#include <string>

struct eprp_casecmp_str : public std::binary_function<const std::string, const std::string, bool> {
    bool operator()(const std::string &lhs, const std::string &rhs) const {
        return strcasecmp(lhs.c_str(), rhs.c_str()) < 0 ;
    }
};

class LGraph;

class Eprp_var {
private:

public:
  typedef std::map<const std::string, std::string, eprp_casecmp_str> Eprp_dict;
  typedef std::vector<LGraph *> Eprp_lgs;

  Eprp_dict dict;
  Eprp_lgs  lgs;

  Eprp_var() {
    dict.clear();
    lgs.clear();
  }
  Eprp_var(const Eprp_dict &_dict)
    :dict(_dict) {
    lgs.clear();
  }
  Eprp_var(const Eprp_lgs &_lgs)
    :lgs(_lgs) {
    dict.clear();
  }

  void add(const Eprp_dict &_dict);
  void add(const Eprp_lgs &_lgs);
  void add(const Eprp_var &_var);

  void add(LGraph *lg);
  void add(const std::string &name, const std::string &value);

  void delete_label(const std::string &name);

  bool has_label(const std::string &name) const { return dict.find(name) != dict.end(); };
  const std::string get(const std::string &name) const;
  //const std::string get(const std::string &name, const std::string &def_val) const;

  void clear() {
    dict.clear();
    lgs.clear();
  }

  bool empty() const { return dict.empty() && lgs.empty(); }
};

#endif

