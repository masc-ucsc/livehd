//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lgraph_manager.hpp"
#include "upass_utils.hpp"

namespace upass {

struct uPass_lgraph {
public:
  explicit uPass_lgraph(std::shared_ptr<Lgraph_manager> &_gm) : gm(_gm) {}
  virtual ~uPass_lgraph() = default;

  void begin_iteration() { changed = false; }
  bool has_changed() const { return changed; }

  virtual void run_once() {}

protected:
  std::shared_ptr<Lgraph_manager> &gm;
  bool                             changed{false};

  void mark_changed() { changed = true; }
};

template <class T>
struct uPass_lgraph_wrapper {
public:
  static std::shared_ptr<uPass_lgraph> get_upass(std::shared_ptr<Lgraph_manager> &gm) { return std::make_unique<T>(gm); }
};

class uPass_lgraph_plugin {
public:
  using Setup_fn = std::function<std::shared_ptr<uPass_lgraph>(std::shared_ptr<Lgraph_manager> &)>;

  struct Setup_entry {
    Setup_fn                 setup_fn;
    std::vector<std::string> depends_on;
  };
  using Map_setup = std::map<std::string, Setup_entry>;

protected:
  static inline Map_setup registry;

public:
  uPass_lgraph_plugin(const std::string &name, Setup_fn setup_fn, std::vector<std::string> depends_on = {}) {
    if (registry.find(name) != registry.end()) {
      upass::error("uPass_lgraph_plugin: {} is already registered\n", name);
      return;
    }
    registry[name] = Setup_entry{.setup_fn = std::move(setup_fn), .depends_on = std::move(depends_on)};
  }

  static const Map_setup &get_registry() { return registry; }
};

}  // namespace upass
