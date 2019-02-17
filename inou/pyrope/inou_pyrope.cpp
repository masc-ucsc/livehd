//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <sstream>
#include <string>
#include <strings.h>

#include "eprp_utils.hpp"
#include "inou_pyrope.hpp"
#include "lgedgeiter.hpp"

// FIXME: We'll eventually need to rethink this data structure.
#define tmp_size 10000
std::string tmp_values[tmp_size];

void setup_inou_pyrope() {
  Inou_pyrope p;
  p.setup();
}

Inou_pyrope::Inou_pyrope()
    : Pass("pyrope") {
}

void Inou_pyrope::setup() {
  Eprp_method m1("inou.pyrope.fromlg", "generate a pyrope output", &Inou_pyrope::fromlg);

  register_inou(m1);
}

void Inou_pyrope::fromlg(Eprp_var &var) {

  Inou_pyrope p;

  auto odir = var.get("odir");
  bool              ok   = p.setup_directory(odir);
  if(!ok)
    return;

  for(const auto &g : var.lgs) {
    std::string f;
    f.append(odir);
    f.append("/");
    f.append(g->get_name());
    f.append(".prp");
    p.to_pyrope(g, f);
  }
}

bool Inou_pyrope::to_dst_var(Out_string &w, const LGraph *g, Index_ID idx) const {
  Index_ID direct_to_register = 0;
  for(const auto &c : g->out_edges(idx)) {
    const auto op = g->get_node(c.get_inp_pin()).get_type();
    if(op.op == SFlop_Op) {
      direct_to_register = c.get_idx();
      break;
    }
  }

  Index_ID direct_to_output = 0;
  if(g->is_graph_output(idx)) {
    direct_to_output = idx;
  } else {
    Index_ID it_idx = idx;
    if(direct_to_register)
      it_idx = direct_to_register;
    for(const auto &c : g->out_edges(it_idx)) {
      if(g->is_graph_output(c.get_idx())) {
        direct_to_output = c.get_idx();
        break;
      }
    }
  }

  if(direct_to_output) {
    w << " %" << g->get_node_wirename(direct_to_output) << " = ";
  } else if(direct_to_register) {
    w << " @tmp" << direct_to_register << " = ";
  } else {
    /*w << " tmp"
      << idx
      << " = ";*/
    return true;
  }
  return false;
}

void Inou_pyrope::to_src_var(Out_string &w, const LGraph *g, Index_ID idx) const {
  if(idx == 0) {
    w << " FIXME" << idx;
    return;
  }
  if(g->is_graph_input(idx)) {
    w << "$" << g->get_node_wirename(idx);
    return;
  }

  Index_ID   direct_to_register = 0;
  int        const_val          = 0;
  const auto op                 = g->node_type_get(idx);
  if(op.op == SFlop_Op) {
    direct_to_register = idx;
  } else if(op.op == U32Const_Op) {
    const_val = g->node_value_get(idx);
  }

  Index_ID direct_to_output = 0;
  if(g->is_graph_output(idx)) {
    direct_to_output = idx;
  } else {
    if(direct_to_register) {
      for(const auto &c : g->out_edges(direct_to_register)) {
        if(g->is_graph_output(c.get_idx())) {
          direct_to_output = c.get_idx();
          break;
        }
      }
    }
  }

  if(direct_to_output) {
    w << "%" << g->get_node_wirename(direct_to_output);
  } else if(direct_to_register) {
    w << "@tmp" << direct_to_register;
  } else if(inline_stmt.find(idx) != inline_stmt.end()) {
    w << " (" << inline_stmt.at(idx) << ")";
  } else if(const_val) {
    w << " " << const_val;
  } else {
    w << tmp_values[idx];
  }
}

