//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "lgraph.hpp"
#include "pass.hpp"
#include "lnast_parser.hpp"
#include "lnast_to_cfg_parser.hpp"

class Inou_cgen_options {
public:
  std::string files;
  std::string path;
};

class Inou_cgen : public Pass {
private:
  Inou_cgen_options opack;
  std::string_view memblock;
  Lnast_parser lnast_parser;
  Lnast_to_cfg_parser *lnast_to_cfg_parser;
  Language_neutral_ast<Lnast_node> *lnast;
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
  std::vector<Declaration>        declarations;

  LGraph *lg;

  void iterate_declarations(Node_pin &pin);

protected:
  void setup_declarations();

  void to_pyrope(LGraph *g, std::string_view filename);

  static void fromlg(Eprp_var &var);

public:
  Inou_cgen();
  static void tocfg(Eprp_var &var);

  void setup() final;

private:
  std::string_view setup_memblock();
  void do_tocfg();
};
