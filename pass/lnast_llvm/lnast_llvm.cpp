//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnast_llvm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cgen_llvm.hpp"
#include "lnast.hpp"

namespace livehd::lnast_llvm {

namespace {

using V = Cgen_llvm::Value;
using Op = Cgen_llvm::Binary_op;

// LNAST_LLVM_TIME=1 prints where generation time goes (walk vs LLVM) to stderr.
struct Phase_timer {
  bool                                  on = std::getenv("LNAST_LLVM_TIME") != nullptr;
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  void lap(const char* what) {
    if (!on) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "lnast_llvm: %-28s %8.2f s\n", what, std::chrono::duration<double>(now - t0).count());
    t0 = now;
  }
};

struct Unsupported {
  std::string why;
};

[[noreturn]] void bail(std::string why) { throw Unsupported{std::move(why)}; }

// An integer literal as written in LNAST: sign, magnitude as little-endian words.
struct Literal {
  bool                  negative = false;
  std::vector<uint64_t> mag;  // magnitude, little endian
};

uint32_t bit_length(const std::vector<uint64_t>& m) {
  for (size_t i = m.size(); i-- > 0;) {
    if (m[i] != 0) {
      return static_cast<uint32_t>(i * 64 + (64 - static_cast<unsigned>(__builtin_clzll(m[i]))));
    }
  }
  return 0;
}

bool is_pow2(const std::vector<uint64_t>& m) {
  uint32_t ones = 0;
  for (auto w : m) {
    ones += static_cast<uint32_t>(__builtin_popcountll(w));
  }
  return ones == 1;
}

std::optional<Literal> parse_literal(std::string_view t) {
  Literal l;
  if (t.empty()) {
    return std::nullopt;
  }
  if (t[0] == '-') {
    l.negative = true;
    t.remove_prefix(1);
  } else if (t[0] == '+') {
    t.remove_prefix(1);
  }
  if (t.empty()) {
    return std::nullopt;
  }
  if (t.size() > 3 && t[0] == '0' && (t[1] == 'u' || t[1] == 's') && t[2] == 'b') {
    // 0ub / 0sb bit patterns; unknown bits (? x z) read as 0, like sim.unknown_zero.
    const bool is_signed = t[1] == 's';
    t.remove_prefix(3);
    std::string bits;
    for (char c : t) {
      if (c == '_') {
        continue;
      }
      if (c == '0' || c == '1') {
        bits.push_back(c);
      } else if (c == '?' || c == 'x' || c == 'X' || c == 'z' || c == 'Z') {
        bits.push_back('0');
      } else {
        return std::nullopt;
      }
    }
    if (bits.empty()) {
      return std::nullopt;
    }
    const size_t n = bits.size();
    l.mag.assign((n + 63) / 64, 0);
    for (size_t i = 0; i < n; ++i) {
      if (bits[n - 1 - i] == '1') {
        l.mag[i / 64] |= uint64_t{1} << (i % 64);
      }
    }
    if (is_signed && bits[0] == '1') {  // value - 2^n: negate the two's complement magnitude
      l.negative = !l.negative;
      uint64_t carry = 1;
      for (size_t i = 0; i < l.mag.size(); ++i) {
        const uint64_t inv = ~l.mag[i];
        l.mag[i]           = inv + carry;
        carry              = (carry != 0 && l.mag[i] == 0) ? 1 : 0;
      }
      if (n % 64 != 0) {
        l.mag.back() &= (uint64_t{1} << (n % 64)) - 1;
      }
    }
    return l;
  }
  if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
    t.remove_prefix(2);
    size_t nibbles = 0;
    for (char c : t) {
      if (c == '_') {
        continue;
      }
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        return std::nullopt;
      }
      ++nibbles;
    }
    l.mag.assign((nibbles + 15) / 16, 0);
    size_t pos = 0;
    for (size_t i = t.size(); i-- > 0;) {
      const char c = t[i];
      if (c == '_') {
        continue;
      }
      const uint64_t d = static_cast<uint64_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10));
      l.mag[pos / 16] |= d << ((pos % 16) * 4);
      ++pos;
    }
    return l;
  }
  // decimal: multiply-add on words
  l.mag.assign(1, 0);
  for (char c : t) {
    if (c == '_') {
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return std::nullopt;
    }
    unsigned __int128 carry = static_cast<unsigned>(c - '0');
    for (auto& w : l.mag) {
      const unsigned __int128 cur = static_cast<unsigned __int128>(w) * 10 + carry;
      w                           = static_cast<uint64_t>(cur);
      carry                       = cur >> 64;
    }
    if (carry != 0) {
      l.mag.push_back(static_cast<uint64_t>(carry));
    }
  }
  return l;
}

// Two's complement words of `l` over `width` bits.
std::vector<uint64_t> twos_words(const Literal& l, uint32_t width) {
  const size_t          n = (width + 63) / 64;
  std::vector<uint64_t> w(n, 0);
  for (size_t i = 0; i < n && i < l.mag.size(); ++i) {
    w[i] = l.mag[i];
  }
  if (l.negative) {
    uint64_t carry = 1;
    for (auto& x : w) {
      x     = ~x + carry;
      carry = (carry != 0 && x == 0) ? 1 : 0;
    }
  }
  if (width % 64 != 0 && !w.empty()) {
    w.back() &= (uint64_t{1} << (width % 64)) - 1;
  }
  return w;
}

struct Type {
  uint32_t bits = 1;
  bool     sign = false;
};

// prim_type_int(max, min) -> the narrowest carrier holding [min, max].
Type type_of_range(const Literal& max, const Literal& min) {
  Type t;
  if (!min.negative || bit_length(min.mag) == 0) {
    t.sign = false;
    t.bits = std::max<uint32_t>(1, bit_length(max.mag));
    return t;
  }
  t.sign                = true;
  const uint32_t pos    = bit_length(max.mag) + 1;
  const uint32_t neg    = is_pow2(min.mag) ? bit_length(min.mag) : bit_length(min.mag) + 1;
  t.bits                = std::max<uint32_t>(std::max(pos, neg), 1);
  return t;
}

class Gen {
public:
  Gen(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, std::string_view fn) : ln_(&ln), top_ln_(&ln), fn_name_(fn) {
    for (const auto& l : lib) {
      lib_[std::string(l->get_top_module_name())] = l.get();
    }
  }

