

#include "invariant_options.hpp"
#include "core/lglog.hpp"

Invariant_find_pack::Invariant_find_pack() {

  assert(Options::get_cargc() != 0); // Options::setup(argc,argv) must be called before setup() is called
  Options::get_desc()->add_options()
      ("elab_lgdb", boost::program_options::value(&elab_lgdb)->required(), "lgdb path for the elaborated netlist")
      ("synth_lgdb", boost::program_options::value(&synth_lgdb)->required(), "lgdb path for the synthesized netlist")
      ("invariant_file,o", boost::program_options::value(&invariant_file), "output file for invariant boundaries (default=invariant)")
      ("hier_sep,s", boost::program_options::value(&hierarchical_separator), "string (or character) used by synthesis tool to delimit hierarchy (default='.')")
      ("top", boost::program_options::value(&top)->required(), "top module name (must match elab and synth)")
      ("clusters,c", boost::program_options::value(&clusters), "cluster invariant regions into n partitions (optional)")
      ("cluster_dir", boost::program_options::value(&cluster_dir), "directory to output cluster netlists (required if clusters is set, ignored otherwise)")
      ("help,h", "print usage message");

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
  if(vm.count("hier_sep")) {
    hierarchical_separator = vm["hier_sep"].as<std::string>();
  } else {
    hierarchical_separator = '.';
  }
  if(vm.count("clusters")) {
    clusters   = vm["clusters"].as<int>();
    do_cluster = true;
  } else {
    do_cluster = false;
  }
  if(vm.count("cluster_dir")) {
    if(do_cluster) {
      cluster_dir = vm["cluster_dir"].as<std::string>();
    } else {
      console->warn("A cluster_dir was provided, but clusters was not, ignoring.\n");
    }
  } else {
    if(do_cluster) {
      console->error("cluster_dir is required when clusters is provided\n");
      exit(-1);
    }
  }
}
