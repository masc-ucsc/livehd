#pragma once

#include <vector>
#include <string>

// List of useful methods for parsers in lgraph
class Eprp_utils {
public:

  static std::vector<std::string> parse_files(const std::string &files, const std::string &module);

  static std::string get_exe_path();

  static bool ends_with(const std::string &s, const std::string &suffix);

};

