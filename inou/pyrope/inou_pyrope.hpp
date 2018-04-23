#ifndef INOU_pyrope_H
#define INOU_pyrope_H

#include <string>
#include <sstream>

#include "inou.hpp"
#include "options.hpp"

class Inou_pyrope_options_pack : public Options_pack {
public:
  Inou_pyrope_options_pack();

  std::string pyrope_output;
  std::string pyrope_input;
};

class Inou_pyrope : public Inou {
private:
  std::map<Index_ID, std::string> inline_stmt;

  typedef std::ostringstream Out_string;

protected:
  Inou_pyrope_options_pack opack;

  void to_pyrope(const LGraph *g, const std::string filename);
  void to_src_var(Out_string &w, const LGraph *g, Index_ID idx) const;
  void to_dst_var(Out_string &w, const LGraph *g, Index_ID idx) const;
  void to_normal_var(Out_string &w, const LGraph *g, Index_ID idx) const;

  bool to_mux(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_join(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_flop(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_graphio(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_logical2(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const;
  bool to_shift(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op) const;
  bool to_sum(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_logical1(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const;
  bool to_subgraph(Out_string &w, Out_string &out, const LGraph *g, Index_ID idx) const;
  bool to_equals(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_const(Out_string &w, const LGraph *g, Index_ID idx) const;
  bool to_pick(Out_string &w, const LGraph *g, Index_ID idx) const;

public:
  Inou_pyrope();

  std::vector<LGraph *> generate() override final;
  using Inou::generate;
  void generate(std::vector<const LGraph *> out) override final;
};


#endif

