//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnast_llvm.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "cgen_llvm.hpp"
#include "debug_env.hpp"
#include "lnast.hpp"
#include "sim_compile_workers.hpp"

namespace livehd::lnast_llvm {

namespace {

using V = Cgen_llvm::Value;
using Op = Cgen_llvm::Binary_op;
using Op_kind = Cgen_llvm::Binary_op;

// LHD_SIM_LNAST_TIME=1 prints where generation time goes (walk vs LLVM) to stderr.
struct Phase_timer {
  bool                                  on = livehd::debug::env<"LHD_SIM_LNAST_TIME">() != nullptr;
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

// The recorded kernel: the Cgen_llvm calls the LNAST walk makes, kept as data instead of being built
// at once. One walk of the design then yields every kernel: the tape is pruned to what a kernel
// publishes (the next state, or the outputs settled after the edge), cut into consecutive intervals
// -- an interval of a topological order never closes a cycle -- and each interval is replayed into
// its own Cgen_llvm on a worker, so a large design compiles as many small functions in parallel.
// A value is its op's index; `V{id, width, unsign}` views it exactly as Cgen_llvm::Value does.
class Tape {
public:
  enum class K : uint8_t {
    input,            // imm: state location (Tape::locs)
    constant,         // imm: value
    constant_words,   // x, n: words_
    resize,           // a
    concat,           // x, n: refs_ (most significant lane first)
    unary_not,        // a
    reduce_or,        // a
    count_bits,       // a; imm: count; bop: parity
    bitfield_insert,  // a: base, b: inserted; imm: lo | hi << 32
    sign_extend,      // a; imm: sign bit
    binary,           // bop; a, b
    mux,              // a: select, b: when false, c: when true
    array_read,       // imm: array; a: address
    array_write,      // imm: array; a: enable, b: address, c: data            (side effect)
    array_clear,      // imm: array; a: condition                              (side effect)
    loop_begin,       // imm: trip count; width: index bits; x, n: refs_ (carry seeds); c.id: words_ index of `first`
    loop_index,       // a: its loop_begin
    loop_phi,         // a: its loop_begin; imm: carry number
    loop_end,         // a: its loop_begin; x, n: refs_ (carry next values)
    loop_final,       // a: its loop_end; imm: carry number
  };
  static constexpr uint32_t none = std::numeric_limits<uint32_t>::max();
  struct Ref {
    uint32_t id     = none;
    uint32_t width  = 0;
    bool     unsign = true;
  };
  struct Op {
    K        kind   = K::constant;
    uint8_t  bop    = 0;
    bool     unsign = true;
    uint32_t width  = 0;
    Ref      a, b, c;
    uint64_t imm = 0;
    uint32_t x = 0, n = 0;
  };
  struct Array {
    uint32_t bits = 0;
    uint64_t size = 0;
    size_t   loc  = 0;
  };
  std::vector<Op>       ops;
  std::vector<uint64_t> words;
  std::vector<Ref>      refs;
  std::vector<Array>    arrays;  // by register index (scalars: size 0)
  // State is addressed by LOCATION while the tape is recorded and analyzed; the word of each location
  // is only fixed afterwards (the layout depends on which registers can be written in place).
  std::vector<size_t>   locs;      // location -> first state word
  std::vector<uint32_t> loc_bits;  // location -> footprint in bits
  size_t                new_loc(uint64_t bits) {
    loc_bits.push_back(static_cast<uint32_t>(bits));
    locs.push_back(none);
    return loc_bits.size() - 1;
  }
  std::vector<std::pair<uint32_t, uint32_t>> loops;  // outermost loops: [loop_begin, last loop_final or loop_end]

  static Ref  ref(const V& v) { return Ref{static_cast<uint32_t>(v.id), v.width, v.unsign}; }
  V           value(uint32_t id) const { return V{id, ops[id].width, ops[id].unsign}; }
  static bool is_free(K k) { return k == K::input || k == K::constant || k == K::constant_words; }

