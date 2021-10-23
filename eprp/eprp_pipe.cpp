//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "eprp.hpp"
#include "eprp_pipe.hpp"
#include "lbench.hpp"

void Eprp_pipe::add_command(const Eprp_method &method, const Eprp_var &var) {
  steps.emplace_back(method, var);
}

void Pipe_step::run(Eprp_var &last_cmd_var) {
  last_cmd_var.add(var_fields);

  for (const auto &label : m.labels) {
    if (!label.second.default_value.empty() && !last_cmd_var.has_label(label.first))
      last_cmd_var.add(label.first, label.second.default_value);
  }

  auto [err, err_msg] = m.check_labels(last_cmd_var);
  if (err) {
    fmt::print("error:{}\n", err_msg);
    throw std::runtime_error(err_msg.to_s());
    return;
  }

  m.method(last_cmd_var);
  if (next_step) { // FIXME: instead of calling here. Call inside the method, then we can parallize
    next_step->run(last_cmd_var);
  }
}

void Eprp_pipe::run() {

  if (steps.empty())
    return;

  for(auto i=0u;i<steps.size();++i) {
    if ((i+1)<steps.size())
      steps[i].next_step = &steps[i+1];
    else
      assert(steps[i].next_step == nullptr);
  }

  Eprp_var last_cmd_var;
  steps[0].run(last_cmd_var);
}
