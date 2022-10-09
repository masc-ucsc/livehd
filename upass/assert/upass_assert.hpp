//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "fmt/format.h"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "upass_core.hpp"

struct uPass_assert : public upass::uPass {
public:
  uPass_assert(std::shared_ptr<upass::Lnast_manager>&);
  uPass_assert() = delete;
  virtual ~uPass_assert() {}

  void process_func_call();

protected:
  Symbol_table st;

  inline auto current_bundle() { return st.get_bundle(current_text()); }

  inline auto current_pyrope_value() { return Lconst::from_pyrope(current_text()); }

  inline auto current_prim_value() {
    if (is_type(Lnast_ntype::Lnast_ntype_ref)) {
      return st.get_trivial(current_text());
    } else {
      return Lconst::from_pyrope(current_text());
    }
  }
};

static upass::uPass_plugin plugin_assert("assert", upass::uPass_wrapper<uPass_assert>::get_upass);