  V input(size_t loc, uint32_t width, bool unsign) {
    const auto key = std::make_tuple(loc, width, unsign);
    if (auto it = inputs_.find(key); it != inputs_.end()) {
      return value(it->second);
    }
    Op o;
    o.kind       = K::input;
    o.imm        = loc;
    const auto v = add(o, width, unsign);
    inputs_.emplace(key, static_cast<uint32_t>(v.id));
    return v;
  }
  V constant(uint32_t width, uint64_t val, bool unsign = true) {
    const auto key = std::make_tuple(val, width, unsign);
    if (auto it = constants_.find(key); it != constants_.end()) {
      return value(it->second);
    }
    Op o;
    o.kind       = K::constant;
    o.imm        = val;
    const auto v = add(o, width, unsign);
    constants_.emplace(key, static_cast<uint32_t>(v.id));
    return v;
  }
  V constant_words(uint32_t width, const std::vector<uint64_t>& w, bool unsign = true) {
    if (width <= 64) {
      return constant(width, w.empty() ? 0 : w[0], unsign);
    }
    Op o;
    o.kind = K::constant_words;
    o.x    = static_cast<uint32_t>(words.size());
    o.n    = static_cast<uint32_t>(w.size());
    words.insert(words.end(), w.begin(), w.end());
    return add(o, width, unsign);
  }
  V resize(V v, uint32_t rw, bool ru) {
    if (v.width == rw && v.unsign == ru) {
      return v;
    }
    Op o;
    o.kind = K::resize;
    o.a    = ref(v);
    return add(o, rw, ru);
  }
  V concat(const std::vector<V>& lanes) {
    Op o;
    o.kind         = K::concat;
    o.x            = static_cast<uint32_t>(refs.size());
    o.n            = static_cast<uint32_t>(lanes.size());
    uint64_t total = 0;
    for (const auto& l : lanes) {
      refs.push_back(ref(l));
      total += l.width;
    }
    return add(o, static_cast<uint32_t>(total), true);
  }
  V unary_not(V v, uint32_t rw, bool ru) { return unary(K::unary_not, v, rw, ru, 0); }
  V reduce_or(V v, uint32_t rw, bool ru) { return unary(K::reduce_or, v, rw, ru, 0); }
  V count_bits(V v, uint32_t count, uint32_t rw, bool parity) {
    Op o;
    o.kind = K::count_bits;
    o.a    = ref(v);
    o.imm  = count;
    o.bop  = parity ? 1 : 0;
    return add(o, rw, true);
  }
  V bitfield_insert(V base, V inserted, uint32_t lo, uint32_t hi, uint32_t rw, bool ru) {
    Op o;
    o.kind = K::bitfield_insert;
    o.a    = ref(base);
    o.b    = ref(inserted);
    o.imm  = uint64_t{lo} | (uint64_t{hi} << 32);
    return add(o, rw, ru);
  }
  V sign_extend_from(V v, uint32_t sign_bit, uint32_t rw, bool ru) { return unary(K::sign_extend, v, rw, ru, sign_bit); }
  V binary(Op_kind op, V l, V r, uint32_t rw, bool ru) {
    Op o;
    o.kind = K::binary;
    o.bop  = static_cast<uint8_t>(op);
    o.a    = ref(l);
    o.b    = ref(r);
    return add(o, rw, ru);
  }
  V mux(V sel, V when_false, V when_true, uint32_t rw, bool ru) {
    Op o;
    o.kind = K::mux;
    o.a    = ref(sel);
    o.b    = ref(when_false);
    o.c    = ref(when_true);
    return add(o, rw, ru);
  }
  V array_read(size_t array, V address, uint32_t rw, bool ru) {
    Op o;
    o.kind = K::array_read;
    o.imm  = array;
    o.a    = ref(address);
    return add(o, rw, ru);
  }
  bool array_write(size_t array, V enable, V address, V data) {
    Op o;
    o.kind = K::array_write;
    o.imm  = array;
    o.a    = ref(enable);
    o.b    = ref(address);
    o.c    = ref(data);
    add(o, 0, true);
    return true;
  }
  bool array_clear_if(size_t array, V condition) {
    Op o;
    o.kind = K::array_clear;
    o.imm  = array;
    o.a    = ref(condition);
    add(o, 0, true);
    return true;
  }
  bool loop_begin(uint64_t count, int64_t first, uint32_t index_width, const std::vector<V>& carry_init, V& index,
                  std::vector<V>& carries) {
    Op o;
    o.kind = K::loop_begin;
    o.imm  = count;
    o.c.id = static_cast<uint32_t>(words.size());
    words.push_back(static_cast<uint64_t>(first));
    o.x = static_cast<uint32_t>(refs.size());
    o.n = static_cast<uint32_t>(carry_init.size());
    for (const auto& v : carry_init) {
      refs.push_back(ref(v));
    }
    const auto begin = static_cast<uint32_t>(add(o, index_width, true).id);
    open_.push_back(begin);
    Op ix;
    ix.kind = K::loop_index;
    ix.a.id = begin;
    index   = add(ix, index_width, true);
    carries.clear();
    for (size_t i = 0; i < carry_init.size(); ++i) {
      Op ph;
      ph.kind = K::loop_phi;
      ph.a.id = begin;
      ph.imm  = i;
      carries.push_back(add(ph, carry_init[i].width, carry_init[i].unsign));
    }
    return true;
  }
  bool loop_end(const std::vector<V>& carry_next, std::vector<V>& carry_final) {
    if (open_.empty()) {
      return false;
    }
    const auto begin = open_.back();
    open_.pop_back();
    Op o;
    o.kind = K::loop_end;
    o.a.id = begin;
    o.x    = static_cast<uint32_t>(refs.size());
    o.n    = static_cast<uint32_t>(carry_next.size());
    if (o.n != ops[begin].n) {
      return false;
    }
    for (const auto& v : carry_next) {
      refs.push_back(ref(v));
    }
    const auto end = static_cast<uint32_t>(add(o, 0, true).id);
    carry_final.clear();
    for (size_t i = 0; i < carry_next.size(); ++i) {
      const auto& seed = refs[ops[begin].x + i];
      Op          fi;
      fi.kind = K::loop_final;
      fi.a.id = end;
      fi.imm  = i;
      carry_final.push_back(add(fi, seed.width, seed.unsign));
    }
    if (open_.empty()) {
      loops.emplace_back(begin, static_cast<uint32_t>(ops.size() - 1));
    }
    return true;
  }
  // Every value `o` reads.
  template <class Fn>
  void for_each_operand(const Op& o, Fn&& fn) const {
    for (const Ref* r : {&o.a, &o.b, &o.c}) {
      if (r->id != none && !(o.kind == K::loop_begin && r == &o.c)) {
        fn(*r);
      }
    }
    if (o.kind == K::concat || o.kind == K::loop_begin || o.kind == K::loop_end) {
      for (uint32_t i = 0; i < o.n; ++i) {
        fn(refs[o.x + i]);
      }
    }
  }

private:
  V add(Op o, uint32_t width, bool unsign) {
    o.width  = width;
    o.unsign = unsign;
    ops.push_back(o);
    return V{ops.size() - 1, width, unsign};
  }
  V unary(K kind, V v, uint32_t rw, bool ru, uint64_t imm) {
    Op o;
    o.kind = kind;
    o.a    = ref(v);
    o.imm  = imm;
    return add(o, rw, ru);
  }
  std::map<std::tuple<size_t, uint32_t, bool>, uint32_t>   inputs_;
  std::map<std::tuple<uint64_t, uint32_t, bool>, uint32_t> constants_;
  std::vector<uint32_t>                                    open_;
};

class Gen {
public:
  Gen(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, std::string_view fn) : ln_(&ln), top_ln_(&ln), fn_name_(fn) {
    for (const auto& l : lib) {
      lib_[std::string(l->get_top_module_name())] = l.get();
    }
  }

