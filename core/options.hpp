//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef OPTIONS_H
#define OPTIONS_H

#include <strings.h>

#include <string>

class Options_base {
protected:
  bool is_opt(std::string const &s1, std::string const &s2) const {
    if (s1.length() != s2.length())
      return false;
    return strcasecmp(s1.c_str(), s2.c_str()) == 0;
  }

  void set_val(const std::string &label, const std::string &value) {
    if (is_opt(label, "path")) {
      path = value;
    } else if (is_opt(label, "name")) {
      name = value;
    }
  };

public:
  Options_base() {
    path = "lgdb";
    name = "";
  }
  std::string path;
  std::string name;

  void virtual set(const std::string &label, const std::string &value) = 0;
};

#endif
