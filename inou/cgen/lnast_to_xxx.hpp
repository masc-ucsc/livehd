#pragma once

#include "lnast.hpp"

class Lnast_to_xxx {
protected:
  Lnast *lnast;
  std::string_view path;

  std::map<std::string, std::string> file_map;
  std::string buffer;
public:
  Lnast_to_xxx(Lnast *_lnast, std::string_view _path);
  virtual void generate() = 0;
};
