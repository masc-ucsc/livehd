//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// top_api. This are commands at the top level like filter, files...

#include "top_api.hpp"

#include <dirent.h>

#include <iostream>
#include <regex>
#include <string>

#include "lgraph.hpp"

void Top_api::files(Eprp_var &var) {
  std::string src_path(var.get("src_path"));
  std::string match(var.get("match"));
  std::string filter(var.get("filter"));

  try {
    const std::regex txt_regex(match);
    const std::regex filter_regex(filter);

    DIR *dirp = opendir(src_path.c_str());
    if (dirp == 0) {
      Main_api::error("invalid src_path:{}, is it a valid directory?", src_path);
      return;
    }
    std::vector<std::string> sort_files;
    struct dirent           *dp;
    while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_type == DT_DIR) {
        continue;
      }
      std::string filename(dp->d_name);  // string looks for 0 sequence
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
      if (!files.empty()) {
        absl::StrAppend(&files, ",", src_path, "/", s);
      } else {
        files = absl::StrCat(src_path, "/", s);
      }
    }

    var.add("files", files);

  } catch (const std::regex_error &e) {
    Main_api::error(
        "invalid regex. It is a FULL regex unlike bash. To test, try: `ls src_path | grep -E \"match\" | grep -v \"filter\"`",
        match);
  }
}

void Top_api::filter(Eprp_var &var) {
  auto match = var.get("match");

  if (match.empty()) {
    var.lgs.clear();
    var.lnasts.clear();
    return;
  }

  try {
    std::string       match_str{match};
    const std::regex  match_regex{match_str};

    Eprp_var::Eprp_lgs filtered_lgs;
    for (auto *lg : var.lgs) {
      std::string name{lg->get_name()};
      if (std::regex_search(name, match_regex)) {
        filtered_lgs.push_back(lg);
      }
    }
    var.lgs = std::move(filtered_lgs);

    Eprp_var::Eprp_lnasts filtered_lnasts;
    for (const auto &ln : var.lnasts) {
      std::string name{ln->get_top_module_name()};
      if (std::regex_search(name, match_regex)) {
        filtered_lnasts.push_back(ln);
      }
    }
    var.lnasts = std::move(filtered_lnasts);

  } catch (const std::regex_error &e) {
    Main_api::error("invalid match:{} regex for filter command", match);
  }
}

void Top_api::setup(Eprp &eprp) {
  // Alphabetical order sorted to avoid undeterminism in different file orders
  Eprp_method m1("files", "match file names in alphabetical order. Like `ls {src_path} | grep -E {match} | sort`", &Top_api::files);
  m1.add_label_optional("src_path", "source path to match the file search. by default", ".");
  m1.add_label_optional("match", "quoted string of regex to match. E.g: match:\"\\.v$\" for verilog files.");
  m1.add_label_optional("filter", "quoted string of regex to filter out or remove from match.");

  eprp.register_method(m1);

  //---------------------
  Eprp_method m2("filter", "filter lgraphs/lnasts by name regex. If no match is provided, nothing passes through", &Top_api::filter);
  m2.add_label_optional("match", "quoted string of regex to match against lgraph/lnast names");

  eprp.register_method(m2);
}