  Result run(const std::string& dir);

private:
  struct Reg_info {
    Field                    field;
    std::string              local;  // name inside its module
    std::optional<std::string> reset_pin;
    bool                     negreset = false;
    bool                     sync     = true;
    std::optional<Literal>   initial;
    bool                     has_clock = false;
    uint64_t                 mem_size  = 0;  // > 0: a memory of mem_size entries (field.bits = entry bits)
  };
  using Env = absl::flat_hash_map<std::string, V>;

  // One module occurrence: its LNAST, its ports and the registers it owns (global indices).
  struct Frame {
    const Lnast*                             ln = nullptr;
    std::string                              prefix;  // instance path, "" for the top
    std::vector<Field>                       in, out;
    std::vector<Lnast_nid>                   stmts;
    absl::flat_hash_map<std::string, size_t> reg_idx;  // local name -> index in regs_
    absl::flat_hash_set<std::string>         clocks;
    std::vector<size_t>                      owned;  // regs_ indices of this occurrence
  };
  const Lnast*     ln_;      // the LNAST of the frame being executed
  const Lnast*     top_ln_;
  std::string      fn_name_;
  Cgen_llvm*       k_ = nullptr;
  std::vector<Reg_info> regs_;
  absl::flat_hash_map<std::string, const Lnast*>            lib_;
  absl::flat_hash_map<std::string, std::unique_ptr<Frame>>  frames_;  // key: instance path
  Frame*                                                    fr_  = nullptr;
  Frame*                                                    top_ = nullptr;
  absl::flat_hash_map<std::string, Env>                     inst_env_;  // per walk: instance path -> final env
  std::vector<V>                                            cur_, next_;  // per walk register values / next state
  size_t                                   words_ = 0;
  // memories (kernel-local arrays seeded from the state): array id per register index, rebuilt for every walk
  std::vector<size_t> arrays_;
  struct Undo {
    Env*                  env;
    std::string           name;
    std::optional<V>      old;
  };
  std::vector<Undo>   log_;  // every env write, so an `if` arm can be rolled back without copying the env
  // A register's name always READS its current value; a store to it only sets the NEXT value, kept under
  // name + next_tag. (Reading `r` after `store(r, x)` in the same cycle still gives the old Q.)
  static constexpr char next_tag = '\x01';
  bool is_scalar_reg(const std::string& name) const {
    auto it = fr_->reg_idx.find(name);
    return it != fr_->reg_idx.end() && regs_[it->second].mem_size == 0;
  }
  void put(Env& env, const std::string& name_in, V v) {
    const std::string name = (!name_in.empty() && name_in.back() != next_tag && is_scalar_reg(name_in)) ? name_in + next_tag : name_in;
    auto it = env.find(name);
    log_.push_back({&env, name, it == env.end() ? std::nullopt : std::optional<V>(it->second)});
    env[name] = v;
  }
  bool                record_ = true;  // false in the settle walk: next-state is not rebuilt
  V                   path_;           // AND of the enclosing if conditions (width 0: none)
  V                   path_enable() { return path_.width != 0 ? path_ : k_->constant(1, 1, true); }
  void                start_walk(const std::vector<V>& base);
  std::vector<V>      walk_base_;  // memory contents at the start of the walk
  size_t              mem_array(const std::string& name, const Env& env) {
    auto it = fr_->reg_idx.find(name);
    if (it == fr_->reg_idx.end() || regs_[it->second].mem_size == 0) {
      bail("'" + name + "' is not a memory");
    }
    return ensure_array(it->second, env);
  }
  // The array of memory `idx`: it lives in the state allocation (no seed, no publish); created on first use.
  size_t ensure_array(size_t idx, const Env&) {
    if (arrays_[idx] != std::numeric_limits<size_t>::max()) {
      return arrays_[idx];
    }
    const auto& r = regs_[idx];
    arrays_[idx]  = k_->array_create_in_state(r.field.bits, r.mem_size, r.field.word);
    return arrays_[idx];
  }

  // ---- tree helpers ----
  std::vector<Lnast_nid> kids(const Lnast_nid& n) const {
    std::vector<Lnast_nid> v;
    for (auto c : ln_->children(n)) {
      v.push_back(c);
    }
    return v;
  }
  Lnast_ntype::Lnast_ntype_int ty(const Lnast_nid& n) const { return ln_->get_type(n); }
  std::string                  nm(const Lnast_nid& n) const { return std::string(ln_->get_name(n)); }

  Type type_node(const Lnast_nid& n) const {
    if (ty(n) != Lnast_ntype::Lnast_ntype_prim_type_int) {
      bail("non-integer type");
    }
    auto c = kids(n);
    if (c.size() != 2) {
      bail("prim_type_int arity");
    }
    auto mx = parse_literal(nm(c[0]));
    auto mn = parse_literal(nm(c[1]));
    if (!mx || !mn) {
      bail("unbounded or symbolic type range");
    }
    return type_of_range(*mx, *mn);
  }

  void   setup_io(const Lnast_nid& io, Frame& f);
  Frame* build_frame(const Lnast* ln, const std::string& prefix);
  void   collect_attrs(Frame& f);
  void   scan_calls(const std::vector<Lnast_nid>& stmts, Frame& f);
  void   finish_frame(Frame& f, const Env& pre, const Env& end);
  void   run_frame(Frame& f, Env& env);
  void exec_block(const std::vector<Lnast_nid>& stmts, Env& env);
  void exec_if(const std::vector<Lnast_nid>& c, size_t at, Env& env);
  void exec_stmt(const std::vector<Lnast_nid>& stmts, size_t& i, Env& env);

  V    operand(const Lnast_nid& n, const Env& env);
  V    literal(const Literal& l);
  static size_t bail_idx(std::string why) { bail(std::move(why)); }
  V    fit(V v, Type t) { return k_->resize(v, t.bits, !t.sign); }
  V    fit_to(V v, uint32_t bits, bool sign) { return k_->resize(v, bits, !sign); }
  std::optional<Type> typed_next(const std::vector<Lnast_nid>& stmts, size_t i, const std::string& dst) const;
  V    combine(Env& env, const V& c, const V& t, const V& f);
};

V Gen::literal(const Literal& l) {
  if (!l.negative) {
    const uint32_t w = std::max<uint32_t>(1, bit_length(l.mag));
    auto           words = twos_words(l, w);
    return k_->constant_words(w, words, true);
  }
  Type t = type_of_range(Literal{false, {0}}, l);
  if (t.bits == 0) {
    t.bits = 1;
  }
  return k_->constant_words(t.bits, twos_words(l, t.bits), false);
}

