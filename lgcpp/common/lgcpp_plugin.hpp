//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include "absl/container/flat_hash_map.h"

#include "lgtuple.hpp"
#include "pass.hpp"

class Lgcpp_plugin {
public:
  using Comptime_fn = std::function<void(const std::shared_ptr<Lgtuple> inp, std::shared_ptr<Lgtuple> out)>;
  using Map_setup = absl::flat_hash_map<std::string, Comptime_fn>;
protected:

  static Map_setup registry;

public:
  Lgcpp_plugin(const std::string &name, Comptime_fn fn) {
    if (registry.find(name) != registry.end()) {
      Pass::error("Lgcpp_plugin: {} is already registered", name);
      return;
    }
    registry[name] = fn;
  }

  static const Map_setup &get_registry() { return registry; }
};

