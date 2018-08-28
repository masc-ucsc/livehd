//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <sstream>
#include <string>
#include <strings.h>

#include "inou_pyrope.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void Inou_pyrope_options::set(const py::dict &dict) {
  for (auto item : dict) {
    const auto &key = item.first.cast<std::string>();
    try {
      // No custom option for the moment
      set_val(key,item.second);
    } catch (const std::invalid_argument& ia) {
      fmt::print("ERROR: key {} has an invalid argument {}\n",key);
    }
  }
}

Inou_pyrope::Inou_pyrope() {
}

std::vector<LGraph *> Inou_pyrope::generate() {

  std::vector<LGraph *> lgs;

  if(opack.graph_name != "") {
    lgs.push_back(new LGraph(opack.lgdb_path, opack.graph_name, false)); // Do not clear
  } else {
    assert(false); // Still not implemented
  }

  return lgs;
}

void Inou_pyrope::generate(std::vector<const LGraph *> &out) {
  for(const auto &g : out) {
    to_pyrope(g, opack.pyrope_output);
  }
}

void Inou_pyrope::to_dst_var(Out_string &w, const LGraph *g, Index_ID idx) const {
  Index_ID direct_to_register = 0;
  for(const auto &c : g->out_edges(idx)) {
    const auto op = g->node_type_get(c.get_idx());
    if(op.op == Flop_Op) {
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
    w << " %"
      << g->get_graph_output_name(direct_to_output)
      << " = ";
  } else if(direct_to_register) {
    w << " @tmp"
      << direct_to_register
      << " = ";
  } else {
    w << " tmp"
      << idx
      << " = ";
  }
}

void Inou_pyrope::to_src_var(Out_string &w, const LGraph *g, Index_ID idx) const {
  if(idx == 0) {
    w << " FIXME" << idx;
    return;
  }
  if(g->is_graph_input(idx)) {
    w << " $" << g->get_graph_input_name(idx);
    return;
  }

  Index_ID   direct_to_register = 0;
  int        const_val          = 0;
  const auto op                 = g->node_type_get(idx);
  if(op.op == Flop_Op) {
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
    w << " %" << g->get_graph_output_name(direct_to_output);
  } else if(direct_to_register) {
    w << " @tmp" << direct_to_register;
  } else if(inline_stmt.find(idx) != inline_stmt.end()) {
    w << " (" << inline_stmt.at(idx) << ")";
  } else if(const_val) {
    w << " " << const_val;
  } else {
    w << " tmp" << idx;
  }
}

void Inou_pyrope::to_normal_var(Out_string &w, const LGraph *g, Index_ID idx) const {
  if(idx == 0) {
    w << " FIXME" << idx;
    return;
  }
  if(g->is_graph_input(idx)) {
    w << " " << g->get_graph_input_name(idx);
    return;
  }
  if(g->is_graph_output(idx)) {
    w << " " << g->get_graph_output_name(idx);
    return;
  }
}

bool Inou_pyrope::to_mux(Out_string &w, const LGraph *g, Index_ID idx) const {

  Index_ID t_idx = 0;
  Index_ID f_idx = 0;
  Index_ID c_idx = 0;

  // WARNING: input edges dst_pid can go anywhere, must search reverse edge to see input
  for(const auto &c : g->inp_edges(idx)) {
    switch(c.get_inp_pin().get_pid()) { // FIXME: Either c.get_inp_pid() or c.get_out_pid()??
    case 0:
      f_idx = c.get_idx();
      break;
    case 1:
      t_idx = c.get_idx();
      break;
    case 2:
      c_idx = c.get_idx();
      break;
    default:
      assert(false);
    }
  }

  Index_ID direct_to_register = 0;
  if(g->is_graph_input(c_idx)) {
    if(strcasecmp(g->get_graph_input_name(c_idx), "reset") == 0) {

      for(const auto &c : g->out_edges(idx)) {
        const auto op = g->node_type_get(c.get_idx());
        if(op.op == Flop_Op) {
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
      w << " @"
        << g->get_graph_output_name(direct_to_output)
        << " ";
    } else {
      w << " @tmp"
        << direct_to_register
        << " ";
    }
    w << " __reset:";
    to_src_var(w, g, t_idx);
    w << "\n";

    to_dst_var(w, g, idx);
    to_src_var(w, g, f_idx);
    return true;
  }

  w << " if ";
  to_src_var(w, g, c_idx);
  w << "{ \n   ";
  // ----------- TRUE
  to_dst_var(w, g, idx);
  to_src_var(w, g, t_idx);
  // ----------- FALSE
  w << "\n}else{\n   ";
  to_dst_var(w, g, idx);
  to_src_var(w, g, f_idx);
  w << "\n}\n";

  return false;
}

bool Inou_pyrope::to_join(Out_string &w, const LGraph *g, Index_ID idx) const {
  Out_string tmp;
  //const auto node = g->get_node_int(idx);
  int ninputs = 0;
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
      w << " %"
        << g->get_graph_output_name(direct_to_output)
        << " as __bits:"
        << g->get_bits(idx)
        << "\n";
    }
  } else {
    w << " @tmp"
      << idx
      << " as __bits:"
      << g->get_bits(idx)
      << "\n";
  }

  for(const auto &c : g->inp_edges(idx)) {
    const auto op = g->node_type_get(c.get_idx());
    if(op.op == Mux_Op) {
      for(const auto &c2 : g->inp_edges(c.get_idx())) {
        if(g->is_graph_input(c2.get_idx())) {
          if(strcasecmp(g->get_graph_input_name(c2.get_idx()), "reset") == 0) {
            return false;
          }
        }
      }
    }
  }

  if(direct_to_output) {
    w << " @"
      << g->get_graph_output_name(direct_to_output)
      << " =";
  } else {
    w << " @tmp"
      << idx
      << " =";
  }
  for(const auto &c : g->inp_edges(idx)) {
    if(c.get_inp_pin().get_pid() != 0)
      to_src_var(w, g, c.get_idx());
  }

  return false;
}

bool Inou_pyrope::to_graphio(Out_string &w, const LGraph *g, Index_ID idx) const {

  auto bits = g->get_bits(idx);

  if(g->is_graph_input(idx)) {
    if(bits) {
      w << " $"
        << g->get_graph_input_name(idx)
        << " as __bits:"
        << bits;
    }
  } else {
    assert(g->is_graph_output(idx));

    const auto &node = g->get_node_int(idx);
    if(!node.has_inputs())
      return false; // Not driven used output
    if(bits) {
      w << " %"
        << g->get_graph_output_name(idx)
        << " as __bits:"
        << bits;
    }
  }

  return false;
}

bool Inou_pyrope::to_logical2(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op, const char *s_op) const {

  bool first = true;
  for(const auto &c : g->inp_edges(idx)) {
    if(first) {
      first = false;
    } else {
      if(c.get_bits() == 1)
        w << " " << s_op;
      else
        w << " " << c_op;
    }
    to_src_var(w, g, c.get_idx());
  }

  return true;
}

bool Inou_pyrope::to_shift(Out_string &w, const LGraph *g, Index_ID idx, const char *c_op) const {

  // How to differentiate between ">>" and ">>>"?

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
        w << " +";
      }
      to_src_var(w, g, c.get_idx());
    }
  }

  for(const auto &c : g->inp_edges(idx)) {
    if(c.get_inp_pin().get_pid() != 0 && c.get_inp_pin().get_pid() != 1) {
      if(first) {
        first = false;
      } else {
        w << " -";
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
        w << " " << s_op;
      else
        w << " " << c_op;
    } else {
      assert(false); // Should not happen with operations like NOT
    }
    to_src_var(w, g, c.get_idx());
  }

  return true;
}