V Gen::operand(const Lnast_nid& n, const Env& env) {
  const auto t = ty(n);
  if (t == Lnast_ntype::Lnast_ntype_const) {
    const auto text = nm(n);
    if (text == "true") {
      return k_->constant(1, 1, true);
    }
    if (text == "false") {
      return k_->constant(1, 0, true);
    }
    auto lit = parse_literal(text);
    if (!lit) {
      bail("non-numeric constant operand '" + text + "'");
    }
    return literal(*lit);
  }
  if (t != Lnast_ntype::Lnast_ntype_ref) {
    bail("operand is neither ref nor const");
  }
  const auto name = nm(n);
  if (name == "true") {
    return k_->constant(1, 1, true);
  }
  if (name == "false") {
    return k_->constant(1, 0, true);
  }
  if (fr_->clocks.contains(name)) {
    return k_->constant(1, 0, true);
  }
  auto it = env.find(name);
  if (it == env.end()) {
    bail("read of undefined '" + name + "'");
  }
  return it->second;
}

std::optional<Type> Gen::typed_next(const std::vector<Lnast_nid>& stmts, size_t i, const std::string& dst) const {
  if (i + 1 >= stmts.size() || ty(stmts[i + 1]) != Lnast_ntype::Lnast_ntype_type_spec) {
    return std::nullopt;
  }
  auto c = kids(stmts[i + 1]);
  if (c.size() != 2 || nm(c[0]) != dst) {
    return std::nullopt;
  }
  return type_node(c[1]);
}

void Gen::setup_io(const Lnast_nid& io, Frame& fr) {
  auto groups = kids(io);
  if (groups.size() != 2) {
    bail("io shape");
  }
  for (size_t g = 0; g < 2; ++g) {
    for (const auto& st : kids(groups[g])) {
      if (ty(st) != Lnast_ntype::Lnast_ntype_store) {
        bail("io entry is not a store");
      }
      auto c = kids(st);
      if (c.size() < 3) {
        bail("io store arity");
      }
      Field f;
      f.name = nm(c[0]);
      auto t = type_node(c[2]);
      f.bits = t.bits;
      f.sign = t.sign;
      (g == 0 ? fr.in : fr.out).push_back(f);
    }
  }
}

void Gen::collect_attrs(Frame& fr) {
  const auto& stmts = fr.stmts;
  // declarations first: an attr_set may precede the declare it configures (memory ordering)
  for (int pass = 0; pass < 2; ++pass)
  for (const auto& s : stmts) {
    const auto t = ty(s);
    if (pass == 1 && t != Lnast_ntype::Lnast_ntype_attr_set) {
      continue;
    }
    if (pass == 0 && t != Lnast_ntype::Lnast_ntype_declare) {
      continue;
    }
    if (t == Lnast_ntype::Lnast_ntype_declare) {
      auto c = kids(s);
      if (c.size() < 4 || nm(c[2]) != "reg") {
        bail("declare of a non-register");
      }
      Reg_info r;
      r.local      = nm(c[0]);
      r.field.name = fr.prefix + nm(c[0]);
      if (ty(c[1]) == Lnast_ntype::Lnast_ntype_comp_type_array) {
        auto ac = kids(c[1]);
        if (ac.size() != 2) {
          bail("array type shape");
        }
        auto tt      = type_node(ac[0]);
        r.field.bits = tt.bits;
        r.field.sign = tt.sign;
        auto txt     = nm(ac[1]);
        if (txt.size() < 3 || txt.front() != '[' || txt.back() != ']') {
          bail("array size '" + txt + "'");
        }
        auto n = parse_literal(std::string_view(txt).substr(1, txt.size() - 2));
        if (!n || n->negative || n->mag.size() != 1 || n->mag[0] == 0) {
          bail("array size '" + txt + "'");
        }
        r.mem_size = n->mag[0];
      } else {
        auto tt      = type_node(c[1]);
        r.field.bits = tt.bits;
        r.field.sign = tt.sign;
      }
      fr.reg_idx[r.local] = regs_.size();
      fr.owned.push_back(regs_.size());
      regs_.push_back(std::move(r));
    } else if (t == Lnast_ntype::Lnast_ntype_attr_set) {
      auto c = kids(s);
      if (c.size() != 3) {
        bail("attr_set arity");
      }
      const auto target = nm(c[0]);
      const auto key    = nm(c[1]);
      auto       it     = fr.reg_idx.find(target);
      if (it == fr.reg_idx.end()) {
        bail("attr_set on '" + target + "' which is not a register");
      }
      auto& r = regs_[it->second];
      if (r.mem_size != 0) {
        if (key == "ordering") {
          if (nm(c[2]) != "\"old\"") {
            bail("memory ordering other than old");
          }
        } else if (key == "__store_clock_pin") {
          fr.clocks.insert(nm(c[2]));
        } else if (key == "__store_posclk") {
          if (nm(c[2]) == "false") {
            bail("negedge memory");
          }
        } else if (key == "initial") {
          auto lit = parse_literal(nm(c[2]));
          if (!lit || bit_length(lit->mag) != 0) {
            bail("non-zero memory image");
          }
        } else if (key == "reset_pin") {
          if (nm(c[2]) != "false" && nm(c[2]) != "nil") {
            r.reset_pin = nm(c[2]);
          }
        } else if (key == "sync") {
          r.sync = nm(c[2]) != "false";
        } else if (key == "negreset") {
          r.negreset = nm(c[2]) == "true";
        } else {
          bail("unsupported memory attribute '" + key + "'");
        }
        continue;
      }
      if (key == "clock_pin") {
        fr.clocks.insert(nm(c[2]));
        r.has_clock = true;
      } else if (key == "posclk") {
        if (nm(c[2]) == "false") {
          bail("negedge register");
        }
      } else if (key == "initial") {
        auto lit = parse_literal(nm(c[2]));
        if (!lit) {
          bail("non-numeric initial");
        }
        r.initial = *lit;
      } else if (key == "reset_pin") {
        if (nm(c[2]) != "false" && nm(c[2]) != "nil") {
          r.reset_pin = nm(c[2]);
        }
      } else if (key == "sync") {
        r.sync = nm(c[2]) != "false";
      } else if (key == "negreset") {
        r.negreset = nm(c[2]) == "true";
      } else {
        bail("unsupported attribute '" + key + "'");
      }
    }
  }
}

// Value of `c ? t : f`, widths unified to the common carrier.
V Gen::combine(Env&, const V& c, const V& t, const V& f) {
  if (t.id == f.id && t.width == f.width) {
    return t;
  }
  uint32_t w;
  bool     sign;
  if (t.unsign == f.unsign) {
    w    = std::max(t.width, f.width);
    sign = !t.unsign;
  } else {
    w    = std::max(t.unsign ? t.width + 1 : t.width, f.unsign ? f.width + 1 : f.width);
    sign = true;
  }
  return k_->mux(c, f, t, w, !sign);
}

