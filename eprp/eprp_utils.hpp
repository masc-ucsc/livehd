#pragma once

#include <vector>
#include <string>

// List of useful methods for parsers in lgraph
class Eprp_utils {
public:
  static std::vector<std::string> parse_files(std::string_view files, std::string_view module);

  static std::string get_exe_path();

  static bool ends_with(const std::string &s, const std::string &suffix);

  static void clean_dir(const std::string &dir);
};

