#ifndef LGLOG_H
#define LGLOG_H

#include "spdlog/spdlog.h"

#include <iostream>

//FIXME: is it possible to remove the dependency on ostr.h?
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
    console1->info("lg initialized to console with info level at core::lglog");
    spdlog::set_level(spdlog::level::warn);
  }
  return 0;
}

//FIXME: come up with a C++14 friendly solution
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++17-extensions"
inline int Console_init::_static_initializer = initialize_logger();
#pragma clang diagnostic pop

#endif
