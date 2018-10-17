//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#ifndef INOU_RAMGEN_H
#define INOU_RAMGEN_H

#include <atomic>
#include <string>

#include "inou.hpp"
#include "options.hpp"

class Inou_ramgen_options : public Options_base {
public:
  Inou_ramgen_options()
    : odir(".") {
  }

  std::string odir;

  void set(const std::string &key, const std::string &value) final;
};

class Inou_ramgen : public Inou {
private:
protected:
  Inou_ramgen_options opack;

  std::atomic<int> total;
  void inc_total(Index_ID idx) {
    total++;
  };

public:
  Inou_ramgen();
  virtual ~Inou_ramgen();

  std::vector<LGraph *> tolg() final;
  void fromlg(std::vector<const LGraph *> &out) final;

  void set(const std::string &key, const std::string &value) final {
    opack.set(key,value);
  }
};

#endif
