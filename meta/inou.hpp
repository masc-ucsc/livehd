//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef INOU_H
#define INOU_H

#include "lgraph.hpp"
#include "options.hpp"

// Abstract class that any inou block must support
class Inou {
private:
protected:
public:
  virtual std::vector<LGraph *> generate() = 0; // Input modules like random graph generation (must call console->sync at the end)
  virtual void                  generate(std::vector<const LGraph *> &out) = 0; // Output modules like to llvm target

  void generate(const LGraph *g);
  void generate(const LGraph &g);
  void generate(LGraph &g);

  void register_inou(const std::string &name, Options_pack *opack);
};

class Inou_trivial : public Inou {
private:
  Options_pack opack;

public:
  bool is_graph_name_provided() const;

  Inou_trivial() {
  }
  explicit Inou_trivial(Options_pack &opt)
      : opack(opt) {
  }

  std::vector<LGraph *> generate() override;
  void                  generate(std::vector<const LGraph *> &out) override;
};

#endif