bool Inou_pyrope::to_equals(Out_string &w, const LGraph *g, Index_ID idx) const {

  bool first = true;
  for(const auto &c : g->inp_edges(idx)) {
    if(first) {
      first = false;
    } else {
      w << " ==";
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

  bool first_val = true;
  for(const auto &c : g->inp_edges(idx)) {
    const auto op = g->node_type_get(c.get_idx());
    if(op.op == U32Const_Op) {
      if(first_val) {
        w << "[[" << g->node_value_get(c.get_idx());
        first_val = false;
      } else {
        w << "..." << g->node_value_get(c.get_idx()) << "]]\n";
      }
    } else {
      to_src_var(w, g, c.get_idx());
    }
  }

  return true;
}

bool Inou_pyrope::to_op(Out_string &s, Out_string &sub, const LGraph *g, Index_ID idx, bool is_subgraph, std::vector<const char *> output_vars) const {
  const auto op = g->node_type_get(idx);
  bool dest;
  switch(op.op) {
    case GraphIO_Op:
      dest = to_graphio(s, g, idx);
      if(g->is_graph_output(idx) && is_subgraph) {
        output_vars.push_back(g->get_graph_output_name(idx));
      }
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
      dest = to_logical2(s, g, idx, "<", "<");
      break;
    case GreaterThan_Op:
      dest = to_logical2(s, g, idx, ">", ">");
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
    case Flop_Op:
      dest = to_flop(s, g, idx);
      break;
    case SubGraph_Op:
      dest = to_subgraph(sub, s, g, idx);
      break;
    case U32Const_Op:
      dest = to_const(s, g, idx);
      break;
    case Equals_Op:
      dest = to_equals(s, g, idx);
      break;
    case Pick_Op:
      dest = to_pick(s, g, idx);
      break;
    default:
      dest = false;
      s << "# FIXME idx" << idx
        << " " << op.get_name()
        << " op:" << op.op;
    }

  return dest;
}

bool Inou_pyrope::to_subgraph(Out_string &w, Out_string &out, const LGraph *g, Index_ID idx) const {

  std::vector<LGraph *> lgs;

  const std::string subgraph_name = g->get_library()->get_name(g->subgraph_id_get(idx));

  lgs.push_back(new LGraph(opack.lgdb_path, subgraph_name, false));

  std::vector<const char *> output_vars;

  for(const auto &sg : lgs) {
    std::string prp_file;

    size_t pos = sg->get_name().find("lgraph_");
    if(pos != std::string::npos)
      prp_file = sg->get_name().substr(pos + 7);
    else
      prp_file = sg->get_name();

    w << subgraph_name << " :(";

    for(const auto &c : sg->inp_edges(idx)) {
      to_normal_var(w, sg, c.get_idx());
    }

    w << "):{\n";

    for(auto &edge : sg->out_edges(idx)) {
      w << sg->get_graph_output_name(edge.get_idx());
    }

    for(auto idx : sg->forward()) {
      assert(sg->is_root(idx));
      Out_string s;

      bool dest = to_op(s, s,  sg, idx, true, output_vars);

      if(dest)
        to_dst_var(w, sg, idx);
      w << s.str();
      w << "\n";
    }
  }
  w << "}\n";

  const auto instancename = g->get_node_instancename(idx);

  out << " " << instancename << " = " << subgraph_name;

  for(const auto &c : g->inp_edges(idx)) {
    to_normal_var(out, g, c.get_idx());
  }
  /*
  for (auto &edge:g->out_edges(idx)) {
    out << g->get_graph_output_name_from_pid(edge.get_out_pin().get_pid());
  }
  for (auto &edge:g->inp_edges(idx)) {
    out << g->get_graph_input_name_from_pid(edge.get_inp_pin().get_pid());
  }*/
  /*
std::set<Port_ID> visited_out_pids;

      for(auto &edge : g->out_edges(idx)) {
        //only once per pid
        if(visited_out_pids.find(edge.get_out_pin().get_pid()) != visited_out_pids.end())
          continue;

        visited_out_pids.insert(edge.get_out_pin().get_pid());
        const char* out_name = subgraph->get_graph_output_name_from_pid(edge.get_out_pin().get_pid());
        uint16_t    out_size = subgraph->get_bits(subgraph->get_graph_output(out_name).get_nid());
*/

  // find better way of getting output vars than this method below
  std::vector<const char *>::iterator i;
  i = output_vars.begin();
  for(const auto &c : g->out_edges(idx)) {
    out << '\n';

    to_dst_var(out, g, c.get_idx());

    // FIXME? g->get_graph_output_name(idx) instead of using output_vars
    out << instancename << '.' << *i;

    i++;
  }

  return false;
}

void Inou_pyrope::to_pyrope(const LGraph *g, const std::string filename) {

  std::string prp_file;

  size_t pos = g->get_name().find("lgraph_");
  if(pos != std::string::npos)
    prp_file = g->get_name().substr(pos + 7);
  else
    prp_file = g->get_name();

  Out_string w, sub;
  w << "# " << prp_file << ".prp file from " << g->get_name() << "\n";

  inline_stmt.clear();

  // FIXME:
  //
  // Algorithm:
  //
  // 1-fast iterate, put all the input/output/register types "as ..."
  //
  // 2-Put all the logic (no flops, or outputs, or logic that uses reset/clk)
  //
  // 3-Connect the outputs and flops with tmps. If reset was there, connect correctly (no mux)
  //

  // to get subgraph names use pin ids and this is shown in dump_yosys.cpp!
  // fix pick ops!
  // change name to lg2prp everywhere!
  std::vector<const char *> output_vars; //remove this later
  for(auto idx : g->forward()) {

    assert(g->is_root(idx));

    Out_string s;

    bool dest = to_op(s, sub, g, idx, false, output_vars);

    if(dest)
      to_dst_var(w, g, idx);
    w << s.str();
    w << "\n";
  }

  if(filename == "-" || filename == "") {
    std::cout << sub.str() << "\n"
              << w.str() << std::endl;
  } else {
    std::fstream fs;
    fs.open(filename, std::ios::out | std::ios::trunc);
    if(!fs.is_open()) {
      std::cerr << "ERROR: could not open pyrope file [" << filename << "] for writing\n";
      exit(-4);
    }
    fs << sub.str() << "\n"
       << w.str() << std::endl;
    fs.close();
  }
}