bool Inou_pyrope::to_mux(Out_string &w, const LGraph *g, Index_ID idx) const {

  Index_ID t_idx = 0;
  Index_ID f_idx = 0;
  Index_ID c_idx = 0;

  // WARNING: input edges dst_pid can go anywhere, must search reverse edge to see input
  for(const auto &c : g->inp_edges(idx)) {
    switch(c.get_inp_pin().get_pid()) {
    case 0:
      c_idx = c.get_idx();
      break;
    case 1:
      f_idx = c.get_idx();
      break;
    case 2:
      t_idx = c.get_idx();
      break;
    default:
      assert(false);
    }
  }

  Index_ID direct_to_register = 0;
  if(g->is_graph_input(c_idx)) {
    if(is_reset(g->get_node_wirename(c_idx))) {

      for(const auto &c : g->out_edges(idx)) {
        const auto op = g->node_type_get(c.get_idx());
        if(op.op == SFlop_Op) {
          direct_to_register = c.get_idx();
          break;
        }
      }
    }
  }

  if(direct_to_register) {
    Index_ID direct_to_output = 0;
    for(const auto &c : g->out_edges(direct_to_register)) {
      if(g->is_graph_output(c.get_idx())) {
        direct_to_output = c.get_idx();
        break;
      }
    }
    if(direct_to_output) {
      w << " @" << g->get_node_wirename(direct_to_output) << " ";
    } else {
      w << " @tmp" << direct_to_register << " ";
    }
    w << " __reset: ";
    to_src_var(w, g, t_idx);
    w << "\n";

    to_dst_var(w, g, idx);
    to_src_var(w, g, f_idx);
    return true;
  }

  w << " if ";
  to_src_var(w, g, c_idx);
  w << " { \n   ";
  // ----------- TRUE
  bool is_tmp; // HC_A
  is_tmp = to_dst_var(w, g, idx);
  if(is_tmp) {
    w << "tmp" << idx << " = ";
  }
  to_src_var(w, g, t_idx);
  // ----------- FALSE
  if(t_idx == f_idx) {
    w << "\n }\n";
  } else {
    w << "\n } else {\n   ";
    is_tmp = to_dst_var(w, g, idx);
    if(is_tmp) {
      w << "Stmp" << idx << " = ";
    }
    to_src_var(w, g, f_idx);
    w << "\n }\n";
  }

  Out_string tmpOut;
  tmpOut << "tmp" << idx;
  tmp_values[idx] = tmpOut.str();

  return false;
}

bool Inou_pyrope::to_join(Out_string &w, const LGraph *g, Index_ID idx) const {
  Out_string tmp;
  int        ninputs = 0;
  for(const auto &c : g->inp_edges(idx)) {
    ninputs++;
    to_src_var(tmp, g, c.get_idx());
  }
  if(ninputs > 1)
    w << " (" << tmp.str() << ")[[]]";
  else
    w << tmp.str();

  return true;
}

bool Inou_pyrope::to_flop(Out_string &w, const LGraph *g, Index_ID idx) const {

  Index_ID direct_to_output = 0;
  for(const auto &c : g->out_edges(idx)) {
    if(g->is_graph_output(c.get_idx())) {
      direct_to_output = c.get_idx();
      break;
    }
  }

  if(direct_to_output) {
    if(g->get_bits(direct_to_output) == 0) {
      w << " %" << g->get_node_wirename(direct_to_output) << " as __bits: " << g->get_bits(idx) << "\n";
    }
  } else {
    w << " @tmp" << idx << " as __bits: " << g->get_bits(idx) << "\n";
  }

  for(const auto &c : g->inp_edges(idx)) {
    const auto op = g->node_type_get(c.get_idx());
    if(op.op != Mux_Op)
      continue;

    for(const auto &c2 : g->inp_edges(c.get_idx())) {
      if(g->is_graph_input(c2.get_idx())) {
        if(is_reset(g->get_node_wirename(c2.get_idx()))) {
          return false;
        }
      }
    }
  }

  if(direct_to_output) {
    w << " @" << g->get_node_wirename(direct_to_output) << " =";
  } else {
    w << " @tmp" << idx << " =";
  }
  for(const auto &c : g->inp_edges(idx)) {
    if(c.get_inp_pin().get_pid() != 0)
      to_src_var(w, g, c.get_idx());
  }

  return false;
}