  Result run(const Sim_objects& out);

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
  Tape*                                                     k_ = nullptr;
  std::vector<Reg_info> regs_;
  absl::flat_hash_map<std::string, const Lnast*>            lib_;
  absl::flat_hash_map<std::string, std::unique_ptr<Frame>>  frames_;  // key: instance path
  Frame*                                                    fr_  = nullptr;
  Frame*                                                    top_ = nullptr;
  absl::flat_hash_map<std::string, Env>                     inst_env_;  // per walk: instance path -> final env
  std::vector<V>                                            cur_, next_;  // per walk register values / next state
  size_t                                   words_ = 0;
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
  static constexpr bool record_ = true;  // one walk: the settled outputs are a replay of the same tape
  V                   path_;           // AND of the enclosing if conditions (width 0: none)
  V                   path_enable() { return path_.width != 0 ? path_ : k_->constant(1, 1, true); }
  size_t              mem_array(const std::string& name, const Env& env) {
    auto it = fr_->reg_idx.find(name);
    if (it == fr_->reg_idx.end() || regs_[it->second].mem_size == 0) {
      bail("'" + name + "' is not a memory");
    }
    return ensure_array(it->second, env);
  }
  // A memory is addressed by its register index: the array lives in the state allocation (Tape::arrays).
  size_t ensure_array(size_t idx, const Env&) { return idx; }

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
  // A register's reset is applied by the commit kernel, once per distinct reset signal and as one
  // unlikely branch, not as a select inside every register's next-state cone.
  struct Reset_item {
    size_t  idx;
    V       active;  // 1 bit: this register is being reset
    Literal init;
  };
  std::vector<Reset_item> reset_items_;
  absl::flat_hash_map<std::string, V> reset_active_;  // reset pin name (+ polarity) -> its active condition
  void   run_frame(Frame& f, Env& env);
  void exec_block(const std::vector<Lnast_nid>& stmts, Env& env);
  void exec_if(const std::vector<Lnast_nid>& c, size_t at, Env& env);
  void exec_for(const std::vector<Lnast_nid>& c, Env& env);
  // Statements under `stmts`, nested blocks included (a loop's size for the unroll budget).
  uint64_t                            count_statements(const Lnast_nid& stmts) const {
    uint64_t n = 0;
    for (const auto& s : kids(stmts)) {
      ++n;
      for (const auto& k : kids(s)) {
        if (ty(k) == Lnast_ntype::Lnast_ntype_stmts) {
          n += count_statements(k);
        }
      }
    }
    return n;
  }
  static bool no_unroll() { return livehd::debug::env<"LHD_SIM_LNAST_NO_UNROLL">() != nullptr; }  // debug A/B
  void collect_assigned(const Lnast_nid& stmts, std::vector<std::string>& out, absl::flat_hash_set<std::string>& seen) const;
  absl::flat_hash_map<std::string, std::pair<int64_t, int64_t>> ranges_;  // range tmp -> [first, last] (literal bounds)
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
          r.initial = *lit;  // an initialized (zero) image: not undefined at power-on
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
        const auto text = nm(c[2]);
        if (text == "nil") {
          continue;  // no initializer
        }
        auto lit = text == "true" ? parse_literal("1") : text == "false" ? parse_literal("0") : parse_literal(text);
        if (!lit) {
          bail("register '" + target + "' has a non-numeric initial value '" + text + "'");
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

// Names a statement list assigns (first child of every non-structural statement), nested blocks included.
void Gen::collect_assigned(const Lnast_nid& stmts, std::vector<std::string>& out, absl::flat_hash_set<std::string>& seen) const {
  using NT = Lnast_ntype;
  for (const auto& s : kids(stmts)) {
    const auto t = ty(s);
    if (t == NT::Lnast_ntype_stmts) {
      collect_assigned(s, out, seen);
    } else if (t == NT::Lnast_ntype_if || t == NT::Lnast_ntype_unique_if || t == NT::Lnast_ntype_for || t == NT::Lnast_ntype_while) {
      for (const auto& k : kids(s)) {
        if (ty(k) == NT::Lnast_ntype_stmts) {
          collect_assigned(k, out, seen);
        }
      }
    } else if (t == NT::Lnast_ntype_type_spec || t == NT::Lnast_ntype_attr_set || t == NT::Lnast_ntype_declare || t == NT::Lnast_ntype_range) {
      continue;
    } else {
      const auto k = kids(s);
      if (!k.empty() && ty(k[0]) == NT::Lnast_ntype_ref) {
        const auto name = nm(k[0]);
        if (fr_->reg_idx.contains(name) && regs_[fr_->reg_idx.at(name)].mem_size != 0) {
          continue;  // a memory write is a store in place
        }
        if (seen.insert(name).second) {
          out.push_back(name);
        }
      }
    }
  }
}

// `for i in lo..hi { body }`: one native loop, the body lowered once. Variables the body assigns that
// exist before it are the carries (header phis); memories are read and written in place. The index is
// an unsigned value of just the bits the range needs.
void Gen::exec_for(const std::vector<Lnast_nid>& c, Env& env) {
  if (c.size() < 3 || ty(c[2]) != Lnast_ntype::Lnast_ntype_stmts) {
    bail("for shape");
  }
  const auto ivar = nm(c[0]);
  const auto rit  = ranges_.find(nm(c[1]));
  if (rit == ranges_.end()) {
    bail("for over a range that is not a pair of literals");
  }
  const auto [lo, hi] = rit->second;
  if (hi < lo) {
    return;  // empty
  }
  if (lo < 0) {
    bail("for with a negative index");
  }
  std::vector<std::string>         assigned;
  absl::flat_hash_set<std::string> seen;
  collect_assigned(c[2], assigned, seen);
  std::vector<std::string> carried;
  std::vector<V>           init;
  for (const auto& n : assigned) {
    if (n == ivar) {
      continue;
    }
    if (is_scalar_reg(n)) {
      bail("register written inside a for body");
    }
    if (auto it = env.find(n); it != env.end()) {
      carried.push_back(n);
      init.push_back(it->second);
    }
  }
  // A short loop is unrolled here, in the native code only (the LNAST and the graph keep it rolled):
  // every trip then sees its index as a constant, so a lane position is a fixed slice instead of a
  // variable shift of the whole value. bitcoin's 32-trip byte swap of a 256-bit hash did four 256-bit
  // variable shifts per trip, rolled: 473k cycles/s against 3.2M unrolled. The budget is the one
  // inou.cgen.sim's native loops use: at most 256 trips and 16,384 body statements in all.
  {
    const uint64_t trips = static_cast<uint64_t>(hi - lo + 1);
    if (trips <= 256 && trips * count_statements(c[2]) <= 16384 && !no_unroll()) {
      uint32_t width = 1;
      while (width < 63 && (uint64_t{1} << width) <= static_cast<uint64_t>(hi)) {
        ++width;
      }
      for (int64_t trip = lo; trip <= hi; ++trip) {
        put(env, ivar, k_->constant(width, static_cast<uint64_t>(trip), true));
        exec_block(kids(c[2]), env);
      }
      return;
    }
  }
  uint32_t iw = 1;
  while (iw < 63 && (uint64_t{1} << iw) <= static_cast<uint64_t>(hi)) {
    ++iw;
  }
  V              index;
  std::vector<V> phis;
  if (!k_->loop_begin(static_cast<uint64_t>(hi - lo + 1), lo, iw, init, index, phis)) {
    bail("native loop");
  }
  for (size_t i = 0; i < carried.size(); ++i) {
    put(env, carried[i], phis[i]);
  }
  put(env, ivar, index);
  exec_block(kids(c[2]), env);
  std::vector<V> next;
  for (const auto& n : carried) {
    next.push_back(env.at(n));
  }
  std::vector<V> fin;
  if (!k_->loop_end(next, fin)) {
    bail("native loop end");
  }
  for (size_t i = 0; i < carried.size(); ++i) {
    put(env, carried[i], fin[i]);
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
    case NT::Lnast_ntype_range: {
      if (c.size() != 3 || ty(c[0]) != NT::Lnast_ntype_ref) {
        bail("range shape");
      }
      auto as_int = [&](const Lnast_nid& n) {
        const auto l = parse_literal(nm(n));
        if (!l || l->mag.size() > 1 || l->mag.empty() || (l->mag[0] >> 62) != 0) {
          bail("range bound is not a small literal");
        }
        return l->negative ? -static_cast<int64_t>(l->mag[0]) : static_cast<int64_t>(l->mag[0]);
      };
      ranges_[nm(c[0])] = {as_int(c[1]), as_int(c[2])};
      break;
    }
    case NT::Lnast_ntype_for:
      exec_for(c, env);
      break;
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
      const auto key = f.prefix + "/" + *r.reset_pin + (r.negreset ? "!" : "");
      auto       ai  = reset_active_.find(key);
      if (ai == reset_active_.end()) {
        ai = reset_active_
                 .emplace(key,
                          r.negreset ? k_->binary(Op::eq, *rv, k_->constant(1, 0, true), 1, true)
                                     : k_->binary(Op::ne, *rv, k_->constant(1, 0, true), 1, true))
                 .first;
      }
      reset_items_.push_back({
          idx,
          ai->second,
          r.initial ? *r.initial : Literal{false, {0}}
      });
    }
    next_[idx] = k_->resize(n, r.field.bits, !r.field.sign);
  }
}

// ---- emission: the tape, pruned and cut, replayed into kernels ----

// What one kernel family computes: the ops it needs, in the order they are emitted, and where its
// results go.
// One published result: `value` stored at state location `loc`. A register that keeps its value
// while a condition holds (`r' = c ? x : r`) is `guarded`: when its next value lands in the register
// itself (`loc == home`) it is published as `if (guard) r = data`, so a disabled register is not
// rewritten and one branch serves every register sharing the enable; otherwise as the mux it is.
struct Root {
  Tape::Ref value;
  size_t    loc       = 0;
  bool      guarded   = false;
  bool      when_zero = false;  // update while the guard is zero
  Tape::Ref guard, data, hold;  // `hold`: the register's current value
  size_t    home   = Tape::none;
  size_t    kernel = 0;  // the kernel that publishes it
  template <class Fn>
  void for_each_operand(Fn&& fn) const {
    if (guarded) {
      fn(guard);
      fn(data);
    } else {
      fn(value);
    }
  }
};

struct View {
  std::string           tag;  // file/symbol suffix ("e": edge, "s": settle)
  std::vector<Root>     roots;
  std::vector<uint32_t> order;  // the live computed ops, in emission order
  std::vector<uint32_t> pos;    // per op: its index in `order` (none: not emitted)
  std::vector<uint32_t> cut;    // kernel k replays order[cut[k], cut[k + 1])
  std::vector<uint8_t>  spill;  // per op: read by another kernel of this view
  uint64_t              size = 0;  // words of code (cut_kernels)
  // The kernel that computes (or, for an input or a constant, may as well publish) `id`.
  size_t                kernel_of(uint32_t id) const {
    if (pos[id] == Tape::none || cut.size() < 2) {
      return 0;
    }
    return static_cast<size_t>(std::upper_bound(cut.begin(), cut.end(), pos[id]) - cut.begin()) - 1;
  }
  size_t count_live() const { return order.size(); }
};

struct Kernel_job {
  std::string name, object;
  const View* view  = nullptr;
  size_t      index = 0;
  uint32_t    begin = 0, end = 0;  // positions in view->order
};

// The size of a kernel, in machine words of code (cut_kernels: kernels are one to two times this).
// LLVM's instruction selection and scheduling are superlinear in the size of a basic block, and a
// kernel is mostly one block: boom as ONE kernel spent 364 s there; in pieces the same code is a few
// CPU seconds per worker. Small kernels also run faster on a large design (less is live at once).
uint32_t chunk_target() {
  static const uint32_t target = [] {
    const char* text = livehd::debug::env<"LHD_SIM_LNAST_CHUNK">();  // debug: ops per kernel
    const auto  n    = text == nullptr ? 0ul : std::strtoul(text, nullptr, 10);
    return n == 0 ? 1250u : static_cast<uint32_t>(n);
  }();
  return target;
}

// Emission order: depth first from each result, in the order the results were recorded. The walk
// records statements in dependency (Kahn) order, which is breadth first: every value of one logic
// level is computed before any of the next, so nearly everything is live at once and a kernel is
// mostly spills (dino: 878 of 2,990 instructions touched the stack). Depth first, a value is computed
// right before the cone that consumes it. Only what a root needs is emitted, so this is also the
// dead-code pass. A loop is one unit, emitted whole. A memory write (`side_effects`: the edge view)
// is a root of its own and comes after every read of that memory, as the walk recorded it.
void order_view(const Tape& t, View& v, bool side_effects) {
  using K      = Tape::K;
  const auto n = static_cast<uint32_t>(t.ops.size());
  v.order.clear();
  v.pos.assign(n, Tape::none);
  // unit: an op outside any loop, or a whole outermost loop (named by its loop_begin)
  std::vector<uint32_t> unit(n);
  for (uint32_t i = 0; i < n; ++i) {
    unit[i] = i;
  }
  absl::flat_hash_map<uint32_t, uint32_t> loop_last;  // loop_begin -> last op of the loop
  for (const auto& [lb, le] : t.loops) {
    loop_last[lb] = le;
    for (uint32_t j = lb; j <= le; ++j) {
      unit[j] = lb;
    }
  }
  const auto                                           is_effect = [](K k) { return k == K::array_write || k == K::array_clear; };
  // Memory order (the edge view): a memory is written in place, so its reads and writes keep the
  // order they were recorded in. Per array, each write follows the write before it and every live
  // read recorded since; each read follows the last write recorded before it (a byte-enable write is
  // a chain of read-modify-writes of one entry: each lane must see the lane before it).
  std::vector<uint32_t>                                roots;
  absl::flat_hash_map<uint32_t, std::vector<uint32_t>> after;        // unit -> units it must follow
  absl::flat_hash_map<uint32_t, std::vector<uint32_t>> after_reads;  // write unit -> earlier reads (if live)
  if (side_effects) {
    struct Last {
      uint32_t              effect = Tape::none;
      std::vector<uint32_t> reads;
    };
    absl::flat_hash_map<uint64_t, Last> last;
    for (uint32_t i = 0; i < n; ++i) {
      const auto kind = t.ops[i].kind;
      if (!is_effect(kind) && kind != K::array_read) {
        continue;
      }
      auto&      l = last[t.ops[i].imm];
      const auto u = unit[i];
      if (is_effect(kind)) {
        roots.push_back(u);
        if (l.effect != Tape::none && l.effect != u) {
          after[u].push_back(l.effect);
        }
        for (const auto r : l.reads) {
          if (r != u) {
            after_reads[u].push_back(r);
          }
        }
        l.reads.clear();
        l.effect = u;
      } else {
        if (l.effect != Tape::none && l.effect != u) {
          after[u].push_back(l.effect);
        }
        l.reads.push_back(u);
      }
    }
  }
  // the units a unit reads from, or must follow, outside itself (`live`: also the reads a write follows)
  const auto operands = [&](uint32_t u, std::vector<uint32_t>& out, const std::vector<uint8_t>* live) {
    out.clear();
    const auto last = loop_last.contains(u) ? loop_last.at(u) : u;
    for (uint32_t j = u; j <= last; ++j) {
      t.for_each_operand(t.ops[j], [&](const Tape::Ref& r) {
        if (unit[r.id] != u && !Tape::is_free(t.ops[r.id].kind)) {
          out.push_back(unit[r.id]);
        }
      });
    }
    if (auto it = after.find(u); it != after.end()) {
      out.insert(out.end(), it->second.begin(), it->second.end());
    }
    if (live != nullptr) {
      if (auto it = after_reads.find(u); it != after_reads.end()) {
        for (const auto r : it->second) {
          if ((*live)[r] != 0) {
            out.push_back(r);
          }
        }
      }
    }
  };
  for (const auto& root : v.roots) {
    root.for_each_operand([&](const Tape::Ref& r) { roots.push_back(unit[r.id]); });
  }
  std::ranges::sort(roots);
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  // What is live, before any ordering: a read nothing consumes is not emitted for its memory's sake.
  std::vector<uint8_t> live(n, 0);
  {
    std::vector<uint32_t> work = roots, ops;
    while (!work.empty()) {
      const auto u = work.back();
      work.pop_back();
      if (live[u] != 0) {
        continue;
      }
      live[u] = 1;
      operands(u, ops, nullptr);
      work.insert(work.end(), ops.begin(), ops.end());
    }
  }
  std::vector<uint8_t> done(n, 0);  // per unit: 1 on the stack, 2 emitted
  struct Frame {
    uint32_t              u;
    std::vector<uint32_t> ops;
    size_t                next = 0;
  };
  std::vector<Frame> stack;
  for (auto root : roots) {
    if (done[root] != 0 || Tape::is_free(t.ops[root].kind)) {
      continue;
    }
    stack.push_back({root, {}, 0});
    operands(root, stack.back().ops, &live);
    done[root] = 1;
    while (!stack.empty()) {
      auto& f = stack.back();
      if (f.next < f.ops.size()) {
        const auto child = f.ops[f.next++];
        if (done[child] == 0) {
          done[child] = 1;
          stack.push_back({child, {}, 0});
          operands(child, stack.back().ops, &live);
        }
        continue;
      }
      const auto last = loop_last.contains(f.u) ? loop_last.at(f.u) : f.u;
      for (uint32_t j = f.u; j <= last; ++j) {
        v.pos[j] = static_cast<uint32_t>(v.order.size());
        v.order.push_back(j);
      }
      done[f.u] = 2;
      stack.pop_back();
    }
  }
}

void cut_kernels(const Tape& t, View& v) {
  const auto n = static_cast<uint32_t>(v.order.size());
  v.cut.clear();
  v.spill.assign(t.ops.size(), 0);
  if (n == 0 && v.roots.empty()) {
    return;
  }
  absl::flat_hash_set<uint32_t> loop_tail;  // an op of a loop other than its last one: no cut after it
  for (const auto& [lb, le] : t.loops) {
    for (uint32_t j = lb; j < le; ++j) {
      loop_tail.insert(j);
    }
  }
  // Size is counted in machine words, not ops: an op on a 512-bit value is eight words of code.
  const auto            size_of = [&](uint32_t id) { return std::max<uint64_t>(1, (t.ops[id].width + 63) / 64); };
  std::vector<uint64_t> size_to(n + 1, 0);  // words of code in order[0, p)
  for (uint32_t p = 0; p < n; ++p) {
    size_to[p + 1] = size_to[p] + size_of(v.order[p]);
  }
  // What a cut before order[p] costs: every value computed before p and read at or after it becomes
  // a store in one kernel and a load in another, per word. A depth-first order has natural seams
  // (between the cones of two results) where almost nothing is live; a cut in the middle of a cone
  // crosses everything that cone still holds (picorv32 cut blindly in two ran at half speed).
  std::vector<int64_t> crossing(n + 2, 0);
  {
    std::vector<uint32_t> last_use(t.ops.size(), 0);
    for (uint32_t p = 0; p < n; ++p) {
      t.for_each_operand(t.ops[v.order[p]], [&](const Tape::Ref& r) { last_use[r.id] = std::max(last_use[r.id], p); });
    }
    for (uint32_t p = 0; p < n; ++p) {
      const auto id = v.order[p];
      if (last_use[id] > p) {  // live across every cut in (p, last_use]
        crossing[p + 1]            += static_cast<int64_t>(size_of(id));
        crossing[last_use[id] + 1] -= static_cast<int64_t>(size_of(id));
      }
    }
    for (uint32_t p = 1; p <= n; ++p) {
      crossing[p] += crossing[p - 1];
    }
  }
  // A view that fits in 4 x `target` is ONE kernel: a cut makes everything that crosses it
  // unconditional (what a kernel only needs under a condition it computes under that condition), and
  // on a small design that costs more than it saves -- picorv32 in two kernels executed 4,234
  // instructions a clock against 2,807 in one, at half the speed. A larger view is cut into kernels of
  // `target` to 2 x `target` words, each at the cheapest seam in that window; there small kernels win
  // twice (boom: 13 kernels 26.4k cycles/s and 7.8 s of LLVM, 55 kernels 35.0k and 1.7 s).
  const uint64_t target = chunk_target();
  v.size                = size_to[n];
  v.cut.push_back(0);
  for (uint32_t start = 0; size_to[n] > 4 * target && size_to[n] - size_to[start] > 2 * target;) {
    uint32_t best = n;
    for (uint32_t p = start + 1; p < n && size_to[p] - size_to[start] <= 2 * target; ++p) {
      if (size_to[p] - size_to[start] < target || loop_tail.contains(v.order[p - 1])) {
        continue;
      }
      if (best == n || crossing[p] < crossing[best]) {
        best = p;
      }
    }
    if (best == n) {  // one loop larger than the window: cut right after it
      for (uint32_t p = start + 1; p < n; ++p) {
        if (size_to[p] - size_to[start] >= target && !loop_tail.contains(v.order[p - 1])) {
          best = p;
          break;
        }
      }
      if (best == n) {
        break;
      }
    }
    v.cut.push_back(best);
    start = best;
  }
  v.cut.push_back(n);
  // A value crosses kernels through a state word: mark what another kernel of this view reads.
  for (uint32_t p = 0; p < n; ++p) {
    const auto i    = v.order[p];
    const auto mine = v.kernel_of(i);
    t.for_each_operand(t.ops[i], [&](const Tape::Ref& r) {
      if (!Tape::is_free(t.ops[r.id].kind) && v.kernel_of(r.id) != mine) {
        v.spill[r.id] = 1;
      }
    });
  }
  // A result is published by the last kernel that computes one of its operands; the others cross.
  for (auto& root : v.roots) {
    root.kernel = 0;
    root.for_each_operand([&](const Tape::Ref& r) { root.kernel = std::max(root.kernel, v.kernel_of(r.id)); });
    root.for_each_operand([&](const Tape::Ref& r) {
      if (!Tape::is_free(t.ops[r.id].kind) && v.kernel_of(r.id) != root.kernel) {
        v.spill[r.id] = 1;
      }
    });
  }
}

// Replay order[begin, end) of `view` into one state kernel and write its object. Returns the
// instruction count; throws Unsupported.
size_t build_kernel(const Tape& t, const Kernel_job& job, const std::vector<size_t>& slot, size_t words) {
  const View& v   = *job.view;
  using K         = Tape::K;
  const auto here = [&](uint32_t id) { return v.pos[id] != Tape::none && v.pos[id] >= job.begin && v.pos[id] < job.end; };
  std::vector<Cgen_llvm::State_input>   layout;
  absl::flat_hash_map<uint32_t, size_t> in_index;
  const auto                            need = [&](uint32_t id) {
    const auto& d = t.ops[id];
    if (d.kind == K::input) {
      if (in_index.try_emplace(id, layout.size()).second) {
        layout.push_back({t.locs[d.imm], d.width, d.unsign, 0});
      }
    } else if (!Tape::is_free(d.kind) && !here(id)) {
      if (in_index.try_emplace(id, layout.size()).second) {
        if (slot[id] == Tape::none) {
          bail("internal: a value crosses kernels without a state word");
        }
        layout.push_back({t.locs[slot[id]], d.width, d.unsign, 0});
      }
    }
  };
  for (uint32_t p = job.begin; p < job.end; ++p) {
    t.for_each_operand(t.ops[v.order[p]], [&](const Tape::Ref& r) { need(r.id); });
  }
  std::vector<const Root*> my_roots;
  for (const auto& root : v.roots) {
    if (root.kernel == job.index) {
      my_roots.push_back(&root);
      root.for_each_operand([&](const Tape::Ref& r) { need(r.id); });
      if (root.guarded && root.loc != root.home) {
        need(root.hold.id);  // published as the mux: it reads the register
      }
    }
  }

  Cgen_llvm                             k(job.name, Cgen_llvm::State_layout{words, layout});
  absl::flat_hash_map<uint32_t, V>      map;
  absl::flat_hash_map<uint64_t, size_t> arrays;
  for (const auto& [id, index] : in_index) {
    map[id] = k.input(index);
  }
  const auto val = [&](const Tape::Ref& r) {
    auto it = map.find(r.id);
    if (it == map.end()) {
      const auto& d = t.ops[r.id];
      V           made;
      if (d.kind == K::constant) {
        made = k.constant(d.width, d.imm, d.unsign);
      } else if (d.kind == K::constant_words) {
        made = k.constant_words(d.width, std::vector<uint64_t>(t.words.begin() + d.x, t.words.begin() + d.x + d.n), d.unsign);
      } else {
        bail("internal: a value is read before its definition");
      }
      it = map.emplace(r.id, made).first;
    }
    return V{it->second.id, r.width, r.unsign};
  };
  const auto array_of = [&](uint64_t index) {
    auto it = arrays.find(index);
    if (it == arrays.end()) {
      const auto& a = t.arrays[index];
      it            = arrays.emplace(index, k.array_create_in_state(a.bits, a.size, t.locs[a.loc])).first;
    }
    return it->second;
  };
  const auto made = [&](uint32_t id, V value) {
    if (value.width == 0) {
      bail(std::format("internal: LLVM lowering of op {} failed", id));
    }
    map[id] = value;
  };
  for (uint32_t p = job.begin; p < job.end; ++p) {
    const auto  i = v.order[p];
    const auto& o = t.ops[i];
    switch (o.kind) {
      case K::input:
      case K::constant:
      case K::constant_words: break;  // on demand
      case K::resize        : made(i, k.resize(val(o.a), o.width, o.unsign)); break;
      case K::concat        : {
        std::vector<V> lanes;
        for (uint32_t j = 0; j < o.n; ++j) {
          lanes.push_back(val(t.refs[o.x + j]));
        }
        made(i, k.concat(lanes));
        break;
      }
      case K::unary_not : made(i, k.unary_not(val(o.a), o.width, o.unsign)); break;
      case K::reduce_or : made(i, k.reduce_or(val(o.a), o.width, o.unsign)); break;
      case K::count_bits: made(i, k.count_bits(val(o.a), static_cast<uint32_t>(o.imm), o.width, o.bop != 0)); break;
      case K::bitfield_insert:
        made(i,
             k.bitfield_insert(val(o.a),
                               val(o.b),
                               static_cast<uint32_t>(o.imm),
                               static_cast<uint32_t>(o.imm >> 32),
                               o.width,
                               o.unsign));
        break;
      case K::sign_extend: made(i, k.sign_extend_from(val(o.a), static_cast<uint32_t>(o.imm), o.width, o.unsign)); break;
      case K::binary     : made(i, k.binary(static_cast<Op>(o.bop), val(o.a), val(o.b), o.width, o.unsign)); break;
      case K::mux        : made(i, k.mux(val(o.a), val(o.b), val(o.c), o.width, o.unsign)); break;
      case K::array_read : made(i, k.array_read(array_of(o.imm), val(o.a), o.width, o.unsign)); break;
      case K::array_write:
        if (!k.array_write(array_of(o.imm), val(o.a), val(o.b), val(o.c))) {
          bail("memory write");
        }
        break;
      case K::array_clear:
        if (!k.array_clear_if(array_of(o.imm), val(o.a))) {
          bail("memory reset");
        }
        break;
      case K::loop_begin: {
        // Array base pointers must dominate the body and what follows it (a loop is emitted whole,
        // in recorded order, so its ops follow here until its loop_end).
        for (uint32_t q = p; q < job.end && !(t.ops[v.order[q]].kind == K::loop_end && t.ops[v.order[q]].a.id == i); ++q) {
          const auto& body = t.ops[v.order[q]];
          if (body.kind == K::array_read || body.kind == K::array_write || body.kind == K::array_clear) {
            (void)array_of(body.imm);
          }
        }
        std::vector<V> seeds, phis;
        for (uint32_t j = 0; j < o.n; ++j) {
          seeds.push_back(val(t.refs[o.x + j]));
        }
        V index;
        if (!k.loop_begin(o.imm, static_cast<int64_t>(t.words[o.c.id]), o.width, seeds, index, phis)) {
          bail("native loop");
        }
        map[i + 1] = index;
        for (uint32_t j = 0; j < o.n; ++j) {
          map[i + 2 + j] = phis[j];
        }
        break;
      }
      case K::loop_index:
      case K::loop_phi:
      case K::loop_final: break;  // mapped by their loop_begin / loop_end
      case K::loop_end  : {
        std::vector<V> next, fin;
        for (uint32_t j = 0; j < o.n; ++j) {
          next.push_back(val(t.refs[o.x + j]));
        }
        if (!k.loop_end(next, fin)) {
          bail("native loop end");
        }
        for (uint32_t j = 0; j < o.n; ++j) {
          map[i + 1 + j] = fin[j];
        }
        break;
      }
    }
  }
  std::string error;
  for (uint32_t p = job.begin; p < job.end; ++p) {
    const auto i = v.order[p];
    if (v.spill[i] != 0) {
      const auto& d = t.ops[i];
      if (!k.add_state_output(t.locs[slot[i]], t.locs[slot[i]], val(Tape::Ref{i, d.width, d.unsign}), {}, error)) {
        bail("kernel boundary value: " + error);
      }
    }
  }
  for (const auto* root : my_roots) {
    const auto word = t.locs[root->loc];
    bool       ok   = false;
    if (!root->guarded) {
      ok = k.add_state_output(word, word, val(root->value), {}, error);
    } else if (root->loc == root->home) {
      ok = k.add_state_output_if(val(root->guard),
                                 root->when_zero,
                                 word,
                                 k.resize(val(root->data), root->value.width, root->value.unsign),
                                 error);
    } else {
      const V keep = val(root->hold), data = val(root->data);
      ok = k.add_state_output(word,
                              word,
                              k.mux(val(root->guard),
                                    root->when_zero ? data : keep,
                                    root->when_zero ? keep : data,
                                    root->value.width,
                                    root->value.unsign),
                              {},
                              error);
    }
    if (!ok) {
      bail("state output: " + error);
    }
  }
  if (!k.write_object(job.object, error, true, job.object.substr(0, job.object.size() - 1) + "k")) {
    bail("llvm: " + error);
  }
  return k.instruction_count();
}

Result Gen::run(const Sim_objects& out) {
  Result res;
  try {
    Phase_timer timer;
    ln_  = top_ln_;
    top_ = build_frame(top_ln_, "");
    timer.lap("collect (decls/attrs/frames)");
    fr_  = top_;

    // Every piece of state gets a LOCATION now and a word once the kernels are cut (see the layout
    // below). A Field's `word` holds its location until then.
    Tape tape;
    for (auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        f.word = tape.new_loc(f.bits);
      }
    }
    tape.arrays.resize(regs_.size());
    for (size_t i = 0; i < regs_.size(); ++i) {
      auto& r = regs_[i];
      if (r.mem_size != 0) {
        r.field.word   = tape.new_loc(Cgen_llvm::array_state_words(r.field.bits, r.mem_size) * 64);
        tape.arrays[i] = {r.field.bits, r.mem_size, r.field.word};
      } else {
        r.field.word = tape.new_loc(r.field.bits);
      }
    }
    for (auto& f : top_->out) {
      f.word = tape.new_loc(f.bits);
    }

    // ---- one walk: next state, memory writes and the output expressions, all on the tape ----
    k_ = &tape;
    Env top_in;
    for (const auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        top_in[f.name] = tape.input(f.word, f.bits, !f.sign);
      }
    }
    cur_.assign(regs_.size(), V{});
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size == 0) {
        cur_[i] = tape.input(regs_[i].field.word, regs_[i].field.bits, !regs_[i].field.sign);
      }
    }
    next_.assign(regs_.size(), V{});
    inst_env_.clear();
    path_   = V{};
    Env env = top_in;
    for (size_t idx : top_->owned) {
      if (regs_[idx].mem_size == 0) {
        env[regs_[idx].local] = cur_[idx];
      }
    }
    Env pre = env;
    exec_block(top_->stmts, env);
    finish_frame(*top_, pre, env);
    timer.lap("walk");

