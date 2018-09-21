//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef TECH_OPTIONS_H
#define TECH_OPTIONS_H

#include "lglog.hpp"
#include "options.hpp"
#include <boost/filesystem.hpp>

typedef enum {
  Verilog,
  LEF,
  Liberty
} Tech_file_type;

class Tech_options_pack : public Options_base {
public:
  std::string    file_path;
  std::string    tech_type;
  Tech_file_type type;

  Tech_options_pack() {

    Options::get_desc()->add_options()("tech_file,f", boost::program_options::value(&file_path), "tech library file <filename> to be loaded")("tech_type,t", boost::program_options::value(&tech_type), "tech library format (Verilog|LEF|LIB) of the file");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(*Options::get_desc()).allow_unregistered().run(), vm);

    if(vm.count("tech_file")) {
      file_path = vm["tech_file"].as<std::string>();
    } else {
      console->error("tech_file is required\n");
    }

    if(vm.count("tech_type")) {
      std::string stype = vm["tech_type"].as<std::string>();

      if(stype == "v" || stype == "Verilog") {
        type = Tech_file_type::Verilog;
      } else if(stype == "lib" || stype == "Liberty" || stype == "LIB") {
        type = Tech_file_type::Liberty;
      } else if(stype == "lef" || stype == "LEF") {
        type = Tech_file_type::LEF;
      } else {
        console->error("The file type specified {} is not supported.\n", vm["tech_type"].as<std::string>());
      }
    } else {
      std::string extension = boost::filesystem::extension(file_path);

      if(extension == ".v" || extension == ".sv") {
        type = Tech_file_type::Verilog;
        console->info("A tech file type was not specified, from the file extension, assuming it is Verilog\n");
      } else if(extension == ".lib") {
        type = Tech_file_type::Liberty;
        console->info("A tech file type was not specified, from the file extension, assuming it is LIberty\n");
      } else if(extension == ".lef") {
        type = Tech_file_type::LEF;
        console->info("A tech file type was not specified, from the file extension, assuming it is LEF\n");
      } else {
        console->error("A tech file type was not specified and it was not possible to infer a type from the file extension\n");
      }
    }
  }
};

#endif
