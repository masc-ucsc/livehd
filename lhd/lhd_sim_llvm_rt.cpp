//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// `lhd_simrt SIMDIR [driver args]` -- the run-time half of the LLVM testbench flow
// (todo/livehd/2sl-llvm-only-sim.md). It replaces `drv.bin`: no generated C++ and no Slop. It loads the DUT
// kernel objects pass/lnast_llvm wrote and the testbench object inou/prp/tb_llvm wrote into one JIT
// dylib (`Native_objects`: relocatable machine code, no host compiler or link step), allocates each
// test's flat state, applies the power-on policy, runs the test function and reports in the exact
// shape `lhd sim` reads from the old driver (`PASS name` lines, `--result-json` array, exit code).
//
// Supported driver arguments: --list-tests, --test NAME (repeatable), --seed N, --init-zero,
// --result-json PATH, --set KEY=VALUE (sim.unknown_zero, sim.warn_undefined: others ignored),
// --no-checkpoint / --ckpt-dir DIR, --restart-cycle N, +param=VALUE.

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "tb_integer.hpp"

#include "file_utils.hpp"
#include "fnv1a.hpp"
#include "hlop/readmem.hpp"
#include "json_util.hpp"
#include "lhd_sim_observe.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "sim_native_rt.hpp"
#include "sim_program.hpp"
#include "tb_abi.hpp"
#include "wide_decimal.hpp"

