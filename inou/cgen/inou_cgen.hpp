//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "lgraph.hpp"
#include "pass.hpp"

class Inou_cgen : public Pass {
private:
  typedef std::ostringstream Out_string;

  enum Declaration_type { Decl_local, Decl_inp, Decl_out, Decl_sflop, Decl_aflop, Decl_fflop, Decl_latch };
  struct Declaration {
    std::string_view name;
    int              bits;
    Declaration_type type;
    int              pos; // src LoC position (for relative order when possible)
    bool             is_signed;
    int              order; // relative order for fields in structs

    void format_raw(Out_string &w) const;
  };

  std::multimap<std::string_view, int> declaration_root; // For structs
  std::vector<Declaration>        declaration;

  const LGraph *lg;

  void iterate_declarations(Index_ID idx, Port_ID pid);

protected:
  void setup_declarations();

  void to_pyrope(const LGraph *g, std::string_view filename);

  static void fromlg(Eprp_var &var);

public:
  Inou_cgen();

  void setup() final;
};
