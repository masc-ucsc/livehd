//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include "invariant_options.hpp"
#include "core/lglog.hpp"

Invariant_find_pack::Invariant_find_pack() {

  assert(Options::get_cargc() != 0); // Options::setup(argc,argv) must be called before setup() is called
  Options::get_desc()->add_options()("elab_lgdb", boost::program_options::value(&elab_lgdb)->required(), "lgdb path for the elaborated netlist")("synth_lgdb", boost::program_options::value(&synth_lgdb)->required(), "lgdb path for the synthesized netlist")("invariant_file,o", boost::program_options::value(&invariant_file), "output file for invariant boundaries (default=invariant)")("top", boost::program_options::value(&top)->required(), "top module name (must match elab and synth)")("help,h", "print usage message");

  boost::program_options::variables_map vm;
  boost::program_options::store(boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(*Options::get_desc()).allow_unregistered().run(), vm);

  if(vm.count("help"))
    return;

  if(vm.count("top")) {
    top = vm["top"].as<std::string>();
  } else {
    console->error("top is required\n");
    exit(-1);
  }
  if(vm.count("elab_lgdb")) {
    elab_lgdb = vm["elab_lgdb"].as<std::string>();
  } else {
    console->error("elab_lgdb is required\n");
    exit(-1);
  }
  if(vm.count("synth_lgdb")) {
    synth_lgdb = vm["synth_lgdb"].as<std::string>();
  } else {
    console->error("synth_lgdb is required\n");
    exit(-1);
  }
  if(vm.count("invariant_file")) {
    invariant_file = vm["invariant_file"].as<std::string>();
  } else {
    invariant_file = "invariant";
  }
}