void Gen::exec_if(const std::vector<Lnast_nid>& c, size_t at, Env& env) {
  // c: cond, stmts, [cond, stmts]*, [else stmts]
  if (at >= c.size()) {
    return;
  }
  if (ty(c[at]) == Lnast_ntype::Lnast_ntype_stmts) {  // else arm
    exec_block(kids(c[at]), env);
    return;
  }
  if (at + 1 >= c.size()) {
    bail("if arm without a body");
  }
  V       cond  = operand(c[at], env);
  const V saved = path_;
  V       ncond = k_->binary(Op::eq, cond, k_->constant(1, 0, true), 1, true);
  const size_t mark = log_.size();
  path_ = saved.width != 0 ? k_->binary(Op::bit_and, saved, cond, 1, true) : k_->binary(Op::ne, cond, k_->constant(1, 0, true), 1, true);
  exec_block(kids(c[at + 1]), env);
  // final value of every name the then-arm wrote, then roll the env back to the state before the arm
  absl::flat_hash_map<std::string, V>   then_vals;
  std::vector<std::string>              order;
  for (size_t i = mark; i < log_.size(); ++i) {
    if (!then_vals.contains(log_[i].name)) {
      order.push_back(log_[i].name);
    }
    then_vals[log_[i].name] = env.at(log_[i].name);
  }
  absl::flat_hash_map<std::string, std::optional<V>> before;
  for (size_t i = log_.size(); i-- > mark;) {  // oldest entry per name wins
    before[log_[i].name] = log_[i].old;
  }
  for (size_t i = log_.size(); i-- > mark;) {
    if (log_[i].old) {
      env[log_[i].name] = *log_[i].old;
    } else {
      env.erase(log_[i].name);
    }
  }
  log_.resize(mark);
  path_ = saved.width != 0 ? k_->binary(Op::bit_and, saved, ncond, 1, true) : ncond;
  const size_t else_mark = log_.size();
  exec_if(c, at + 2, env);
  absl::flat_hash_map<std::string, V> else_vals;
  for (size_t i = else_mark; i < log_.size(); ++i) {
    if (!then_vals.contains(log_[i].name) && !else_vals.contains(log_[i].name)) {
      order.push_back(log_[i].name);
    }
    else_vals[log_[i].name] = env.at(log_[i].name);
    if (!before.contains(log_[i].name)) {
      before[log_[i].name] = log_[i].old;
    }
  }
  // restore pre-if values, then write the merged ones (logged for any enclosing arm)
  for (size_t i = log_.size(); i-- > else_mark;) {
    if (log_[i].old) {
      env[log_[i].name] = *log_[i].old;
    } else {
      env.erase(log_[i].name);
    }
  }
  log_.resize(else_mark);
  path_ = saved;
  for (const auto& n : order) {
    std::optional<V> prior = before.at(n);
    if (!prior && !n.empty() && n.back() == next_tag) {
      prior = env.at(n.substr(0, n.size() - 1));  // not written yet: it holds Q
    }
    auto        ti    = then_vals.find(n);
    auto        ei    = else_vals.find(n);
    if (!prior && (ti == then_vals.end() || ei == else_vals.end())) {
      // written in one arm only and undefined before: the other arm leaves it undefined
    }
    V tv = ti != then_vals.end() ? ti->second : (prior ? *prior : ei->second);
    V ev = ei != else_vals.end() ? ei->second : (prior ? *prior : ti->second);
    if (prior && tv.id == prior->id && ev.id == prior->id && tv.width == ev.width) {
      continue;
    }
    put(env, n, combine(env, cond, tv, ev));
  }
}

void Gen::exec_block(const std::vector<Lnast_nid>& stmts, Env& env) {
  for (size_t i = 0; i < stmts.size();) {
    exec_stmt(stmts, i, env);
  }
}