    View edge, settle;
    edge.tag                      = "e";
    settle.tag                    = "s";
    const bool          no_guards = livehd::debug::env<"LHD_SIM_LNAST_NO_GUARD">() != nullptr;  // debug A/B: plain stores only
    // A register's next value is stored straight into the register when no later kernel still reads
    // the old value; otherwise into a pending location the commit kernel copies. Decided once the
    // kernels are cut: until then every register publishes into a pending location of its own.
    std::vector<size_t> pending(regs_.size(), Tape::none);
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size != 0) {
        continue;
      }
      if (next_[i].width == 0) {
        bail("register '" + regs_[i].field.name + "' has no next state");
      }
      pending[i] = tape.new_loc(regs_[i].field.bits);
      Root root;
      root.value = Tape::ref(next_[i]);
      root.loc   = pending[i];
      root.home  = regs_[i].field.word;
      // `r' = c ? x : r` (either arm): a hold, published under its condition when it lands in place.
      if (const auto& m = tape.ops[next_[i].id]; m.kind == Tape::K::mux && m.width == next_[i].width && m.unsign == next_[i].unsign
                                                 && (m.b.id == cur_[i].id) != (m.c.id == cur_[i].id) && !no_guards) {
        root.guarded   = true;
        root.when_zero = m.c.id == cur_[i].id;
        root.guard     = m.a;
        root.data      = root.when_zero ? m.b : m.c;
        root.hold      = Tape::ref(cur_[i]);
      }
      edge.roots.push_back(root);
    }
    for (auto& f : top_->out) {
      auto it = env.find(f.name);
      if (it == env.end()) {
        bail("output '" + f.name + "' is never assigned");
      }
      Root root;
      root.value = Tape::ref(tape.resize(it->second, f.bits, !f.sign));
      root.loc   = f.word;
      settle.roots.push_back(root);
    }
    k_ = nullptr;

    // The commit kernel reads each distinct reset condition: keep those values alive through the edge.
    std::vector<uint32_t> reset_conditions;
    for (const auto& item : reset_items_) {
      const auto id = static_cast<uint32_t>(item.active.id);
      if (std::ranges::find(reset_conditions, id) == reset_conditions.end()) {
        reset_conditions.push_back(id);
      }
    }
    std::vector<size_t> slot(tape.ops.size(), Tape::none);
    auto                give_slot = [&](uint32_t id) {
      if (slot[id] == Tape::none && !Tape::is_free(tape.ops[id].kind)) {
        slot[id] = tape.new_loc(tape.ops[id].width);
      }
    };
    for (const auto id : reset_conditions) {
      give_slot(id);
      if (slot[id] != Tape::none) {
        Root root;
        root.value = Tape::Ref{id, tape.ops[id].width, tape.ops[id].unsign};
        root.loc   = slot[id];
        edge.roots.push_back(root);
      }
    }
    order_view(tape, edge, true);
    order_view(tape, settle, false);
    cut_kernels(tape, edge);
    cut_kernels(tape, settle);
    for (const View* v : {&edge, &settle}) {
      for (uint32_t i = 0; i < v->spill.size(); ++i) {
        if (v->spill[i] != 0) {
          give_slot(i);
        }
      }
    }

    // In place: the kernel that publishes register i's next value is the last edge kernel to read Q(i)
    // (a kernel loads what it reads before it stores anything).
    size_t in_place = 0;
    {
      const auto kernel_of = [&](uint32_t id) { return static_cast<std::ptrdiff_t>(edge.kernel_of(id)); };
      absl::flat_hash_map<uint32_t, std::ptrdiff_t> last_reader;  // input op -> last kernel reading it
      for (const auto i : edge.order) {
        const auto mine = kernel_of(i);
        tape.for_each_operand(tape.ops[i], [&](const Tape::Ref& r) {
          if (tape.ops[r.id].kind == Tape::K::input) {
            auto& last = last_reader[r.id];
            last       = std::max<std::ptrdiff_t>(last, mine);
          }
        });
      }
      absl::flat_hash_map<size_t, std::ptrdiff_t> writer;  // pending location -> the kernel publishing it
      for (const auto& root : edge.roots) {
        root.for_each_operand([&](const Tape::Ref& r) {
          if (tape.ops[r.id].kind == Tape::K::input) {
            auto& last = last_reader[r.id];
            last       = std::max<std::ptrdiff_t>(last, static_cast<std::ptrdiff_t>(root.kernel));
          }
        });
        writer[root.loc] = static_cast<std::ptrdiff_t>(root.kernel);
      }
      absl::flat_hash_map<size_t, size_t> redirect;  // pending location -> the register's own
      for (size_t i = 0; i < regs_.size(); ++i) {
        if (regs_[i].mem_size != 0) {
          continue;
        }
        const auto reader = last_reader.find(static_cast<uint32_t>(cur_[i].id));
        if (reader == last_reader.end() || reader->second <= writer.at(pending[i])) {
          redirect[pending[i]] = regs_[i].field.word;
          pending[i]           = Tape::none;
          ++in_place;
        }
      }
      for (auto& root : edge.roots) {
        if (auto it = redirect.find(root.loc); it != redirect.end()) {
          root.loc = it->second;
        }
      }
    }

    // ---- layout: inputs; registers written in place; the others twice (current, then pending: the
    // commit is one copy); memories; outputs; the words kernels pass values through ----
    auto place = [&](size_t loc) {
      if (tape.locs[loc] == Tape::none) {
        tape.locs[loc]  = words_;
        words_         += (tape.loc_bits[loc] + 63) / 64;
      }
    };
    for (auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        place(f.word);
      }
    }
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size == 0 && pending[i] == Tape::none) {
        place(regs_[i].field.word);
      }
    }
    const size_t q_base = words_;
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size == 0 && pending[i] != Tape::none) {
        place(regs_[i].field.word);
      }
    }
    const size_t q_words = words_ - q_base;
    const size_t d_base  = words_;
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size == 0 && pending[i] != Tape::none) {
        place(pending[i]);
      }
    }
    for (size_t loc = 0; loc < tape.locs.size(); ++loc) {
      place(loc);  // memories, outputs and boundary words, in creation order
    }
    for (auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        f.word = tape.locs[f.word];
      }
    }
    for (auto& r : regs_) {
      r.field.word = tape.locs[r.field.word];
    }
    for (auto& f : top_->out) {
      f.word = tape.locs[f.word];
    }
    timer.lap("prune + cut + layout");

    // ---- kernels: replayed and compiled on the shared workers ----
    const auto              stem = out.object.ends_with(".llvm.o") ? out.object.substr(0, out.object.size() - 7) : out.object;
    std::vector<Kernel_job> jobs;
    for (const View* v : {&edge, &settle}) {
      auto& names = v == &edge ? res.edge_functions : res.settle_functions;
      for (size_t c = 0; c + 1 < v->cut.size(); ++c) {
        Kernel_job job;
        job.name   = std::format("{}_{}{}", out.symbol, v->tag, c);
        job.object = std::format("{}-{}{}.llvm.o", stem, v->tag, c);
        job.view   = v;
        job.index  = c;
        job.begin  = v->cut[c];
        job.end    = v->cut[c + 1];
        names.push_back(job.name);
        jobs.push_back(std::move(job));
      }
    }
    const size_t words = words_;
    {
      livehd::sim::Compile_workers                             workers;
      std::vector<std::future<std::pair<size_t, std::string>>> running;
      for (const auto& job : jobs) {
        running.push_back(workers.submit(
            [&tape, &job, &slot, words]() -> std::pair<size_t, std::string> {
              try {
                return {build_kernel(tape, job, slot, words), {}};
              } catch (const Unsupported& u) {
                return {0, u.why};
              }
            },
            std::max<uint64_t>(1, job.end - job.begin)));
      }
      std::string failure;
      for (auto& p : running) {
        auto [count, why]  = p.get();
        res.instructions  += count;
        if (!why.empty() && failure.empty()) {
          failure = why;
        }
      }
      if (!failure.empty()) {
        bail(failure);
      }
    }
    for (const auto& job : jobs) {
      res.objects.push_back(job.object);
    }
    timer.lap("kernels (LLVM, parallel)");

    // ---- the commit kernel: next state becomes state; a register in reset takes its initial value ----
    if (q_words != 0 || !reset_items_.empty()) {
      std::vector<Cgen_llvm::State_input> layout;
      for (const auto id : reset_conditions) {
        const auto& d = tape.ops[id];
        layout.push_back({tape.locs[d.kind == Tape::K::input ? static_cast<size_t>(d.imm) : slot[id]], d.width, d.unsign, 0});
      }
      res.commit_function = out.symbol + "_c";
      Cgen_llvm      k(res.commit_function, Cgen_llvm::State_layout{words, layout});
      std::string    error;
      // The reset conditions were stored by the edge kernels: read them before the copy below.
      std::vector<V> active;
      for (size_t i = 0; i < reset_conditions.size(); ++i) {
        const auto& d = tape.ops[reset_conditions[i]];
        active.push_back(d.kind == Tape::K::constant ? k.constant(d.width, d.imm, d.unsign) : k.input(i));
      }
      if (!k.copy_state(q_base, d_base, q_words, error)) {
        bail("commit: " + error);
      }
      for (size_t i = 0; i < reset_conditions.size(); ++i) {
        std::vector<std::pair<size_t, V>> stores;
        for (const auto& item : reset_items_) {
          if (item.active.id == reset_conditions[i]) {
            const auto& f = regs_[item.idx].field;
            stores.emplace_back(f.word, k.constant_words(f.bits, twos_words(item.init, f.bits), !f.sign));
          }
        }
        if (!k.state_store_if(active[i], stores)) {
          bail("commit: register reset");
        }
      }
      const auto object = stem + "-c.llvm.o";
      if (!k.write_object(object, error, true, object.substr(0, object.size() - 1) + "k")) {
        bail("llvm: " + error);
      }
      res.instructions += k.instruction_count();
      res.objects.push_back(object);
    }
    timer.lap("commit kernel");
    if (timer.on) {
      std::fprintf(stderr,
                   "lnast_llvm: %zu ops (%zu edge, %zu settle live; %llu + %llu words of code), %zu edge + %zu settle kernels, %zu "
                   "state words, %zu registers in place, %zu words copied at commit\n",
                   tape.ops.size(),
                   edge.count_live(),
                   settle.count_live(),
                   static_cast<unsigned long long>(edge.size),
                   static_cast<unsigned long long>(settle.size),
                   res.edge_functions.size(),
                   res.settle_functions.size(),
                   words,
                   in_place,
                   q_words);
    }

    std::vector<Field> state_inputs;
    for (const auto& f : top_->in) {
      if (!top_->clocks.contains(f.name)) {
        state_inputs.push_back(f);
      } else {
        res.clocks.push_back(f.name);
      }
    }
    std::vector<Field> reg_fields;
    for (auto& r : regs_) {
      Field f = r.field;
      if (r.mem_size != 0) {
        f.entries    = r.mem_size;
        f.entry_bits = r.field.bits;
        f.bits       = static_cast<uint32_t>(Cgen_llvm::array_state_words(r.field.bits, r.mem_size) * 64);
        f.sign       = false;
      }
      if (r.initial) {
        f.has_init = true;
        if (r.mem_size == 0) {
          f.init_words = twos_words(*r.initial, std::max<uint32_t>(f.bits, 1));
          f.init       = f.init_words.empty() ? 0 : f.init_words[0];
        }
      }
      reg_fields.push_back(f);
    }
    if (!out.json.empty()) {
      std::ofstream js(out.json);
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
      auto list = [&](const char* key, const std::vector<std::string>& v) {
        js << "  \"" << key << "\": [";
        for (size_t i = 0; i < v.size(); ++i) {
          js << (i ? ", " : "") << "\"" << v[i] << "\"";
        }
        js << "],\n";
      };
      js << "{\n  \"words\": " << words << ",\n  \"commit\": \"" << res.commit_function << "\",\n";
      list("edge", res.edge_functions);
      list("settle", res.settle_functions);
      list("objects", res.objects);
      dump("inputs", state_inputs, false);
      js << ",\n";
      dump("outputs", top_->out, false);
      js << ",\n";
      dump("regs", reg_fields, true);
      js << "\n}\n";
    }

    res.inputs  = state_inputs;
    res.outputs = top_->out;
    res.regs    = reg_fields;
    res.words   = words;
    res.ok      = true;
  } catch (const Unsupported& u) {
    res.ok  = false;
    res.why = u.why;
  }
  return res;
}

}  // namespace

namespace {
std::string symbol_of(const Lnast& ln) {
  std::string name(ln.get_top_module_name());
  if (name.empty()) {
    name = "top";
  }
  for (auto& ch : name) {
    if (!std::isalnum(static_cast<unsigned char>(ch))) {
      ch = '_';
    }
  }
  return "lnast_" + name;
}
}  // namespace

Result generate(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, const std::string& dir) {
  std::filesystem::create_directories(dir);
  const auto symbol = symbol_of(ln);
  Gen        g(ln, lib, symbol);
  return g.run(Sim_objects{symbol, dir + "/kernel.llvm.o", dir + "/kernel.json"});
}

Result generate_sim(const Lnast& ln, const std::vector<std::shared_ptr<Lnast>>& lib, const Sim_objects& out) {
  Sim_objects o = out;
  if (o.symbol.empty()) {
    o.symbol = symbol_of(ln);
  }
  Gen g(ln, lib, o.symbol);
  return g.run(o);
}

}  // namespace livehd::lnast_llvm
