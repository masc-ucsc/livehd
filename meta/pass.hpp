//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_H
#define PASS_H

#include <string>

#include "eprp.hpp"

class Pass {
private:

  const std::string name;

protected:
  void register_pass(Eprp_method &method);
  void register_inou(Eprp_method &method);

  Pass(const std::string &name_);

  bool setup_directory(const std::string &dir) const;
public:
  // FIXME: NASTY until we move all the pass/inou to the new pass interface
  static Eprp eprp; // TODO: Shared with inou

  virtual void setup() = 0;

  static void error(const std::string &msg) {
    eprp.parser_error(msg);
  }
  static void warn(const std::string &msg) {
    eprp.parser_warn(msg);
  }

  template<typename Arg1, typename... Args>
  void error(const char *fmt, const Arg1 &, const Args &... args) {
    eprp.parser_error(fmt::format(fmt, args...));
  }

  template<typename Arg1, typename... Args>
  void warn(const char *fmt, const Arg1 &, const Args &... args) {
    eprp.parser_warn(fmt::format(fmt, args...));
  }

};

#endif