void Gen::exec_stmt(const std::vector<Lnast_nid>& stmts, size_t& i, Env& env) {
  const auto  s = stmts[i];
  const auto  t = ty(s);
  const auto  c = kids(s);
  using NT      = Lnast_ntype;
  size_t      consumed = 1;

  auto dst_name = [&]() -> std::string {
    if (c.empty() || ty(c[0]) != NT::Lnast_ntype_ref) {
      bail("statement without a destination");
    }
    return nm(c[0]);
  };
  // result type: the type_spec that follows, else the natural one computed by `natural`
  auto finish = [&](const std::string& dst, V v) {
    if (auto tn = typed_next(stmts, i, dst)) {
      v        = fit(v, *tn);
      consumed = 2;
    }
    put(env, dst, v);
  };
  auto result_type = [&](const std::string& dst) { return typed_next(stmts, i, dst); };
  auto sw          = [](const V& v) { return v.unsign ? v.width + 1 : v.width; };  // signed-equivalent width

  switch (t) {
    case NT::Lnast_ntype_declare:
    case NT::Lnast_ntype_attr_set:
      break;  // collected up front
    case NT::Lnast_ntype_store: {
      if (c.size() == 3 && ty(c[0]) == NT::Lnast_ntype_ref && fr_->reg_idx.contains(nm(c[0])) && regs_[fr_->reg_idx.at(nm(c[0]))].mem_size != 0) {
        const auto& r = regs_[fr_->reg_idx.at(nm(c[0]))];
        if (!record_) {
          break;  // the settle walk reads the post-edge memory; the writes were applied in walk A
        }
        V           address = operand(c[1], env);
        V           data    = k_->resize(operand(c[2], env), r.field.bits, !r.field.sign);
        if (!k_->array_write(mem_array(nm(c[0]), env), path_enable(), address, data)) {
          bail("memory write");
        }
        break;
      }
      if (c.size() != 2) {
        bail("store with a path");
      }
      const auto dst = nm(c[0]);
      if (ty(c[0]) != NT::Lnast_ntype_ref) {
        bail("store destination");
      }
      V v = operand(c[1], env);
      put(env, dst, v);
      break;
    }
    case NT::Lnast_ntype_func_call: {
      if (c.size() < 2 || ty(c[0]) != NT::Lnast_ntype_ref || ty(c[1]) != NT::Lnast_ntype_ref) {
        bail("fcall shape");
      }
      if (path_.width != 0) {
        bail("module instance under a condition");
      }
      auto fit_frame = frames_.find(fr_->prefix + nm(c[0]) + ".");
      if (fit_frame == frames_.end()) {
        bail("call to unknown module '" + nm(c[1]) + "'");
      }
      Frame& cf = *fit_frame->second;
      Env    cenv;
      for (const auto& port : cf.in) {
        if (cf.clocks.contains(port.name)) {
          continue;
        }
        bool found = false;
        for (size_t j = 2; j < c.size(); ++j) {
          auto sc = kids(c[j]);
          if (ty(c[j]) == NT::Lnast_ntype_store && sc.size() == 2 && nm(sc[0]) == port.name) {
            cenv[port.name] = fit_to(operand(sc[1], env), port.bits, port.sign);
            found           = true;
            break;
          }
        }
        if (!found) {
          bail("instance '" + nm(c[0]) + "' leaves input '" + port.name + "' unconnected");
        }
      }
      for (size_t idx : cf.owned) {
        if (regs_[idx].mem_size == 0) {
          cenv[regs_[idx].local] = cur_[idx];
        }
      }
      Env pre = cenv;
      run_frame(cf, cenv);
      if (record_) {
        finish_frame(cf, pre, cenv);
      }
      inst_env_[cf.prefix] = std::move(cenv);
      break;
    }
    case NT::Lnast_ntype_tuple_get: {
      const auto dst = dst_name();
      if (c.size() == 3 && ty(c[1]) == NT::Lnast_ntype_ref && frames_.contains(fr_->prefix + nm(c[1]) + ".")) {
        Frame&     cf   = *frames_.at(fr_->prefix + nm(c[1]) + ".");
        const auto port = nm(c[2]);
        auto       ie   = inst_env_.find(cf.prefix);
        auto       oi   = std::find_if(cf.out.begin(), cf.out.end(), [&](const Field& f) { return f.name == port; });
        if (ie == inst_env_.end() || oi == cf.out.end() || !ie->second.contains(port)) {
          bail("instance output '" + port + "' is not driven");
        }
        finish(dst, fit_to(ie->second.at(port), oi->bits, oi->sign));
        break;
      }
      if (c.size() != 3 || ty(c[1]) != NT::Lnast_ntype_ref) {
        bail("tuple_get shape");
      }
      const auto& r = regs_[fr_->reg_idx.count(nm(c[1])) ? fr_->reg_idx.at(nm(c[1])) : bail_idx("tuple_get of a non-memory '" + nm(c[1]) + "'")];
      if (r.mem_size == 0) {
        bail("tuple_get of a register");
      }
      V address = operand(c[2], env);
      V v       = k_->array_read(mem_array(nm(c[1]), env), address, r.field.bits, !r.field.sign);
      if (v.width == 0) {
        bail("memory read");
      }
      finish(dst, v);
      break;
    }
    case NT::Lnast_ntype_type_spec: {
      // a standalone type_spec (not folded into its producer): re-type the value
      if (c.size() != 2) {
        bail("type_spec arity");
      }
      const auto name = nm(c[0]);
      auto       it   = env.find(name);
      if (it == env.end()) {
        bail("type_spec of undefined '" + name + "'");
      }
      put(env, name, fit(it->second, type_node(c[1])));
      break;
    }
    case NT::Lnast_ntype_if:
    case NT::Lnast_ntype_unique_if:
      exec_if(c, 0, env);
      break;
    case NT::Lnast_ntype_stmts:
      exec_block(c, env);
      break;
    case NT::Lnast_ntype_plus:
    case NT::Lnast_ntype_minus: {
      const auto dst = dst_name();
      if (c.size() < 3) {
        bail("arithmetic arity");
      }
      const bool add = t == NT::Lnast_ntype_plus;
      V          acc = operand(c[1], env);
      auto       tn  = result_type(dst);
      for (size_t j = 2; j < c.size(); ++j) {
        V rhs = operand(c[j], env);
        uint32_t w;
        bool     sign;
        if (tn && j + 1 == c.size()) {
          w = tn->bits;
          sign = tn->sign;
        } else {
          w    = std::max(sw(acc), sw(rhs)) + 1;
          sign = !(add && acc.unsign && rhs.unsign);
          if (!sign) {
            w = std::max(acc.width, rhs.width) + 1;
          }
        }
        acc = k_->binary(add ? Op::add : Op::sub, acc, rhs, w, !sign);
      }
      finish(dst, acc);
      break;
    }
    case NT::Lnast_ntype_mult:
    case NT::Lnast_ntype_div:
    case NT::Lnast_ntype_mod: {
      const auto dst = dst_name();
      if (c.size() != 3) {
        bail("mult/div/mod arity");
      }
      V a = operand(c[1], env);
      V b = operand(c[2], env);
      auto tn = result_type(dst);
      uint32_t w;
      bool     sign;
      if (tn) {
        w    = tn->bits;
        sign = tn->sign;
      } else {
        sign = !(a.unsign && b.unsign);
        w    = t == NT::Lnast_ntype_mult ? (sign ? sw(a) + sw(b) : a.width + b.width) : std::max(sw(a), sw(b));
      }
      const Op op = t == NT::Lnast_ntype_mult ? Op::mul : (t == NT::Lnast_ntype_div ? Op::div : Op::rem);
      finish(dst, k_->binary(op, a, b, w, !sign));
      break;
    }
    case NT::Lnast_ntype_bit_and:
    case NT::Lnast_ntype_bit_or:
    case NT::Lnast_ntype_bit_xor: {
      const auto dst = dst_name();
      if (c.size() < 3) {
        bail("bitwise arity");
      }
      std::vector<V> ops;
      for (size_t j = 1; j < c.size(); ++j) {
        ops.push_back(operand(c[j], env));
      }
      auto     tn = result_type(dst);
      uint32_t w  = 0;
      bool     sign = false;
      if (tn) {
        w    = tn->bits;
        sign = tn->sign;
      } else {
        for (const auto& o : ops) {
          sign = sign || !o.unsign;
        }
        for (const auto& o : ops) {
          w = std::max(w, sign ? sw(o) : o.width);
        }
      }
      const Op op = t == NT::Lnast_ntype_bit_and ? Op::bit_and : (t == NT::Lnast_ntype_bit_or ? Op::bit_or : Op::bit_xor);
      // Balanced reduction tree: a linear fold makes an N-deep dependence chain that LLVM's
      // optimizer handles badly on long OR/AND/XOR lists.
      while (ops.size() > 1) {
        std::vector<V> next;
        for (size_t j = 0; j + 1 < ops.size(); j += 2) {
          next.push_back(k_->binary(op, ops[j], ops[j + 1], w, !sign));
        }
        if (ops.size() & 1) {
          next.push_back(ops.back());
        }
        ops.swap(next);
      }
      finish(dst, ops[0]);
      break;
    }
    case NT::Lnast_ntype_bit_not: {
      const auto dst = dst_name();
      if (c.size() == 3) {  // bit_not(dst, a, bits): unsigned complement over `bits`
        V a = operand(c[1], env);
        auto bits = parse_literal(nm(c[2]));
        if (!bits || bits->negative || bits->mag.size() != 1 || bits->mag[0] == 0) {
          bail("bit_not width");
        }
        finish(dst, k_->unary_not(a, static_cast<uint32_t>(bits->mag[0]), true));
      } else if (c.size() == 2) {
        V a  = operand(c[1], env);
        auto tn = result_type(dst);
        const uint32_t w = tn ? tn->bits : sw(a) ;
        finish(dst, k_->unary_not(a, w, false));
      } else {
        bail("bit_not arity");
      }
      break;
    }
    case NT::Lnast_ntype_get_mask: {
      const auto dst = dst_name();
      if (c.size() != 4) {
        bail("get_mask arity");
      }
      V    a  = operand(c[1], env);
      auto lo = parse_literal(nm(c[2]));
      auto hi = parse_literal(nm(c[3]));
      if (!lo || !hi || lo->negative || hi->negative || lo->mag.size() != 1 || hi->mag.size() != 1 || hi->mag[0] <= lo->mag[0]) {
        bail("get_mask with non-constant or empty window");
      }
      const auto l = static_cast<uint32_t>(lo->mag[0]);
      const auto h = static_cast<uint32_t>(hi->mag[0]);
      V          w = k_->resize(a, std::max(a.width, h), a.unsign);
      if (l != 0) {
        w = k_->binary(Op::lshr, w, k_->constant(32, l, true), w.width, true);
      }
      finish(dst, k_->resize(V{w.id, w.width, true}, h - l, true));
      break;
    }
    case NT::Lnast_ntype_set_mask: {
      const auto dst = dst_name();
      if (c.size() != 5) {
        bail("set_mask arity");
      }
      auto lo = parse_literal(nm(c[3]));
      auto hi = parse_literal(nm(c[4]));
      if (!lo || !hi || lo->mag.size() != 1 || hi->mag.size() != 1) {
        bail("set_mask window");
      }
      if (ty(c[1]) == NT::Lnast_ntype_ref && fr_->reg_idx.contains(nm(c[1])) && regs_[fr_->reg_idx.at(nm(c[1]))].mem_size != 0) {
        // whole-array update bus: entry i <- bus[i*bits +: bits], under the path enable
        const auto& r     = regs_[fr_->reg_idx.at(nm(c[1]))];
        const auto  total = static_cast<uint32_t>(r.mem_size * r.field.bits);
        if (nm(c[0]) != nm(c[1]) || lo->mag[0] != 0 || hi->mag[0] != total) {
          bail("partial or renamed whole-array write");
        }
        if (!record_) {
          break;
        }
        V      bus  = k_->resize(operand(c[2], env), total, true);
        size_t arr  = mem_array(nm(c[1]), env);
        const uint32_t abits = std::max<uint32_t>(1, 64 - static_cast<uint32_t>(__builtin_clzll(r.mem_size)));
        for (uint64_t e = 0; e < r.mem_size; ++e) {
          V piece = k_->resize(k_->binary(Op::lshr, bus, k_->constant(32, e * r.field.bits, true), total, true), r.field.bits, true);
          if (!k_->array_write(arr, path_enable(), k_->constant(abits, e, true), V{piece.id, piece.width, !r.field.sign})) {
            bail("whole-array write");
          }
        }
        break;
      }
      V base = operand(c[1], env);
      V ins  = operand(c[2], env);
      auto     tn = result_type(dst);
      uint32_t w  = tn ? tn->bits : std::max<uint32_t>(base.width, static_cast<uint32_t>(hi->mag[0]));
      bool     sg = tn ? tn->sign : !base.unsign;
      finish(dst, k_->bitfield_insert(base, ins, static_cast<uint32_t>(lo->mag[0]), static_cast<uint32_t>(hi->mag[0]), w, !sg));
      break;
    }
    case NT::Lnast_ntype_sext: {
      const auto dst = dst_name();
      if (c.size() != 3) {
        bail("sext arity");
      }
      V    a  = operand(c[1], env);
      auto sb = parse_literal(nm(c[2]));
      auto tn = result_type(dst);
      if (!sb || sb->negative || sb->mag.size() != 1 || !tn) {
        bail("sext needs a constant sign bit and a typed result");
      }
      finish(dst, k_->sign_extend_from(a, static_cast<uint32_t>(sb->mag[0]), tn->bits, !tn->sign));
      break;
    }
    case NT::Lnast_ntype_concat: {
      const auto dst = dst_name();
      if (c.size() < 3 || (c.size() - 1) % 2 != 0) {
        bail("concat arity");
      }
      std::vector<V> lanes;
      for (size_t j = 1; j + 1 < c.size(); j += 2) {
        V    v  = operand(c[j], env);
        auto wl = parse_literal(nm(c[j + 1]));
        if (!wl || wl->negative || wl->mag.size() != 1 || wl->mag[0] == 0) {
          bail("concat lane width");
        }
        const auto w = static_cast<uint32_t>(wl->mag[0]);
        // the lane is the low `w` bits of the value, as an unsigned window
        V x = v.width == w ? v : k_->resize(v, w, v.unsign);
        lanes.push_back(V{x.id, w, true});
      }
      finish(dst, k_->concat(lanes));
      break;
    }
    case NT::Lnast_ntype_shl:
    case NT::Lnast_ntype_sra: {
      const auto dst = dst_name();
      if (c.size() != 3) {
        bail("shift arity");
      }
      V    a  = operand(c[1], env);
      V    b  = operand(c[2], env);
      auto tn = result_type(dst);
      if (!tn) {
        bail("shift needs a typed result");
      }
      // cgen_sim's rule: a right shift of an unsigned value is logical (its top carrier bit is data, not a sign)
      finish(dst, k_->binary(t == NT::Lnast_ntype_shl ? Op::shl : (a.unsign ? Op::lshr : Op::ashr), a, b, tn->bits, !tn->sign));
      break;
    }
    case NT::Lnast_ntype_eq:
    case NT::Lnast_ntype_ne:
    case NT::Lnast_ntype_lt:
    case NT::Lnast_ntype_le:
    case NT::Lnast_ntype_gt:
    case NT::Lnast_ntype_ge: {
      const auto dst = dst_name();
      if (c.size() != 3) {
        bail("compare arity");
      }
      V        a  = operand(c[1], env);
      V        b  = operand(c[2], env);
      Op       op = t == NT::Lnast_ntype_eq   ? Op::eq
                    : t == NT::Lnast_ntype_ne ? Op::ne
                    : t == NT::Lnast_ntype_lt ? Op::lt
                    : t == NT::Lnast_ntype_le ? Op::le
                    : t == NT::Lnast_ntype_gt ? Op::gt
                                              : Op::ge;
      put(env, dst, k_->binary(op, a, b, 1, true));
      break;
    }
    case NT::Lnast_ntype_red_or:
    case NT::Lnast_ntype_red_xor:
    case NT::Lnast_ntype_popcount: {
      const auto dst = dst_name();
      if (c.size() < 2) {
        bail("reduction arity");
      }
      if (t == NT::Lnast_ntype_red_or) {
        V acc = k_->reduce_or(operand(c[1], env), 1, true);
        for (size_t j = 2; j < c.size(); ++j) {
          acc = k_->binary(Op::bit_or, acc, k_->reduce_or(operand(c[j], env), 1, true), 1, true);
        }
        put(env, dst, acc);
      } else {
        if (c.size() != 2) {
          bail("reduction arity");
        }
        V a = operand(c[1], env);
        // the lowering masks to the live low bits first, so the operand is non-negative
        const uint32_t w  = a.width;
        auto           tn = result_type(dst);
        const uint32_t rw = tn ? tn->bits : 32;
        V r = k_->count_bits(a, w, rw, t == NT::Lnast_ntype_red_xor);
        if (tn) {
          consumed = 2;
        }
        put(env, dst, r);
      }
      break;
    }
    default:
      bail(std::format("unsupported statement '{}'", Lnast_ntype::debug_name(t)));
  }
  i += consumed;
}

