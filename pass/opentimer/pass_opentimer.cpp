//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <string>

#include "pass_opentimer.hpp"

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"
#include "str_tools.hpp"

static Pass_plugin sample("pass_opentimer", Pass_opentimer::setup);

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::work);
  m1.add_label_required("files", "Liberty, spef, sdc file[s] for timing");
  m1.add_label_optional("margin", "% arrival time marging (0-100)", "0");

  register_pass(m1);
}

Pass_opentimer::Pass_opentimer(const Eprp_var &var) : Pass("pass.opentimer", var) {
  auto n_lib_read = 0;

  for (const auto f : absl::StrSplit(files, ',')) {
    if (str_tools::ends_with(f, ".lib")) {
      fmt::print("opentimer using liberty file '{}'", f);
      if (n_lib_read == 0)
        timer.read_celllib(f);
      else
        timer.read_celllib(f, ot::MIN);

      n_lib_read++;
    } else if (str_tools::ends_with(f, ".spef")) {
      spef_file_list.emplace_back(f);
    } else if (str_tools::ends_with(f, ".sdc")) {
      sdc_file_list.emplace_back(f);
    } else {
      Pass::error("pass.opentimer unknown file extension '{}'", f);
    }
  }

  if (n_lib_read > 2) {
    Pass::error("pass.opentime only supports 1 or 2 liberty (max/min) files not {}", files);
  }

  if (var.has_label("margin")) {
    std::string txt{var.get("margin")};
    margin = std::stof(txt, nullptr);
    if (margin<0 || margin>100) {
      Pass::error("pass.opentimer margin must be between 0 and 100 not {}", margin);
    }
  }else{
    margin = 0;
  }
  margin_delay = 0;
}

void Pass_opentimer::read_sdc(std::string_view sdc_file) {
  std::ifstream  file(std::string{sdc_file});
  if (!file.is_open()) {
    Pass::error("pass.opentimer could not open sdc:{}", sdc_file);
    return;
  }

  std::string line;

  while (std::getline(file, line)) {
    std::vector<std::string> line_vec = absl::StrSplit(line,' ', absl::SkipWhitespace());

    if (line_vec[0] == "create_clock") {
      float       period = 1000;
      std::string pname  = "clock";
      for (std::size_t i = 1; i < line_vec.size(); i++) {
        if (line_vec[i] == "-period") {
          period = std::stof(line_vec[++i], nullptr);
        } else if (line_vec[i] == "-name") {
          pname = line_vec[++i];
        }
      }
      timer.create_clock(pname, period);

    } else if (line_vec[0] == "set_input_delay") {
      std::string pname;
      float       delay = std::stof(line_vec[1], nullptr);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
        }
      }
      if (pname.empty()) {
        Pass::error("SDC file {} set_input_delay only supports [get_ports XX] syntax not {}", sdc_file, line);
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_at(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_at(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max") {
        timer.set_at(pname, ot::MAX, ot::FALL, delay);
        timer.set_at(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-min") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
        timer.set_at(pname, ot::MIN, ot::RISE, delay);
      } else {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
        timer.set_at(pname, ot::MIN, ot::RISE, delay);
        timer.set_at(pname, ot::MAX, ot::FALL, delay);
        timer.set_at(pname, ot::MAX, ot::RISE, delay);
      }
    } else if (line_vec[0] == "set_input_transition") {
      std::string pname;
      float       delay = std::stof(line_vec[1], nullptr);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
        }
      }
      if (pname.empty()) {
        Pass::error("SDC file {} set_input_transition only supports [get_ports XX] syntax not {}", sdc_file, line);
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_slew(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_slew(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max") {
        timer.set_slew(pname, ot::MAX, ot::FALL, delay);
        timer.set_slew(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
        timer.set_slew(pname, ot::MIN, ot::RISE, delay);
      }else{
        timer.set_slew(pname, ot::MAX, ot::FALL, delay);
        timer.set_slew(pname, ot::MAX, ot::RISE, delay);
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
        timer.set_slew(pname, ot::MIN, ot::RISE, delay);
      }

    } else if (line_vec[0] == "set_output_delay") {
      std::string pname;
      float       delay = std::stof(line_vec[1], nullptr);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
        }
      }
      if (pname.empty()) {
        Pass::error("SDC file {} set_output_delay only supports [get_ports XX] syntax not {}", sdc_file, line);
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_rat(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_rat(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max") {
        timer.set_rat(pname, ot::MAX, ot::FALL, delay);
        timer.set_rat(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-min") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
        timer.set_rat(pname, ot::MIN, ot::RISE, delay);
      }else{
        timer.set_rat(pname, ot::MAX, ot::FALL, delay);
        timer.set_rat(pname, ot::MAX, ot::RISE, delay);
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
        timer.set_rat(pname, ot::MIN, ot::RISE, delay);
      }
    }
  }
  file.close();
}
