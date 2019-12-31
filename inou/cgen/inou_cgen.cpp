//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"

#include <strings.h>

#include <fstream>
#include <sstream>
#include <string>

#include "eprp_utils.hpp"
#include "lgedgeiter.hpp"
#include "lnast_parser.hpp"
#include "lnast_to_cfg_parser.hpp"
#include "lnast_to_cpp_parser.hpp"
#include "lnast_to_prp_parser.hpp"
#include "lnast_to_verilog_parser.hpp"

void setup_inou_cgen() { Inou_cgen::setup(); }

Inou_cgen::Inou_cgen(const Eprp_var &var) : Pass("inou_cgen", var) { lg = 0; }

void Inou_cgen::setup() {
  Eprp_method m2("inou.cgen.cfg.fromlnast", "parse cfg_test -> build lnast -> generate cfg_text", &Inou_cgen::to_cfg);
  m2.add_label_required("files", "cfg_text files to process (comma separated)");
  m2.add_label_optional("odir", "path to put the cfg[s]", ".");

  register_inou("cgen", m2);

  Eprp_method m3("inou.cgen.verilog.fromlnast", "parse cfg_test -> build lnast -> generate verilog", &Inou_cgen::to_verilog);
  m3.add_label_required("files", "cfg_text files to process (comma separated)");
  m3.add_label_optional("odir", "path to put the verilog[s]", ".");

  register_inou("cgen", m3);

  Eprp_method m4("inou.cgen.prp.fromlnast", "parse cfg_test -> build lnast -> generate pyrope", &Inou_cgen::to_prp);
  m4.add_label_required("files", "cfg_text files to process (comma separated)");
  m4.add_label_optional("odir", "path to put the pyrope[s]", ".");

  register_inou("cgen", m4);

  Eprp_method m5("inou.cgen.cpp.fromlnast", "parse cfg_text -> build lnast -> generate cpp", &Inou_cgen::to_cpp);
  m5.add_label_required("files", "cfg_text files to process (comma separated)");
  m5.add_label_optional("odir", "path to put the cpp[s}", ".");

  register_inou("cgen", m5);
}

void Inou_cgen::to_xxx(Cgen_type cgen_type) {
  for (const auto &f : absl::StrSplit(files, ',')) {
    Lnast_parser lnast_parser(f);

    auto *lnast = lnast_parser.ref_lnast();
    // needed??? lnast->ssa_trans();

    std::unique_ptr<Lnast_to_xxx> lnast_to;

    if (cgen_type == Cgen_type::Type_verilog) {
      lnast_to = std::make_unique<Lnast_to_verilog_parser>(lnast, path);
    } else if (cgen_type == Cgen_type::Type_prp) {
      lnast_to = std::make_unique<Lnast_to_prp_parser>(lnast, path);
    } else if (cgen_type == Cgen_type::Type_cfg) {
      lnast_to = std::make_unique<Lnast_to_cfg_parser>(lnast, path);
    } else if (cgen_type == Cgen_type::Type_cpp) {
      lnast_to = std::make_unique<Lnast_to_cpp_parser>(lnast, path);
    } else {
      I(false);  // Invalid
      lnast_to = std::make_unique<Lnast_to_prp_parser>(lnast, path);
    }
    lnast_to->generate();
  }
}

void Inou_cgen::to_verilog(Eprp_var &var) {
  Inou_cgen p(var);
  p.to_xxx(Cgen_type::Type_verilog);
}

void Inou_cgen::to_prp(Eprp_var &var) {
  Inou_cgen p(var);
  p.to_xxx(Cgen_type::Type_prp);
}

void Inou_cgen::to_cfg(Eprp_var &var) {
  Inou_cgen p(var);
  p.to_xxx(Cgen_type::Type_cfg);
}

void Inou_cgen::to_cpp(Eprp_var &var) {
  Inou_cgen p(var);
  p.to_xxx(Cgen_type::Type_cpp);
}

void Inou_cgen::Declaration::format_raw(std::ostringstream &w) const {
  constexpr std::string_view str_type[] = {"local", "input", "output", "sflop", "aflop", "fflop", "latch"};

  w << fmt::format("dec {} bits:{} pos:{} sign:{} order:{} type:{}\n", name, bits, pos, is_signed, order,
                   str_type[static_cast<int>(type)]);
}

