//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace str_tools {

[[nodiscard]] inline int to_i(std::string_view str) {  // convert to integer
  if (str.empty() || !std::isdigit(str.front())) {
    return 0;
  }
  int result{};
  std::from_chars(str.data(), str.data() + str.size(), result);
  return result;
}

[[nodiscard]] inline std::string to_s(uint64_t v) {
  std::array<char, 18> str2;
  auto [ptr, ec] = std::to_chars(str2.data(), str2.data() + str2.size(), v, 10);
  (void)ec;
  std::string str(str2.data(), ptr - str2.data());

  return str;
}

[[nodiscard]] inline bool is_string(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  auto ch = str.front();
  if (std::isdigit(ch) || ch == '-') {
    return false;
  }

  return true;
}

[[nodiscard]] inline bool is_i(std::string_view str) {
  if (str.size() == 0 || !(std::isdigit(str.front()) || str.front() == '-')) {
    return false;
  }

  int result{};
  auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
  (void)p;
  if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) {
    return false;
  }

  return true;
}

[[nodiscard]] inline std::string_view get_str_after_last_if_exists(std::string_view str, const char chr) {
  auto pos = str.rfind(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(pos + 1);
}

[[nodiscard]] inline std::string_view get_str_before_first(std::string_view str, const char chr) {
  auto pos = str.find(chr);
  if (pos == std::string_view::npos) {
    return str;
  }

  return str.substr(0, pos);
}

[[nodiscard]] inline bool ends_with(std::string_view str, std::string_view end) {
  if (end.size() == str.size()) {
    return str == end;  // faster path
  }
  if (end.size() > str.size()) {
    return false;  // end is larger
  }

  const auto* base_en   = end.data();
  const auto* base_self = str.data() + str.size() - end.size();
  return memcmp(base_self, base_en, end.size()) == 0;
}

// ---------------------------------------------------------------------------
// ASCII lowercase fold. LiveHD/Pyrope names are matched CASE-SENSITIVELY; this
// helper exists only so passes can detect names that differ solely by letter
// case (e.g. the upass.ssa `name-case-collision` lint) — it is NOT used for
// name lookup. Only 'A'..'Z' fold; all other bytes pass through unchanged.
// ---------------------------------------------------------------------------

[[nodiscard]] inline char ascii_tolower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// ASCII-lowercased copy of `s`, for case-collision detection only.
[[nodiscard]] inline std::string ascii_fold(std::string_view s) {
  std::string out(s);
  for (auto& c : out) {
    c = ascii_tolower(c);
  }
  return out;
}

// Truthy/falsy pass-option text: empty, "0", "false", "no" and "off" (any
// letter case) read as false; anything else is true. This is the eprp option
// contract, NOT Pyrope boolean-literal semantics (which accept only false/0 —
// see upass_attributes_sticky).
[[nodiscard]] inline bool option_is_true(std::string_view value) {
  const auto lower = ascii_fold(value);
  return !(lower.empty() || lower == "0" || lower == "false" || lower == "no" || lower == "off");
}

// Parse the max_width knob. "0"/"unlimited"/"inf"/"none" (case-insensitive) mean
// no cap -> SIZE_MAX (every `w > max_width` guard is then always-false, so the
// upper bound is disabled while the separate `w == 0` unsized-node check stays).
// A positive integer sets that cap; empty/garbage falls back to the default.
[[nodiscard]] inline size_t parse_max_width(std::string_view s, size_t dflt = 1024) {
  if (s.empty()) {
    return dflt;
  }
  const auto l = ascii_fold(s);
  if (l == "unlimited" || l == "inf" || l == "none" || l == "0") {
    return std::numeric_limits<size_t>::max();
  }
  try {
    size_t v = std::stoul(l);
    return v == 0 ? std::numeric_limits<size_t>::max() : v;
  } catch (...) {
    return dflt;
  }
}

// Canonical reset-name test, token-aware so "first"/"burst" don't match.
// negreset (active-low) is inferred from an _n / _ni / n-suffix spelling.
// Shared by the LEC reset harness and the slang reset demotion so the token
// set cannot drift between them.
[[nodiscard]] inline bool reset_name_polarity(std::string_view name, bool& negreset) {
  const auto lc        = ascii_fold(name);
  bool       tok_match = false;
  size_t     start     = 0;
  for (size_t i = 0; i <= lc.size(); ++i) {
    if (i == lc.size() || lc[i] == '_') {
      std::string_view tok = std::string_view(lc).substr(start, i - start);
      if (tok == "rst" || tok == "reset" || tok == "rstn" || tok == "resetn" || tok == "arst" || tok == "areset" || tok == "nrst"
          || tok == "nreset" || tok == "por") {
        tok_match = true;
      }
      start = i + 1;
    }
  }
  if (!tok_match) {
    return false;
  }
  const auto ends = [&lc](std::string_view s) { return ends_with(lc, s); };
  negreset        = ends("_n") || ends("_ni") || ends("_n_i") || ends("_ni_i") || lc == "rstn" || lc == "resetn" || ends("nrst")
             || ends("nreset");
  return true;
}

[[nodiscard]] inline bool is_reset_like_name(std::string_view name) {
  bool negreset = false;
  return reset_name_polarity(name, negreset);
}

// Canonical cross-frontend module identity. Pyrope graph names include a file
// prefix and primitive template widths (`file.foo__u8_bool`), while an
// elaborated Verilog frontend exposes the same definition as `foo`.
[[nodiscard]] inline std::string canonical_entity_name(std::string_view name) {
  const auto  dot = name.rfind('.');
  std::string entity(dot == std::string_view::npos ? name : name.substr(dot + 1));
  const auto  specialization = entity.find("__");
  if (specialization == std::string::npos || specialization == 0 || specialization + 2 >= entity.size()) {
    return entity;
  }

  size_t pos = specialization + 2;
  while (pos < entity.size()) {
    const auto end   = entity.find('_', pos);
    const auto token = std::string_view(entity).substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    bool       width = token == "bool";
    if (!width && token.size() >= 2 && (token.front() == 'u' || token.front() == 's')) {
      width = std::all_of(token.begin() + 1, token.end(), [](unsigned char ch) { return std::isdigit(ch); });
    }
    if (!width) {
      return entity;
    }
    if (end == std::string::npos) {
      return entity.substr(0, specialization);
    }
    pos = end + 1;
  }
  return entity;
}

}  // namespace str_tools
