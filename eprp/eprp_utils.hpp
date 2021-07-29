#pragma once

#include "mmap_str.hpp"

// List of useful methods for parsers in lgraph
class Eprp_utils {
public:
  static mmap_lib::str get_exe_path();

  static void clean_dir(const mmap_lib::str &dir);
};
