//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Created by Sheng Hong  on 4/14/18.

#ifndef INOU_CFG_H
#define INOU_CFG_H

#include <string>
#include <vector>

#include "inou.hpp"
#include "options.hpp"

class Inou_cfg_options : public Options_base {
public:
  std::string file;
  Inou_cfg_options()= default;

  void set(const std::string &key, const std::string &value);
};

class Inou_cfg : public Inou {
private:
  static bool space(char c) {
    return isspace(c);
  }
  static bool not_space(char c) {
    return !isspace(c);
  }
  std::vector<std::string> split(const std::string &str);
  void build_graph(std::vector<std::string> &, std::string &, LGraph *, std::map<std::string, uint32_t> &,
                   std::map<std::string, std::string> &, std::map<std::string, Node> &,
                   std::map<std::string, std::vector<std::string>> &, std::unordered_map<std::string, std::string> &, Node &);
  void cfg_2_lgraph(char **, std::vector<LGraph *> &, std::unordered_map<std::string, std::string> &, const std::string &);
  void remove_fake_fcall(LGraph *g);
  void collect_fcall_info(LGraph *g, Node node, const std::string &w7, const std::string &w8, const std::string &w9);
  void update_ifs(std::vector<LGraph *> &lgs, std::vector<std::map<std::string, Node>> &node_mappings);

protected:
  Inou_cfg_options opack;

  void lgraph_2_cfg(LGraph *g, const std::string &filename) ;

public:
  Inou_cfg();
  virtual ~Inou_cfg();

  std::vector<LGraph *> tolg() final;
  void                  fromlg(std::vector<LGraph *> &out) ;
  void                  set(const std::string &key, const std::string &value) final {
    opack.set(key, value);
  }
};

#endif
