//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PY_OPTIONS_H
#define PY_OPTIONS_H

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <vector>
#include <string>

class Py_options {
protected:
  bool is_opt(std::string const& s1, std::string const& s2) const {
    if(s1.length() != s2.length())
      return false;
    return strcasecmp(s1.c_str(), s2.c_str()) == 0;
  }

  void set_val(const std::string &key, const py::handle &handle);
public:
  Py_options() {
    lgdb = "lgdb";
    graph_name = "";
  }
  std::string lgdb;
  std::string graph_name;

  void virtual set(const py::dict &dict) = 0;
};

#endif
