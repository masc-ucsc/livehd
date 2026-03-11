//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace upass {

class IR_adapter {
public:
  using Node_id = std::uint64_t;
  struct Replace_effect {
    std::size_t rewired_edges{0};
    std::size_t new_const_nodes{0};
    std::size_t deleted_nodes{0};
  };

  virtual ~IR_adapter() = default;

  // High-level IR identity for runner/scheduler decisions.
  virtual std::string_view kind() const                 = 0;
  virtual std::size_t      node_count() const           = 0;
  virtual std::size_t      const_count() const          = 0;
  virtual std::size_t      arithmetic_count() const     = 0;
  virtual std::size_t      fold_candidate_count() const = 0;

  // Minimal shared node-view API for cross-IR pass logic.
  virtual std::vector<Node_id>        list_nodes() const                                   = 0;
  virtual std::string_view            op_name(Node_id node) const                          = 0;
  virtual std::vector<Node_id>        inputs(Node_id node) const                           = 0;
  virtual bool                        is_const(Node_id node) const                         = 0;
  virtual std::optional<std::int64_t> const_value(Node_id node) const                      = 0;
  virtual Replace_effect              estimate_replace_with_const(Node_id node) const      = 0;
  virtual bool                        replace_with_const(Node_id node, std::int64_t value) = 0;
};

}  // namespace upass
