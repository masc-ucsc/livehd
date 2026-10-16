// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// TEST-ONLY: the frozen reference Sub inliner (inline_sub_reference.cpp says
// what it is). The same contract as graph_util::inline_sub_instance at commit
// 98a7e7c44; see graph/inline_sub.hpp for what each parameter means.

#include <optional>
#include <string>
#include <string_view>

#include "hhds/graph.hpp"

namespace livehd::graph_util_reference {

[[nodiscard]] bool inline_sub_instance(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass,
                                       hhds::Graph* def = nullptr, bool name_state = false, bool prefix_instance = true,
                                       bool inherit_color = false, const std::optional<std::string>& state_name = std::nullopt);

[[nodiscard]] int             sub_port_width(const hhds::Pin_class& port, const hhds::GraphIO& io, std::string_view name);
[[nodiscard]] bool            driver_fits_port(const hhds::Pin_class& driver, int bits, bool is_signed);
[[nodiscard]] bool            range_fits_port(int dbits, bool dsigned, int bits, bool is_signed);
[[nodiscard]] hhds::Pin_class fit_to_port(hhds::Graph& graph, const hhds::Pin_class& driver, int bits, bool is_signed);
[[nodiscard]] std::string     sub_def_port_mismatch(const hhds::Node_class& inst, hhds::Graph* def);

}  // namespace livehd::graph_util_reference
