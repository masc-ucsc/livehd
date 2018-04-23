#ifndef OPTIONS_H
#define OPTIONS_H

#include <boost/program_options.hpp>
#include <vector>

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
  Options() {
  };

protected:
  static int cargc;
  static char **cargv;
  static boost::program_options::options_description desc;

public:
  static int get_cargc() { return cargc; };
  static char **get_cargv() { return cargv; };
  static boost::program_options::options_description *get_desc() { return &desc; };

  static void setup(std::string cmd);
  static void setup(int argc, const char **argv);
  //static void setup(int argc, char **argv) { setup(argc, const_cast<const char**>(argv)); }

  static void setup_lock();
};

#endif

