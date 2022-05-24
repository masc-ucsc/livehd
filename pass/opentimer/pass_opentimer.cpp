//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

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
      fmt::print("opentimer using spef file '{}'", f);
      timer.read_spef(f);
    } else if (str_tools::ends_with(f, ".sdc")) {
      fmt::print("opentimer using sdc file '{}'", f);
      read_sdc(f);
    } else {
      Pass::error("pass.opentimer unknown file extension '{}'", f);
    }
  }

  if (n_lib_read > 2) {
    Pass::error("pass.opentime only supports 1 or 2 liberty (max/min) files not {}", files);
  }
}

void Pass_opentimer::read_sdc(std::string_view sdc_file) {
  std::vector<std::string> line_vec;
  std::ifstream            file(sdc_file);

  if (!file.is_open()) {
    Pass::error("pass.opentimer could not open sdc:{}", sdc_file);
    return;
  }

  std::string line;

  while (getline(file, line)) {
    std::stringstream datastream(line);
    std::copy(std::istream_iterator<std::string>(datastream), std::istream_iterator<std::string>(), std::back_inserter(line_vec));

    if (line_vec[0] == "create_clock") {
      int         period = 1000;
      std::string pname;
      for (std::size_t i = 1; i < line_vec.size(); i++) {
        if (line_vec[i] == "-period") {
          period = stoi(line_vec[++i]);
          continue;
        } else if (line_vec[i] == "-name") {
          pname = line_vec[++i];
          continue;
        }
      }
      timer.create_clock(pname, period);
    } else if (line_vec[0] == "set_input_delay") {
      std::string pname;
      int         delay = stoi(line_vec[1]);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
          continue;
        }
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_at(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_at(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
      }
    } else if (line_vec[0] == "set_input_transition") {
      std::string pname;
      int         delay = stoi(line_vec[1]);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
          continue;
        }
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_slew(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_slew(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
      }
    } else if (line_vec[0] == "set_output_delay") {
      std::string pname;
      int         delay = stoi(line_vec[1]);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
          continue;
        }
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_rat(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_rat(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
      }
    }
    line_vec.clear();
  }
  file.close();
}
