//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <sstream>
#include <string>
#include <strings.h>

#include "eprp_utils.hpp"
#include "inou_cgen.hpp"
#include "lgedgeiter.hpp"

void setup_inou_cgen() {
  Inou_cgen p;
  p.setup();
}

Inou_cgen::Inou_cgen()
    : Pass("cgen") {

  lg = 0;
}

void Inou_cgen::setup() {
  Eprp_method m1("inou.cgen.fromlg", "generate a cgen output", &Inou_cgen::fromlg);

  register_inou(m1);
}

void Inou_cgen::fromlg(Eprp_var &var) {

  Inou_cgen p;

  auto odir = var.get("odir");
  bool  ok  = p.setup_directory(odir);
  if(!ok)
    return;

  for(const auto &g : var.lgs) {
    const auto filename = absl::StrCat(odir,"/",g->get_name(),".prp");

    p.to_pyrope(g, filename);
  }
}

void Inou_cgen::Declaration::format_raw(Out_string &w) const {
  static std::string_view str_type[] = {"local", "input", "output", "sflop", "aflop", "fflop", "latch"};

  w << fmt::format("dec {} bits:{} pos:{} sign:{} order:{} type:{}\n", name, bits, pos, is_signed, order, str_type[type]);
}

void Inou_cgen::iterate_declarations(const Node_pin &pin) {

  auto wn = lg->get_node_wirename(pin);
  if(wn.empty())
    return;

  Declaration d;
  d.name      = wn;
  d.pos       = lg->node_file_loc_get(pin.get_idx()).get_start();
  d.order     = -1; // FIXME: Undefined for the moment
  d.bits      = lg->get_bits(pin);
  d.is_signed = false; // Could change after the traversal

  if(lg->is_graph_input(pin)) {
    d.type = Decl_inp;
  } else if(lg->is_graph_output(pin)) {
    d.type = Decl_out;
  } else if(pin.get_pid() == 0) { // Only pid ==0 is the Q output from flops/latches
    assert(false); // FIXME: Not sure what is this code, but an iterator over outputs should never reach this
    const auto &nt = lg->node_type_get(pin.get_idx());
    switch(nt.op) {
    case SFlop_Op: d.type = Decl_sflop; break;
    case AFlop_Op: d.type = Decl_aflop; break;
    case FFlop_Op: d.type = Decl_fflop; break;
    case Latch_Op: d.type = Decl_latch; break;
    default:       d.type = Decl_local;
    }
  } else {
    d.type = Decl_local;
  }

  declaration.push_back(d);

  auto found = wn.find('.');
  if (found == std::string_view::npos)
    return;

  auto       id = declaration.size() - 1;
  auto sub_str  = wn.substr(found);

  declaration_root.insert(std::pair(sub_str, id));
}

void Inou_cgen::setup_declarations() {

  declaration.clear();
  declaration_root.clear();

  lg->each_output(&Inou_cgen::iterate_declarations, this);
}

void Inou_cgen::to_pyrope(const LGraph *g, std::string_view filename) {

  assert(lg == 0);
  lg = g;

  setup_declarations();
  Out_string w;

  w << fmt::format("// {} \n", filename);

  for(const auto &d : declaration) {
    d.format_raw(w);
  }

  for(auto idx : lg->forward()) {
    auto wn = lg->get_node_wirename(idx);
    if(wn.empty())
      w << fmt::format(" {} op:{} \n", idx, lg->node_type_get(idx).get_name());
    else
      w << fmt::format(" {} {} op:{}\n", idx, wn, lg->node_type_get(idx).get_name());
  }

  std::fstream fs;
  fs.open(std::string(filename), std::ios::out | std::ios::trunc);
  if(!fs.is_open()) {
    error(fmt::format("could not open destination pyrope file {}", filename));
    exit(-4);
  }
  fs << w.str() << std::endl;
  fs.close();

  std::cout << w.str() << std::endl;

  lg = 0; // just being clean
}
