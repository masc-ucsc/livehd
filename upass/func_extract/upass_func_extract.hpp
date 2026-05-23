//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "lnast.hpp"
#include "upass_core.hpp"

struct uPass_func_extract : public upass::uPass {
public:
  uPass_func_extract(std::shared_ptr<upass::Lnast_manager>&);
  uPass_func_extract()           = delete;
  ~uPass_func_extract() override = default;

  void process_func_def() override;

  upass::Emit_decision                classify_statement() override;
  bool                                overrides_classify_statement() const override { return true; }
  std::vector<std::shared_ptr<Lnast>> take_new_lnasts() override;

private:
  bool drop_current_func_def{false};

  std::vector<std::shared_ptr<Lnast>> extracted_lnasts;
  std::unordered_set<std::string>     extracted_names;

  static std::string strip_io_prefix(std::string_view name);

  void copy_current_subtree(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent);
  void copy_current_children(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent);

  bool emit_io_tuple_from_decl(const std::shared_ptr<Lnast>& dst, const Lnast_nid& io_idx);
};