void Gen::start_walk(const std::vector<V>& base) {
  if (arrays_.size() != regs_.size()) {
    arrays_.assign(regs_.size(), std::numeric_limits<size_t>::max());
  }
  walk_base_ = base;
  path_      = V{};
}

Gen::Frame* Gen::build_frame(const Lnast* ln, const std::string& prefix) {
  auto fr    = std::make_unique<Frame>();
  fr->ln     = ln;
  fr->prefix = prefix;
  const Lnast* saved = ln_;
  ln_                = ln;
  Lnast_nid top = ln->get_root();
  if (ty(top) != Lnast_ntype::Lnast_ntype_top) {
    auto rc = kids(top);
    if (rc.empty() || ty(rc[0]) != Lnast_ntype::Lnast_ntype_top) {
      bail("no top node");
    }
    top = rc[0];
  }
  auto tc = kids(top);
  if (tc.size() != 2 || ty(tc[0]) != Lnast_ntype::Lnast_ntype_io || ty(tc[1]) != Lnast_ntype::Lnast_ntype_stmts) {
    bail("top is not {io, stmts}");
  }
  setup_io(tc[0], *fr);
  fr->stmts = kids(tc[1]);
  collect_attrs(*fr);
  Frame* raw      = fr.get();
  frames_[prefix] = std::move(fr);
  scan_calls(raw->stmts, *raw);
  ln_ = saved;
  return raw;
}

