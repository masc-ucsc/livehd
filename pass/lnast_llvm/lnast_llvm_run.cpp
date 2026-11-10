//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// lnast_llvm_run DIR CYCLES: load DIR/kernel.o (+ kernel.json) and print one
// `TRACE <cycle> name=value ...` line per cycle, driven by the same closed-form
// stimulus the Pyrope testbench uses:
//   input i, cycle c:  (((c+1)*P_i + (c+1)*(c+1)*Q_i) >> 3) masked to the width
//   P_i = 2654435761 + 1234567*i,  Q_i = 40503 + 2*i   (i = index among the inputs)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "inou/cgen/sim_native_rt.hpp"

namespace {

struct Field {
  std::string name;
  uint32_t    bits = 0;
  bool        sign = false;
  size_t      word = 0;
  uint64_t    init = 0;
};

// kernel.json is written by lnast_llvm.cpp in a fixed shape; read it with a scanner, not a JSON library.
std::string slurp(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::vector<Field> parse_list(const std::string& js, const std::string& key) {
  std::vector<Field> out;
  auto               at = js.find("\"" + key + "\"");
  if (at == std::string::npos) {
    return out;
  }
  auto open  = js.find('[', at);
  auto close = js.find(']', open);
  auto body  = js.substr(open + 1, close - open - 1);
  size_t pos = 0;
  auto   num = [&](const std::string& obj, const std::string& k) -> uint64_t {
    auto p = obj.find("\"" + k + "\":");
    return p == std::string::npos ? 0 : std::strtoull(obj.c_str() + p + k.size() + 3, nullptr, 10);
  };
  while ((pos = body.find('{', pos)) != std::string::npos) {
    auto end = body.find('}', pos);
    auto obj = body.substr(pos, end - pos + 1);
    Field f;
    auto  n = obj.find("\"name\":\"") + 8;
    f.name  = obj.substr(n, obj.find('"', n) - n);
    f.bits  = static_cast<uint32_t>(num(obj, "bits"));
    f.sign  = num(obj, "sign") != 0;
    f.word  = num(obj, "word");
    f.init  = num(obj, "init");
    out.push_back(f);
    pos = end + 1;
  }
  return out;
}

// Decimal text of a `bits`-wide value held in little-endian words (two's complement when `sign`).
std::string wide_decimal(const uint64_t* w, uint32_t bits, bool sign) {
  const size_t          n = (bits + 63) / 64;
  std::vector<uint64_t> v(w, w + n);
  if (bits % 64 != 0) {
    v[n - 1] &= (uint64_t{1} << (bits % 64)) - 1;
  }
  bool neg = false;
  if (sign && ((v[(bits - 1) / 64] >> ((bits - 1) % 64)) & 1)) {
    neg = true;
    for (auto& x : v) {
      x = ~x;
    }
    if (bits % 64 != 0) {
      v[n - 1] &= (uint64_t{1} << (bits % 64)) - 1;
    }
    for (auto& x : v) {  // + 1
      if (++x != 0) {
        break;
      }
    }
  }
  std::string out;
  for (;;) {
    unsigned __int128 rem = 0;
    bool              any = false;
    for (size_t i = n; i-- > 0;) {
      const unsigned __int128 cur = (rem << 64) | v[i];
      v[i]                        = static_cast<uint64_t>(cur / 10);
      rem                         = cur % 10;
      any                         = any || v[i] != 0;
    }
    out.push_back(static_cast<char>('0' + static_cast<int>(rem)));
    if (!any) {
      break;
    }
  }
  if (neg) {
    out.push_back('-');
  }
  return std::string(out.rbegin(), out.rend());
}

uint64_t mask_of(uint32_t bits) { return bits >= 64 ? ~uint64_t{0} : ((uint64_t{1} << bits) - 1); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: lnast_llvm_run DIR CYCLES\n");
    return 2;
  }
  const std::string dir    = argv[1];
  const uint64_t    cycles = std::strtoull(argv[2], nullptr, 10);
  const auto        js     = slurp(dir + "/kernel.json");
  auto              ins    = parse_list(js, "inputs");
  auto              outs   = parse_list(js, "outputs");
  auto              regs   = parse_list(js, "regs");
  auto              pos    = js.find("\"words\":");
  const size_t      words  = std::strtoull(js.c_str() + pos + 8, nullptr, 10);
  auto              fp     = js.find("\"function\": \"") + 13;
  const std::string fn     = js.substr(fp, js.find('"', fp) - fp);

  livehd::sim::Native_objects objects;
  std::string                 error;
  if (!objects.load(dir + "/kernel.o", error)) {
    std::fprintf(stderr, "load: %s\n", error.c_str());
    return 1;
  }
  auto entry = objects.lookup(fn, error);
  if (entry == nullptr) {
    std::fprintf(stderr, "lookup %s: %s\n", fn.c_str(), error.c_str());
    return 1;
  }
  const char*           dump = std::getenv("LNAST_LLVM_DUMP");
  // LNAST_LLVM_EVERY=N: print only cycles with c % N == N-1 (speed runs; the testbench matches).
  const uint64_t every = std::getenv("LNAST_LLVM_EVERY") != nullptr ? std::strtoull(std::getenv("LNAST_LLVM_EVERY"), nullptr, 10) : 1;
  std::vector<uint64_t> state(words + 1, 0);
  auto                  store = [&](const Field& f, uint64_t v) {
    v &= mask_of(f.bits);
    if (f.sign && f.bits < 64 && ((v >> (f.bits - 1)) & 1)) {
      v |= ~mask_of(f.bits);
    }
    state[f.word] = v;
  };
  for (const auto& r : regs) {
    store(r, r.init);
  }
  for (uint64_t c = 0; c < cycles; ++c) {
    for (size_t i = 0; i < ins.size(); ++i) {
      const uint64_t p = 2654435761ull + 1234567ull * i;
      const uint64_t q = 40503ull + 2ull * i;
      store(ins[i], ((((c + 1) * p) + (c + 1) * (c + 1) * q) >> 3));
    }
    entry(state.data(), nullptr);
    if (every > 1 && c % every != every - 1) {
      continue;
    }
    std::printf("TRACE %llu", static_cast<unsigned long long>(c));
    for (const auto& o : outs) {
      if (o.bits > 64) {
        std::printf(" %s=%s", o.name.c_str(), wide_decimal(&state[o.word], o.bits, o.sign).c_str());
        continue;
      }
      uint64_t v = state[o.word] & mask_of(o.bits);
      if (o.sign && o.bits < 64 && ((v >> (o.bits - 1)) & 1)) {
        std::printf(" %s=%lld", o.name.c_str(), static_cast<long long>(v | ~mask_of(o.bits)));
      } else {
        std::printf(" %s=%llu", o.name.c_str(), static_cast<unsigned long long>(v));
      }
    }
    std::printf("\n");
    if (dump != nullptr) {  // LNAST_LLVM_DUMP=name1,name2: registers/memories as hex words (debug aid)
      std::printf("STATE %llu", static_cast<unsigned long long>(c));
      for (const auto& r : regs) {
        if ((std::string(",") + dump + ",").find("," + r.name + ",") == std::string::npos) {
          continue;
        }
        std::printf(" %s=", r.name.c_str());
        for (size_t w = (r.bits + 63) / 64; w-- > 0;) {
          std::printf("%016llx", static_cast<unsigned long long>(state[r.word + w]));
        }
      }
      std::printf("\n");
    }
  }
  return 0;
}
