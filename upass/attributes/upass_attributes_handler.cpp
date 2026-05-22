//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes_handler.hpp"

#include <algorithm>
#include <utility>

#include "upass_attributes_sticky.hpp"

namespace upass {
namespace attributes {

Attribute_handler* Handler_registry::lookup(std::string_view name) const {
  // Exact-name match wins (category-A/B/C built-ins registered explicitly).
  auto it = exact.find(std::string{name});
  if (it != exact.end()) {
    return it->second.get();
  }
  // Sticky pattern: `debug` (alias of `_debug`) and any `_*` user attr.
  if (sticky_pattern && Sticky_handler::is_sticky_name(name)) {
    return sticky_pattern.get();
  }
  // Default: category-D pass-through.
  return default_handler.get();
}

void Handler_registry::register_exact(std::string name, std::shared_ptr<Attribute_handler> h) {
  exact[std::move(name)] = std::move(h);
  rebuild_unique_handlers();
}

void Handler_registry::register_sticky_pattern(std::shared_ptr<Attribute_handler> h) {
  sticky_pattern = std::move(h);
  rebuild_unique_handlers();
}

void Handler_registry::register_default(std::shared_ptr<Attribute_handler> h) {
  default_handler = std::move(h);
  rebuild_unique_handlers();
}

void Handler_registry::rebuild_unique_handlers() {
  unique_handlers.clear();
  auto add = [&](Attribute_handler* h) {
    if (!h) {
      return;
    }
    if (std::find(unique_handlers.begin(), unique_handlers.end(), h) == unique_handlers.end()) {
      unique_handlers.push_back(h);
    }
  };
  for (const auto& [_, h] : exact) {
    add(h.get());
  }
  add(sticky_pattern.get());
  add(default_handler.get());
}

void Handler_registry::for_each_handler(const std::function<void(Attribute_handler&)>& fn) const {
  // unique_handlers is rebuilt by register_* calls — visiting it directly
  // avoids per-invocation std::set allocation on the hot LNAST-op loop.
  for (auto* h : unique_handlers) {
    fn(*h);
  }
}

}  // namespace attributes
}  // namespace upass