bool Inou_pyrope::to_graphio(Out_string &w, const LGraph *g, Index_ID idx) const {

  auto bits = g->get_bits(idx);
  if(!bits)
    return false;

  w << " $" << g->get_node_wirename(idx) << " as __bits: " << bits << "\n";

  return false;
}

bool Inou_pyrope::to_logical2(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const {

  bool first = true;
  for(const auto &c : g->inp_edges(idx)) {
    if(c.get_bits() == 0) {
      w << " " << c_op; // for reduction ops
    } else if(first) {
      first = false;
    } else {
      if(c.get_bits() == 1)
        w << " " << s_op << " ";
      else
        w << " " << c_op << " ";
    }
    to_src_var(w, g, c.get_idx());
  }

  return true;
}

bool Inou_pyrope::to_shift(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op) const {

  bool first = true, done = false;
  for(const auto &c : g->inp_edges(idx)) {
    if(first) {
      first = false;
    } else if(!done) {
      w << " " << c_op;
      if(c.get_inp_pin().get_pid() > 1) {
        done = true;
        continue;
      }
    }
    to_src_var(w, g, c.get_idx());
  }

  return true;
}

bool Inou_pyrope::to_sum(Out_string &w, const LGraph *g, Index_ID idx) const {

  bool first = true;
  for(const auto &c : g->inp_edges(idx)) {
    if(c.get_inp_pin().get_pid() == 0 || c.get_inp_pin().get_pid() == 1) {
      if(first) {
        first = false;
      } else {
        w << " + ";
      }
      to_src_var(w, g, c.get_idx());
    }
  }

  for(const auto &c : g->inp_edges(idx)) {
    if(c.get_inp_pin().get_pid() != 0 && c.get_inp_pin().get_pid() != 1) {
      if(first) {
        first = false;
      } else {
        w << " - ";
      }
      to_src_var(w, g, c.get_idx());
    }
  }

  return true;
}

bool Inou_pyrope::to_logical1(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const {

  bool first = true;
  for(const auto &c : g->inp_edges(idx)) {
    if(first) {
      first = false;
      if(c.get_bits() == 1)
        w << s_op << " ";
      else
        w << c_op << " ";
    } else {
      assert(false); // Should not happen with operations like NOT
    }
    to_src_var(w, g, c.get_idx());
  }

  return true;
}

bool Inou_pyrope::to_compare(Out_string &w, const LGraph *g, Index_ID idx, const char *op) const {

  bool first = true;
  for(const auto &c : g->inp_edges(idx)) {
    if(first) {
      first = false;
    } else {
      w << " " << op << " ";
    }
    to_src_var(w, g, c.get_idx());
  }

  return true;
}

bool Inou_pyrope::to_const(Out_string &w, const LGraph *g, Index_ID idx) const {

  w << g->node_value_get(idx);

  return true;
}

bool Inou_pyrope::to_pick(Out_string &w, const LGraph *g, Index_ID idx) const {

  int upper = 0, lower = 0;
  for(const auto &c : g->inp_edges(idx)) {
    const auto op = g->node_type_get(c.get_idx());
    if(op.op == U32Const_Op) {
      g->node_value_get(c.get_idx());
      upper = lower + g->get_bits(idx) - 1;
      assert(lower <= upper);
      w << "[" << lower << ":" << upper << "]\n";
    } else {
      to_src_var(w, g, c.get_idx());
    }
  }

  return true;
}

bool Inou_pyrope::to_latch(Out_string &w, const LGraph *g, Index_ID idx) const {

  Index_ID latch_idx = 0, inp_idx = 0;

  for(const auto &c : g->out_edges(idx)) {
    latch_idx = c.get_idx();
  }

  for(const auto &c : g->inp_edges(idx)) {
    inp_idx = c.get_idx();
    break;
  }

  const auto &node = g->get_node(latch_idx);
  if(!node.has_inputs())
    return false; // Not driven used output

  w << " @" << g->get_node_wirename(latch_idx) << " as __fflop:false";
  w << "\n @" << g->get_node_wirename(latch_idx) << " =";
  to_src_var(w, g, inp_idx);

  return false;
}

bool Inou_pyrope::to_strconst(Out_string &w, const LGraph *g, Index_ID idx) const {

  if(g->is_graph_input(idx)) {
    w << " $" << g->get_node_wirename(idx) << " = " << g->node_const_value_get(idx);
  } else if(g->is_graph_output(idx)) {
    w << " %" << g->get_node_wirename(idx) << " = " << g->node_const_value_get(idx);
  } else {
    tmp_values[idx] = g->node_const_value_get(idx);
  }

  return false;
}

bool Inou_pyrope::to_op(Out_string &s, Out_string &sub, const LGraph *g, Index_ID idx) const {
  const auto op = g->node_type_get(idx);
  bool       dest;
  switch(op.op) {
  case GraphIO_Op:
    // dest = to_graphio(s, g, idx);
    break;
  case And_Op:
    dest = to_logical2(s, g, idx, "&", "and");
    break;
  case Or_Op:
    dest = to_logical2(s, g, idx, "|", "or");
    break;
  case Xor_Op:
    dest = to_logical2(s, g, idx, "^", "xor");
    break;
  case LessThan_Op:
    dest = to_compare(s, g, idx, "<");
    break;
  case GreaterThan_Op:
    dest = to_compare(s, g, idx, ">");
    break;
  case LessEqualThan_Op:
    dest = to_compare(s, g, idx, "<=");
    break;
  case GreaterEqualThan_Op:
    dest = to_compare(s, g, idx, ">=");
    break;
  case Equals_Op:
    dest = to_compare(s, g, idx, "==");
    break;
  case Not_Op:
    dest = to_logical1(s, g, idx, "~", "not");
    break;
  case ShiftLeft_Op:
    dest = to_shift(s, g, idx, "<<");
    break;
  case ShiftRight_Op:
    dest = to_shift(s, g, idx, ">>");
    break;
  case Mux_Op:
    dest = to_mux(s, g, idx);
    break;
  case Sum_Op:
    dest = to_sum(s, g, idx);
    break;
  case Join_Op:
    dest = to_join(s, g, idx);
    break;
  case SFlop_Op:
    dest = to_flop(s, g, idx);
    break;
  case SubGraph_Op:
    dest = to_subgraph(sub, s, g, idx);
    break;
  case U32Const_Op:
    dest = to_const(s, g, idx);
    break;
  case Pick_Op:
    dest = to_pick(s, g, idx);
    break;
  case Latch_Op:
    dest = to_latch(s, g, idx);
    break;
  case StrConst_Op:
    dest = to_strconst(s, g, idx);
    break;
  default:
    dest = false;
    s << "# FIXME idx" << idx << " " << op.get_name() << " op:" << op.op;
    tmp_values[idx] = "0";
  }

  return dest;
}

bool Inou_pyrope::to_subgraph(Out_string &w, Out_string &out, const LGraph *g, Index_ID idx) const {

  assert(false); // code being deprecased (cgen replaces pyrope code gen)

#if 0
  std::vector<LGraph *> lgs;

  // FIXME: const std::string subgraph_name = g->get_library().get_name(g->subgraph_id_get(idx));
  const std::string subgraph_name = "inner";

  const auto &source = g->get_library().get_source(g->get_name());
  lgs.push_back(LGraph::create(g->get_path(), subgraph_name, source));

  const char **subgraph_input_names;
  const char **subgraph_output_names;
  int **       subgraph_output_ids;
  int          num_outputs = 0;

  for(const auto &sg : lgs) {
    std::string prp_file;

    size_t pos = sg->get_name().find("lgraph_");
    if(pos != std::string::npos)
      prp_file = sg->get_name().substr(pos + 7);
    else
      prp_file = sg->get_name();

    w << subgraph_name << " = :(";

    int iter = 0;
    for(const auto &c : sg->inp_edges(idx)) {
      w << " ";
      to_src_var(w, sg, c.get_idx());
      iter++;
    }
    subgraph_input_names = new const char *[iter];

    iter = 0;
    for(const auto &c : sg->inp_edges(idx)) {
      subgraph_input_names[iter] = sg->get_node_wirename(c.get_idx());
      iter++;
    }

    sg->each_output([sg, &w, &num_outputs](Index_ID idx) {
      w << " %" << sg->get_node_wirename(idx);
      num_outputs++;
    });
    subgraph_output_names = new const char *[num_outputs];
    subgraph_output_ids   = new int *[num_outputs];
    for(int i = 0; i < num_outputs; i++) {
      subgraph_output_ids[i] = new int[2];
    }

    iter = 0;
    sg->each_output([sg, &subgraph_output_ids, &subgraph_output_names, &iter](Index_ID idx, Port_ID pid) {
      subgraph_output_names[iter]  = sg->get_node_wirename(idx);
      subgraph_output_ids[iter][0] = (int)idx;
      subgraph_output_ids[iter][1] = (int)pid;
      iter++;
    });

    w << "):{\n";

    for(auto idx : sg->forward()) {
      assert(sg->is_root(idx));
      Out_string s;

      bool dest   = to_op(s, s, sg, idx);
      bool is_tmp = false; // HC_A

      if(dest)
        is_tmp = to_dst_var(w, sg, idx);

      if(is_tmp)
        tmp_values[idx] = "(" + s.str() + ")";
      else {
        w << s.str();
        w << "\n";
      }
    }
  }
  w << "}\n";

  const auto instancename = g->get_node_instancename(idx);

  out << " " << instancename << " = " << subgraph_name;

  int iter = 0;
  for(const auto &c : g->inp_edges(idx)) {
    out << " " << subgraph_input_names[iter] << ":$";
    out << g->get_node_wirename(c.get_idx());
    iter++;
  }

  for(const auto &c : g->out_edges(idx)) {
    out << '\n';

    to_dst_var(out, g, c.get_idx());

    for(int i = 0; i < num_outputs; i++) {
      if(c.get_out_pin().get_pid() == subgraph_output_ids[i][1]) {
        out << instancename << '.' << subgraph_output_names[i];
        break;
      }
    }
  }

  delete[] subgraph_input_names;
  delete[] subgraph_output_names;
  for(int i = 0; i < num_outputs; i++) {
    delete[] subgraph_output_ids[i];
  }
  delete[] subgraph_output_ids;
#endif

  return false;
}

void Inou_pyrope::to_pyrope(const LGraph *g, const std::string &filename) {

  std::string prp_file;

  size_t pos = g->get_name().find("lgraph_");
  if(pos != std::string::npos)
    prp_file = g->get_name().substr(pos + 7);
  else
    prp_file = g->get_name();

  Out_string w, sub;
  w << "// " << prp_file << ".prp file from " << g->get_name() << "\n";

  inline_stmt.clear();

  g->each_input([g,this,&w](const Node_pin &pin) {
    to_graphio(w,g,pin.get_idx());
  });

  for(auto idx : g->forward()) {

    assert(g->is_root(idx));

    Out_string s;

    bool dest   = to_op(s, sub, g, idx);
    bool is_tmp = false; // I think putting false here is okay.

    if(dest)
      is_tmp = to_dst_var(w, g, idx);

    if(is_tmp) {
      if(s.str().length() != 1) // FIXME: Should instead count # of spaces, if > 0, then do this. If = 0, then no parentheses!
        tmp_values[idx] = "(" + s.str() + ")";
      else
        tmp_values[idx] = s.str();
    } else {
      w << s.str();
      w << "\n";
    }
  }

  if(filename == "-" || filename == "") {
    std::cout << sub.str() << "\n" << w.str() << std::endl;
  } else {
    std::fstream fs;
    fs.open(filename, std::ios::out | std::ios::trunc);
    if(!fs.is_open()) {
      std::cerr << "ERROR: could not open pyrope file [" << filename << "] for writing\n";
      exit(-4);
    }
    fs << sub.str() << "\n" << w.str() << std::endl;
    fs.close();
  }
}
