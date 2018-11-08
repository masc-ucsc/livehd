//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef INOU_PYROPE_H
#define INOU_PYROPE_H

#include <string>
#include <iostream>

#include "lgraph.hpp"
#include "pass.hpp"

class Inou_pyrope : public Pass {
private:
  std::map<Index_ID, std::string> inline_stmt;

  typedef std::ostringstream Out_string;

protected:
  void to_pyrope(const LGraph *g, const std::string& filename);
  void to_src_var(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_dst_var(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_mux(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_join(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_flop(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_graphio(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_logical2(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const;
  bool to_shift(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op) const;
  bool to_sum(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_logical1(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const;
  bool to_subgraph(Out_string &w, Out_string &out, const LGraph *g, Index_ID idx) const;
  bool to_compare(Out_string &w, const LGraph *g, Index_ID idx, const char *op) const;
  bool to_const(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_pick(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_latch(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_strconst(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_op(Out_string &s, Out_string &sub, const LGraph *g, Index_ID idx) const;

  static void fromlg(Eprp_var &var);

public:
  Inou_pyrope();

  void setup() final;

};

#endif