void Inou_cgen::iterate_declarations(Node_pin &pin) {
  (void)pin;
  I(false);
  /*
  auto wn = pin.get_name();
  if(wn.empty())
    return;

  Declaration d;
  d.name      = wn;
  d.pos       = -1; // SH:FIXME: Undefined for this moment
  d.order     = -1; // FIXME: Undefined for the moment
  d.bits      = pin.get_bits();
  d.is_signed = false; // Could change after the traversal

  if(pin.is_graph_input()){
    I(false); //SH:FIXME: Impossible for this case as declarations only consider graph output pins
    d.type = Decl_inp;
  } else if(pin.is_graph_output()) {
    d.type = Decl_out;
  } else if(pin.get_pid() == 0) { // Only pid ==0 is the Q output from flops/latches
    I(false); // FIXME: Not sure what is this code, but an iterator over outputs should never reach this
    const auto &nt = pin.get_node().get_type();
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

  declarations.push_back(d);

  auto found = wn.find('.');
  if (found == std::string_view::npos)
    return;

  auto       id = declarations.size() - 1;
  auto sub_str  = wn.substr(found);

  declaration_root.insert(std::pair(sub_str, id));
   */
}

void Inou_cgen::setup_declarations() {
  declarations.clear();
  declaration_root.clear();

  lg->each_graph_output([this](const Node_pin &pin) {
    auto wn = pin.get_name();
    if (wn.empty()) return;

    Declaration d;
    d.name      = wn;
    d.pos       = -1;  // SH:FIXME: Undefined for this moment
    d.order     = -1;  // FIXME: Undefined for the moment
    d.bits      = pin.get_bits();
    d.is_signed = false;  // Could change after the traversal

    if (pin.is_graph_input()) {
      I(false);  // SH:FIXME: Impossible for this case as declarations only consider graph output pins
      d.type = Declaration_type::Decl_inp;
    } else if (pin.is_graph_output()) {
      d.type = Declaration_type::Decl_out;
    } else if (pin.get_pid() == 0) {  // Only pid ==0 is the Q output from flops/latches
      I(false);                       // FIXME: Not sure what is this code, but an iterator over outputs should never reach this
      const auto &nt = pin.get_node().get_type();
      switch (nt.op) {
        case SFlop_Op: d.type = Declaration_type::Decl_sflop; break;
        case AFlop_Op: d.type = Declaration_type::Decl_aflop; break;
        case FFlop_Op: d.type = Declaration_type::Decl_fflop; break;
        case Latch_Op: d.type = Declaration_type::Decl_latch; break;
        default: d.type = Declaration_type::Decl_local;
      }
    } else {
      d.type = Declaration_type::Decl_local;
    }

    declarations.push_back(d);

    auto found = wn.find('.');
    if (found == std::string_view::npos) return;

    auto id      = declarations.size() - 1;
    auto sub_str = wn.substr(found);

    declaration_root.insert(std::pair(sub_str, id));
  });
}

void Inou_cgen::generate_prp(LGraph *g, std::string_view filename) {
  assert(lg == nullptr);
  lg = g;

  setup_declarations();
  std::ostringstream w;

  w << fmt::format("// {} \n", filename);

  for (const auto &d : declarations) {
    d.format_raw(w);
  }

  for (auto node : lg->forward()) {
    auto wn = node.get_name();
    if (wn.empty())
      w << fmt::format(" {} op:{} \n", node.debug_name(), node.get_type().get_name());
    else
      w << fmt::format(" {} {} op:{}\n", node.debug_name(), wn, node.get_type().get_name());
  }

  std::fstream fs;
  fs.open(std::string(filename), std::ios::out | std::ios::trunc);
  if (!fs.is_open()) {
    error(fmt::format("could not open destination pyrope file {}", filename));
    exit(-4);
  }
  fs << w.str() << std::endl;
  fs.close();

  std::cout << w.str() << std::endl;

  lg = nullptr;  // just being clean
}