void Gen::scan_calls(const std::vector<Lnast_nid>& stmts, Frame& f) {
  for (const auto& st : stmts) {
    const auto t = ty(st);
    if (t == Lnast_ntype::Lnast_ntype_func_call) {
      auto c = kids(st);
      if (c.size() < 2) {
        bail("fcall shape");
      }
      auto it = lib_.find(nm(c[1]));
      if (it == lib_.end()) {
        bail("call to unknown module '" + nm(c[1]) + "'");
      }
      build_frame(it->second, f.prefix + nm(c[0]) + ".");
    } else if (Lnast_ntype::is_if_like(t) || t == Lnast_ntype::Lnast_ntype_stmts) {
      for (const auto& ch : kids(st)) {
        if (ty(ch) == Lnast_ntype::Lnast_ntype_stmts) {
          scan_calls(kids(ch), f);
        }
      }
    }
  }
}

void Gen::run_frame(Frame& f, Env& env) {
  Frame*       saved_fr = fr_;
  const Lnast* saved_ln = ln_;
  fr_                   = &f;
  ln_                   = f.ln;
  exec_block(f.stmts, env);
  fr_ = saved_fr;
  ln_ = saved_ln;
}

// Next-state of every register this occurrence owns, from its final `end` values (`pre`: ports at entry).
void Gen::finish_frame(Frame& f, const Env& pre, const Env& end) {
  for (size_t idx : f.owned) {
    const auto& r = regs_[idx];
    if (r.mem_size != 0) {
      if (r.reset_pin) {  // reset clears the whole image (initial image is zero) at the edge, over any write
        const V* rv = nullptr;  // an input or any signal settled this cycle
        if (auto e = end.find(*r.reset_pin); e != end.end()) {
          rv = &e->second;
        } else if (auto q = pre.find(*r.reset_pin); q != pre.end()) {
          rv = &q->second;
        } else {
          bail("memory reset signal '" + *r.reset_pin + "' is not a known signal");
        }
        V active = r.negreset ? k_->binary(Op::eq, *rv, k_->constant(1, 0, true), 1, true)
                              : k_->binary(Op::ne, *rv, k_->constant(1, 0, true), 1, true);
        if (!k_->array_clear_if(ensure_array(idx, pre), active)) {
          bail("memory reset");
        }
      }
      next_[idx] = V{};  // the memory is updated in place: nothing to publish
      continue;
    }
    auto nx = end.find(r.local + next_tag);
    V    n  = nx != end.end() ? nx->second : end.at(r.local);
    if (r.reset_pin) {
      const V* rv = nullptr;  // an input or any signal settled this cycle
      if (auto e = end.find(*r.reset_pin); e != end.end()) {
        rv = &e->second;
      } else if (auto q = pre.find(*r.reset_pin); q != pre.end()) {
        rv = &q->second;
      } else {
        bail("reset signal '" + *r.reset_pin + "' is not a known signal");
      }
      V active = r.negreset ? k_->binary(Op::eq, *rv, k_->constant(1, 0, true), 1, true)
                            : k_->binary(Op::ne, *rv, k_->constant(1, 0, true), 1, true);
      V init   = literal(r.initial ? *r.initial : Literal{false, {0}});
      n        = k_->mux(active, n, init, r.field.bits, !r.field.sign);
    }
    next_[idx] = k_->resize(n, r.field.bits, !r.field.sign);
  }
}

