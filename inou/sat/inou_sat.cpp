//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inou_sat.hpp"
#include "core/lglog.hpp"

Sat_pass_pack::Sat_pass_pack(int argc, const char **argv) {
  std::cout << " Entering.....setup" << std::endl;
  Options::setup(argc, argv);
  std::cout << " Exting ..setup ...entering...options_add" << std::endl;
  Options::get_desc()->add_options()("sat_lgdb,o", boost::program_options::value(&sat_lgdb)->required(),
                                     "lgdb path of the original netlist");
  // std::cout<<" Exting ........add_options"<<std::endl;
  boost::program_options::variables_map vm;
  boost::program_options::store(boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv())
                                    .options(*Options::get_desc())
                                    .allow_unregistered()
                                    .run(),
                                vm);
  // std::cout<<" before help......"<<std::endl;
  /*if(vm.count("help"))
    return;*/
  std::cout << " after help......" << std::endl;
  if(vm.count("sat_lgdb")) {
    std::cout << " sat......" << std::endl;
    sat_lgdb = vm["sat_lgdb"].as<std::string>();
    std::cout << " after sat......" << std::endl;
  } else {
    Pass::error("sat_lgdb is required");
    return;
  }
  std::cout << " before lock....." << std::endl;
  Options::setup_lock();
  std::cout << " after lock....." << std::endl;
  std::cout << " Exiting from ...pass pack successfully............" << std::endl;
}
