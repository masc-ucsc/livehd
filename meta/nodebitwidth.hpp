//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "dense.hpp"
#include "lgraphbase.hpp"

class Node_bitwidth {
public:
  class Explicit_range {
  public:
    int64_t max = 0;
    int64_t min = 0;

    bool sign     = false;
    bool overflow = false;
    bool max_set  = false;
    bool min_set  = false;
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
    bool shift_left(const Implicit_range &i);
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

class LGraph_Node_bitwidth : virtual public LGraph_Base {
private:
  mutable Dense<Node_bitwidth> node_bitwidth;

protected:
  void node_bitwidth_emplace_back();

public:
  LGraph_Node_bitwidth() = delete;
  explicit LGraph_Node_bitwidth(const std::string &path, const std::string &name) noexcept;
  virtual ~LGraph_Node_bitwidth(){};

  virtual void clear();
  virtual void reload();
  virtual void sync();
  virtual void emplace_back();

  void  node_bitwidth_set(Index_ID nid, const Node_bitwidth &t);
  Node_bitwidth &node_bitwidth_get(Index_ID nid) const;
};

