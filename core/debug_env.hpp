// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>

namespace livehd::debug {

// A literal template argument gives every name one process-wide snapshot,
// shared across translation units and call sites. Copy the value: setenv and
// unsetenv may invalidate the pointer returned by getenv. Unset and empty are
// distinct, since most diagnostic switches test presence rather than truth.
template <size_t N>
struct Env_name {
  char text[N];
  constexpr Env_name(const char (&name)[N]) {
    for (size_t i = 0; i < N; ++i) {
      text[i] = name[i];
    }
  }
};

template <Env_name Name>
[[nodiscard]] const char* env() {
  static_assert(sizeof(Name.text) > 4 && Name.text[0] == 'L' && Name.text[1] == 'H' && Name.text[2] == 'D' && Name.text[3] == '_',
                "LiveHD debug environment controls must use the LHD_ prefix");
  static const std::optional<std::string> value = []() -> std::optional<std::string> {
    if (const char* text = std::getenv(Name.text)) {
      return std::string{text};
    }
    return std::nullopt;
  }();
  return value ? value->c_str() : nullptr;
}

}  // namespace livehd::debug
