//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LGLOG_H
#define LGLOG_H

#include "spdlog/spdlog.h"

#include <iostream>

// FIXME: is it possible to remove the dependency on ostr.h?
//#include "spdlog/fmt/ostr.h"
#ifndef console
#define console spdlog::get("console")
#endif

class Console_init {
  class _init {
  public:
    _init();
  };

  static int _static_initializer;
};

inline int initialize_logger() {
  if(console == 0) {
    auto console1 = spdlog::stdout_color_mt("console");
    const char *log = getenv("LGRAPH_LOG");
    if (log==0) {
      spdlog::set_level(spdlog::level::warn);
    }else if (strcasecmp(log,"warn")==0) {
      spdlog::set_level(spdlog::level::warn);
    }else if (strcasecmp(log,"err")==0) {
      spdlog::set_level(spdlog::level::err);
    }else if (strcasecmp(log,"debug")==0) {
      spdlog::set_level(spdlog::level::debug);
    }else if (strcasecmp(log,"info")==0) {
      spdlog::set_level(spdlog::level::info);
    }else{
      spdlog::set_level(spdlog::level::info);
      console->error("lglog.hpp: Invalid console login level {}. Available are warn, err, debug, info", log);
    }
  }
  return 0;
}

#endif
