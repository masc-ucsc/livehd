//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_BITWIDTH_H
#define PASS_BITWIDTH_H

#include <string>

#include "bm.h"

#include "pass.hpp"
#include "options.hpp"
#include "lgraph.hpp"

class Pass_bitwidth_options_pack : public Options_base {
public:
  int max_iterations = 10;

  void set(const std::string &label, const std::string &value);
};

class Pass_bitwidth : public Pass {
protected:
  Pass_bitwidth_options_pack opack;
  class Node_properties {
  public:
    class Explicit_range {
    public:
      int64_t max = 0;
      int64_t min = 0;

      bool sign = false;

      bool overflow = false;

      bool max_set = false;
      bool min_set = false;
      bool sign_set = false;

      void dump() const;
      bool is_unsigned() const;
      void set_uconst(uint32_t value);
      void set_ubits(uint16_t size);
      void set_sbits(uint16_t size);
    };

    class Implicit_range {
    public:
      int64_t max;
      int64_t min;

      bool sign;
      bool overflow;

      Implicit_range() {
        max = 0;
        min = 0;
        sign = false;
        overflow = false;
      }
      Implicit_range(const Implicit_range &i) {
        max = i.max;
        min = i.min;
        sign = i.sign;
        overflow = i.overflow;
      }
      void dump() const;
      int64_t round_power2(int64_t x) const;
      bool expand(const Implicit_range &i, bool round2);
      void pick(const Explicit_range &e);

    };

    Implicit_range i;
    Explicit_range e;
    uint16_t niters = 0;

    void set_implicit() {
      i.min = e.min;
      i.max = e.max;

      i.sign = !e.is_unsigned();
      i.overflow = e.overflow;
    }

  };

  std::vector<Node_properties> bw;
  bm::bvector<>      pending;
  bm::bvector<>      next_pending;

  void mark_all_outputs(const LGraph *lg, Index_ID idx);

  void iterate_graphio(const LGraph *lg, Index_ID idx);
  void iterate_logic(const LGraph *lg, Index_ID idx);
  void iterate_arith(const LGraph *lg, Index_ID idx);
  void iterate_pick(const LGraph *lg, Index_ID idx);

  void iterate_node(LGraph *lg, Index_ID idx);

  void bw_pass_setup(LGraph *lg);
  void bw_pass_dump(LGraph *lg);
  bool bw_pass_iterate(LGraph *lg);

public:
  Pass_bitwidth();

  void trans(LGraph *orig) final;

  LGraph *regen(const LGraph *orig) {
    assert(false);
  }

  //no options needed?
  void set(const std::string &key, const std::string &value) { }
};

#endif
