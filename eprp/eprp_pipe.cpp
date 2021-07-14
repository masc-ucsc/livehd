//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "eprp.hpp"
#include "eprp_pipe.hpp"

void Eprp_pipe::add_command(const Eprp_method &method, const Eprp_var &var) {
  steps.emplace_back(method, var);
}

void Eprp_pipe::run() {

  Eprp_var last_cmd_var;

  for(auto &s:steps) {

    last_cmd_var.add(s.var_fields);

    for (const auto &label : s.m.labels) {
      if (!label.second.default_value.empty() && !last_cmd_var.has_label(label.first))
        last_cmd_var.add(label.first, label.second.default_value);
    }

    std::string err_msg;
    bool        err = s.m.check_labels(last_cmd_var, err_msg);
    if (err) {
      fmt::print("error:{}\n", err_msg);
      throw std::runtime_error(std::string(err_msg));
      return;
    }

    s.m.method(last_cmd_var);
  }

}
