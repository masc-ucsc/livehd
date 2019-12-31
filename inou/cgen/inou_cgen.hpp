//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph.hpp"
#include "pass.hpp"

class Inou_cgen : public Pass {
private:

  enum class Cgen_type { Type_verilog, Type_prp, Type_cfg, Type_cpp };

  enum class Declaration_type { Decl_local=0, Decl_inp, Decl_out, Decl_sflop, Decl_aflop, Decl_fflop, Decl_latch };
  struct Declaration {
    std::string_view name;
    int              bits;
    Declaration_type type;
    int              pos; // src LoC position (for relative order when possible)
    bool             is_signed;
    int              order; // relative order for fields in structs

    void format_raw(std::ostringstream &w) const;
  };

  std::multimap<std::string_view, int> declaration_root; // For structs
  std::vector<Declaration>        declarations;

  LGraph *lg;

  void iterate_declarations(Node_pin &pin);

  void setup_declarations();

  void generate_prp(LGraph *g, std::string_view filename);

  void to_xxx(Cgen_type cgen_type);

  // callback entry points
  static void to_verilog(Eprp_var &var);
  static void to_prp(Eprp_var &var);
  static void to_cfg(Eprp_var &var);
  static void to_cpp(Eprp_var &var);
public:
  Inou_cgen(const Eprp_var &var);

  static void setup();
};
