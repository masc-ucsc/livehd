// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>

#include "hhds/graph.hpp"

namespace livehd::graph_util {

struct Io_port_clash {
  hhds::Port_id    id;
  std::string_view first, second;
};

inline std::optional<Io_port_clash> find_io_port_clash(const hhds::GraphIO& io) {
  const auto scan = [](const auto& declarations) -> std::optional<Io_port_clash> {
    std::unordered_map<hhds::Port_id, std::string_view> owner;
    for (const auto& d : declarations) {
      if (const auto [it, fresh] = owner.try_emplace(d.port_id, d.name); !fresh) {
        return Io_port_clash{d.port_id, it->second, d.name};
      }
    }
    return {};
  };
  // Graph inputs and outputs are pins on different HHDS nodes.
  if (auto clash = scan(io.get_input_pin_decls())) {
    return clash;
  }
  return scan(io.get_output_pin_decls());
}

inline void copy_io_decl(hhds::GraphIO& dst, const hhds::GraphIO::DeclaredIoPin& declaration, bool input) {
  const auto& d = declaration;
  if (input) {
    dst.add_input(d.name, d.port_id, d.loop_break);
  } else {
    dst.add_output(d.name, d.port_id, d.loop_break);
  }
  if (d.bits != 0) {
    dst.set_bits(d.name, d.bits);
  }
  dst.set_unsign(d.name, d.unsign);
}

template <class Before_copy>
void copy_io_decls(const hhds::GraphIO& src, hhds::GraphIO& dst, Before_copy before_copy) {
  for (const auto& input : src.get_input_pin_decls()) {
    before_copy();
    copy_io_decl(dst, input, true);
  }
  for (const auto& output : src.get_output_pin_decls()) {
    before_copy();
    copy_io_decl(dst, output, false);
  }
}

inline void copy_io_decls(const hhds::GraphIO& src, hhds::GraphIO& dst) {
  copy_io_decls(src, dst, [] {});
}

}  // namespace livehd::graph_util
