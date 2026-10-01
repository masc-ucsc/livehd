// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string_view>
namespace livehd::sim {
// Shared across generated DUT translation units and the Pyrope driver.
inline constexpr std::string_view kPlusargHelper = R"cpp(
#ifndef LHD_SIM_PLUSARGS_V1
#define LHD_SIM_PLUSARGS_V1
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
  inline std::vector<std::string> __lhd_sim_args;
  inline std::set<std::string>    __lhd_sim_arg_reads;
  inline void (*__lhd_sim_print_hook)(const std::string&) = nullptr;
  inline void __lhd_sv_print(const std::string& text) {
    if (__lhd_sim_print_hook) {
      __lhd_sim_print_hook(text);
    } else {
      std::fwrite(text.data(), 1, text.size(), stdout);
    }
  }
  template <int W, bool S, class T>
  inline auto __lhd_sv_fit(T input) {
    static_assert(W >= 1 && W <= 64);
    uint64_t value = static_cast<uint64_t>(input);
    if constexpr (W < 64) {
      value &= (uint64_t{1} << W) - 1;
    }
    if constexpr (S) {
      if constexpr (W < 64) {
        if (value & (uint64_t{1} << (W - 1))) {
          value |= ~((uint64_t{1} << W) - 1);
        }
      }
      return std::bit_cast<int64_t>(value);
    } else {
      return value;
    }
  }
  inline const std::string* __lhd_sv_match(const std::string& prefix) {
    for (const auto& arg : __lhd_sim_args) {
      if (arg.size() >= prefix.size() + 1 && arg.compare(1, prefix.size(), prefix) == 0) {
        const auto eq = arg.find('=');
        __lhd_sim_arg_reads.insert(arg.substr(1, eq == std::string::npos ? eq : eq - 1));
        return &arg;
      }
    }
    return nullptr;
  }
  inline int                          __lhd_sv_test(const std::string& prefix) { return __lhd_sv_match(prefix) ? 1 : 0; }
  inline std::pair<std::string, char> __lhd_sv_spec(const std::string& format) {
    std::string prefix;
    for (size_t i = 0; i < format.size(); ++i) {
      if (format[i] != '%') {
        prefix += format[i];
        continue;
      }
      if (++i == format.size()) {
        break;
      }
      if (format[i] == '%') {
        prefix += '%';
        continue;
      }
      if (format[i] == '0' && ++i == format.size()) {
        break;
      }
      const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(format[i])));
      if (std::string("dbhxos").find(kind) != std::string::npos) {
        return {prefix, kind};
      }
      break;
    }
    throw std::runtime_error("unsupported $value$plusargs format: " + format);
  }
  template <int W, bool S, class T>
  inline int __lhd_sv_value(const std::string& format, T& destination) {
    const auto [prefix, kind] = __lhd_sv_spec(format);
    const auto* match         = __lhd_sv_match(prefix);
    if (!match) {
      return 0;  // The destination is untouched on a miss.
    }
    const std::string text = match->substr(prefix.size() + 1);
    if constexpr (std::is_same_v<T, std::string>) {
      if (kind != 's') {
        throw std::runtime_error("string $value$plusargs requires %s");
      }
      destination = text;
    } else {
      uint64_t value = 0;
      if (kind == 's') {
        for (unsigned char c : text) {
          value = (value << 8) | c;
        }
      } else if (kind == 'd') {
        // SV returns success for a matching argument, even when conversion yields 0.
        value = static_cast<uint64_t>(std::strtoll(text.c_str(), nullptr, 10));
      } else {
        const unsigned shift = kind == 'b' ? 1 : kind == 'o' ? 3 : 4;
        for (unsigned char c : text) {
          if (c == '_' || std::isspace(c)) {
            continue;
          }
          unsigned digit;
          if (c >= '0' && c <= '9') {
            digit = c - '0';
          } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
          } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
          } else if (c == 'x' || c == 'X' || c == 'z' || c == 'Z' || c == '?') {
            digit = 0;
          } else {
            break;
          }
          if (digit >= (1u << shift)) {
            break;
          }
          value = (value << shift) | digit;
        }
      }
      destination = __lhd_sv_fit<W, S>(value);
    }
    return 1;
  }
  struct __lhd_sv_print_arg {
    uint64_t    value;
    std::string text;
    int         bits;
    bool        sign;
    bool        string;
  };
  template <class T>
  inline __lhd_sv_print_arg __lhd_sv_arg(T value, int bits, bool sign) {
    if constexpr (std::is_same_v<T, std::string>) {
      return {0, value, 0, false, true};
    } else {
      return {static_cast<uint64_t>(value), {}, bits, sign, false};
    }
  }
  inline std::string __lhd_sv_format(const std::string& fmt, std::initializer_list<__lhd_sv_print_arg> args) {
    std::string out;
    auto        arg = args.begin();
    for (size_t i = 0; i < fmt.size(); ++i) {
      if (fmt[i] != '%') {
        out += fmt[i];
        continue;
      }
      if (++i == fmt.size()) {
        throw std::runtime_error("incomplete SV debug format");
      }
      if (fmt[i] == '%') {
        out += '%';
        continue;
      }
      bool     compact = false;
      unsigned width   = 0;
      if (fmt[i] == '0') {
        compact = true;
        ++i;
      }
      while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
        width = width * 10 + unsigned(fmt[i++] - '0');
        if (width > 100000) {
          throw std::runtime_error("SV debug format width too large");
        }
      }
      if (i == fmt.size() || arg == args.end()) {
        throw std::runtime_error("SV debug format argument mismatch");
      }
      const char  kind = static_cast<char>(std::tolower(static_cast<unsigned char>(fmt[i])));
      const auto& a    = *arg++;
      std::string text;
      if (kind == 's') {
        if (a.string) {
          text = a.text;
        } else {
          for (int bit = ((a.bits + 7) / 8 - 1) * 8; bit >= 0; bit -= 8) {
            const char c = static_cast<char>(a.value >> bit);
            if (c) {
              text += c;
            }
          }
        }
      } else if (kind == 'd') {
        text = a.sign ? std::to_string(std::bit_cast<int64_t>(a.value)) : std::to_string(a.value);
        if (!compact && !width) {
          width = unsigned((a.bits * 30103) / 100000 + 1 + (a.sign ? 1 : 0));
        }
      } else if (kind == 'h' || kind == 'x' || kind == 'b' || kind == 'o') {
        const unsigned shift = kind == 'b' ? 1 : kind == 'o' ? 3 : 4;
        uint64_t       v     = a.value;
        if (a.bits < 64) {
          v &= (uint64_t{1} << a.bits) - 1;
        }
        do {
          text.insert(text.begin(), "0123456789abcdef"[v & ((1u << shift) - 1)]);
          v >>= shift;
        } while (v);
        if (!compact && !width) {
          width = unsigned(a.bits + shift - 1) / shift;
        }
        if (text.size() < width) {
          text.insert(0, width - text.size(), '0');
        }
      } else {
        throw std::runtime_error("unsupported SV debug display format");
      }
      if (text.size() < width) {
        text.insert(0, width - text.size(), ' ');
      }
      out += text;
    }
    if (arg != args.end()) {
      throw std::runtime_error("extra SV debug format arguments");
    }
    return out;
  }
#endif
)cpp";
}  // namespace livehd::sim
