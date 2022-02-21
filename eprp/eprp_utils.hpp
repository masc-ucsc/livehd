//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

// List of useful methods for parsers in lgraph
class Eprp_utils {
public:
  static std::string get_exe_path();

  static void clean_dir(std::string_view dir);
};
