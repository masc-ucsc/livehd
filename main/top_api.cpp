//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// top_api. This are commands at the top level like filter, files...

#include "top_api.hpp"

#include <dirent.h>

#include <iostream>
#include <regex>
#include <string>

void Top_api::files(Eprp_var &var) {
  auto src_path = var.get("src_path");
  auto match    = var.get("match");
  auto filter   = var.get("filter");

  try {
    const std::regex txt_regex(match.to_s());
    const std::regex filter_regex(filter.to_s());

    DIR *dirp = opendir(src_path.to_s().c_str());
    if (dirp == 0) {
      Main_api::error("invalid src_path:{}, is it a valid directory?", src_path);
      return;
    }
    std::vector<std::string> sort_files;
    struct dirent *          dp;
    while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_type == DT_DIR)
        continue;
      std::string filename(dp->d_name); // string looks for 0 sequence
      if (match.empty()) {
        if (filter.empty()) {
          sort_files.push_back(filename);
        } else if (!std::regex_search(filename, filter_regex)) {
          sort_files.push_back(filename);
        }
      } else if (std::regex_search(filename, txt_regex)) {
        if (filter.empty()) {
          sort_files.push_back(filename);
        } else if (!std::regex_search(filename, filter_regex)) {
          sort_files.push_back(filename);
        }
      }
    }
    closedir(dirp);

    std::sort(sort_files.begin(), sort_files.end());
    std::string files;
    for (const auto &s : sort_files) {
      if (!files.empty())
        absl::StrAppend(&files, ",", src_path, "/", s);
      else
        files = abls::StrCat(        src_path, "/", s);
    }

    var.add("files", files);

  } catch (const std::regex_error &e) {
    Main_api::error(
        "invalid regex. It is a FULL regex unlike bash. To test, try: `ls src_path | grep -E \"match\" | grep -v \"filter\"`",
        match);
  }
}

void Top_api::setup(Eprp &eprp) {
  // Alphabetical order sorted to avoid undeterminism in different file orders
  Eprp_method m1("files", "match file names in alphabetical order. Like `ls {src_path} | grep -E {match} | sort`", &Top_api::files);
  m1.add_label_optional("src_path", "source path to match the file search. by default", ".");
  m1.add_label_optional("match", "quoted string of regex to match. E.g: match:\"\\.v$\" for verilog files.");
  m1.add_label_optional("filter", "quoted string of regex to filter out or remove from match.");

  eprp.register_method(m1);
}
