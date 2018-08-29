//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef INOU_pyrope_H
#define INOU_pyrope_H

#include <sstream>
#include <string>

#include "inou.hpp"
#include "py_options.hpp"

class Inou_pyrope_options : public Py_options {
public:
  std::string pyrope_output;
  std::string pyrope_input;
//  std::string graph_name;
//  std::string lgdb_path;

  Inou_pyrope_options() {
    pyrope_output = "pyrope_out";
    pyrope_input = "pyrope_in";
  }
  void set(const py::dict &dict) final;
};

class Inou_pyrope : public Inou {
private:

  std::map<Index_ID, std::string> inline_stmt;

  typedef std::ostringstream Out_string;

protected:
  Inou_pyrope_options opack;

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

  bool to_op(Out_string &s, Out_string &sub, const LGraph *g, Index_ID idx, bool is_subgraph, std::vector<const char *> output_vars) const;
public:
  Inou_pyrope();

  std::vector<LGraph *> generate() final;
  using Inou::generate;
  void generate(std::vector<const LGraph *> &out) final;

  // Python interface
  Inou_pyrope(const py::dict &dict);

  std::vector<LGraph *> py_generate() { return generate(); };
  void py_set(const py::dict &dict);
};

#endif
