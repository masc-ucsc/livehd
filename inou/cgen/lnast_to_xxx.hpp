#pragma once

#include "lnast.hpp"

class Lnast_to_xxx {
protected:
  std::string_view memblock;
  Lnast *lnast;

  std::map<std::string, std::string> file_map;
  std::string buffer;
public:
  Lnast_to_base(std::string_view _memblock, Lnast *_lnast);
  virtual void generate(std::string_view path, std::string_view module_name) = 0;
};
