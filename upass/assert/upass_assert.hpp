//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>

#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_assert : public upass::uPass {
public:
  uPass_assert(std::shared_ptr<upass::Lnast_manager>&);
  uPass_assert() = delete;
  virtual ~uPass_assert() {}

  void process_func_call();

protected:
  inline Const current_pyrope_value() { return *Dlop::from_pyrope(current_text()); }

  inline Const current_prim_value() {
    // A ref operand is not a literal, so it is statically unknown here (an
    // invalid Const → the caller treats it as "cannot decide"). Only literal
    // operands fold to a concrete value via from_pyrope.
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return Const{};
    } else {
      return *Dlop::from_pyrope(current_text());
    }
  }
};

// Plugin registration lives in upass_assert.cpp to avoid duplicate
// construction when multiple TUs include this header.
