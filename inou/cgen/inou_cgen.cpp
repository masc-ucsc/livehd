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

  Eprp_method m2("inou.cgen.cfg.fromlnast", "parse cfg_test -> build lnast -> generate cfg_text", &Inou_cgen::tocfg);
  m2.add_label_required("files", "cfg_text files to process (comma separated)");
  m2.add_label_optional("path", "path to put the cfg[s]", "lgdb");

  register_inou(m2);

  Eprp_method m3("inou.cgen.verilog.fromlnast", "parse cfg_test -> build lnast -> generate verilog", &Inou_cgen::toverilog);
  m3.add_label_required("files", "cfg_text files to process (comma separated)");
  m3.add_label_optional("path", "path to put the verilog[s]", "lgdb");

  register_inou(m3);

  Eprp_method m4("inou.cgen.pyrope.fromlnast", "parse cfg_test -> build lnast -> generator pyrope", &Inou_cgen::topyrope);
  m4.add_label_required("files", "cfg_text files to process (comma separated)");
  m4.add_label_optional("path", "path to put the pyrope[s]", "lgdb");

  register_inou(m4);
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

void Inou_cgen::tocfg(Eprp_var &var) {
  Inou_cgen p;

  p.opack.files = var.get("files");
  p.opack.path = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.cgen.tocfg needs an input cfg_text!"));
    I(false);
    return;
  }

  // cfg_text to lnast
  p.memblock = p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock, p.token_list);

  p.lnast = p.lnast_parser.get_ast().get();
  p.lnast->ssa_trans();

  // lnast to cfg_text
  p.lnast_to_cfg_parser = new Lnast_to_cfg_parser(p.memblock, p.lnast);
  fmt::print("{}\n", p.lnast_to_cfg_parser->stringify());
}

void Inou_cgen::toverilog(Eprp_var &var) {
  Inou_cgen p;

  p.opack.files = var.get("files");
  p.opack.path = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.cgen.toverilog needs an input cfg_text!"));
    I(false);
    return;
  }

  // cfg text to lnast
  p.memblock = p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock, p.token_list);

  p.lnast = p.lnast_parser.get_ast().get();
  p.lnast->ssa_trans();

  // lnast to verilog
  p.lnast_to_verilog_parser = new Lnast_to_verilog_parser(p.memblock, p.lnast);
  fmt::print("{}\n", p.lnast_to_verilog_parser->stringify());
}

void Inou_cgen::topyrope(Eprp_var &var) {
  Inou_cgen p;

  p.opack.files = var.get("files");
  p.opack.path = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.cgen.topyrope needs an input cfg_text!"));
    I(false);
    return;
  }

  // cfg text to lnast
  p.memblock = p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock, p.token_list);

  p.lnast = p.lnast_parser.get_ast().get();
  p.lnast->ssa_trans();

  // lnast to pyrope
  p.lnast_to_pyrope_parser = new Lnast_to_pyrope_parser(p.memblock, p.lnast);
  fmt::print("{}\n", p.lnast_to_pyrope_parser->stringify());
}

std::string_view Inou_cgen::setup_memblock() {
  auto file_path = opack.files;
  int fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "error, could not open %s\n", file_path.c_str());
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);
  printf("Size: %lu\n", (uint64_t)sb.st_size);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  fprintf(stderr, "Content of memblock: \n%s\n", memblock);
  if (memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }
  return memblock;
}

void Inou_cgen::Declaration::format_raw(Out_string &w) const {
  static std::string_view str_type[] = {"local", "input", "output", "sflop", "aflop", "fflop", "latch"};

  w << fmt::format("dec {} bits:{} pos:{} sign:{} order:{} type:{}\n", name, bits, pos, is_signed, order, str_type[type]);
}

void Inou_cgen::iterate_declarations(Node_pin &pin) {
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
  });
}

void Inou_cgen::to_pyrope(LGraph *g, std::string_view filename) {

  assert(lg == nullptr);
  lg = g;

  setup_declarations();
  Out_string w;

  w << fmt::format("// {} \n", filename);

  for(const auto &d : declarations) {
    d.format_raw(w);
  }

  for(auto node : lg->forward()) {
    auto wn = node.get_name();
    if(wn.empty())
      w << fmt::format(" {} op:{} \n", node.debug_name(), node.get_type().get_name());
    else
      w << fmt::format(" {} {} op:{}\n", node.debug_name(), wn, node.get_type().get_name());
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

  lg = nullptr; // just being clean
}
