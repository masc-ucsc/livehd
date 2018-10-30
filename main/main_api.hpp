#ifndef MAIN_API_H
#define MAIN_API_H

#include <functional>

#include "eprp.hpp"
#include "spdlog/spdlog.h"

class Main_api {
protected:
  static Eprp eprp;
  static std::string main_path;

public:
  static void error(const std::string &msg) {
    eprp.parser_error(msg);
  }
  static void warn(const std::string &msg) {
    eprp.parser_warn(msg);
  }

  template<typename Arg1, typename... Args>
  static void error(const char *fmt, const Arg1 &, const Args &... args) {
    eprp.parser_error(fmt::format(fmt, args...));
  }

  template<typename Arg1, typename... Args>
  static void warn(const char *fmt, const Arg1 &, const Args &... args) {
    eprp.parser_warn(fmt::format(fmt, args...));
  }

  static void setup(std::function<void(Eprp &)> fn) {
    fn(eprp);
  }

  static void parse(const std::string &line) {
    eprp.parse("stdin",line);
  }

  static void get_commands(std::function<void(const std::string &, const std::string &)> fn) {
    eprp.get_commands(fn);
  };

  static const std::string &get_command_help(const std::string &cmd) {
    return eprp.get_command_help(cmd);
  }

  static void get_labels(const std::string &cmd, std::function<void(const std::string &, const std::string &, bool required)> fn) {
    eprp.get_labels(cmd,fn);
  }

  static const std::string &get_main_path() { return main_path; }

  static void init();
};

#endif
