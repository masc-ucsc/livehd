//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#ifndef INOU_graphviz_H
#define INOU_graphviz_H

#include <atomic>
#include <string>

#include "inou.hpp"
#include "options.hpp"

class Inou_graphviz_options : public Options_base {
public:
  Inou_graphviz_options()
    : odir(".") {
  }

  std::string odir;

  void set(const std::string &key, const std::string &value) final;
};

class Inou_graphviz : public Inou {
private:
protected:
  Inou_graphviz_options opack;

  std::atomic<int> total;
  void inc_total(Index_ID idx) {
    total++;
  };

public:
  Inou_graphviz();
  virtual ~Inou_graphviz();

  std::vector<LGraph *> tolg() final;
  void fromlg(std::vector<const LGraph *> &out) final;

  void set(const std::string &key, const std::string &value) final {
    opack.set(key,value);
  }
};

#endif
