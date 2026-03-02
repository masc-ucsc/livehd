//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "upass_lgraph_core.hpp"
#include "lgraph_manager.hpp"

class uPass_runner_lgraph {
public:
  uPass_runner_lgraph(std::shared_ptr<upass::Lgraph_manager> _gm, const std::vector<std::string> &upass_names = {}, bool _dry_run = false);

  void                      run(std::size_t max_iters = 1);
  bool                      has_configuration_error() const { return configuration_error; }
  const std::string        &get_configuration_error() const { return configuration_error_msg; }
  std::size_t               visit_fast(bool visit_sub = false) const;
  std::vector<std::string>  collect_type_names(bool visit_sub = false) const;
  std::size_t               get_last_visited_count() const { return last_visited_count; }
  upass::Lgraph_manager::Fold_scan_summary get_last_scan_summary() const { return last_scan_summary; }

private:
  struct Pass_entry {
    std::string                        name;
    std::shared_ptr<upass::uPass_lgraph> pass;
  };

  std::vector<Pass_entry>               upasses;
  std::shared_ptr<upass::Lgraph_manager> gm;
  bool                                   dry_run{false};
  bool                                   configuration_error{false};
  std::string                            configuration_error_msg;
  std::size_t                            last_visited_count{0};
  upass::Lgraph_manager::Fold_scan_summary last_scan_summary{};

protected:
  std::vector<std::string> resolve_order(const std::vector<std::string> &requested_names, std::string *error_msg = nullptr) const;
  std::vector<std::string> changed_passes() const;
  void                     execute_passes();
};
