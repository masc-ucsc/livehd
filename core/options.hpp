#ifndef OPTIONS_H
#define OPTIONS_H

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <boost/program_options.hpp>
#include <vector>
#include <string>

class Options_pack {
private:
  static bool failed;
  static bool initialized;

protected:
public:
  std::string lgdb_path;
  std::string graph_name;

  Options_pack();
};

// Pure static class
class Options {
private:
  Options(){};

protected:
  static int                                         cargc;
  static char **                                     cargv;
  static boost::program_options::options_description desc;

public:
  static int                                          get_cargc() { return cargc; };
  static char **                                      get_cargv() { return cargv; };
  static boost::program_options::options_description *get_desc() { return &desc; };

  static void setup(std::string cmd);
  static void setup(int argc, const char **argv);
  //static void setup(int argc, char **argv) { setup(argc, const_cast<const char**>(argv)); }

  static void setup_lock();
};

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
