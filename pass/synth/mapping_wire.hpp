// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <bit>
#include <cmath>
#include <stdexcept>

#include "tmap.hpp"
namespace livehd::synth::wire {
struct Writer {
  std::string data;
  void        number(uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
      data.push_back(static_cast<char>(value >> (i * 8)));
    }
  }
  void real(double value) { number(std::bit_cast<uint64_t>(value)); }
  void text(std::string_view value) {
    number(value.size());
    data.append(value);
  }
};
struct Reader {
  std::string_view data;
  uint64_t         number() {
    if (data.size() < 8) {
      throw std::runtime_error("short template record");
    }
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
      result |= uint64_t(static_cast<unsigned char>(data[i])) << (i * 8);
    }
    data.remove_prefix(8);
    return result;
  }
  size_t count(size_t limit) {
    auto value = number();
    if (value > limit) {
      throw std::runtime_error("oversized template record");
    }
    return static_cast<size_t>(value);
  }
  double real() {
    double result = std::bit_cast<double>(number());
    if (!std::isfinite(result)) {
      throw std::runtime_error("invalid template number");
    }
    return result;
  }
  std::string text() {
    auto        size = count(data.size() >= 8 ? data.size() - 8 : 0);
    std::string result(data.substr(0, size));
    data.remove_prefix(size);
    return result;
  }
};

std::string     encode_request(const Mapping_request& request);
Mapping_request decode_request(std::string_view data);
std::string     encode_fragment(const Mapped_fragment& fragment);
Mapped_fragment decode_fragment(std::string_view data);
}  // namespace livehd::synth::wire
