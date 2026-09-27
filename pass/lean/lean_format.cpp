// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lean_format.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace lean_export {
const std::unordered_set<std::string> kLeanReserved = {
    "abbrev",  "axiom",     "by",   "class",    "def",      "deriving", "do",        "else",     "end",        "example",
    "false",   "for",       "fun",  "if",       "import",   "in",       "inductive", "instance", "let",        "match",
    "mutual",  "namespace", "open", "opaque",   "partial",  "private",  "protected", "rec",      "set_option", "structure",
    "theorem", "then",      "true", "universe", "variable", "where",    "with",
};

std::string sanitize_lean(std::string_view name) {
  std::string out;
  out.reserve(name.size() + 4);

  for (unsigned char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (ok) {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "_x%02x_", c);
      out += buf;
    }
  }

  if (out.empty()) {
    out = "id";
  }
  if (out[0] >= '0' && out[0] <= '9') {
    out = "id_" + out;
  }
  if (kLeanReserved.count(out) > 0) {
    out = "id_" + out;
  }
  return out;
}

std::string lean_integer(std::string_view value) {
  if (value.empty()) {
    throw std::invalid_argument("empty decimal integer");
  }
  if (value == "0") {
    return "0";
  }
  if (value.front() == '-') {
    return "(-Int.ofNat " + std::string(value.substr(1)) + ")";
  }
  return "(Int.ofNat " + std::string(value) + ")";
}
std::string nat_array(const std::vector<uint32_t>& values) {
  std::ostringstream out;
  out << "#[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) {
      out << ", ";
    }
    out << values[i];
  }
  out << "]";
  return out.str();
}
void write_atomic(const std::string& path, const std::function<void(std::ostream&)>& emit) {
  const auto tmp = path + ".tmp";
  try {
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(tmp);
    emit(out);
    out.close();
    std::filesystem::rename(tmp, path);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw;
  }
}
std::string lit_bv(uint32_t w, std::string_view v) { return "(BitVec.ofInt " + std::to_string(w) + " (" + std::string(v) + "))"; }

std::string lit_zero(uint32_t w) { return "(0#" + std::to_string(w) + ")"; }

std::string lit_one(uint32_t w) { return "(1#" + std::to_string(w) + ")"; }

std::string bst_literal(const std::vector<std::pair<uint32_t, std::string>>& sorted, size_t lo, size_t hi) {
  if (lo >= hi) {
    return "BT.lf";
  }
  const size_t mid = lo + (hi - lo) / 2;
  return "(BT.nd " + std::to_string(sorted[mid].first) + " (" + sorted[mid].second + ") " + bst_literal(sorted, lo, mid) + " "
         + bst_literal(sorted, mid + 1, hi) + ")";
}

std::string bst_literal(std::vector<std::pair<uint32_t, std::string>> pairs) {
  std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  return bst_literal(pairs, 0, pairs.size());
}

std::string nat_list(const std::vector<uint32_t>& xs) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < xs.size(); ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << xs[i];
  }
  oss << "]";
  return oss.str();
}

}  // namespace lean_export
