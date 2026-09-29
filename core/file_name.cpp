// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "file_name.hpp"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SHA256.h"

namespace livehd {

namespace {
constexpr size_t kDigestChars = 64;  // SHA-256 as lowercase hex
constexpr size_t kMaxExtBytes = 5;   // '.' + up to 4 characters

// The trailing `.ext` to preserve, or empty when the name has none short
// enough to count as one. A LEADING dot is the name's first character, not an
// extension (`.gitignore`), and a trailing dot has nothing after it to keep.
std::string_view trailing_ext(std::string_view name) {
  const auto dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
    return {};
  }
  const auto ext = name.substr(dot);
  return ext.size() <= kMaxExtBytes ? ext : std::string_view{};
}
}  // namespace

std::string shorten_file_name(std::string_view name) {
  if (name.size() <= kMaxFileNameBytes) {
    return std::string(name);
  }
  const auto ext = trailing_ext(name);

  llvm::SHA256 hash;
  hash.update(llvm::StringRef(name.data(), name.size()));
  const auto digest = hash.final();

  // head + '_' + digest + ext == kMaxFileNameBytes. kMaxExtBytes is far below
  // the budget, so the head is always at least 130 bytes of readable prefix.
  std::string out(name.substr(0, kMaxFileNameBytes - 1 - kDigestChars - ext.size()));
  // Do not split a UTF-8 code point at the readable-prefix boundary.
  while (!out.empty() && (static_cast<unsigned char>(name[out.size()]) & 0xc0) == 0x80) {
    out.pop_back();
  }
  out                  += '_';
  constexpr char hex[]  = "0123456789abcdef";
  for (const auto byte : digest) {
    out += hex[byte >> 4];
    out += hex[byte & 0xf];
  }
  out += ext;
  return out;
}

std::string unit_file_stem(std::string_view name) {
  std::string stem(name);
  for (char& c : stem) {
    if (c == '/' || c == '\\') {
      c = '_';
    }
  }
  return shorten_file_name(stem);
}

}  // namespace livehd
