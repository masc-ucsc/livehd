//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_attributes_handler.hpp"

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
}

void Handler_registry::register_sticky_pattern(std::shared_ptr<Attribute_handler> h) {
  sticky_pattern = std::move(h);
}

void Handler_registry::register_default(std::shared_ptr<Attribute_handler> h) {
  default_handler = std::move(h);
}

}  // namespace attributes
}  // namespace upass
