//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <vector>

#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_runner : public upass::uPass_struct {
public:
  uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names);

  void               run(std::size_t max_iters = 1);
  bool               has_configuration_error() const { return configuration_error; }
  const std::string& get_configuration_error() const { return configuration_error_msg; }

protected:
  struct Pass_entry {
    std::string                   name;
    std::shared_ptr<upass::uPass> pass;
  };

  std::vector<Pass_entry> upasses;
  bool                    configuration_error{false};
  std::string             configuration_error_msg;

  std::vector<std::string> resolve_order(const std::vector<std::string>& requested_names, std::string* error_msg = nullptr) const;
  std::vector<std::string> changed_passes() const;

  void process_top() override;
  void process_stmts() override;
  void process_if() override;
  void process_lnast();
};
