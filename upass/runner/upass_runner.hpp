//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <iostream>
#include <memory>

#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_runner : public upass::uPass_struct {
public:
  uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string> upass_names) : uPass_struct(_lm) {
    auto upass_registry = upass::uPass_plugin::get_registry();
    for (const auto& name : upass_names) {
      if (upass_registry.count(name)) {
        std::print("uPass - add {}\n", name);
        upasses.push_back(upass_registry[name](_lm));
      } else {
        std::print("{} is not defined.\n", name);
      }
    }
  }

  void run() { process_lnast(); }

protected:
  std::vector<std::shared_ptr<upass::uPass>> upasses;

  void process_top() override;
  void process_stmts() override;
  void process_lnast();
};