namespace {

namespace obs = lhd::sim_observe;
namespace fs = std::filesystem;

std::string slurp(const std::string& path) { return livehd::file_utils::read_file(path).value_or(std::string{}); }

using livehd::hash_util::fnv1a64;
using livehd::json_util::escape;

struct Image_ref {
  std::string path;
  int         radix = 0;
};

struct Field {
  std::string           name;
  uint32_t              bits = 0;
  size_t                word = 0;
  int                   kind = 0;
  bool                  sign = false;
  uint64_t              entries = 0;
  int                   fill    = 0;
  std::vector<uint64_t> init;
  bool                  has_init = false;
  Image_ref             image;  // a `std.readmemh/b` preload (radix 0: none)
};

struct Undef_site {
  std::string what, where;
  size_t      word = 0;
};

struct Dut {
  std::string              sim_init;  // the simulation-only initialization program (core/sim_program.hpp), encoded; empty: none
  std::vector<Undef_site>  undefined;
  std::string              stem;
  size_t                   words = 0;
  std::vector<std::string> objects, clocks;
  std::vector<Field>       fields;
};

Dut load_dut(const std::string& simdir, const std::string& stem) {
  const auto          text = slurp(simdir + "/" + stem + ".dut.json");
  rapidjson::Document d;
  d.Parse(text.c_str());
  Dut u;
  u.stem  = stem;
  u.words = d["words"].GetUint64();
  for (const auto& o : d["objects"].GetArray()) {
    u.objects.emplace_back(o.GetString());
  }
  for (const auto& c : d["clocks"].GetArray()) {
    u.clocks.emplace_back(c.GetString());
  }
  if (d.HasMember("sim_init") && d["sim_init"].IsString()) {
    u.sim_init = d["sim_init"].GetString();
  }
  for (const auto& x : d["undefined"].GetArray()) {
    u.undefined.push_back({x["what"].GetString(), x["where"].GetString(), x["word"].GetUint64()});
  }
  for (const auto& f : d["fields"].GetArray()) {
    Field fld;
    fld.name    = f["name"].GetString();
    fld.bits    = f["bits"].GetUint();
    fld.word    = f["word"].GetUint64();
    fld.kind    = f["kind"].GetInt();
    fld.sign    = f["sign"].GetBool();
    fld.entries = f["entries"].GetUint64();
    fld.fill    = f["fill"].GetInt();
    if (f.HasMember("image") && f["image"].IsObject()) {
      fld.image.path  = f["image"]["path"].GetString();
      fld.image.radix = f["image"]["radix"].GetInt();
    }
    if (f["init"].IsArray()) {
      fld.has_init = true;
      for (const auto& w : f["init"].GetArray()) {
        fld.init.push_back(std::strtoull(w.GetString(), nullptr, 16));
      }
    }
    u.fields.push_back(std::move(fld));
  }
  return u;
}

// ---- simulation-only initialization: `initial` blocks with $display / $value$plusargs / $fatal ------------------------
// A tiny interpreter of core/sim_program.hpp, run once per DUT instance at power-on. Variables are private to the
// program; a `$fatal` (or a failed assert) is the test's error.
struct Sv_val {
  bool        is_str = false;
  int         bits   = 0;      // 0 for a string
  bool        sign   = false;
  uint64_t    value  = 0;
  std::string text;
};

Sv_val sv_fit(Sv_val v, int bits, bool sign) {
  if (v.is_str || bits == 0) {
    return v;
  }
  if (bits < 1 || bits > 64) {
    throw std::runtime_error("unsupported simulation integer width");
  }
  if (bits < 64) {
    v.value &= (uint64_t{1} << bits) - 1;
    if (sign && ((v.value >> (bits - 1)) & 1) != 0) {
      v.value |= ~((uint64_t{1} << bits) - 1);
    }
  }
  v.bits = bits;
  v.sign = sign;
  return v;
}

struct Sv_env {
  const std::vector<std::string>*     tokens   = nullptr;  // every `+...` as given, in order
  std::set<std::string>*              consumed = nullptr;
  std::map<std::string, Sv_val>       vars;
  bool                               quiet = false;
};

const std::string* sv_match(Sv_env& env, const std::string& prefix) {
  for (const auto& arg : *env.tokens) {
    if (arg.size() >= prefix.size() + 1 && arg.compare(1, prefix.size(), prefix) == 0) {
      const auto eq = arg.find('=');
      env.consumed->insert(arg.substr(1, eq == std::string::npos ? eq : eq - 1));
      return &arg;
    }
  }
  return nullptr;
}

std::pair<std::string, char> sv_spec(const std::string& format) {
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

std::string sv_format(const std::string& fmt, const std::vector<Sv_val>& args) {
  std::string out;
  size_t      next = 0;
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
    while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i])) != 0) {
      width = width * 10 + unsigned(fmt[i++] - '0');
      if (width > 100000) {
        throw std::runtime_error("SV debug format width too large");
      }
    }
    if (i == fmt.size() || next == args.size()) {
      throw std::runtime_error("SV debug format argument mismatch");
    }
    const char  kind = static_cast<char>(std::tolower(static_cast<unsigned char>(fmt[i])));
    const auto& a    = args[next++];
    std::string text;
    if (kind == 's') {
      if (a.is_str) {
        text = a.text;
      } else {
        for (int bit = ((a.bits + 7) / 8 - 1) * 8; bit >= 0; bit -= 8) {
          const char c = static_cast<char>(a.value >> bit);
          if (c != 0) {
            text += c;
          }
        }
      }
    } else if (kind == 'd') {
      text = a.sign ? std::to_string(static_cast<int64_t>(a.value)) : std::to_string(a.value);
      if (!compact && width == 0) {
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
      } while (v != 0);
      if (!compact && width == 0) {
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
  if (next != args.size()) {
    throw std::runtime_error("extra SV debug format arguments");
  }
  return out;
}

Sv_val sv_eval(const livehd::sim_ir::Node& n, Sv_env& env);

void sv_assign(const livehd::sim_ir::Node& dest, Sv_val v, Sv_env& env) {
  if (dest.op != "var") {
    throw std::runtime_error("simulation assignment target is not a variable");
  }
  auto& slot = env.vars[dest.text];
  slot       = v.is_str ? v : sv_fit(v, dest.bits, dest.sign);
}

Sv_val sv_eval(const livehd::sim_ir::Node& n, Sv_env& env) {
  const auto arg = [&](size_t i) { return sv_eval(n.kids.at(i), env); };
  Sv_val     r;
  if (n.op == "str") {
    r.is_str = true;
    r.text   = n.text;
    return r;
  }
  if (n.op == "num") {
    r.value = std::stoull(n.text);
    return sv_fit(r, n.bits == 0 ? 64 : n.bits, n.sign);
  }
  if (n.op == "var") {
    return env.vars[n.text];
  }
  if (n.op == "cast") {
    return sv_fit(arg(0), n.bits, n.sign);
  }
  if (n.op == "test") {
    r.value = sv_match(env, n.text) != nullptr ? 1 : 0;
    return sv_fit(r, 32, true);
  }
  if (n.op == "value") {
    const auto [prefix, kind] = sv_spec(n.text);
    const auto* match         = sv_match(env, prefix);
    r.value                   = match != nullptr ? 1 : 0;
    if (match == nullptr) {
      return sv_fit(r, 32, true);  // the destination stays untouched on a miss
    }
    const std::string text = match->substr(prefix.size() + 1);
    const auto&       dest = n.kids.at(0);
    if (env.vars[dest.text].is_str) {
      if (kind != 's') {
        throw std::runtime_error("string $value$plusargs requires %s");
      }
      Sv_val s;
      s.is_str = true;
      s.text   = text;
      sv_assign(dest, s, env);
      return sv_fit(r, 32, true);
    }
    uint64_t value = 0;
    if (kind == 's') {
      for (const unsigned char c : text) {
        value = (value << 8) | c;
      }
    } else if (kind == 'd') {
      value = static_cast<uint64_t>(std::strtoll(text.c_str(), nullptr, 10));
    } else {
      const unsigned shift = kind == 'b' ? 1 : kind == 'o' ? 3 : 4;
      for (const unsigned char c : text) {
        if (c == '_' || std::isspace(c) != 0) {
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
    Sv_val num;
    num.value = value;
    sv_assign(dest, num, env);
    return sv_fit(r, 32, true);
  }
  if (n.op == "unary") {
    const Sv_val a = arg(0);
    const auto   u = a.value;
    if (n.text == "!") {
      r.value = u == 0 ? 1 : 0;
    } else if (n.text == "~") {
      r.value = ~u;
    } else if (n.text == "-") {
      r.value = ~u + 1;
    } else if (n.text == "+") {
      r.value = u;
    } else {
      throw std::runtime_error("invalid simulation unary op");
    }
    return sv_fit(r, n.bits == 0 ? 64 : n.bits, n.sign);
  }
  if (n.op == "binary") {
    const Sv_val a = arg(0), b = arg(1);
    if (a.is_str || b.is_str) {
      if (!(a.is_str && b.is_str) || (n.text != "==" && n.text != "!=")) {
        throw std::runtime_error("invalid simulation string operation");
      }
      r.value = (a.text == b.text) == (n.text == "==") ? 1 : 0;
      return sv_fit(r, 32, true);
    }
    const bool     both_signed = a.sign && b.sign;
    const uint64_t ua = a.value, ub = b.value;
    const int64_t  sa = static_cast<int64_t>(ua), sb = static_cast<int64_t>(ub);
    if (n.text == "+") {
      r.value = ua + ub;
    } else if (n.text == "-") {
      r.value = ua - ub;
    } else if (n.text == "*") {
      r.value = ua * ub;
    } else if (n.text == "&") {
      r.value = ua & ub;
    } else if (n.text == "|") {
      r.value = ua | ub;
    } else if (n.text == "^") {
      r.value = ua ^ ub;
    } else if (n.text == "<<") {
      r.value = ub >= 64 ? 0 : ua << ub;
    } else if (n.text == ">>") {
      r.value = ub >= 64 ? 0 : (a.sign ? static_cast<uint64_t>(sa >> ub) : ua >> ub);
    } else if (n.text == "==") {
      r.value = ua == ub;
    } else if (n.text == "!=") {
      r.value = ua != ub;
    } else if (n.text == "<") {
      r.value = both_signed ? sa < sb : ua < ub;
    } else if (n.text == "<=") {
      r.value = both_signed ? sa <= sb : ua <= ub;
    } else if (n.text == ">") {
      r.value = both_signed ? sa > sb : ua > ub;
    } else if (n.text == ">=") {
      r.value = both_signed ? sa >= sb : ua >= ub;
    } else if (n.text == "&&") {
      r.value = (ua != 0 && ub != 0) ? 1 : 0;
    } else if (n.text == "||") {
      r.value = (ua != 0 || ub != 0) ? 1 : 0;
    } else {
      throw std::runtime_error("invalid simulation binary op");
    }
    return sv_fit(r, n.bits == 0 ? 64 : n.bits, n.sign);
  }
  throw std::runtime_error("unsupported simulation expression " + n.op);
}

void sv_exec(const livehd::sim_ir::Node& n, Sv_env& env) {
  if (n.op == "seq") {
    for (const auto& k : n.kids) {
      sv_exec(k, env);
    }
  } else if (n.op == "decl") {
    Sv_val v = sv_eval(n.kids.at(0), env);
    if (n.bits == 0) {
      Sv_val s;
      s.is_str = true;
      s.text   = v.is_str ? v.text : std::string{};
      env.vars[n.text] = s;
    } else {
      env.vars[n.text] = sv_fit(v, n.bits, n.sign);
    }
  } else if (n.op == "set") {
    sv_assign(n.kids.at(0), sv_eval(n.kids.at(1), env), env);
  } else if (n.op == "expr") {
    (void)sv_eval(n.kids.at(0), env);
  } else if (n.op == "if") {
    if (sv_eval(n.kids.at(0), env).value != 0) {
      sv_exec(n.kids.at(1), env);
    } else if (n.kids.size() > 2) {
      sv_exec(n.kids.at(2), env);
    }
  } else if (n.op == "assert") {
    if (sv_eval(n.kids.at(0), env).value == 0) {
      throw std::runtime_error(n.text);
    }
  } else if (n.op == "display" || n.op == "write" || n.op == "fatal") {
    std::vector<Sv_val> args;
    for (const auto& k : n.kids) {
      args.push_back(sv_eval(k, env));
    }
    const std::string text = sv_format(n.text, args);
    if (n.op == "fatal") {
      throw std::runtime_error(text);
    }
    if (!env.quiet) {
      std::fputs(text.c_str(), stdout);
      if (n.op == "display") {
        std::fputc('\n', stdout);
      }
    }
  } else {
    throw std::runtime_error("unsupported simulation statement " + n.op);
  }
}

void run_sim_init(const Dut& d, const std::vector<std::string>& tokens, std::set<std::string>& consumed, bool quiet) {
  if (d.sim_init.empty()) {
    return;
  }
  Sv_env env;
  env.tokens   = &tokens;
  env.consumed = &consumed;
  env.quiet    = quiet;
  sv_exec(livehd::sim_ir::decode(d.sim_init), env);
}

// ---- the state-word layout of one field (lnast_sim_rt.hpp's `put`, kept in step with it) --------------
uint32_t store_bits(const Field& f) { return f.bits <= 8 ? 8 : f.bits <= 16 ? 16 : f.bits <= 32 ? 32 : ((f.bits + 63) / 64) * 64; }
bool     packed(const Field& f) { return store_bits(f) != f.bits && uint64_t{f.bits} * f.entries <= 128; }

void put(uint64_t* st, const Field& f, uint64_t e, const uint64_t* in) {
  const size_t n = (f.bits + 63) / 64;
  if (f.kind != 3) {
    std::memcpy(&st[f.word], in, n * 8);
    if (f.sign && f.bits % 64 != 0 && ((in[n - 1] >> ((f.bits - 1) % 64)) & 1) != 0) {
      st[f.word + n - 1] |= ~((uint64_t{1} << (f.bits % 64)) - 1);
    }
  } else if (packed(f)) {
    const size_t      bytes = ((uint64_t{f.bits} * f.entries + 63) / 64) * 8;
    unsigned __int128 whole = 0;
    std::memcpy(&whole, &st[f.word], bytes);
    const unsigned __int128 mask = ((static_cast<unsigned __int128>(1) << f.bits) - 1) << (e * f.bits);
    whole                        = (whole & ~mask) | ((static_cast<unsigned __int128>(in[0]) << (e * f.bits)) & mask);
    std::memcpy(&st[f.word], &whole, bytes);
  } else {
    std::memcpy(reinterpret_cast<char*>(&st[f.word]) + e * (store_bits(f) / 8), in, store_bits(f) / 8);
  }
}

// Power-on, as lnast_sim_rt.hpp defines it: every word zero, a register with an initializer at that value,
// unknown state drawn from the seed and the field NAME (never the layout order).
void power_on(uint64_t* st, const Dut& d, bool zero_uninitialized, bool unknown_zero, uint64_t seed) {
  std::memset(st, 0, (d.words + 1) * sizeof(uint64_t));
  for (const auto& f : d.fields) {
    if (f.kind < 2) {
      continue;
    }
    if (f.kind == 2 && f.has_init) {
      for (size_t w = 0; w < (f.bits + 63) / 64 && w < f.init.size(); ++w) {
        st[f.word + w] = f.init[w];
      }
      continue;
    }
    if (f.kind == 3 && f.image.radix != 0) {
      // A `std.readmemh/b` preload: the file is read now, so a content-only change needs no rebuild, and a missing or
      // malformed file stops the run with its name (hlop's parser rejects the whole file before any entry is written).
      for (const auto& w : hlop::read_memory_image(f.image.path, f.image.radix, static_cast<int>(f.bits), f.entries)) {
        std::vector<uint64_t> words((f.bits + 63) / 64 + 1, 0);
        for (size_t i = 0; i < w.binary.size(); ++i) {  // MSB first; an x/z bit reads as 0
          if (w.binary[w.binary.size() - 1 - i] == '1') {
            words[i / 64] |= uint64_t{1} << (i % 64);
          }
        }
        put(st, f, w.address, words.data());
      }
      continue;
    }
    if (f.kind == 3 && f.has_init) {
      const size_t nw = (f.bits + 63) / 64;
      for (uint64_t e = 0; e < f.entries; ++e) {
        put(st, f, e, f.init.data() + e * nw);
      }
      continue;
    }
    if (f.fill == 0 || (f.fill == 1 && zero_uninitialized)) {
      continue;
    }
    if (unknown_zero && f.kind == 2 && f.name.starts_with("__unknown")) {
      continue;
    }
    const uint64_t named = fnv1a64(f.name, seed ^ 0x6c6e6173745f7369ULL);
    for (uint64_t e = 0; e < (f.kind == 3 ? f.entries : 1); ++e) {
      std::vector<uint64_t> words((f.bits + 63) / 64 + 1, 0);
      uint64_t              s = named ^ (e * 0xD6E8FEB86659FD93ULL);
      for (size_t w = 0; w < (f.bits + 63) / 64; ++w) {
        s += 0x9E3779B97F4A7C15ULL;
        uint64_t r = s;
        r          = (r ^ (r >> 30)) * 0xBF58476D1CE4E5B9ULL;
        r          = (r ^ (r >> 27)) * 0x94D049BB133111EBULL;
        words[w]   = r ^ (r >> 31);
      }
      if (f.bits % 64 != 0) {
        words[(f.bits - 1) / 64] &= (uint64_t{1} << (f.bits % 64)) - 1;
      }
      put(st, f, e, words.data());
    }
  }
}

// ---- parameters --------------------------------------------------------------------------------------
std::optional<llvm::APInt> parse_int(std::string_view text) {
  const auto literal = livehd::parse_pyrope_integer_spelling(text);
  return literal ? prp_tb::integer_value(*literal) : std::nullopt;
}

struct Site {
  std::string loc, cond, lhs, op, rhs, msg;
  int         line = 0;
  bool        cmp = false, lhs_lit = false, rhs_lit = false;
  unsigned    lhs_bits = 0, rhs_bits = 0;
};

struct Print_seg {
  bool        hole = false;
  std::string text;
  char        base  = 'd';
  size_t      width = 0;
  bool        sep   = false;
  unsigned    bits  = 0;  // the hole value's width (signed two's complement)
};

// A literal with `?` bits: known bits + the mask of the unknown ones (little-endian words), drawn once per run.
struct Unknown_site {
  unsigned              bits = 0;
  bool                  sign = false;
  std::vector<uint64_t> known, mask;
  uint64_t              key = 0;
};
struct Print_site {
  std::string            prefix;
  bool                   newline = true;
  std::vector<Print_seg> segs;
};

struct Fail_info {
  bool        has   = false;
  int64_t     cycle = 0;
  std::string cond, file, msg, loc;
  int         line = 0;
};

struct Ckpt_cfg {
  bool        enabled = false;
  std::string dir;  // <workdir>/ckpt
  double      min_secs = 10.0, max_overhead = 0.10;
  long        max = 10, every = 0;
};

// ---- the VCD of a replay (todo/livehd/2sl-llvm-only-sim.md): raw words in, text out; no Slop ----------
struct Vcd_var {
  size_t      field = 0;  // index in the DUT's fields
  std::string id;
};
struct Vcd_out {
  std::ofstream            out;
  std::vector<Vcd_var>     vars;
  std::vector<std::string> last;
  std::vector<std::string> clock_ids;
  bool                     started = false;
};

std::string vcd_id(size_t n) {
  std::string id;
  do {
    id += static_cast<char>('!' + n % 90);
    n  /= 90;
  } while (n != 0);
  return id;
}

using Settle  = void (*)(uint64_t*);
using Test_fn = int64_t (*)(prp_tb::Rt*, uint64_t*, const int64_t*);

struct Observed_field {
  const Field* field;
  size_t       instance;
};

struct Replay_capture {
  const obs::Observer*               observer = nullptr;
  const std::vector<Observed_field>* fields   = nullptr;
  const std::vector<Settle>*         settle   = nullptr;
  const std::vector<std::string>*    scopes   = nullptr;
  std::ostream*                      samples  = nullptr;
  obs::Observer*                     reducer  = nullptr;
  std::vector<Vcd_out>               vcds;
  std::vector<std::string>           row;
  std::vector<uint64_t>              scratch;
  std::vector<std::vector<size_t>>   outputs;
  int64_t                            from = 0, to = -1;
  int64_t                            vcd_from = 0, vcd_to = -1;
  int64_t                            sample_cycle = -1;

  void flush_row() {
    if (sample_cycle < 0) {
      return;
    }
    if (reducer != nullptr) {
      reducer->sample(sample_cycle, row);
      return;
    }
    *samples << sample_cycle;
    for (const auto& hex : row) {
      *samples << ' ' << hex;
    }
    *samples << '\n';
  }
};

struct Rt_ctx : prp_tb::Rt {
  const std::vector<Site>*         sites    = nullptr;
  const std::vector<Print_site>*   prints   = nullptr;
  const std::vector<Unknown_site>* unknowns = nullptr;
  const std::vector<Print_site>*   strings  = nullptr;
  std::vector<std::string>         pool;  // interned string values: a handle is an index + 1
  std::map<std::string, int64_t>   interned;
  uint64_t                         seed         = 1;
  bool                             unknown_zero = false;
  Fail_info*                       first        = nullptr;
  std::string                      file;
  // checkpoints
  Ckpt_cfg                         ckpt;
  std::string                      test_dir;  // <ckpt dir>/<test>
  size_t                           state_words = 0;
  size_t                           frame_slots = 0;
  std::string                      design_id;
  std::string args_id;  // the supplied arguments and the resolved parameters: a checkpoint is only reusable under the same
  // std.testplusarg / std.valueplusarg
  const std::map<std::string, std::string>*     plus     = nullptr;
  const std::set<std::string>*                  flags    = nullptr;
  std::set<std::string>*                        consumed = nullptr;
  std::string                                   plusarg_err;
  std::chrono::steady_clock::time_point         last_ckpt        = std::chrono::steady_clock::now();
  double                                        interval         = 10.0;
  pid_t                                         child            = -1;
  bool                                          wrote_checkpoint = false;
  // trace replay
  Replay_capture*                               capture          = nullptr;
  bool                                          replaying        = false;
  bool                                          legacy           = false;
  // sim.warn_undefined
  bool                                          warn_undefined   = true;
  std::vector<const Dut*>                       inst_duts;  // per instance of the running test
  std::vector<size_t>                           inst_bases;
  std::set<std::pair<std::string, std::string>> reported;
};

long poll_period(const Ckpt_cfg& c) { return c.every > 0 ? c.every : 256; }

struct Checkpoint {
  int64_t                  cycle      = -1;
  int64_t                  stmt       = -1;
  int64_t                  tick_index = -1;
  int64_t                  period     = 0;
  std::filesystem::path    path;
  std::vector<std::string> pool;
  Fail_info                first;
};

bool write_checkpoint_files(const Rt_ctx& rt, const std::string& dir, const uint64_t* state, int64_t cycle, int64_t stmt,
                            int64_t tick_index) {
  namespace fs = std::filesystem;
  std::error_code   ec;
  const std::string tmp = dir + ".tmp-" + std::to_string(::getpid());
  fs::create_directories(tmp, ec);
  const auto write_words = [&](const char* file, const void* data, size_t words) {
    std::ofstream f(tmp + file, std::ios::binary);
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(words * 8));
    f.close();
    return static_cast<bool>(f);
  };
  if (ec || !write_words("/state.bin", state, rt.state_words) || !write_words("/frame.bin", rt.frame, rt.frame_slots)) {
    fs::remove_all(tmp, ec);
    return false;
  }
  {
    rapidjson::StringBuffer                    buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    w.StartObject();
    w.Key("cycle");
    w.Int64(cycle);
    w.Key("stmt");
    w.Int64(stmt);
    w.Key("tick_index");
    w.Int64(tick_index);
    w.Key("period");
    w.Int64(rt.period);
    w.Key("words");
    w.Uint64(rt.state_words);
    w.Key("slots");
    w.Uint64(rt.frame_slots);
    w.Key("design");
    w.String(rt.design_id.c_str());
    w.Key("args");
    w.String(rt.args_id.data(), static_cast<rapidjson::SizeType>(rt.args_id.size()));
    w.Key("strings");
    w.StartArray();
    for (const auto& str : rt.pool) {
      w.String(str.data(), static_cast<rapidjson::SizeType>(str.size()));
    }
    w.EndArray();
    if (rt.first->has) {
      const auto& first = *rt.first;
      w.Key("first_failure");
      w.StartObject();
      w.Key("cycle");
      w.Int64(first.cycle);
      w.Key("line");
      w.Int(first.line);
      for (const auto& [key, value] : {
               std::pair{"cond", &first.cond},
               std::pair{"file", &first.file},
               std::pair{ "msg",  &first.msg},
               std::pair{ "loc",  &first.loc}
      }) {
        w.Key(key);
        w.String(value->data(), static_cast<rapidjson::SizeType>(value->size()));
      }
      w.EndObject();
    }
    w.EndObject();
    std::ofstream f(tmp + "/meta.json");
    f << buf.GetString() << '\n';
    f.close();
    if (!f || !std::ofstream(tmp + "/complete")) {
      fs::remove_all(tmp, ec);
      return false;
    }
  }
  // The previous run's snapshot at this cycle may be from different inputs.
  // Only replace it after the entire new snapshot has been written.
  fs::remove_all(dir, ec);
  fs::rename(tmp, dir, ec);
  const bool ok = !ec;
  if (!ok) {
    fs::remove_all(tmp, ec);
  }
  return ok;
}

std::vector<std::pair<long, std::string>> list_checkpoints(const std::string& test_dir) {
  std::vector<std::pair<long, std::string>> out;
  std::error_code                           ec;
  for (const auto& e : std::filesystem::directory_iterator(test_dir, ec)) {
    const auto name = e.path().filename().string();
    if (name.size() > 1 && name.front() == 'c' && name.find_first_not_of("0123456789", 1) == std::string::npos
        && std::filesystem::exists(e.path() / "complete", ec)) {
      out.emplace_back(std::strtol(name.c_str() + 1, nullptr, 10), e.path().string());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<Checkpoint> compatible_checkpoints(const Rt_ctx& rt, long max_cycle, bool legacy, bool strict_args,
                                               std::string& error) {
  std::vector<Checkpoint> all;
  if (rt.ckpt.dir.empty()) {
    return all;
  }
  auto entries = list_checkpoints(rt.test_dir);
  // Check newest first so an explicit restart still diagnoses changed arguments at its selected snapshot.
  for (const auto& [cycle, path] : std::views::reverse(entries)) {
    if (max_cycle >= 0 && cycle > max_cycle) {
      continue;
    }
    rapidjson::Document md;
    md.Parse(slurp(path + "/meta.json").c_str());
    const auto has = [&](const char* key) { return md.IsObject() && md.HasMember(key); };
    if (!has("design") || !md["design"].IsString() || md["design"].GetString() != rt.design_id || !has("words")
        || !md["words"].IsUint64() || md["words"].GetUint64() != rt.state_words || !has("slots") || !md["slots"].IsUint64()
        || md["slots"].GetUint64() != rt.frame_slots || !has("cycle") || !md["cycle"].IsInt64() || md["cycle"].GetInt64() != cycle
        || !has("stmt") || !md["stmt"].IsInt64() || md["stmt"].GetInt64() < 0) {
      continue;
    }
    if (!has("args") || !md["args"].IsString()
        || std::string_view(md["args"].GetString(), md["args"].GetStringLength()) != rt.args_id) {
      if (strict_args) {
        error = "checkpoint simulation arguments differ from this run's (" + path + "): rerun without --restart-cycle";
        return {};
      }
      continue;
    }
    Checkpoint ck{cycle, md["stmt"].GetInt64(), cycle, cycle, path, rt.pool, {}};
    if (has("tick_index") && md["tick_index"].IsInt64() && has("period") && md["period"].IsInt64() && has("strings")
        && md["strings"].IsArray()) {
      ck.tick_index = md["tick_index"].GetInt64();
      ck.period     = md["period"].GetInt64();
      if (ck.tick_index < 0 || ck.period < 0) {
        continue;
      }
      ck.pool.clear();
      for (const auto& str : md["strings"].GetArray()) {
        if (!str.IsString()) {
          break;
        }
        ck.pool.emplace_back(str.GetString(), str.GetStringLength());
      }
      if (ck.pool.size() != md["strings"].Size()) {
        continue;
      }
    } else if (!legacy) {
      continue;
    }
    if (has("first_failure")) {
      const auto& f = md["first_failure"];
      if (!f.IsObject() || !f.HasMember("cycle") || !f["cycle"].IsInt64() || !f.HasMember("line") || !f["line"].IsInt()) {
        continue;
      }
      ck.first.has   = true;
      ck.first.cycle = f["cycle"].GetInt64();
      ck.first.line  = f["line"].GetInt();
      for (const auto& [key, value] : {
               std::pair{"cond", &ck.first.cond},
               std::pair{"file", &ck.first.file},
               std::pair{ "msg",  &ck.first.msg},
               std::pair{ "loc",  &ck.first.loc}
      }) {
        if (f.HasMember(key) && f[key].IsString()) {
          value->assign(f[key].GetString(), f[key].GetStringLength());
        }
      }
    }
    all.push_back(std::move(ck));
    if (strict_args) {
      break;
    }
  }
  std::reverse(all.begin(), all.end());
  return all;
}

bool restore_checkpoint(Rt_ctx& rt, const Checkpoint& ck, std::vector<uint64_t>& state, std::vector<int64_t>& frame) {
  const auto read_words = [&](const char* file, void* data, size_t words) {
    std::ifstream f(ck.path / file, std::ios::binary);
    return f.read(static_cast<char*>(data), static_cast<std::streamsize>(words * 8)) && f.peek() == EOF;
  };
  if (!read_words("state.bin", state.data(), rt.state_words) || !read_words("frame.bin", frame.data(), rt.frame_slots)) {
    return false;
  }
  rt.resume      = ck.cycle;
  rt.resume_stmt = ck.stmt;
  rt.tick_index  = ck.tick_index;
  rt.period      = ck.period;
  if (!rt.replaying) {
    *rt.first = ck.first;
  }
  rt.pool = ck.pool;
  rt.interned.clear();
  for (size_t k = 0; k < rt.pool.size(); ++k) {
    rt.interned.emplace(rt.pool[k], static_cast<int64_t>(k + 1));
  }
  return true;
}

void reap_checkpoint(Rt_ctx& rt) {
  if (rt.child > 0) {
    int   status = 0;
    pid_t reaped;
    do {
      reaped = ::waitpid(rt.child, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    if (reaped < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      std::fprintf(stderr, "lhd sim: checkpoint write failed in %s\n", rt.test_dir.c_str());
    }
    rt.child = -1;
  }
  if (rt.ckpt.enabled && rt.wrote_checkpoint && rt.ckpt.max > 0) {
    const auto      all = list_checkpoints(rt.test_dir);
    std::error_code ec;
    for (size_t i = 0; i + static_cast<size_t>(rt.ckpt.max) < all.size(); ++i) {
      std::filesystem::remove_all(all[i].second, ec);
    }
  }
}

void checkpoint_cb(prp_tb::Rt* base, uint64_t* state, int64_t cycle, int64_t stmt) {
  auto* rt              = static_cast<Rt_ctx*>(base);
  rt->ckpt_left         = poll_period(rt->ckpt);
  const auto tick_index = rt->legacy ? cycle : rt->tick_index;
  if (!rt->ckpt.enabled) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (rt->ckpt.every > 0) {
    if (cycle == 0) {
      return;
    }
  } else if (std::chrono::duration<double>(now - rt->last_ckpt).count() < rt->interval) {
    return;
  }
  reap_checkpoint(*rt);
  const std::string dir = rt->test_dir + "/c" + std::to_string(cycle);
  std::error_code   ec;
  std::filesystem::create_directories(rt->test_dir, ec);
  rt->wrote_checkpoint = true;
  const pid_t pid      = ::fork();
  if (pid == 0) {  // the child owns a copy-on-write snapshot: the parent keeps simulating
    ::_exit(write_checkpoint_files(*rt, dir, state, cycle, stmt, tick_index) ? 0 : 1);
  }
  if (pid < 0) {
    if (!write_checkpoint_files(*rt, dir, state, cycle, stmt, tick_index)) {
      std::fprintf(stderr, "lhd sim: checkpoint write failed in %s\n", dir.c_str());
    }
  } else {
    rt->child = pid;
  }
  const double cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - now).count();
  rt->last_ckpt     = std::chrono::steady_clock::now();
  rt->interval      = std::max(rt->ckpt.min_secs, rt->ckpt.max_overhead > 0.0 ? cost / rt->ckpt.max_overhead : rt->ckpt.min_secs);
}

// ---- VCD sampling: one scope per instance, dotted field names as nested scopes, edge-aligned ----------
std::string vcd_value(const Field& f, const uint64_t* st) {
  std::string v;
  if (f.bits <= 1) {
    return (st[f.word] & 1) != 0 ? "1" : "0";
  }
  v.reserve(f.bits + 1);
  v.push_back('b');
  for (uint32_t b = f.bits; b-- > 0;) {
    v.push_back(((st[f.word + b / 64] >> (b % 64)) & 1) != 0 ? '1' : '0');
  }
  return v;
}

void vcd_begin(Vcd_out& v, const std::string& scope, const Dut& d) {
  v.out << "$date lhd sim $end\n$version lhd_simrt replay $end\n$timescale 1ns $end\n";
  v.out << "$scope module " << scope << " $end\n";
  size_t n = 0;
  v.clock_ids.clear();
  for (const auto& ck : d.clocks) {
    v.clock_ids.push_back(vcd_id(n));
    v.out << "$var wire 1 " << vcd_id(n++) << " " << ck << " $end\n";
  }
  if (d.clocks.empty()) {
    v.clock_ids.push_back(vcd_id(n));
    v.out << "$var wire 1 " << vcd_id(n++) << " clock $end\n";
  }
  // fields in name order so dotted names nest cleanly
  std::vector<size_t> order;
  for (size_t i = 0; i < d.fields.size(); ++i) {
    if (d.fields[i].kind != 3 && !d.fields[i].name.starts_with("__unknown")) {
      order.push_back(i);
    }
  }
  std::vector<std::string> open;
  for (const size_t i : order) {
    const auto&              f = d.fields[i];
    std::vector<std::string> parts;
    for (size_t pos = 0;;) {
      const auto dot = f.name.find('.', pos);
      parts.push_back(f.name.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos));
      if (dot == std::string::npos) {
        break;
      }
      pos = dot + 1;
    }
    const std::string leaf = parts.back();
    parts.pop_back();
    size_t common = 0;
    while (common < open.size() && common < parts.size() && open[common] == parts[common]) {
      ++common;
    }
    while (open.size() > common) {
      v.out << "$upscope $end\n";
      open.pop_back();
    }
    for (size_t k = common; k < parts.size(); ++k) {
      v.out << "$scope module " << parts[k] << " $end\n";
      open.push_back(parts[k]);
    }
    const std::string id = vcd_id(n++);
    v.out << "$var wire " << std::max<uint32_t>(f.bits, 1) << " " << id << " " << leaf;
    if (f.bits > 1) {
      v.out << "[" << f.bits - 1 << ":0]";
    }
    v.out << " $end\n";
    v.vars.push_back({i, id});
  }
  while (!open.empty()) {
    v.out << "$upscope $end\n";
    open.pop_back();
  }
  v.out << "$upscope $end\n$enddefinitions $end\n";
  v.last.assign(v.vars.size(), "");
  v.started = true;
}

void vcd_sample(Vcd_out& v, const std::string& scope, const Dut& d, const uint64_t* st, uint64_t t) {
  const bool first = !v.started;
  if (first) {
    vcd_begin(v, scope, d);
  }
  // A period is 10 time units: the clock rises at t, the settled values are
  // recorded there, then the clock falls at t + 5.
  v.out << "#" << t << "\n";
  if (first) {
    v.out << "$dumpvars\n";
  }
  for (const auto& id : v.clock_ids) {
    v.out << "1" << id << "\n";
  }
  for (size_t k = 0; k < v.vars.size(); ++k) {
    const auto& f   = d.fields[v.vars[k].field];
    auto        val = vcd_value(f, st);
    if (val != v.last[k]) {
      v.out << val << (f.bits > 1 ? " " : "") << v.vars[k].id << "\n";
      v.last[k] = std::move(val);
    }
  }
  if (first) {
    v.out << "$end\n";
  }
  v.out << "#" << t + 5 << "\n";
  for (const auto& id : v.clock_ids) {
    v.out << "0" << id << "\n";
  }
}

std::string sample_hex(const Field& f, const uint64_t* state, int64_t index) {
  uint64_t offset = f.word * 64;
  if (index >= 0) {
    offset += static_cast<uint64_t>(index) * (packed(f) ? f.bits : store_bits(f));
  }
  std::string hex((f.bits + 3) / 4, '0');
  for (size_t i = 0; i < hex.size(); ++i) {
    const uint64_t at    = offset + i * 4;
    uint64_t       digit = state[at / 64] >> (at % 64);
    if (at % 64 > 60 && i * 4 + 64 - at % 64 < f.bits) {
      digit |= state[at / 64 + 1] << (64 - at % 64);
    }
    const unsigned bits     = std::min<unsigned>(4, f.bits - static_cast<unsigned>(i * 4));
    hex[hex.size() - i - 1] = "0123456789abcdef"[digit & ((1u << bits) - 1)];
  }
  return hex;
}

void trace_cb(prp_tb::Rt* base, uint64_t* state, uint32_t before) {
  auto& rt      = *static_cast<Rt_ctx*>(base);
  auto& capture = *rt.capture;
  if (rt.cycle > capture.to) {
    rt.stop = 1;
    return;
  }
  if (rt.cycle < capture.from) {
    return;
  }
  const bool collect = (capture.reducer != nullptr || capture.samples != nullptr) && capture.observer->wants_sample(rt.cycle);
  if (before != 0) {
    if (collect) {
      if (capture.sample_cycle != rt.cycle) {
        capture.flush_row();
        capture.sample_cycle = rt.cycle;
      }
      // Observe pre-edge outputs on a private copy. Settle uses the just-poked
      // inputs, and its activity bookkeeping must not perturb the replay.
      for (size_t inst = 0; inst < capture.outputs.size(); ++inst) {
        if (capture.outputs[inst].empty()) {
          continue;
        }
        const auto* st = state + rt.inst_bases[inst];
        capture.scratch.assign(st, st + rt.inst_duts[inst]->words + 1);
        (*capture.settle)[inst](capture.scratch.data());
        for (const size_t col : capture.outputs[inst]) {
          const auto signal = capture.observer->columns()[col].signal;
          capture.row[col]  = sample_hex(*(*capture.fields)[signal].field, capture.scratch.data(), -1);
        }
      }
    }
    return;
  }
  if (collect) {
    for (size_t i = 0; i < capture.row.size(); ++i) {
      const auto column = capture.observer->columns()[i];
      const auto f      = (*capture.fields)[column.signal];
      if (f.field->kind != 1) {
        capture.row[i] = sample_hex(*f.field, state + rt.inst_bases[f.instance], column.index);
      }
    }
  }
  if (rt.cycle >= capture.vcd_from && rt.cycle <= capture.vcd_to) {
    for (size_t i = 0; i < capture.vcds.size(); ++i) {
      vcd_sample(capture.vcds[i],
                 (*capture.scopes)[i],
                 *rt.inst_duts[i],
                 state + rt.inst_bases[i],
                 static_cast<uint64_t>(rt.period) * 10);
    }
  }
}

// ---- arbitrary-width integers from 64-bit words (two's complement, `bits` wide) ----------------------
bool word_bit(const uint64_t* w, unsigned bit) { return ((w[bit / 64] >> (bit % 64)) & 1) != 0; }

using livehd::wide_decimal;

// A hole's text: `[width]` is the minimum digit count (zero padded), `s` groups digits with `_`.
std::string render_hole(const Print_seg& g, const uint64_t* w) {
  std::string digits;
  if (g.base == 'd') {
    std::string d   = wide_decimal(w, g.bits);
    const bool  neg = !d.empty() && d[0] == '-';
    digits          = neg ? d.substr(1) : d;
    while (digits.size() < g.width) {
      digits.insert(digits.begin(), '0');
    }
    if (g.sep) {
      std::string grouped;
      for (size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) {
          grouped += '_';
        }
        grouped += digits[i];
      }
      digits = grouped;
    }
    return neg ? "-" + digits : digits;
  }
  // hex / binary: the two's-complement bits of the value
  const unsigned per  = g.base == 'b' ? 1 : 4;
  const char*    set  = g.base == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
  const unsigned nd   = (g.bits + per - 1) / per;
  std::string    all;
  for (unsigned d = nd; d-- > 0;) {
    unsigned val = 0;
    for (unsigned b = 0; b < per; ++b) {
      const unsigned bit = d * per + b;
      if (bit < g.bits && word_bit(w, bit)) {
        val |= 1u << b;
      }
    }
    all.push_back(set[val]);
  }
  const auto nz = all.find_first_not_of('0');
  digits        = nz == std::string::npos ? "0" : all.substr(nz);
  while (digits.size() < g.width) {
    digits.insert(digits.begin(), '0');
  }
  if (g.sep) {
    std::string grouped;
    for (size_t i = 0; i < digits.size(); ++i) {
      if (i != 0 && (digits.size() - i) % 4 == 0) {
        grouped += '_';
      }
      grouped += digits[i];
    }
    digits = grouped;
  }
  return digits;
}

// A site's text: its literal segments and its holes rendered from the value words (`s`: an interned string).
std::string format_site(const Rt_ctx& rt, const Print_site& s, const uint64_t* words) {
  std::string line;
  size_t      off = 0;
  for (const auto& g : s.segs) {
    if (!g.hole) {
      line += g.text;
      continue;
    }
    if (g.base == 's') {
      const auto id = static_cast<int64_t>(words[off]);
      line += id >= 1 && static_cast<size_t>(id) <= rt.pool.size() ? rt.pool[static_cast<size_t>(id) - 1] : std::string{};
    } else {
      line += render_hole(g, words + off);
    }
    off += (g.bits + 63) / 64;
  }
  return line;
}

void print_cb(prp_tb::Rt* base, uint32_t site, const uint64_t* words) {
  auto*       rt   = static_cast<Rt_ctx*>(base);
  if (rt->replaying) {
    return;
  }
  const auto& s    = (*rt->prints)[site];
  std::string line = s.prefix + format_site(*rt, s, words);
  if (s.newline) {
    line += '\n';
  }
  std::fwrite(line.data(), 1, line.size(), stdout);
}

// A string value: formatted now, interned so equal text has an equal handle (a comparison is an integer compare).
int64_t str_cb(prp_tb::Rt* base, uint32_t site, const uint64_t* words) {
  auto*       rt   = static_cast<Rt_ctx*>(base);
  const auto  text = format_site(*rt, (*rt->strings)[site], words);
  const auto [it, fresh] = rt->interned.try_emplace(text, 0);
  if (fresh) {
    rt->pool.push_back(text);
    it->second = static_cast<int64_t>(rt->pool.size());
  }
  return it->second;
}

int64_t intern(Rt_ctx& rt, const std::string& text) {
  const auto [it, fresh] = rt.interned.try_emplace(text, 0);
  if (fresh) {
    rt.pool.push_back(text);
    it->second = static_cast<int64_t>(rt.pool.size());
  }
  return it->second;
}

// `std.testplusarg` / `std.valueplusarg` (mode 0 presence, 1 integer, 2 string, 3 Bool): first occurrence of a `+key` wins,
// a bare `+key` carries no value, and the first problem is the test's error.
int64_t plusarg_cb(prp_tb::Rt* base, int64_t mode, int64_t key_handle, int64_t fallback, int64_t has_default) {
  auto*       rt  = static_cast<Rt_ctx*>(base);
  const auto& key = rt->pool[static_cast<size_t>(key_handle - 1)];
  rt->consumed->insert(key);
  const auto it = rt->plus->find(key);
  if (mode == 0) {
    return it != rt->plus->end() ? 1 : 0;
  }
  const auto fail = [&](const std::string& why) -> int64_t {
    if (rt->plusarg_err.empty()) {
      rt->plusarg_err = why;
    }
    return 0;
  };
  const bool bare = rt->flags->contains(key);
  if (it == rt->plus->end()) {
    if (has_default == 0) {
      return fail("missing simulation argument +" + key + "=VALUE");
    }
    return fallback;  // an integer, or the interned default string
  }
  if (bare) {
    return mode == 3 ? 1 : fail("+" + key + " requires a value");
  }
  if (mode == 2) {
    return intern(*rt, it->second);
  }
  if (mode == 3) {
    if (it->second == "true" || it->second == "1") {
      return 1;
    }
    if (it->second == "false" || it->second == "0") {
      return 0;
    }
    return fail("+" + key + " expects true, false, 1 or 0");
  }
  const auto value = parse_int(it->second);
  if (!value || !value->isSignedIntN(64)) {
    return fail("+" + key + " expects an integer, got '" + it->second + "'");
  }
  return value->getSExtValue();
}

// An edge kernel flagged an undefined access in instance `inst`: report each site ONCE (what and where), then clear its
// word. The power-on settle runs no edge kernel, so the evaluation before the testbench drives anything never warns.
void undefined_cb(prp_tb::Rt* base, uint64_t* state, uint32_t inst) {
  auto*       rt  = static_cast<Rt_ctx*>(base);
  const auto& d   = *rt->inst_duts[inst];
  uint64_t*   st  = state + rt->inst_bases[inst];
  for (const auto& u : d.undefined) {
    if (st[u.word] == 0) {
      continue;
    }
    st[u.word] = 0;
    if (!rt->warn_undefined || !rt->reported.insert({u.what, u.where}).second) {
      continue;
    }
    char when[40] = "";
    if (rt->cycle >= 0) {
      std::snprintf(when, sizeof when, " (cycle %lld)", static_cast<long long>(rt->cycle));
    }
    std::fprintf(stderr,
                 "lhd sim: warning: %s at %s%s: the value is undefined; later occurrences at this site are not reported "
                 "(--set sim.warn_undefined=false silences)\n",
                 u.what.c_str(), u.where.c_str(), when);
  }
}

void assert_fail(prp_tb::Rt* base, uint32_t site, int64_t cycle, const uint64_t* lhs, const uint64_t* rhs) {
  auto*       rt = static_cast<Rt_ctx*>(base);
  if (rt->replaying) {
    return;
  }
  const auto& s  = (*rt->sites)[site];
  if (s.cmp) {
    std::printf("%s:assert fail: clock=%ld: %s%s%s %s %s%s%s", s.loc.c_str(), static_cast<long>(cycle), s.lhs.c_str(), s.lhs_lit ? "" : "=",
                s.lhs_lit ? "" : wide_decimal(lhs, s.lhs_bits).c_str(), s.op.c_str(), s.rhs.c_str(), s.rhs_lit ? "" : "=",
                s.rhs_lit ? "" : wide_decimal(rhs, s.rhs_bits).c_str());
  } else {
    std::printf("%s:assert fail: clock=%ld: condition `%s` is false", s.loc.c_str(), static_cast<long>(cycle), s.cond.c_str());
  }
  if (!s.msg.empty()) {
    std::printf("  [%s]", s.msg.c_str());
  }
  std::printf("\n");
  if (!rt->first->has) {
    rt->first->has   = true;
    rt->first->cycle = cycle;
    rt->first->cond  = s.cond;
    rt->first->file  = rt->file;
    rt->first->line  = s.line;
    rt->first->msg   = s.msg;
    rt->first->loc   = s.loc;
  }
}

// The value of a `?` literal for this run: known bits, each `?` bit drawn from (seed, the literal's key) alone --
// never from a shared stream, so which literal is touched first cannot change another's value -- or 0 under
// sim.unknown_zero. Called once per site per test start.
void unknown_cb(prp_tb::Rt* base, uint32_t site, uint64_t* out) {
  auto*       rt = static_cast<Rt_ctx*>(base);
  const auto& u  = (*rt->unknowns)[site];
  const size_t n = (u.bits + 63) / 64;
  for (size_t w = 0; w < n; ++w) {
    out[w] = u.known[w];
  }
  if (!rt->unknown_zero) {
    uint64_t s = rt->seed ^ (u.key * 0x9E3779B97F4A7C15ULL);
    for (unsigned bit = 0; bit < u.bits; ++bit) {
      if (!word_bit(u.mask.data(), bit)) {
        continue;
      }
      s += 0x9E3779B97F4A7C15ULL;
      uint64_t r = s;
      r          = (r ^ (r >> 30)) * 0xBF58476D1CE4E5B9ULL;
      r          = (r ^ (r >> 27)) * 0x94D049BB133111EBULL;
      r ^= r >> 31;
      if ((r & 1) != 0) {
        out[bit / 64] |= uint64_t{1} << (bit % 64);
      }
    }
  }
  // a signed literal's last word is sign-extended
  if (u.sign && u.bits % 64 != 0 && word_bit(out, u.bits - 1)) {
    out[n - 1] |= ~((uint64_t{1} << (u.bits % 64)) - 1);
  }
}

struct Replay_config {
  obs::Request request;
  std::string  vcd_out, results, debug_json, query_json;
  int64_t      vcd_from = 0, vcd_to = -1;
  unsigned     jobs = 1;
  bool         requested() const {
    return request.list_signals || !request.probe.empty() || !request.break_when.empty() || !request.query.empty()
           || !vcd_out.empty();
  }
};

struct Signal_catalog {
  obs::Catalog                catalog;
  std::vector<Observed_field> fields;
  std::vector<std::string>    scopes;
};

Signal_catalog make_catalog(const std::string& name, const rapidjson::Value& test, const std::map<std::string, Dut>& duts) {
  Signal_catalog out;
  out.catalog.test  = name;
  out.catalog.clock = "clock";
  std::set<std::string> names;
  size_t                inst = 0;
  for (const auto& in : test["instances"].GetArray()) {
    const auto&       dut   = duts.at(in["stem"].GetString());
    const std::string scope = in["var"].GetString();
    out.scopes.push_back(scope);
    // Keep register names when an output and a register have the same name.
    for (const int kind : {2, 0, 1, 3}) {
      for (const auto& f : dut.fields) {
        if (f.kind != kind || f.bits == 0 || f.name.starts_with("__unknown")) {
          continue;
        }
        const auto full = scope + "." + f.name;
        if (!names.insert(full).second) {
          continue;
        }
        const auto  alias = kind == 0 ? scope + ".__in." + f.name : std::string{};
        std::string type  = kind == 0 ? "input" : kind == 1 ? "output" : kind == 2 ? "flop" : "memory";
        out.catalog.sigs.push_back(
            {full, alias, std::move(type), f.bits, f.bits, kind == 3 ? static_cast<long>(f.entries) : -1, f.sign});
        out.fields.push_back({&f, inst});
      }
    }
    ++inst;
  }
  return out;
}

void write_replay_json(const std::string& path, const std::string& json) {
  if (!path.empty() && !json.empty()) {
    std::ofstream out(path);
    out << json << '\n';
    out.close();
    if (!out) {
      throw std::runtime_error("could not write " + path);
    }
  }
}

void publish_observation(const Replay_config& config, const obs::Observer& observer) {
  write_replay_json(config.debug_json, observer.debug_json());
  write_replay_json(config.query_json, observer.query_json());
  if (const auto summary = observer.break_summary(); !summary.empty()) {
    std::printf("%s\n", summary.c_str());
  }
}

struct Replay_run {
  int64_t last    = std::numeric_limits<int64_t>::max();
  int64_t failure = -1;
};

Replay_run replay_run(const std::string& results, const std::string& test) {
  if (results.empty()) {
    return {};
  }
  rapidjson::Document d;
  d.Parse(slurp(results).c_str());
  if (d.IsArray()) {
    for (const auto& t : d.GetArray()) {
      if (t["test"].GetString() == test) {
        return {t["sim_cycles"].GetInt64() - 1, t.HasMember("cycle") ? t["cycle"].GetInt64() : -1};
      }
    }
  }
  throw std::runtime_error("no recorded simulation result for " + test);
}

struct Replay_interval {
  const Checkpoint* checkpoint = nullptr;
  int64_t           from, to;
};

std::vector<Replay_interval> replay_intervals(const std::vector<Checkpoint>& checkpoints, const std::vector<obs::Range>& wants,
                                              int64_t last) {
  std::vector<Replay_interval> intervals;
  const auto                   add = [&](const Checkpoint* checkpoint, int64_t lo, int64_t hi) {
    int64_t from = -1, to = -1;
    for (const auto& range : wants) {
      const int64_t start = std::max(lo, range.from);
      const int64_t end   = std::min(hi, range.to);
      if (start <= end) {
        from = from < 0 ? start : std::min(from, start);
        to   = std::max(to, end);
      }
    }
    if (from >= 0) {
      intervals.push_back({checkpoint, from, to});
    }
  };
  const Checkpoint* prev = nullptr;
  for (const auto& next : checkpoints) {
    if (next.cycle > last) {
      break;
    }
    add(prev, prev == nullptr ? 0 : prev->cycle, next.cycle - 1);
    prev = &next;
  }
  add(prev, prev == nullptr ? 0 : prev->cycle, last);
  return intervals;
}

struct Replay_directory {
  fs::path path;
  Replay_directory() {
    const auto        spec = (fs::temp_directory_path() / "lhd_replay_XXXXXX").string();
    std::vector<char> buf(spec.c_str(), spec.c_str() + spec.size() + 1);
    if (::mkdtemp(buf.data()) == nullptr) {
      throw std::runtime_error("could not create a temporary replay directory");
    }
    path = buf.data();
  }
  ~Replay_directory() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

// Every segment starts with a full sample. Keep the first header and dumpvars,
// then omit re-stating values that already hold at the next checkpoint boundary.
struct Vcd_merge {
  fs::path                           path;
  fs::path                           temporary;
  std::ofstream                      out;
  std::map<std::string, std::string> values;
  bool                               header = false;
  Vcd_merge(const fs::path& target, std::string_view suffix)
      : path(target), temporary(target.string() + "." + std::string(suffix)), out(temporary) {
    if (!out) {
      throw std::runtime_error("could not open " + temporary.string());
    }
  }
  ~Vcd_merge() {
    std::error_code ec;
    fs::remove(temporary, ec);
  }

  void append(const fs::path& part) {
    std::ifstream in(part);
    if (!in) {
      throw std::runtime_error("could not read " + part.string());
    }
    bool       definitions = true;
    const bool first       = !header;
    for (std::string line; std::getline(in, line);) {
      if (definitions) {
        if (first) {
          out << line << '\n';
        }
        if (line == "$enddefinitions $end") {
          definitions = false;
          header      = true;
        }
      } else if (line.starts_with('#') || line.starts_with('$')) {
        if (first || !line.starts_with('$')) {
          out << line << '\n';
        }
      } else {
        const auto split = line.starts_with('b') ? line.find(' ') + 1 : 1;
        auto&      value = values[line.substr(split)];
        if (value != line) {
          out << line << '\n';
          value = std::move(line);
        }
      }
    }
    if (!in.eof()) {
      throw std::runtime_error("could not read " + part.string());
    }
  }

  void finish() {
    out.close();
    if (!out) {
      throw std::runtime_error("could not write " + path.string());
    }
    fs::rename(temporary, path);
  }
};

void run_replay(const Replay_config& config, const Signal_catalog& catalog, const Rt_ctx& original,
                const std::vector<uint64_t>& initial_state, const std::vector<int64_t>& args, const std::vector<Settle>& settle,
                Test_fn run) {
  const auto    recorded = replay_run(config.results, catalog.catalog.test);
  obs::Observer observer(catalog.catalog, config.request, recorded.failure, original.seed);
  auto          wants = observer.ranges();
  if (!config.vcd_out.empty()) {
    wants.push_back({config.vcd_from, config.vcd_to < 0 ? std::numeric_limits<int64_t>::max() : config.vcd_to});
  }
  if (wants.empty()) {
    publish_observation(config, observer);
    return;
  }
  std::string   error;
  const int64_t last_wanted = std::min(recorded.last, std::ranges::max(wants, {}, &obs::Range::to).to);
  const auto    checkpoints
      = last_wanted < 0 ? std::vector<Checkpoint>{} : compatible_checkpoints(original, last_wanted, false, false, error);
  const auto                              intervals = replay_intervals(checkpoints, wants, last_wanted);
  const Replay_directory                  scratch;
  std::vector<std::unique_ptr<Vcd_merge>> vcd;
  if (!config.vcd_out.empty()) {
    const fs::path output(config.vcd_out);
    for (const auto& scope : catalog.scopes) {
      const auto path
          = catalog.scopes.size() == 1 ? output : output.parent_path() / (output.stem().string() + "." + scope + ".vcd");
      vcd.push_back(std::make_unique<Vcd_merge>(path, scratch.path.filename().string()));
    }
  }
  const auto prefix = [&](size_t i) { return (scratch.path / std::to_string(i)).string(); };
  struct Replayed {
    int64_t            start  = -1;
    int64_t            cycles = 0;
    std::exception_ptr error;
  };
  const auto replay_interval = [&](size_t index, Replayed& result, obs::Observer* reducer) {
    try {
      const auto& interval = intervals[index];
      auto        rt       = original;
      std::vector<uint64_t> resolved(args.size());
      rt.resolved_params   = resolved.data();
      rt.replaying         = true;
      rt.warn_undefined    = false;
      rt.ckpt.enabled      = false;
      rt.ckpt_left         = int64_t{1} << 60;
      std::set<std::string> consumed;
      rt.consumed = &consumed;
      std::vector<uint64_t> state;
      std::vector<int64_t>  frame(rt.frame_slots + 1, 0);
      rt.frame = frame.data();
      if (interval.checkpoint != nullptr) {
        state.resize(rt.state_words);
        if (!restore_checkpoint(rt, *interval.checkpoint, state, frame)) {
          throw std::runtime_error("could not restore checkpoint " + interval.checkpoint->path.string());
        }
        result.start = interval.checkpoint->cycle;
      } else {
        state = initial_state;
      }

      Replay_capture capture;
      capture.observer = &observer;
      capture.fields   = &catalog.fields;
      capture.settle   = &settle;
      capture.scopes   = &catalog.scopes;
      capture.from     = interval.from;
      capture.to       = interval.to;
      capture.vcd_from = config.vcd_from;
      capture.vcd_to   = config.vcd_to < 0 ? interval.to : config.vcd_to;
      capture.row.resize(observer.columns().size());
      capture.outputs.resize(catalog.scopes.size());
      for (size_t c = 0; c < observer.columns().size(); ++c) {
        const auto field = catalog.fields[observer.columns()[c].signal];
        if (field.field->kind == 1) {
          capture.outputs[field.instance].push_back(c);
        }
      }
      const auto    stem = prefix(index);
      std::ofstream samples;
      if (!observer.columns().empty() && reducer != nullptr) {
        capture.reducer = reducer;
      } else if (!observer.columns().empty()) {
        samples.open(stem + ".samples");
        if (!samples) {
          throw std::runtime_error("could not write replay samples");
        }
        capture.samples = &samples;
      }
      capture.vcds.resize(vcd.size());
      for (size_t i = 0; i < vcd.size(); ++i) {
        auto& out = capture.vcds[i].out;
        out.open(stem + "." + std::to_string(i) + ".vcd");
        if (!out) {
          throw std::runtime_error("could not write a VCD segment");
        }
      }
      rt.capture                = &capture;
      const auto initial_period = rt.period;
      run(&rt, state.data(), args.data());
      result.cycles = std::max<int64_t>(0, std::min(rt.cycle, interval.to) - std::max<int64_t>(0, result.start) + 1);
      if (rt.period != initial_period) {
        capture.flush_row();
      }
      if (samples.is_open()) {
        samples.close();
        if (!samples) {
          throw std::runtime_error("could not write replay samples");
        }
      }
      for (auto& part : capture.vcds) {
        part.out.close();
        if (!part.out) {
          throw std::runtime_error("could not write a VCD segment");
        }
      }
      if (!rt.plusarg_err.empty()) {
        throw std::runtime_error(rt.plusarg_err);
      }
    } catch (...) {
      result.error = std::current_exception();
    }
  };

  // Replay with private state and frame storage. Consume completed segments in
  // cycle order so changes and edge predicates see exactly one neighbor across
  // each boundary, and retire segment files as each batch is merged.
  const size_t jobs = std::max<size_t>(1, config.jobs);
  for (size_t start = 0; start < intervals.size(); start += jobs) {
    const size_t          count = std::min(jobs, intervals.size() - start);
    std::vector<Replayed> results(count);
    {
      std::vector<std::jthread> workers;
      for (size_t i = 1; i < count; ++i) {
        workers.emplace_back([&, i] { replay_interval(start + i, results[i], nullptr); });
      }
      replay_interval(start, results[0], &observer);
    }
    for (size_t i = 0; i < count; ++i) {
      if (results[i].error) {
        std::rethrow_exception(results[i].error);
      }
      observer.replayed(results[i].start, results[i].cycles);
      const auto stem = prefix(start + i);
      if (i != 0 && !observer.columns().empty()) {
        const fs::path file = stem + ".samples";
        std::ifstream  in(file);
        observer.read_samples(in);
        fs::remove(file);
      }
      for (size_t j = 0; j < vcd.size(); ++j) {
        const fs::path file = stem + "." + std::to_string(j) + ".vcd";
        vcd[j]->append(file);
        fs::remove(file);
      }
    }
  }
  for (auto& out : vcd) {
    out->finish();
  }
  publish_observation(config, observer);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: lhd_simrt SIMDIR [driver args]\n");
    return 2;
  }
  const std::string simdir = argv[1];
  std::vector<std::string> only;
  std::string              result_json;
  bool                     list_tests = false, init_zero = false, unknown_zero = false, warn_undefined = true;
  uint64_t                 seed = 1;
  Ckpt_cfg                 ckpt;
  long                     restart_cycle = -1;
  Replay_config replay;
  bool check_observation = false;
  std::map<std::string, std::string> plus;
  std::set<std::string>              plus_flags;  // keys whose first occurrence was bare (`+verbose`)
  std::vector<std::string>           arg_tokens;  // every `+...` as given: order, duplicates, bare flags, empty values
  for (int i = 2; i < argc; ++i) {
    std::string       a = argv[i];
    const auto        next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string{}; };
    if (a == "--list-tests") {
      list_tests = true;
    } else if (a == "--test") {
      only.push_back(next());
    } else if (a == "--seed") {
      seed = std::strtoull(next().c_str(), nullptr, 0);
    } else if (a == "--init-zero") {
      init_zero = true;
    } else if (a == "--result-json") {
      result_json = next();
    } else if (a == "--set") {
      const auto kv = next();
      if (kv == "sim.unknown_zero=true") {
        unknown_zero = true;
      } else if (kv == "sim.warn_undefined=false") {
        warn_undefined = false;
      } else if (kv == "sim.init_zero=true") {
        init_zero = true;
      }
    } else if (a == "--no-checkpoint") {
      ckpt.enabled = false;
      ckpt.dir.clear();
    } else if (a == "--ckpt-dir") {
      ckpt.dir     = next();
      ckpt.enabled = !ckpt.dir.empty();
    } else if (a == "--checkpoint-min-secs") {
      ckpt.min_secs = std::strtod(next().c_str(), nullptr);
    } else if (a == "--checkpoint-max") {
      ckpt.max = std::strtol(next().c_str(), nullptr, 10);
    } else if (a == "--checkpoint-max-overhead") {
      ckpt.max_overhead = std::strtod(next().c_str(), nullptr);
    } else if (a == "--checkpoint-every") {
      ckpt.every = std::strtol(next().c_str(), nullptr, 10);
    } else if (a == "--restart-cycle") {
      restart_cycle = std::strtol(next().c_str(), nullptr, 10);
    } else if (a == "--vcd-out") {
      replay.vcd_out = next();
    } else if (a == "--vcd-from") {
      replay.vcd_from = std::strtoll(next().c_str(), nullptr, 10);
    } else if (a == "--vcd-to") {
      replay.vcd_to = std::strtoll(next().c_str(), nullptr, 10);
    } else if (a == "--debug-json") {
      replay.debug_json = next();
    } else if (a == "--query-json") {
      replay.query_json = next();
    } else if (a == "--query-file") {
      const auto path = next();
      replay.request.query = slurp(path);
      if (replay.request.query.empty()) {
        std::fprintf(stderr, "lhd sim: could not read a query request from %s\n", path.c_str());
        return 2;
      }
    } else if (a == "--probe") {
      replay.request.probe = next();
    } else if (a == "--probe-from") {
      replay.request.probe_from = std::strtol(next().c_str(), nullptr, 10);
    } else if (a == "--probe-to") {
      replay.request.probe_to = std::strtol(next().c_str(), nullptr, 10);
    } else if (a == "--break-when") {
      replay.request.break_when = next();
    } else if (a == "--list-signals") {
      replay.request.list_signals = true;
    } else if (a == "--check-observation") {
      check_observation = true;
    } else if (a == "--replay-results") {
      replay.results = next();
    } else if (a == "--jobs") {
      replay.jobs = static_cast<unsigned>(std::strtoul(next().c_str(), nullptr, 10));
    } else if (a.starts_with("+") || (a.starts_with("--") && a.find('=') != std::string::npos)) {
      // `+name[=value]`, or the old spelling `--name=value`
      if (a.starts_with("--")) {
        a = "+" + a.substr(2);
      }
      const auto eq = a.find('=');
      const auto key = a.substr(1, eq == std::string::npos ? std::string::npos : eq - 1);
      if (key.empty()) {
        std::fprintf(stderr, "lhd sim: expected +name[=value]\n");
        return 2;
      }
      arg_tokens.push_back(a);
      if (plus.emplace(key, eq == std::string::npos ? "" : a.substr(eq + 1)).second && eq == std::string::npos) {
        plus_flags.insert(key);
      }
    }
  }

  const auto          manifest_text = slurp(simdir + "/" + prp_tb::kManifest);
  rapidjson::Document man;
  man.Parse(manifest_text.c_str());
  if (man.HasParseError() || !man.IsObject()) {
    std::fprintf(stderr, "lhd sim: %s/%s is missing or unreadable (run the setup first)\n", simdir.c_str(), prp_tb::kManifest);
    return 2;
  }
  if (list_tests) {
    std::string j     = "{\"file\":\"" + escape(man["file"].GetString()) + "\",\"tests\":[";
    bool        first = true;
    for (const auto& t : man["tests"].GetArray()) {
      j       += std::string(first ? "" : ",") + "{\"name\":\"" + escape(t["name"].GetString()) + "\",\"params\":[";
      first = false;
      bool pf = true;
      for (const auto& p : t["params"].GetArray()) {
        j += std::string(pf ? "" : ",") + "{\"name\":\"" + escape(p["name"].GetString())
             + "\",\"required\":" + (p["required"].GetBool() ? "true" : "false");
        if (!p["required"].GetBool()) {
          j += ",\"default\":\"" + escape(p["default"].GetString()) + "\"";
        }
        j += "}";
        pf = false;
      }
      j += "]}";
    }
    std::printf("%s]}\n", j.c_str());
    return 0;
  }

  const bool legacy      = !man.HasMember("schema_version") || man["schema_version"].GetInt() < 2;
  uint64_t   design_hash = fnv1a64(manifest_text);

  // DUT descriptors and the one JIT dylib.
  std::map<std::string, Dut> duts;
  for (const auto& s : man["duts"].GetArray()) {
    duts.emplace(s.GetString(), load_dut(simdir, s.GetString()));
  }
  std::optional<Signal_catalog> observation;
  if (replay.requested()) {
    const rapidjson::Value* test = nullptr;
    for (const auto& t : man["tests"].GetArray()) {
      if (!only.empty() && std::find(only.begin(), only.end(), t["name"].GetString()) == only.end()) {
        continue;
      }
      if (test != nullptr) {
        std::fprintf(stderr, "lhd sim: observation needs a single test; select one by name\n");
        return 2;
      }
      test = &t;
    }
    if (test == nullptr) {
      std::fprintf(stderr, "lhd sim: no test matched the observation request\n");
      return 2;
    }
    observation = make_catalog((*test)["name"].GetString(), *test, duts);
    try {
      const auto          recorded = replay_run(replay.results, observation->catalog.test);
      const obs::Observer observer(observation->catalog, replay.request, recorded.failure, seed);
      if (check_observation) {
        return 0;
      }
      if (observer.ranges().empty() && replay.vcd_out.empty()) {
        publish_observation(replay, observer);
        return 0;
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "lhd sim: %s\n", e.what());
      return 2;
    }
    if (legacy || !fs::exists(fs::path(simdir) / prp_tb::kTraceObject)) {
      std::fprintf(stderr, "lhd sim: observation needs the current trace object; rerun setup with the observation options\n");
      return 2;
    }
  }
  livehd::sim::Native_objects objects;
  std::string                 error;
  const bool                  hash_design = !legacy && !ckpt.dir.empty();
  if (hash_design) {
    design_hash = fnv1a64(slurp(simdir + "/" + prp_tb::kObject), design_hash);
  }
  for (const auto& [stem, d] : duts) {
    if (hash_design) {
      design_hash = fnv1a64(slurp(simdir + "/" + stem + ".dut.json"), design_hash);
    }
    for (const auto& o : d.objects) {
      if (hash_design) {
        design_hash = fnv1a64(slurp(simdir + "/" + o), design_hash);
      }
      if (!objects.load(simdir + "/" + o, error)) {
        std::fprintf(stderr, "lhd sim: %s\n", error.c_str());
        return 1;
      }
    }
  }
  if (!objects.load(simdir + "/" + (observation ? prp_tb::kTraceObject : prp_tb::kObject), error)) {
    std::fprintf(stderr, "lhd sim: %s\n", error.c_str());
    return 1;
  }

  std::string rj         = "[";
  bool        rj_first   = true;
  long        fail_tests = 0;
  std::set<std::string> consumed;
  for (const auto& t : man["tests"].GetArray()) {
    const std::string name = t["name"].GetString();
    if (!only.empty() && std::find(only.begin(), only.end(), name) == only.end()) {
      continue;
    }
    std::vector<std::string>       pre_pool;  // interned string parameters: a handle is an index + 1 (the run's pool starts as this)
    std::map<std::string, int64_t> pre_interned;
    const auto                     intern_pre = [&](const std::string& text) -> int64_t {
      const auto [it, fresh] = pre_interned.try_emplace(text, 0);
      if (fresh) {
        pre_pool.push_back(text);
        it->second = static_cast<int64_t>(pre_pool.size());
      }
      return it->second;
    };
    std::string err;
    // parameters
    std::string          args_id;  // supplied tokens (length framed) + resolved parameters
    for (const auto& token : arg_tokens) {
      args_id += std::to_string(token.size()) + ":" + token;
    }
    args_id += ";defaults:";
    std::vector<int64_t> args;
    std::vector<std::string> displayed;
    for (const auto& p : t["params"].GetArray()) {
      const std::string pname  = p["name"].GetString();
      const std::string type   = p["type"].GetString();
      const bool        is_str = p.HasMember("string") && p["string"].GetBool();
      const bool        is_bool = type == "Bool";
      const bool        dyn     = p.HasMember("dynamic") && p["dynamic"].GetBool() && !p["required"].GetBool();
      const bool        typed_int
          = type.size() > 1 && (type[0] == 'U' || type[0] == 'S') && type.find_first_not_of("0123456789", 1) == std::string::npos;
      const bool  is_unsigned = typed_int && type[0] == 'U';
      // Version 2 objects take one value word even for wider declarations.
      const unsigned width       = p.HasMember("bits") ? p["bits"].GetUint() : 64;
      if (width == 0 || width > prp_tb::kMaxTbWidth) {
        err = "invalid test parameter width for " + pname;
        break;
      }
      const auto  it       = plus.find(pname);
      const bool  supplied = it != plus.end();
      const bool  bare     = supplied && plus_flags.contains(pname);
      std::string shown    = supplied ? it->second : p["default"].GetString();
      llvm::APInt value(64, 0);
      if (supplied) {
        consumed.insert(pname);
      }
      if (!supplied && p["required"].GetBool()) {
        err = "test parameter '" + pname + "' is required: pass +" + pname + "=VALUE";
        break;
      }
      if (bare && !is_bool) {
        err = "+" + pname + " requires a value";
        break;
      }
      if (!supplied && dyn) {
        // The compiled test evaluates this expression using the previous bindings.
      } else if (is_str) {
        if (!supplied) {
          if (p.HasMember("string_default") && p["string_default"].IsString()) {
            shown.assign(p["string_default"].GetString(), p["string_default"].GetStringLength());
          } else if (shown.size() >= 2) {
            shown = shown.substr(1, shown.size() - 2);  // older object/manifest
          }
        }
        value = intern_pre(shown);
      } else if (is_bool) {
        if (bare) {
          shown = "true";
        }
        if (shown == "true" || shown == "1") {
          value = 1;
        } else if (shown != "false" && shown != "0") {
          err = "+" + pname + " expects true, false, 1 or 0";
          break;
        }
      } else {
        auto integer = parse_int(shown);
        if (!integer) {
          err = "+" + pname + " expects an integer, got '" + shown + "'";
          break;
        }
        const unsigned range = typed_int ? static_cast<unsigned>(std::strtoul(type.c_str() + 1, nullptr, 10)) : width;
        if (is_unsigned ? integer->isNegative() || !integer->isIntN(range) : !integer->isSignedIntN(range)) {
          err = "+" + pname + " is outside " + (type.empty() ? "signed " + std::to_string(width) + "-bit" : type) + " range";
          break;
        }
        if (!p.HasMember("bits") && !(is_unsigned ? integer->isIntN(64) : integer->isSignedIntN(64))) {
          err = "+" + pname + " requires rerunning simulation setup for wide arguments";
          break;
        }
        value = std::move(*integer);
      }
      const auto words = (width + 63) / 64;
      value            = value.sextOrTrunc(words * 64);
      args.insert(args.end(), value.getRawData(), value.getRawData() + words);
      args.push_back(supplied);
      args_id     += std::to_string(pname.size()) + ":" + pname + std::to_string(shown.size()) + ":" + shown;
      displayed.push_back(std::move(shown));
    }

    long      fails = -1;
    Fail_info first;
    uint64_t  sim_cycles = 0;
    const auto t0       = std::chrono::steady_clock::now();
    if (err.empty()) {
      // state: each instance's words end to end, powered on and settled
      std::vector<uint64_t> state(t["state_words"].GetUint64() + 1, 0);
      std::vector<int64_t>  frame(t["frame_words"].GetUint64() + 1, 0);
      std::vector<Settle> settle_functions;
      for (const auto& in : t["instances"].GetArray()) {
        const auto& d    = duts.at(in["stem"].GetString());
        auto*       base = state.data() + in["base"].GetUint64();
        try {
          power_on(base, d, init_zero, unknown_zero, seed);
          run_sim_init(d, arg_tokens, consumed, observation.has_value());
        } catch (const std::exception& ex) {
          err = ex.what();  // e.g. `readmem: <file>: cannot open image`
          break;
        }
        std::string e1;
        std::string sym;
        for (const char c : d.stem) {
          sym += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
        }
        auto dirty  = objects.lookup("sim_dirty_" + sym, e1);
        auto settle = objects.lookup("sim_settle_" + sym, e1);
        if (dirty == nullptr || settle == nullptr) {
          err = e1;
          break;
        }
        reinterpret_cast<void (*)(uint64_t*)>(reinterpret_cast<void*>(dirty))(base);
        settle_functions.push_back(reinterpret_cast<Settle>(reinterpret_cast<void*>(settle)));
        settle_functions.back()(base);
      }
      if (err.empty()) {
        std::vector<Site> sites;
        for (const auto& a : t["asserts"].GetArray()) {
          Site s;
          s.line    = a["line"].GetInt();
          s.loc     = a["loc"].GetString();
          s.cond    = a["cond"].GetString();
          s.lhs     = a["lhs"].GetString();
          s.op      = a["op"].GetString();
          s.rhs     = a["rhs"].GetString();
          s.msg     = a["msg"].GetString();
          s.cmp     = a["cmp"].GetBool();
          s.lhs_lit = a["lhs_lit"].GetBool();
          s.rhs_lit = a["rhs_lit"].GetBool();
          s.lhs_bits = a["lhs_bits"].GetUint();
          s.rhs_bits = a["rhs_bits"].GetUint();
          sites.push_back(std::move(s));
        }
        std::vector<Print_site> prints;
        for (const auto& a : t["prints"].GetArray()) {
          Print_site ps;
          ps.prefix  = a["prefix"].GetString();
          ps.newline = a["newline"].GetBool();
          for (const auto& g : a["segs"].GetArray()) {
            Print_seg sg;
            sg.hole  = g["hole"].GetBool();
            sg.text.assign(g["text"].GetString(), g["text"].GetStringLength());
            sg.base  = g["base"].GetString()[0];
            sg.width = g["width"].GetUint64();
            sg.sep   = g["sep"].GetBool();
            sg.bits  = g["bits"].GetUint();
            ps.segs.push_back(std::move(sg));
          }
          prints.push_back(std::move(ps));
        }
        std::vector<Print_site> strings;
        for (const auto& a : t["strings"].GetArray()) {
          Print_site ps;
          for (const auto& g : a["segs"].GetArray()) {
            Print_seg sg;
            sg.hole  = g["hole"].GetBool();
            sg.text.assign(g["text"].GetString(), g["text"].GetStringLength());
            sg.base  = g["base"].GetString()[0];
            sg.width = g["width"].GetUint64();
            sg.sep   = g["sep"].GetBool();
            sg.bits  = g["bits"].GetUint();
            ps.segs.push_back(std::move(sg));
          }
          strings.push_back(std::move(ps));
        }
        std::vector<Unknown_site> unknowns;
        for (const auto& a : t["unknowns"].GetArray()) {
          Unknown_site u;
          u.bits = a["bits"].GetUint();
          u.sign = a["sign"].GetBool();
          u.key  = a["key"].GetUint64();
          const auto words = [](const std::string& csv) {
            std::vector<uint64_t> out;
            for (size_t pos = 0; pos <= csv.size();) {
              const auto comma = csv.find(',', pos);
              out.push_back(std::strtoull(csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos).c_str(), nullptr, 16));
              if (comma == std::string::npos) {
                break;
              }
              pos = comma + 1;
            }
            return out;
          };
          u.known = words(a["known"].GetString());
          u.mask  = words(a["mask"].GetString());
          u.known.resize((u.bits + 63) / 64 + 1, 0);
          u.mask.resize((u.bits + 63) / 64 + 1, 0);
          unknowns.push_back(std::move(u));
        }
        std::string e2;
        auto        fn = objects.lookup(t["symbol"].GetString(), e2);
        if (fn == nullptr) {
          err = e2;
        } else {
          Rt_ctx rt{};
          rt.first       = &first;
          rt.legacy      = legacy;
          rt.assert_fail = assert_fail;
          rt.print       = print_cb;
          rt.unknown     = unknown_cb;
          rt.str         = str_cb;
          rt.undefined   = undefined_cb;
          rt.plusarg     = plusarg_cb;
          rt.pool        = pre_pool;
          for (size_t h = 0; h < pre_pool.size(); ++h) {
            rt.interned.emplace(pre_pool[h], static_cast<int64_t>(h + 1));
          }
          rt.plus           = &plus;
          rt.flags          = &plus_flags;
          rt.consumed       = &consumed;
          rt.args_id        = args_id;
          rt.warn_undefined = warn_undefined;
          rt.strings        = &strings;
          rt.unknowns       = &unknowns;
          rt.seed           = seed;
          rt.unknown_zero   = unknown_zero;
          rt.checkpoint     = checkpoint_cb;
          rt.trace          = trace_cb;
          rt.prints         = &prints;
          rt.user           = nullptr;
          rt.cycle          = -1;
          rt.resume         = -1;
          rt.resume_stmt    = -1;
          rt.stop           = 0;
          rt.frame          = frame.data();
          // checkpoints: <dir>/<test>/c<cycle>/{state.bin,frame.bin,meta.json,complete}
          rt.ckpt           = ckpt;
          rt.ckpt.enabled   = ckpt.enabled && !observation;
          std::string safe;
          for (const char c : name) {
            safe += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
          }
          rt.test_dir       = ckpt.dir + "/" + safe;
          rt.state_words    = state.size();
          rt.frame_slots    = t["frame_words"].GetUint64();
          rt.interval       = ckpt.min_secs;
          uint64_t run_hash = design_hash;
          if (hash_design) {
            run_hash = fnv1a64(name, run_hash);
            run_hash = fnv1a64({reinterpret_cast<const char*>(state.data()), state.size() * sizeof(uint64_t)}, run_hash);
            run_hash = fnv1a64(std::to_string(seed) + (init_zero ? ":init0" : ":init") + (unknown_zero ? ":x0" : ":x"), run_hash);
          }
          rt.design_id = std::to_string(run_hash);
          rt.ckpt_left = rt.ckpt.enabled ? poll_period(ckpt) : (int64_t{1} << 60);
          if (restart_cycle >= 0) {
            const auto checkpoints = compatible_checkpoints(rt, restart_cycle, legacy, true, err);
            if (!checkpoints.empty()) {
              const auto& best = checkpoints.back();
              if (restore_checkpoint(rt, best, state, frame)) {
                std::fprintf(stderr,
                             "lhd sim: restarted from checkpoint cycle %lld (target %ld)\n",
                             static_cast<long long>(best.cycle),
                             restart_cycle);
              } else {
                err = "could not restore checkpoint " + best.path.string();
              }
            } else if (err.empty()) {
              std::fprintf(stderr, "lhd sim: no compatible checkpoint <= %ld; replaying from cycle 0\n", restart_cycle);
            }
          }
          for (const auto& in : t["instances"].GetArray()) {
            rt.inst_duts.push_back(&duts.at(in["stem"].GetString()));
            rt.inst_bases.push_back(in["base"].GetUint64());
          }
          rt.sites = &sites;
          rt.file  = man["file"].GetString();
          auto run = reinterpret_cast<Test_fn>(reinterpret_cast<void*>(fn));
          if (observation) {
            try {
              run_replay(replay, *observation, rt, state, args, settle_functions, run);
              return 0;
            } catch (const std::exception& e) {
              std::fprintf(stderr, "lhd sim: %s\n", e.what());
              return 2;
            }
          }
          if (err.empty()) {
            std::vector<uint64_t> resolved(args.size());
            rt.resolved_params = resolved.data();
            fails = run(&rt, state.data(), args.data());
            size_t offset      = 0;
            for (const auto& p : t["params"].GetArray()) {
              if (!p.HasMember("bits")) {
                break;
              }  // older object
              const auto   width = p["bits"].GetUint();
              const size_t words = (width + 63) / 64;
              if (p["dynamic"].GetBool() && !args[offset + words]) {
                auto&       shown = displayed[&p - t["params"].Begin()];
                const auto* data  = resolved.data() + offset;
                if (p["string"].GetBool()) {
                  if (*data > 0 && *data <= rt.pool.size()) {
                    shown = rt.pool[*data - 1];
                  }
                } else if (std::string_view(p["type"].GetString()) == "Bool") {
                  shown = *data ? "true" : "false";
                } else {
                  shown = livehd::wide_decimal(data, width);
                }
              }
              offset += words + 1;
            }
            if (!rt.plusarg_err.empty()) {
              fails = -1;
              err   = rt.plusarg_err;
            }
          }
          sim_cycles     = rt.cycle < 0 ? 0 : static_cast<uint64_t>(rt.cycle) + 1;
          reap_checkpoint(rt);
        }
      }
    }
    std::string params_json = "{";
    for (size_t i = 0; i < displayed.size(); ++i) {
      const auto& p  = t["params"][static_cast<rapidjson::SizeType>(i)];
      params_json   += std::string(i ? "," : "") + "\"" + escape(p["name"].GetString()) + "\":\"" + escape(displayed[i]) + "\"";
    }
    params_json           += "}";
    std::string args_json = "[";
    for (size_t k = 0; k < arg_tokens.size(); ++k) {
      args_json += std::string(k ? "," : "") + "\"" + escape(arg_tokens[k]) + "\"";
    }
    args_json += "]";
    const auto sim_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    const char* status = fails < 0 ? "error" : (fails > 0 ? "fail" : "pass");
    if (fails < 0) {
      std::printf("FAIL %s (%s)\n", name.c_str(), err.c_str());
      ++fail_tests;
    } else if (fails > 0) {
      if (first.has) {
        std::printf("FAIL %s (%ld assert(s) failed; first at %s clock=%ld)\n", name.c_str(), fails, first.loc.c_str(), static_cast<long>(first.cycle));
      } else {
        std::printf("FAIL %s (%ld assert(s) failed)\n", name.c_str(), fails);
      }
      ++fail_tests;
    } else {
      std::printf("PASS %s\n", name.c_str());
    }
    if (!result_json.empty()) {
      rj += rj_first ? "" : ",";
      rj_first = false;
      rj       += "{\"test\":\"" + escape(name) + "\",\"status\":\"" + status + "\",\"arguments\":" + args_json
                  + ",\"parameters\":" + params_json;
      if (fails < 0) {
        rj += ",\"error\":\"" + escape(err) + "\"";
      } else if (fails > 0 && first.has) {
        rj += ",\"cycle\":" + std::to_string(first.cycle) + ",\"failing_assert\":\"" + escape(first.cond) + "\",\"prp_file\":\""
              + escape(first.file) + "\",\"line\":" + std::to_string(first.line);
        if (!first.msg.empty()) {
          rj += ",\"msg\":\"" + escape(first.msg) + "\"";
        }
      }
      rj += ",\"sim_cycles\":" + std::to_string(sim_cycles) + ",\"sim_ns\":" + std::to_string(sim_ns) + "}";
    }
    std::fflush(stdout);
  }
  for (const auto& [k, v] : plus) {
    if (!consumed.contains(k)) {
      std::fprintf(stderr, "lhd sim: warning: +%s=%s matches no test parameter\n", k.c_str(), v.c_str());
    }
  }
  if (!result_json.empty()) {
    std::ofstream out(result_json, std::ios::trunc);
    out << rj << "]\n";
  }
  if (observation) {
    std::fprintf(stderr, "lhd sim: the selected test could not be initialized for replay\n");
    return 2;
  }
  return fail_tests == 0 ? 0 : 1;
}