Result Gen::run(const std::string& dir) {
  Result res;
  res.function = fn_name_;
  try {
    Phase_timer timer;
    ln_  = top_ln_;
    top_ = build_frame(top_ln_, "");
    timer.lap("collect (decls/attrs/frames)");
    fr_  = top_;
    for (const auto& r : regs_) {
      // An asynchronous reset is treated as the synchronous one: every step carries an edge and the
      // testbench samples after it, so the post-step state is the reset value either way.
      if (r.initial && r.field.bits > 64 && (r.initial->negative || bit_length(r.initial->mag) > 64) && bit_length(r.initial->mag) != 0) {
        bail("wide register initial value");
      }
    }

    // state layout: non-clock top inputs, then every register (all occurrences), then the top outputs
    std::vector<Cgen_llvm::State_input> layout_inputs;
    auto place = [&](Field& f) {
      f.word = words_;
      words_ += (f.bits + 63) / 64;
    };
    for (auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        place(f);
        layout_inputs.push_back({f.word, f.bits, !f.sign, 0});
      }
    }
    for (auto& r : regs_) {
      if (r.mem_size != 0) {
        // In-place array layout (Cgen_llvm::array_state_words); the input slot is a placeholder.
        Field whole = r.field;
        whole.bits  = static_cast<uint32_t>(Cgen_llvm::array_state_words(r.field.bits, r.mem_size) * 64);
        place(whole);
        r.field.word = whole.word;
        layout_inputs.push_back({whole.word, 1, true, 0});
        continue;
      }
      place(r.field);
      layout_inputs.push_back({r.field.word, r.field.bits, !r.field.sign, 0});
    }
    for (auto& f : top_->out) {
      place(f);
    }
    Cgen_llvm kernel(fn_name_, Cgen_llvm::State_layout{words_, layout_inputs});
    k_ = &kernel;

    size_t slot = 0;
    Env    top_in;
    for (const auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        top_in[f.name] = k_->input(slot++);
      }
    }
    std::vector<V> base(regs_.size());
    for (size_t i = 0; i < regs_.size(); ++i) {
      base[i] = k_->input(slot + i);
    }
    auto walk = [&](const std::vector<V>& cur, bool record) {
      cur_ = cur;
      record_ = record;
      next_.assign(regs_.size(), V{});
      inst_env_.clear();
      start_walk(cur);
      Env env = top_in;
      for (size_t idx : top_->owned) {
        if (regs_[idx].mem_size == 0) {
          env[regs_[idx].local] = cur[idx];
        }
      }
      Env pre = env;
      ln_     = top_ln_;
      fr_     = top_;
      exec_block(top_->stmts, env);
      if (record) {
        finish_frame(*top_, pre, env);
      }
      return env;
    };
    walk(base, true);  // next state of every register from the current state
    timer.lap("walk A (next state)");
    const std::vector<V> next = next_;
    Env                  env_b = walk(next, false);  // outputs settled on the post-edge registers
    timer.lap("walk B (settled outputs)");
    for (auto& f : top_->out) {
      auto it = env_b.find(f.name);
      if (it == env_b.end()) {
        bail("output '" + f.name + "' is never assigned");
      }
      V           v = k_->resize(it->second, f.bits, !f.sign);
      std::string error;
      if (!k_->add_state_output(f.word, f.word, v, {}, error)) {
        bail("output: " + error);
      }
    }
    for (size_t i = 0; i < regs_.size(); ++i) {
      std::string error;
      if (regs_[i].mem_size != 0) {
        continue;
      }
      if (next[i].width == 0) {
        bail("register '" + regs_[i].field.name + "' has no next state");
      }
      if (!k_->add_state_output(regs_[i].field.word, regs_[i].field.word, next[i], {}, error)) {
        bail("register: " + error);
      }
    }
    timer.lap("state outputs");
    std::filesystem::create_directories(dir);
    std::string error;
    if (!k_->write_object(dir + "/kernel.o", error)) {
      bail("llvm: " + error);
    }
    timer.lap("LLVM optimize + codegen");
    res.instructions = k_->instruction_count();
    k_ = nullptr;

    std::ofstream js(dir + "/kernel.json");
    auto          dump = [&](const char* key, const std::vector<Field>& v, bool with_init) {
      js << "  \"" << key << "\": [";
      for (size_t i = 0; i < v.size(); ++i) {
        js << (i ? ", " : "") << "{\"name\":\"" << v[i].name << "\",\"bits\":" << v[i].bits << ",\"sign\":" << (v[i].sign ? 1 : 0)
           << ",\"word\":" << v[i].word;
        if (with_init) {
          js << ",\"init\":" << v[i].init;
        }
        js << "}";
      }
      js << "]";
    };
    std::vector<Field> state_inputs;
    for (const auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        state_inputs.push_back(f);
      }
    }
    std::vector<Field> reg_fields;
    for (auto& r : regs_) {
      Field f = r.field;
      if (r.mem_size != 0) {
        f.bits = static_cast<uint32_t>(Cgen_llvm::array_state_words(r.field.bits, r.mem_size) * 64);
        f.sign = false;
      }
      if (r.initial) {
        auto w = twos_words(*r.initial, std::max<uint32_t>(f.bits, 1));
        f.init = w.empty() ? 0 : w[0];
      }
      reg_fields.push_back(f);
    }
    js << "{\n  \"function\": \"" << fn_name_ << "\",\n  \"words\": " << words_ << ",\n";
    dump("inputs", state_inputs, false);
    js << ",\n";
    dump("outputs", top_->out, false);
    js << ",\n";
    dump("regs", reg_fields, true);
    js << "\n}\n";

    res.inputs  = state_inputs;
    res.outputs = top_->out;
    res.regs    = reg_fields;
    res.words   = words_;
    res.ok      = true;
  } catch (const Unsupported& u) {
    res.ok  = false;
    res.why = u.why;
  }
  return res;
}

}  // namespace

Result generate(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, const std::string& dir) {
  std::string name(ln.get_top_module_name());
  if (name.empty()) {
    name = "top";
  }
  for (auto& ch : name) {
    if (!std::isalnum(static_cast<unsigned char>(ch))) {
      ch = '_';
    }
  }
  Gen g(ln, lib, "lnast_" + name + "_step");
  return g.run(dir);
}

}  // namespace livehd::lnast_llvm
