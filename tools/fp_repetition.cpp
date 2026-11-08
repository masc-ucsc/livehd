// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Detection-only replay of `synth.place.fp=auto` on a structural (gate-level)
// Verilog netlist: prints the repeated-hierarchy report (pass/place/fp_repetition.hpp).
// The netlist scan reads only module headers and cell instances; it is not a
// Verilog parser and does not elaborate parameters or generate blocks.
#include "pass/place/fp_repetition.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "inou/lefdef/physical.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace ph = livehd::physical;
namespace pl = livehd::placement;
namespace {
struct Instance {
  std::string master, name;
};
struct Module {
  std::vector<Instance> instances;
};
std::string strip_comments_and_attributes(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\') {  // escaped identifier: copy through the terminating whitespace
      while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
        out += s[i++];
      }
      if (i < s.size()) {
        out += s[i];
      }
    } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      i = s.find('\n', i);
      if (i == std::string::npos) {
        break;
      }
      out += '\n';
    } else if (s[i] == '(' && i + 1 < s.size() && s[i + 1] == '*' && (i + 2 == s.size() || s[i + 2] != ')')) {
      i = s.find("*)", i + 2);  // attribute instance `(* ... *)`; `(*)` is a sensitivity list
      if (i == std::string::npos) {
        break;
      }
      ++i;
      out += ' ';
    } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      i = s.find("*/", i + 2);
      if (i == std::string::npos) {
        break;
      }
      ++i;
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}
class Scanner {
  const std::string& s_;
  size_t             p_ = 0;

public:
  explicit Scanner(const std::string& s) : s_(s) {}
  bool done() {
    space();
    return p_ >= s_.size();
  }
  void space() {
    while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) {
      ++p_;
    }
  }
  char peek() {
    space();
    return p_ < s_.size() ? s_[p_] : '\0';
  }
  void        skip() { ++p_; }
  void        line() { p_ = std::min(s_.find('\n', p_), s_.size()); }
  std::string ident() {
    space();
    const auto start = p_;
    if (p_ < s_.size() && s_[p_] == '\\') {
      while (p_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[p_]))) {
        ++p_;
      }
      return s_.substr(start + 1, p_ - start - 1);
    }
    while (p_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[p_])) || s_[p_] == '_' || s_[p_] == '$')) {
      ++p_;
    }
    return s_.substr(start, p_ - start);
  }
  // Skip to the end of the current statement (`;` outside parentheses).
  void statement() {
    int depth = 0;
    while (p_ < s_.size()) {
      const char c = s_[p_];
      if (c == '\\') {
        while (p_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[p_]))) {
          ++p_;
        }
        continue;
      }
      ++p_;
      if (c == '(') {
        ++depth;
      } else if (c == ')') {
        --depth;
      } else if (c == ';' && depth <= 0) {
        return;
      }
    }
  }
  void balanced() {  // at '('
    int depth = 0;
    while (p_ < s_.size()) {
      const char c = s_[p_++];
      if (c == '(') {
        ++depth;
      } else if (c == ')' && --depth == 0) {
        return;
      }
    }
  }
};
std::map<std::string, Module> read_netlist(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read netlist: " + path);
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto                         text = strip_comments_and_attributes(buffer.str());
  Scanner                            scan(text);
  std::map<std::string, Module>      modules;
  Module*                            current = nullptr;
  static const std::set<std::string> keywords{
      "input", "output", "inout",  "wire",    "reg",    "assign",  "parameter", "localparam", "supply0", "supply1",  "tri",
      "wand",  "wor",    "genvar", "integer", "always", "initial", "specify",   "function",   "task",    "defparam", "timescale"};
  while (!scan.done()) {
    if (scan.peek() == '`') {  // compiler directive (`timescale 1ns/1ps, `default_nettype none): drop the line
      scan.line();
      continue;
    }
    const auto word = scan.ident();
    if (word.empty()) {
      scan.skip();
      continue;
    }
    if (word == "module") {
      current = &modules[scan.ident()];
      scan.statement();
    } else if (word == "endmodule") {
      current = nullptr;
    } else if (!current || keywords.contains(word)) {
      scan.statement();
    } else {
      if (scan.peek() == '#') {
        scan.skip();
        scan.balanced();
      }
      auto name = scan.ident();
      if (!name.empty()) {
        current->instances.push_back({word, std::move(name)});
      }
      scan.statement();
    }
  }
  return modules;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> lefs;
    std::string              verilog, liberty, top;
    pl::Fp_rep_options       options;
    size_t                   limit = 5;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (i + 1 == argc) {
        throw std::invalid_argument("missing argument value for " + key);
      }
      const std::string value = argv[++i];
      if (key == "--lef") {
        lefs.push_back(value);
      } else if (key == "--verilog") {
        verilog = value;
      } else if (key == "--liberty") {
        liberty += (liberty.empty() ? "" : " ") + value;
      } else if (key == "--top") {
        top = value;
      } else if (key == "--share") {
        options.share = std::stod(value);
      } else if (key == "--min-copies") {
        options.min_copies = std::stoi(value);
      } else if (key == "--min-copy-share") {
        options.min_copy_share = std::stod(value);
      } else if (key == "--tolerance") {
        options.tolerance = std::stod(value);
      } else if (key == "--candidates") {
        options.candidates = value != "0" && value != "false";
      } else if (key == "--fp-min-macros") {
        options.fp_min_macros = std::stoi(value);
      } else if (key == "--fp-min-copies") {
        options.fp_min_copies = std::stoi(value);
      } else if (key == "--limit") {
        limit = std::stoul(value);
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (lefs.empty() || verilog.empty() || liberty.empty()) {
      throw std::invalid_argument(
          "usage: fp_repetition --lef tech.lef --lef cells.lef --liberty cells.lib --verilog netlist.v [--top name] "
          "[--share 0.2 --min-copies 2 --min-copy-share 0.02 --tolerance 0.05 --candidates 1 --fp-min-macros 2 --fp-min-copies 4 "
          "--limit 5]");
    }
    const auto tech       = ph::read_lef(lefs);
    const auto sequential = livehd::liberty::sequential_cell_names(liberty);
    const auto netlist    = read_netlist(verilog);
    if (top.empty()) {
      std::set<std::string> used;
      for (const auto& [name, m] : netlist) {
        for (const auto& inst : m.instances) {
          used.insert(inst.master);
        }
      }
      for (const auto& [name, m] : netlist) {
        if (!used.contains(name)) {
          if (!top.empty()) {
            throw std::invalid_argument("several top modules (" + top + ", " + name + "): use --top");
          }
          top = name;
        }
      }
    }
    if (!netlist.contains(top)) {
      throw std::invalid_argument("top module not found: " + top);
    }
    pl::Fp_rep_design             design;
    std::map<std::string, size_t> ids;
    for (const auto& [name, m] : netlist) {
      ids.emplace(name, design.modules.size());
      design.modules.push_back({name, {}, {}});
    }
    design.top = ids.at(top);
    for (const auto& [name, m] : netlist) {
      auto& module = design.modules[ids.at(name)];
      for (const auto& inst : m.instances) {
        if (auto found = ids.find(inst.master); found != ids.end()) {
          module.children.push_back({inst.name, found->second});
          continue;
        }
        const auto macro = tech.macros.find(inst.master);
        if (macro == tech.macros.end()) {
          continue;
        }
        const double area = macro->second.width * macro->second.height;
        if (sequential.contains(inst.master)) {
          module.leaves.push_back({inst.name, area, false});
        } else if (pl::fp_rep_memory_class(macro->second.klass)) {
          module.leaves.push_back({inst.name, area, true});
        }
      }
    }
    const auto result = pl::detect_fp_repetition(design, options);
    std::cout << pl::fp_rep_json(result, options, limit) << '\n';
  } catch (const std::exception& e) {
    std::cerr << "fp_repetition: " << e.what() << '\n';
    return 1;
  }
}
