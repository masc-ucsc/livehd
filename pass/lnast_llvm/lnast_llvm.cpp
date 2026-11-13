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
  // A 0ub/0sb pattern with `?` bits (read as 0 in `mag`): `unk` marks them, `raw` is the known bits as
  // the n-bit pattern, `raw_bits` = n and `raw_signed` whether the pattern is a two's complement value.
  std::vector<uint64_t> unk, raw;
  uint32_t              raw_bits   = 0;
  bool                  raw_signed = false;
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
    {
      // which bits were `?` / x / z: the loop above already turned them into '0'
      std::vector<uint64_t> unk((n + 63) / 64, 0);
      bool                  any = false;
      size_t                at  = 0;
      for (char c : std::string_view(t)) {  // `t` is the text after the prefix; walk it MSB first
        if (c == '_') {
          continue;
        }
        if (c == '?' || c == 'x' || c == 'X' || c == 'z' || c == 'Z') {
          const size_t bit = n - 1 - at;
          unk[bit / 64]   |= uint64_t{1} << (bit % 64);
          any              = true;
        }
        ++at;
      }
      if (any) {
        l.unk        = std::move(unk);
        l.raw        = l.mag;
        l.raw_bits   = static_cast<uint32_t>(n);
        l.raw_signed = is_signed;
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
    array_clear,      // imm: array; a: condition; b (optional): the image reloaded instead of zero (side effect)
    loop_begin,       // imm: trip count; width: index bits; x, n: refs_ (carry seeds); c.id: words_ index of `first`
    loop_index,       // a: its loop_begin
    loop_phi,         // a: its loop_begin; imm: carry number
    loop_end,         // a: its loop_begin; x, n: refs_ (carry next values)
    loop_final,       // a: its loop_end; imm: carry number
    clock,            // the root clock's level in the view being replayed (0: low, 1: high)
    latch,            // a: enable (1 bit), b: data, c: the held value (an input); imm: register index
    wire,             // a: the value it carries, known later (set_wire): a net read at its settled value
    lane_get,         // a: a wide value, b: bit position (+ imm); the `width` bits from there (recognize_lanes)
    lane_set,         // a: a wide value, b: bit position, c: value; imm: lane bits; `a` with that lane replaced
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
  // Per loop_begin op: the carries (by index) that live in a lane buffer for the loop (recognize_lanes).
  absl::flat_hash_map<uint32_t, std::vector<uint8_t>> memory_carries;

  static Ref  ref(const V& v) { return Ref{static_cast<uint32_t>(v.id), v.width, v.unsign}; }
  V           value(uint32_t id) const { return V{id, ops[id].width, ops[id].unsign}; }
  static bool is_free(K k) { return k == K::input || k == K::constant || k == K::constant_words || k == K::clock; }

  // The root clock as a value: a constant in each view (a clock gate, a latch on a clock phase and
  // any logic that reads the clock fold accordingly).
  V clock() {
    if (clock_ == none) {
      Op o;
      o.kind = K::clock;
      clock_ = static_cast<uint32_t>(add(o, 1, true).id);
    }
    return value(clock_);
  }
  // A transparent latch, as its readers see it: `enable ? data : held`. Created when the walk first
  // needs the latch's value and completed (set_latch) once its data and enable are known, so the
  // tape is a dataflow graph, not a list in dependency order: each view orders it for itself.
  V latch(V held, size_t reg) {
    Op o;
    o.kind = K::latch;
    o.c    = ref(held);
    o.imm  = reg;
    return add(o, held.width, held.unsign);
  }
  void set_latch(size_t id, V enable, V data) {
    ops[id].a = ref(enable);
    ops[id].b = ref(data);
  }
  // A net read before the statement that assigns it (`delay_assign tmp net 1`: the settled value).
  // Like a latch, created where it is first read and completed when the value is known.
  V wire(uint32_t width, bool unsign) {
    Op o;
    o.kind = K::wire;
    return add(o, width, unsign);
  }
  void     set_wire(size_t id, V value) { ops[id].a = ref(value); }
  // What a (complete) wire carries: the op at the end of a chain of wires. `none`: the chain closes
  // on itself (nets assigned from each other and from nothing else).
  uint32_t carried(uint32_t id) const {
    for (size_t hops = 0; ops[id].kind == K::wire; ++hops) {
      if (ops[id].a.id == none || hops > ops.size()) {
        return none;
      }
      id = ops[id].a.id;
    }
    return id;
  }
  // Once every wire is complete its readers take the value itself, and the wires are dead. A wire is
  // not a value of its own: left in place, two registers reset by one signal through two nets would
  // look like two resets, and a register enabled through a net like no hold at all.
  bool resolve_wires() {
    bool       ok  = true;
    const auto fix = [&](Ref& r) {
      if (r.id != none && ops[r.id].kind == K::wire) {
        const auto to = carried(r.id);
        ok            = ok && to != none;
        r.id          = to != none ? to : r.id;
      }
    };
    for (auto& o : ops) {
      if (o.kind == K::wire) {
        continue;
      }
      for (Ref* r : {&o.a, &o.b, &o.c}) {
        if (!(o.kind == K::loop_begin && r == &o.c)) {
          fix(*r);
        }
      }
    }
    for (auto& r : refs) {
      fix(r);
    }
    return ok;
  }
  bool in_loop() const { return !open_.empty(); }

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
  bool array_clear_if(size_t array, V condition, V image = V{}) {
    Op o;
    o.kind = K::array_clear;
    o.imm  = array;
    o.a    = ref(condition);
    if (image.width != 0) {
      o.b = ref(image);
    }
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
  uint32_t                                                 clock_ = none;

public:
  absl::flat_hash_map<uint32_t, std::string> names;  // diagnostics: latch op -> the latch's name
};

// Lane access at a position that depends on a loop index. `x#[i*8..+8] = y#[(31-i)*8..+8]` reaches
// the tape as arithmetic on the WHOLE values:
//   read    low W bits of (y >> p)
//   write   (x & ((1 << p) - 1)) | (((x >> p) with its low W bits replaced by v) << p), cut to x's width
// In a rolled loop that is several variable shifts of a wide value per trip (bitcoin's 32-trip byte
// swap of a 256-bit hash: about 300 instructions a trip, 8.5 times slower than the same loop
// unrolled). Inside loops the two shapes become one op each (lane_get, lane_set), and a carried
// value that is only ever read and written through them lives in a buffer while the loop runs
// (Tape::memory_carries): an aligned lane is then one load or one store.
void recognize_lanes(Tape& t) {
  using K         = Tape::K;
  // through resizes that keep every bit (the same value, zero-extended or not at all)
  const auto same = [&](uint32_t id) {
    while (t.ops[id].kind == K::resize && t.ops[id].a.unsign && t.ops[id].width >= t.ops[id].a.width
           && t.ops[id].a.width == t.ops[t.ops[id].a.id].width) {
      id = t.ops[id].a.id;
    }
    return id;
  };
  const auto is_binary = [&](uint32_t id, Op op) { return t.ops[id].kind == K::binary && static_cast<Op>(t.ops[id].bop) == op; };
  const auto is_one    = [&](uint32_t id) {
    id = same(id);
    return t.ops[id].kind == K::constant && t.ops[id].imm == 1;
  };
  // `s` is a right shift: the wide value read, where (the position op), and a constant added to it.
  // `(x >> p) >> 8` is the lane at p + 8 of x -- how `(x >> p)#[8..<16]` arrives.
  struct Lane_read {
    uint32_t  x = Tape::none;
    Tape::Ref at;
    uint64_t  plus = 0;
  };
  const auto lane_read = [&](uint32_t s) -> Lane_read {
    if (!is_binary(s, Op::lshr) || !t.ops[s].a.unsign) {
      return {};
    }
    const auto inner = same(t.ops[s].a.id);
    const auto by    = same(t.ops[s].b.id);
    if (t.ops[by].kind == K::constant && is_binary(inner, Op::lshr) && t.ops[inner].a.unsign
        && t.ops[inner].width >= t.ops[s].width) {
      const auto x = same(t.ops[inner].a.id);
      if (t.ops[x].width > 64 && t.ops[x].unsign && t.ops[by].imm < t.ops[x].width) {
        return {x, t.ops[inner].b, t.ops[by].imm};
      }
    }
    const auto x = same(t.ops[s].a.id);
    if (t.ops[x].width > 64 && t.ops[x].unsign) {
      return {x, t.ops[s].b, 0};
    }
    return {};
  };
  std::vector<uint8_t> in_loop(t.ops.size(), 0);
  for (const auto& [lb, le] : t.loops) {
    for (uint32_t i = lb; i <= le; ++i) {
      in_loop[i] = 1;
    }
  }
  const auto n = static_cast<uint32_t>(t.ops.size());
  for (uint32_t i = 0; i < n; ++i) {
    if (in_loop[i] == 0) {
      continue;
    }
    const Tape::Op o = t.ops[i];
    if (o.kind == K::resize && o.width <= 64 && o.a.width >= o.width) {
      // low W bits of (x >> p)
      const auto s = same(o.a.id);
      if (is_binary(s, Op::lshr) && t.ops[s].width >= o.width) {
        if (const auto read = lane_read(s); read.x != Tape::none) {
          auto& g = t.ops[i];
          g.kind  = K::lane_get;
          g.a     = Tape::Ref{read.x, t.ops[read.x].width, true};
          g.b     = read.at;
          g.c     = Tape::Ref{};
          g.imm   = read.plus;
        }
      }
      continue;
    }
    if (is_binary(i, Op::bit_and) && o.width <= 64) {
      // (x >> p) & (2^W - 1): how an indexed read of an array carried as a packed value arrives
      for (int swap = 0; swap < 2; ++swap) {
        const auto s    = same(swap == 0 ? o.a.id : o.b.id);
        const auto mask = same(swap == 0 ? o.b.id : o.a.id);
        if (!is_binary(s, Op::lshr) || t.ops[mask].kind != K::constant || t.ops[s].width < o.width
            || t.ops[mask].imm != (o.width == 64 ? ~uint64_t{0} : (uint64_t{1} << o.width) - 1)) {
          continue;
        }
        if (const auto read = lane_read(s); read.x != Tape::none) {
          auto& g = t.ops[i];
          g.kind  = K::lane_get;
          g.bop   = 0;
          g.a     = Tape::Ref{read.x, t.ops[read.x].width, true};
          g.b     = read.at;
          g.c     = Tape::Ref{};
          g.imm   = read.plus;
          break;
        }
      }
      continue;
    }
    if (!is_binary(i, Op::bit_or) || o.width <= 64 || !o.unsign) {
      continue;
    }
    for (int swap = 0; swap < 2; ++swap) {
      const auto low  = same(swap == 0 ? o.a.id : o.b.id);  // x & ((1 << p) - 1)
      auto       high = swap == 0 ? o.b.id : o.a.id;        // ((x >> p) with the lane replaced) << p
      if (!is_binary(low, Op::bit_and)) {
        continue;
      }
      if (t.ops[high].kind == K::resize && t.ops[high].width == o.width) {
        high = t.ops[high].a.id;  // the cut to x's width
      }
      high = same(high);
      if (!is_binary(high, Op::shl) || t.ops[high].width < o.width) {
        continue;
      }
      const auto filled = same(t.ops[high].a.id);
      const auto at     = same(t.ops[high].b.id);
      if (t.ops[filled].kind != K::bitfield_insert || static_cast<uint32_t>(t.ops[filled].imm) != 0) {
        continue;
      }
      const auto lane = static_cast<uint32_t>(t.ops[filled].imm >> 32);
      const auto down = same(t.ops[filled].a.id);
      if (lane == 0 || lane > 64 || !is_binary(down, Op::lshr) || same(t.ops[down].b.id) != at) {
        continue;
      }
      const auto x = same(t.ops[down].a.id);
      if (t.ops[x].width != o.width || !t.ops[x].unsign || t.ops[filled].width < o.width || t.ops[down].width < o.width) {
        continue;
      }
      bool masked = false;
      for (int side = 0; side < 2 && !masked; ++side) {
        const auto kept = same(side == 0 ? t.ops[low].a.id : t.ops[low].b.id);
        const auto mask = same(side == 0 ? t.ops[low].b.id : t.ops[low].a.id);
        if (kept != x || !is_binary(mask, Op::sub) || !is_one(t.ops[mask].b.id)) {
          continue;
        }
        const auto bit = same(t.ops[mask].a.id);
        masked         = is_binary(bit, Op::shl) && is_one(t.ops[bit].a.id) && same(t.ops[bit].b.id) == at;
      }
      if (!masked) {
        continue;
      }
      auto& w = t.ops[i];
      w.kind  = K::lane_set;
      w.bop   = 0;
      w.a     = Tape::Ref{x, o.width, true};
      w.b     = t.ops[high].b;
      w.c     = t.ops[filled].b;
      w.imm   = lane;
      break;
    }
  }
  // A carried value lives in a buffer when, of the ops of its loop that are still needed, exactly
  // the lane ops touch it: reads of the current content, then the one write that makes the next
  // content, and so on to the value the loop carries on.
  for (uint32_t lb = 0; lb < n; ++lb) {
    if (t.ops[lb].kind != K::loop_begin || t.ops[lb].n == 0) {
      continue;
    }
    uint32_t le = lb + 1;
    while (le < n && !(t.ops[le].kind == K::loop_end && t.ops[le].a.id == lb)) {
      ++le;
    }
    if (le == n) {
      continue;
    }
    // what the loop needs of its own ops: the carries' next values and its memory writes
    absl::flat_hash_map<uint32_t, std::vector<uint32_t>> users;
    {
      std::vector<uint8_t>  live(le - lb + 1, 0);
      std::vector<uint32_t> work;
      const auto            want = [&](uint32_t id) {
        if (id > lb && id < le && live[id - lb] == 0) {
          live[id - lb] = 1;
          work.push_back(id);
        }
      };
      for (uint32_t j = 0; j < t.ops[le].n; ++j) {
        want(t.refs[t.ops[le].x + j].id);
      }
      for (uint32_t i = lb + 1; i < le; ++i) {
        if (t.ops[i].kind == K::array_write || t.ops[i].kind == K::array_clear || t.ops[i].kind == K::loop_end) {
          want(i);
        }
      }
      while (!work.empty()) {
        const auto i = work.back();
        work.pop_back();
        t.for_each_operand(t.ops[i], [&](const Tape::Ref& r) {
          users[r.id].push_back(i);
          want(r.id);
        });
      }
    }
    std::vector<uint8_t> flags(t.ops[lb].n, 0);
    bool                 any = false;
    for (uint32_t j = 0; j < t.ops[lb].n; ++j) {
      const uint32_t phi = lb + 2 + j;
      if (t.ops[phi].width <= 64 || t.ops[phi].width % 64 != 0 || !t.ops[phi].unsign) {
        continue;
      }
      const auto last   = same(t.refs[t.ops[le].x + j].id);
      uint32_t   cur    = phi;
      size_t     writes = 0;
      bool       ok     = true;
      while (ok && cur != last) {
        uint32_t write = Tape::none;
        if (auto it = users.find(cur); it != users.end()) {
          for (const auto u : it->second) {
            const auto& uo = t.ops[u];
            if (uo.kind == K::lane_set && uo.a.id == cur && uo.b.id != cur && uo.c.id != cur
                && (write == Tape::none || write == u)) {
              write = u;
            } else if (!(uo.kind == K::lane_get && uo.a.id == cur && uo.b.id != cur)) {
              ok = false;
            }
          }
          for (const auto u : it->second) {  // every read of this content is recorded before the write
            ok = ok && (u == write || (write != Tape::none && u < write));
          }
        }
        ok  = ok && write != Tape::none;
        cur = write;
        ++writes;
      }
      if (ok && writes != 0) {
        if (auto it = users.find(last); it != users.end()) {  // the final content: read only as lanes
          for (const auto u : it->second) {
            ok = ok && t.ops[u].kind == K::lane_get && t.ops[u].a.id == last;
          }
        }
        if (ok) {
          flags[j] = 1;
          any      = true;
        }
      }
    }
    if (any) {
      t.memory_carries[lb] = std::move(flags);
    }
  }
}

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
    std::optional<Literal>   image;  // a memory's non-zero initial image: size*bits packed, entry 0 low
    bool                       has_clock = false;
    uint64_t                   mem_size  = 0;  // > 0: a memory of mem_size entries (field.bits = entry bits)
    // What the element is clocked by (a clock port or a clock gate's output; empty: the root clock) and
    // on which edge; a latch has no clock: it follows its data while `enable` (active high unless
    // !enable_high) holds.
    std::string                clock_name;
    bool                       negedge     = false;
    bool                       latch       = false;
    bool                       has_enable  = false;
    bool                       enable_high = true;
    std::string                enable_name;
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
    // Nets that may be read at their settled value before they are assigned (`declare ... mut`), and
    // the tape op standing for each one that was (completed in finish_frame).
    absl::flat_hash_map<std::string, Type>   wires;
    absl::flat_hash_map<std::string, V>      wire_ops;
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
  // The enclosing `if` conditions, as (condition, taken when it is zero). Their AND is only built
  // when something asks for it (a memory write under a condition): an `if` whose arms only assign
  // merges by mux on the condition itself, and a 257-arm unique_if used to record three path ops per
  // arm that nothing read.
  std::vector<std::pair<V, bool>> path_terms_;
  std::vector<V>                  path_made_;  // path_made_[i]: AND of terms [0, i], or width 0: not built
  bool                            under_condition() const { return !path_terms_.empty(); }
  void                            push_path(V condition, bool when_zero) {
    path_terms_.emplace_back(condition, when_zero);
    path_made_.emplace_back();
  }
  void pop_path() {
    path_terms_.pop_back();
    path_made_.pop_back();
  }
  V path_enable() {
    if (path_terms_.empty()) {
      return k_->constant(1, 1, true);
    }
    size_t start = 0;
    V      acc;
    for (size_t j = path_terms_.size(); j-- > 0;) {
      if (path_made_[j].width != 0) {
        acc   = path_made_[j];
        start = j + 1;
        break;
      }
    }
    for (size_t j = start; j < path_terms_.size(); ++j) {
      const V term  = k_->binary(path_terms_[j].second ? Op::eq : Op::ne, path_terms_[j].first, k_->constant(1, 0, true), 1, true);
      acc           = acc.width != 0 ? k_->binary(Op::bit_and, acc, term, 1, true) : term;
      path_made_[j] = acc;
    }
    return acc;
  }
  // A memory write happens when its statement is reached AND its (possibly gated) clock takes the edge.
  V                     write_enable(const Reg_info& r, const Env& env) {
    V enable = path_enable();
    if (!r.clock_name.empty()) {
      auto it = env.find(r.clock_name);
      if (it == env.end()) {
        bail("memory '" + r.field.name + "' is clocked by unknown signal '" + r.clock_name + "'");
      }
      if (const V gate = all_of(clock_enables(it->second, "memory '" + r.field.name + "'")); gate.width != 0) {
        enable = k_->binary(Op::bit_and, enable, gate, 1, true);
      }
    }
    return enable;
  }
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
  // ---- clocks, clock gates and latches ----
  // A clock gate `Clock(clock_pin=clk, enable=en)` is what an ICG is: `clk & h`, where the latch `h`
  // follows `en` while `clk` is low (`invert`: `clk | !h`, `h` open while `clk` is high). Its output
  // is an ordinary value, so anything may read it; a register clocked by it takes its edge only
  // while `h` is set, evaluated in the view the register is sampled in.
  struct Gate {
    V    clk;    // the clock it gates (the root clock or another gate's output)
    V    latch;  // h
    bool invert = false;
    // Recognized from a plain `latch & clk` AND (not a Clock() cell): glitch-free only when the latch
    // is closed while `clk` is high, which is checked once every latch is complete (check_and_gates).
    bool from_and = false;
  };
  void check_and_gates();
  struct Hidden_latch {
    std::string name;
    size_t      loc = 0;
    size_t      op  = 0;
  };
  absl::flat_hash_map<size_t, Gate> gates_;      // gate output op -> its parts
  std::vector<Hidden_latch>         hidden_;     // one per clock gate: state the design does not name
  struct Unknown_site {  // one `?` literal drawn at power-on
    std::string name;
    size_t      loc  = 0;
    uint32_t    bits = 0;
  };
  bool                              draw_unknown_ = false;  // sim.unknown_zero is off
  std::vector<Unknown_site>         unknown_sites_;
  absl::flat_hash_map<std::string, size_t> unknown_locs_;  // literal -> its draw's state location (one draw per literal)
  // sim.warn_undefined: one site per statement that can compute a value the source leaves undefined
  // (a division or remainder by zero, a memory access outside its array). Each site is a one-word
  // state array (Tape::arrays, past the registers') that the rise view writes 1 into when the access
  // happens: an effect, so it is never pruned, and one array per site, so the sites are not chained
  // in memory order. The locations are created together after the walk, so the words are
  // contiguous; the class around the kernels polls them after each clock (lnast_sim.cpp).
  struct Undefined_site {
    std::string what, where;
    size_t      array = 0;
  };
  bool                              warn_undefined_ = false;
  std::vector<Undefined_site>       undefined_sites_;
  void                              undefined_if(V condition, std::string_view what, const Lnast_nid& stmt, bool path = true);
  void                              undefined_at(V condition, std::string_view what, std::string where, bool path);
  // Out-of-range condition of a memory address (none when the address cannot leave the array).
  V                                 outside(V address, uint64_t size);
  std::vector<size_t>               latch_ops_;  // every latch op (registers declared `latch`, and the hidden ones)
  // What a latch's held value follows: `if (guard) held = data`, in every view where the guard can be on.
  struct Latch_update {
    size_t   loc = 0;
    V        guard, data;
    uint32_t bits   = 0;
    bool     unsign = true;
  };
  std::vector<Latch_update> latch_updates_;
  std::vector<V>            held_;  // per register: a latch's held value (the input op)
  void                      exec_clock_gate(const std::vector<Lnast_nid>& c, Env& env);
  std::vector<V>            clock_enables(V clock, const std::string& what);
  struct Late_gate {
    size_t      op;     // the 1-bit wire standing for "every gate from here to the root clock is on"
    V           clock;  // where the search stopped
    std::string what;
  };
  std::vector<Late_gate> late_gates_;
  void                   finish_late_gates() {
    for (size_t i = 0; i < late_gates_.size(); ++i) {  // completing one may add another (a longer chain)
      const auto g = late_gates_[i];
      if (k_->ops[g.clock.id].a.id == Tape::none) {
        bail(g.what + " is clocked through a net that is never assigned");
      }
      const V rest = all_of(clock_enables(g.clock, g.what));
      k_->set_wire(g.op, rest.width != 0 ? rest : k_->constant(1, 1, true));
    }
    late_gates_.clear();
  }
  // AND of `enables` (empty: no value, i.e. always).
  V                         all_of(const std::vector<V>& enables) {
    V acc;
    for (const auto& e : enables) {
      acc = acc.width == 0 ? e : k_->binary(Op::bit_and, acc, e, 1, true);
    }
    return acc;
  }
  const V* signal(const std::string& name, const Env& pre, const Env& end) const {
    if (auto e = end.find(name); e != end.end()) {
      return &e->second;
    }
    if (auto q = pre.find(name); q != pre.end()) {
      return &q->second;
    }
    return nullptr;
  }
  void                                finish_latch(Frame& f, size_t idx, const Env& pre, const Env& end);
  void                                bypass_exclusive_latches();
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
  // LHD_SIM_LNAST_UNROLL=N / LHD_SIM_LNAST_NO_UNROLL (the same as N = 0): see exec_for.
  static std::optional<uint64_t> unroll_limit() {
    if (livehd::debug::env<"LHD_SIM_LNAST_NO_UNROLL">() != nullptr) {
      return 0;
    }
    if (const char* text = livehd::debug::env<"LHD_SIM_LNAST_UNROLL">()) {
      return std::strtoull(text, nullptr, 10);
    }
    return std::nullopt;
  }
  // A `for` anywhere under `stmts`.
  bool has_loop(const Lnast_nid& stmts) const {
    for (const auto& st : kids(stmts)) {
      if (ty(st) == Lnast_ntype::Lnast_ntype_for) {
        return true;
      }
      for (const auto& k : kids(st)) {
        if (ty(k) == Lnast_ntype::Lnast_ntype_stmts && has_loop(k)) {
          return true;
        }
      }
    }
    return false;
  }
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

void Gen::undefined_if(V condition, std::string_view what, const Lnast_nid& stmt, bool path) {
  // A rolled native loop body is not instrumented (as in inou.cgen.sim): an effect inside it would
  // change how the loop is lowered.
  if (!warn_undefined_ || k_ == nullptr || k_->in_loop() || condition.width == 0) {
    return;
  }
  std::string where;
  if (const auto sp = ln_->span_of_nearest(stmt); sp.start_line) {
    where = std::format("{}:{}", sp.file.empty() ? std::string{"?"} : sp.file, *sp.start_line);
  } else if (!sp.file.empty()) {
    where = sp.file;
  } else {  // no source line: the module and the value, as inou.cgen.sim names a cell
    const auto c = kids(stmt);
    where        = std::string(ln_->get_top_module_name());
    if (!c.empty() && ty(c[0]) == Lnast_ntype::Lnast_ntype_ref) {
      where += "." + nm(c[0]);
    }
  }
  undefined_at(condition, what, std::move(where), path);
}

void Gen::undefined_at(V condition, std::string_view what, std::string where, bool path) {
  if (!warn_undefined_ || k_ == nullptr || k_->in_loop() || condition.width == 0) {
    return;
  }
  const V hit = path && under_condition() ? k_->binary(Op::bit_and, condition, path_enable(), 1, true) : condition;
  k_->arrays.push_back(Tape::Array{64, 1, Tape::none});  // its location is created after the walk
  const size_t array = k_->arrays.size() - 1;
  undefined_sites_.push_back({std::string(what), std::move(where), array});
  (void)k_->array_write(array, hit, k_->constant(1, 0, true), k_->constant(64, 1, true));
}

V Gen::outside(V address, uint64_t size) {
  if (address.unsign && address.width < 64 && (uint64_t{1} << address.width) <= size) {
    return V{};  // every value of the address is an entry
  }
  const V past = k_->binary(Op::ge, address, k_->constant(64, size, true), 1, true);
  if (address.unsign) {
    return past;
  }
  const V negative = k_->binary(Op::lt, address, k_->constant(1, 0, true), 1, true);
  return k_->binary(Op::bit_or, past, negative, 1, true);
}

V Gen::literal(const Literal& l) {
  if (draw_unknown_ && !l.unk.empty()) {
    // Unknown bits drawn once per run: a state word nothing writes, filled at power-on from the seeded PRNG
    // (a field with fill 2), under the literal's mask; the known bits are fixed.
    const uint32_t n = l.raw_bits;
    // One literal is ONE draw wherever it is spelled (the graph has one constant node; lg2lnast prints
    // it at every use): the key is the whole literal, so equal spellings share one state word.
    std::string key = std::to_string(n) + (l.raw_signed ? "s" : "u");
    for (size_t w = 0; w < l.unk.size(); ++w) {
      key += ":" + std::to_string(l.raw[w]) + "/" + std::to_string(l.unk[w]);
    }
    // The cache holds the draw's state word, not a value: a value built inside one loop body must not be
    // reused after it (or in another), so the few ops that read the word are rebuilt at every use.
    auto site_loc = unknown_locs_.find(key);
    if (site_loc == unknown_locs_.end()) {
      Unknown_site site;
      site.name = "__unknown" + std::to_string(unknown_sites_.size());
      site.loc  = k_->new_loc(n);
      site.bits = n;
      unknown_sites_.push_back(site);
      site_loc = unknown_locs_.emplace(key, site.loc).first;
    }
    const V drawn = k_->input(site_loc->second, n, true);
    const V mask  = k_->constant_words(n, l.unk, true);
    const V known = k_->constant_words(n, l.raw, true);
    V       v     = k_->binary(Op::bit_or, k_->binary(Op::bit_and, drawn, mask, n, true), known, n, true);
    if (l.raw_signed) {
      v = k_->sign_extend_from(v, n - 1, n, false);
    }
    return v;
  }
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
      if (c.size() == 3 && nm(c[2]) == "mut" && ty(c[1]) != Lnast_ntype::Lnast_ntype_comp_type_array) {
        fr.wires[nm(c[0])] = type_node(c[1]);
        continue;
      }
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
          r.clock_name = nm(c[2]);
        } else if (key == "__store_posclk") {
          if (nm(c[2]) == "false") {
            bail("negedge memory");
          }
        } else if (key == "initial") {
          // The packed contents, entry 0 in the low bits (pass.lg2lnast, 04b-attributes.md).
          auto lit = parse_literal(nm(c[2]));
          if (!lit || lit->negative) {
            bail("memory image '" + nm(c[2]) + "' is not an unsigned integer");
          }
          r.initial = Literal{false, {0}};  // initialized: not undefined at power-on
          if (bit_length(lit->mag) != 0) {
            r.image = *lit;
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
        r.has_clock  = true;
        r.clock_name = nm(c[2]);
      } else if (key == "posclk") {
        r.negedge = nm(c[2]) == "false";
      } else if (key == "latch") {
        r.latch = nm(c[2]) != "false";
      } else if (key == "enable") {
        r.has_enable  = true;
        r.enable_name = nm(c[2]);
      } else if (key == "enable_high") {
        r.enable_high = nm(c[2]) != "false";
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
  const size_t mark = log_.size();
  push_path(cond, false);
  exec_block(kids(c[at + 1]), env);
  pop_path();
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
  const size_t else_mark = log_.size();
  push_path(cond, true);
  exec_if(c, at + 2, env);
  pop_path();
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
  // Loops stay loops: unrolled code is that many times the footprint. Only an INNERMOST loop of
  // fewer than five trips is unrolled, in the native code only (the LNAST and the graph keep it
  // rolled). LHD_SIM_LNAST_UNROLL=N (debug, to see what unrolling would buy): unroll any loop of up
  // to N trips and 16,384 body statements in all; 0 never.
  {
    const uint64_t trips  = static_cast<uint64_t>(hi - lo + 1);
    const auto     forced = unroll_limit();
    const bool     unroll = forced ? trips <= *forced && trips * count_statements(c[2]) <= 16384 : trips < 5 && !has_loop(c[2]);
    if (unroll) {
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
    case NT::Lnast_ntype_delay_assign: {
      // The settled value of a net assigned further down (an instance on a ring of instances).
      if (c.size() != 3 || ty(c[1]) != NT::Lnast_ntype_ref || nm(c[2]) != "1") {
        bail("delay_assign shape (only a settled read, offset 1)");
      }
      const auto net = fr_->wires.find(nm(c[1]));
      if (net == fr_->wires.end()) {
        bail("settled read of '" + nm(c[1]) + "', which is not a declared net");
      }
      if (under_condition() || k_->in_loop()) {
        bail("settled read under a condition or in a loop");
      }
      auto [op, created] = fr_->wire_ops.try_emplace(net->first);
      if (created) {
        op->second = k_->wire(net->second.bits, !net->second.sign);
      }
      finish(dst_name(), op->second);
      break;
    }
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
        const V     enable  = write_enable(r, env);
        if (const V out = warn_undefined_ ? outside(address, r.mem_size) : V{}; out.width != 0) {
          undefined_if(k_->binary(Op::bit_and, enable, out, 1, true), "memory write outside the array", s, false);
        }
        if (!k_->array_write(mem_array(nm(c[0]), env), enable, address, data)) {
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
      if (constexpr std::string_view prefix = "\x1flgundef\x1f"; nm(c[1]) == "lgundef" && !lib_.contains("lgundef")) {
        // A sim.warn_undefined marker (pass.lg2lnast, `lhd sim` only): `cond` is true while the access it
        // guards stays defined, the path condition already folded in; the name is "<prefix><what>\x1f<loc>".
        if (c.size() != 3 || ty(c[2]) != NT::Lnast_ntype_store || kids(c[2]).size() != 2) {
          bail("lgundef marker shape");
        }
        std::string text = nm(c[0]);
        if (text.starts_with(prefix)) {
          text.erase(0, prefix.size());
        }
        const auto sep   = text.find('\x1f');
        std::string what  = sep == std::string::npos ? std::string{"undefined value"} : text.substr(0, sep);
        std::string where = sep == std::string::npos ? text : text.substr(sep + 1);
        const V     cond  = operand(kids(c[2])[1], env);
        undefined_at(k_->binary(Op::eq, cond, k_->constant(1, 0, true), 1, true), what, std::move(where), true);
        break;
      }
      if (under_condition()) {
        bail("module instance under a condition");
      }
      if (nm(c[1]) == "Clock" && !lib_.contains("Clock")) {
        exec_clock_gate(c, env);
        break;
      }
      auto fit_frame = frames_.find(fr_->prefix + nm(c[0]) + ".");
      if (fit_frame == frames_.end()) {
        bail("call to unknown module '" + nm(c[1]) + "'");
      }
      Frame& cf = *fit_frame->second;
      Env    cenv;
      for (const auto& port : cf.in) {
        bool found = false;
        for (size_t j = 2; j < c.size(); ++j) {
          auto sc = kids(c[j]);
          if (ty(c[j]) == NT::Lnast_ntype_store && sc.size() == 2 && nm(sc[0]) == port.name) {
            cenv[port.name] = fit_to(operand(sc[1], env), port.bits, port.sign);
            found           = true;
            break;
          }
        }
        if (!found && cf.clocks.contains(port.name)) {
          cenv[port.name] = k_->clock();  // an unbound clock port takes the root clock
          found           = true;
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
      if (const V out = warn_undefined_ ? outside(address, r.mem_size) : V{}; out.width != 0) {
        undefined_if(out, "memory read outside the array", s);
      }
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
      if (op == Op::div || op == Op::rem) {
        // A zero divisor is X (Pyrope leaves it to the Verilog meaning): the native divide would trap on it,
        // so divide by 1 instead and give 0, a value in range.
        const V zero_b = k_->binary(Op::eq, b, k_->constant(std::max<uint32_t>(b.width, 1), 0, b.unsign), 1, true);
        undefined_if(zero_b, op == Op::div ? "division by zero" : "remainder by zero", s);
        const V safe_b = k_->mux(zero_b, b, k_->constant(std::max<uint32_t>(b.width, 1), 1, b.unsign), std::max<uint32_t>(b.width, 1), b.unsign);
        const V quot   = k_->binary(op, a, safe_b, w, !sign);
        finish(dst, k_->mux(zero_b, quot, k_->constant(std::max<uint32_t>(w, 1), 0, !sign), w, !sign));
        break;
      }
      finish(dst, k_->binary(op, a, b, w, !sign));
      break;
    }
    case NT::Lnast_ntype_bit_and:
    case NT::Lnast_ntype_bit_or:
    case NT::Lnast_ntype_bit_xor: {
      const auto dst = dst_name();
      if (c.size() < 2) {
        bail("bitwise arity");
      }
      std::vector<V> ops;  // one operand: the value itself (a reduced n-ary cell)
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
      const std::vector<V> pair_ops = ops;  // an ICG is `latch & clock`: recognized below once both are known
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
      if (op == Op::bit_and && pair_ops.size() == 2) {
        // The standard integrated clock gate as hardware writes it: `gated = enable_latch & clk`, the latch
        // transparent while `clk` is low (always_latch if (!clk) en = ...). Its edges are the clock's, taken only
        // while the latch holds 1: the same gate `Clock(clock_pin=, enable=)` builds, so a register or memory on
        // `gated` is handled by clock_enables like any other gated clock.
        const auto base = [&](V v) {
          size_t id = v.id;
          while (k_->ops[id].kind == Tape::K::resize && k_->ops[id].a.id != Tape::none) {
            id = k_->ops[id].a.id;
          }
          return id;
        };
        for (int swap = 0; swap < 2; ++swap) {
          const size_t clk_id = base(pair_ops[swap]);
          const size_t lat_id = base(pair_ops[1 - swap]);
          if ((k_->ops[clk_id].kind == Tape::K::clock || gates_.contains(clk_id)) && k_->ops[lat_id].kind == Tape::K::latch) {
            size_t out_id = ops[0].id;
            while (k_->ops[out_id].kind == Tape::K::resize && k_->ops[out_id].a.id != Tape::none) {
              out_id = k_->ops[out_id].a.id;
            }
            gates_[out_id] = Gate{V{clk_id, 1, true}, V{lat_id, 1, true}, false, true};
            break;
          }
        }
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
          if (!k_->array_write(arr, write_enable(r, env), k_->constant(abits, e, true), V{piece.id, piece.width, !r.field.sign})) {
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
      const auto argument = [&](std::string_view port) -> std::optional<std::string> {  // the ref bound to `port`
        for (size_t j = 2; j < c.size(); ++j) {
          auto sc = kids(c[j]);
          if (ty(c[j]) == Lnast_ntype::Lnast_ntype_store && sc.size() == 2 && nm(sc[0]) == port
              && ty(sc[1]) == Lnast_ntype::Lnast_ntype_ref) {
            return nm(sc[1]);
          }
        }
        return std::nullopt;
      };
      auto it = lib_.find(nm(c[1]));
      if (it == lib_.end()) {
        if (nm(c[1]) == "Clock") {  // a clock gate: its clock_pin is a clock of this module
          if (auto clk = argument("clock_pin")) {
            f.clocks.insert(*clk);
          }
          continue;
        }
        if (nm(c[1]) == "lgundef") {  // a sim.warn_undefined marker: no instance, nothing clocked
          continue;
        }
        bail("call to unknown module '" + nm(c[1]) + "'");
      }
      // What the instance uses as a clock is a clock here too (through as many levels as it takes).
      const Frame* child = build_frame(it->second, f.prefix + nm(c[0]) + ".");
      for (const auto& port : child->in) {
        if (child->clocks.contains(port.name)) {
          if (auto clk = argument(port.name)) {
            f.clocks.insert(*clk);
          }
        }
      }
    } else if (Lnast_ntype::is_if_like(t) || t == Lnast_ntype::Lnast_ntype_stmts) {
      for (const auto& ch : kids(st)) {
        if (ty(ch) == Lnast_ntype::Lnast_ntype_stmts) {
          scan_calls(kids(ch), f);
        }
      }
    }
  }
}

void Gen::exec_clock_gate(const std::vector<Lnast_nid>& c, Env& env) {
  using NT = Lnast_ntype;
  if (ty(c[0]) != NT::Lnast_ntype_ref) {
    bail("clock gate shape");
  }
  V    clk, en = k_->constant(1, 1, true);
  bool invert = false;
  for (size_t j = 2; j < c.size(); ++j) {
    auto sc = kids(c[j]);
    if (ty(c[j]) != NT::Lnast_ntype_store || sc.size() != 2) {
      bail("clock gate argument");
    }
    const auto key = nm(sc[0]);
    if (key == "clock_pin") {
      clk = k_->resize(operand(sc[1], env), 1, true);
    } else if (key == "enable") {
      en = k_->binary(Op::ne, operand(sc[1], env), k_->constant(1, 0, true), 1, true);
    } else if (key == "invert") {
      invert = nm(sc[1]) == "true";
    } else {
      bail("clock gate argument '" + key + "'");
    }
  }
  if (clk.width == 0) {
    bail("clock gate without a clock");
  }
  Hidden_latch h;
  h.name                                   = fr_->prefix + nm(c[0]) + "__en";
  h.loc                                    = k_->new_loc(1);
  const V held                             = k_->input(h.loc, 1, true);
  const V lat                              = k_->latch(held, Tape::none);
  h.op                                     = lat.id;
  k_->names[static_cast<uint32_t>(lat.id)] = h.name;
  k_->set_latch(lat.id, invert ? clk : k_->unary_not(clk, 1, true), en);
  const V out
      = invert ? k_->binary(Op::bit_or, clk, k_->unary_not(lat, 1, true), 1, true) : k_->binary(Op::bit_and, clk, lat, 1, true);
  gates_[out.id] = Gate{clk, lat, invert};
  hidden_.push_back(h);
  latch_ops_.push_back(lat.id);
  latch_updates_.push_back({h.loc, invert ? clk : k_->unary_not(clk, 1, true), en, 1, true});
  put(env, nm(c[0]), out);
}

// The gate latches between `clock` and the root clock: a state element on it takes its edge only
// while all of them are set. Empty: every edge.
std::vector<V> Gen::clock_enables(V clock, const std::string& what) {
  std::vector<V> enables;
  for (int hops = 0; hops < 64; ++hops) {
    size_t id = clock.id;
    while (k_->ops[id].kind == Tape::K::resize || (k_->ops[id].kind == Tape::K::wire && k_->ops[id].a.id != Tape::none)) {
      id = k_->ops[id].a.id;
    }
    if (k_->ops[id].kind == Tape::K::clock) {
      return enables;
    }
    if (k_->ops[id].kind == Tape::K::wire) {
      // The clock arrives through a net that is assigned further down (the instance was called early,
      // on a ring of instances): what gates it is known once the walk is over. Until then one
      // enable stands for the gates still to be found (finish_late_gates).
      const V rest = k_->wire(1, true);
      late_gates_.push_back({
          static_cast<size_t>(rest.id),
          V{id, 1, true},
          what
      });
      enables.push_back(rest);
      return enables;
    }
    auto gate = gates_.find(id);
    if (gate == gates_.end()) {
      bail(what + " is clocked by a signal that is neither the clock nor a clock gate of it");
    }
    if (gate->second.invert) {
      bail(what + " is clocked through an inverted clock gate");
    }
    enables.push_back(gate->second.latch);
    clock = gate->second.clk;
  }
  bail(what + ": clock gates nest too deep");
}

// A latch: its readers see `enable ? data : held` (they got the latch op when the frame started), and
// `held` follows while the latch is open. A level reset forces it open onto the reset value.
void Gen::finish_latch(Frame& f, size_t idx, const Env& pre, const Env& end) {
  (void)f;
  const auto& r    = regs_[idx];
  const V     held = held_[idx];
  auto        nx   = end.find(r.local + next_tag);
  V           enable, data;
  if (nx == end.end()) {  // never written: it holds
    enable = k_->constant(1, 0, true);
    data   = held;
  } else {
    data = k_->resize(nx->second, r.field.bits, !r.field.sign);
    if (!r.has_enable || r.enable_name == "true") {
      enable = k_->constant(1, 1, true);
    } else if (r.enable_name == "false") {
      enable = k_->constant(1, 0, true);
    } else {
      const V* e = signal(r.enable_name, pre, end);
      if (e == nullptr) {
        bail("latch '" + r.field.name + "' is enabled by unknown signal '" + r.enable_name + "'");
      }
      enable = k_->binary(Op::ne, *e, k_->constant(1, 0, true), 1, true);
    }
    if (!r.enable_high) {
      enable = k_->unary_not(enable, 1, true);
    }
  }
  const size_t loc = static_cast<size_t>(k_->ops[held.id].imm);
  if (r.reset_pin) {
    // A level reset: `reset ? initial : (enable ? data : held)`. The latch itself stays a plain
    // latch under a mux, and the held value follows two exclusive conditions, so in the phase where
    // the latch is closed nothing reaches its data (a hi/lo latch pair is a ring otherwise).
    const V* rv = signal(*r.reset_pin, pre, end);
    if (rv == nullptr) {
      bail("reset signal '" + *r.reset_pin + "' is not a known signal");
    }
    const V active = r.negreset ? k_->binary(Op::eq, *rv, k_->constant(1, 0, true), 1, true)
                                : k_->binary(Op::ne, *rv, k_->constant(1, 0, true), 1, true);
    const V init   = k_->resize(literal(r.initial ? *r.initial : Literal{false, {0}}), r.field.bits, !r.field.sign);
    const V inner  = k_->latch(held, idx);
    k_->set_latch(inner.id, enable, data);
    k_->names[static_cast<uint32_t>(inner.id)] = r.field.name;
    latch_ops_.push_back(inner.id);
    auto& shown = k_->ops[cur_[idx].id];  // what the readers hold: now the mux over the latch
    shown.kind  = Tape::K::mux;
    shown.a     = Tape::ref(active);
    shown.b     = Tape::ref(inner);
    shown.c     = Tape::ref(init);
    k_->names.erase(static_cast<uint32_t>(cur_[idx].id));
    latch_updates_.push_back(
        {loc, k_->binary(Op::bit_and, enable, k_->unary_not(active, 1, true), 1, true), data, r.field.bits, !r.field.sign});
    latch_updates_.push_back({loc, active, init, r.field.bits, !r.field.sign});
  } else {
    k_->set_latch(cur_[idx].id, enable, data);
    latch_ops_.push_back(cur_[idx].id);
    latch_updates_.push_back({loc, enable, data, r.field.bits, !r.field.sign});
  }
  next_[idx] = V{};
}

// Two-phase latch logic on a GATED clock: `a` is open while `gclk` is high, `b` while it is low,
// and each feeds the other through logic. With the gate off `gclk` stays low through the clock's
// high phase, so in neither view is either enable a constant: structurally the two are a ring, and
// only their exclusiveness says they are never transparent at once.
//
// So a latch's data is evaluated with every latch that is provably closed WHILE THIS ONE IS OPEN read
// as its held value: while this latch is open that is what the other one shows, and while it is
// closed its data is not used. Only the ops between the two are cloned, once per excluding condition
// (every latch on one phase of a gated clock shares them); what nothing else reads of the originals
// is then dead.
//
// Exclusive is proven syntactically and conservatively: the two enables, as conjunctions, hold one
// condition with opposite polarities (`gclk` and `gclk == 0`).
void Gen::bypass_exclusive_latches() {
  auto& t                = *k_;
  using K                = Tape::K;
  const auto constant_is = [&](uint32_t id, uint64_t value) { return t.ops[id].kind == K::constant && t.ops[id].imm == value; };
  // `id` as (condition, polarity): through `!= 0`, `== 0`, `!`, and `if c {1} else {0}`
  std::function<std::pair<uint32_t, bool>(uint32_t, int)> literal = [&](uint32_t id, int depth) -> std::pair<uint32_t, bool> {
    const auto& o = t.ops[id];
    if (depth > 16 || o.width != 1) {
      return {id, true};
    }
    const auto flip = [](std::pair<uint32_t, bool> l) { return std::pair{l.first, !l.second}; };
    if ((o.kind == K::resize || o.kind == K::wire) && o.a.width == 1) {
      return literal(o.a.id, depth + 1);
    }
    if (o.kind == K::unary_not && o.a.width == 1) {
      return flip(literal(o.a.id, depth + 1));
    }
    if (o.kind == K::binary && o.a.width == 1 && constant_is(o.b.id, 0)) {
      if (static_cast<Op>(o.bop) == Op::ne) {
        return literal(o.a.id, depth + 1);
      }
      if (static_cast<Op>(o.bop) == Op::eq) {
        return flip(literal(o.a.id, depth + 1));
      }
    }
    if (o.kind == K::mux && o.a.width == 1) {
      if (constant_is(o.b.id, 0) && constant_is(o.c.id, 1)) {
        return literal(o.a.id, depth + 1);
      }
      if (constant_is(o.b.id, 1) && constant_is(o.c.id, 0)) {
        return flip(literal(o.a.id, depth + 1));
      }
    }
    return {id, true};
  };
  using Literal_set                                          = std::vector<std::pair<uint32_t, bool>>;  // sorted, unique
  std::function<void(uint32_t, Literal_set&, int)> conjuncts = [&](uint32_t id, Literal_set& out, int depth) {
    const auto l = literal(id, 0);
    if (std::ranges::find(out, l) != out.end()) {
      return;
    }
    out.push_back(l);
    if (!l.second || depth > 32) {
      return;
    }
    const auto& o = t.ops[l.first];
    if (o.kind == K::binary && static_cast<Op>(o.bop) == Op::bit_and && o.width == 1 && o.a.width == 1 && o.b.width == 1) {
      conjuncts(o.a.id, out, depth + 1);
      conjuncts(o.b.id, out, depth + 1);
    }
  };
  // per latch: what its enable requires; per condition: which latches require it, by polarity
  const std::vector<size_t>                              latches = latch_ops_;  // the loop below records ops, never latches
  absl::flat_hash_map<size_t, Literal_set>               requires_;
  absl::flat_hash_map<std::pair<uint32_t, bool>, size_t> required_by;  // literal -> how many latches
  for (const auto id : latches) {
    auto& set = requires_[id];
    conjuncts(static_cast<uint32_t>(t.ops[id].a.id), set, 0);
    std::ranges::sort(set);
    for (const auto& l : set) {
      ++required_by[l];
    }
  }
  // A latch op, or the latch under a reset mux (what a reader of a latch with a level reset holds).
  const auto latch_under = [&](uint32_t id) -> size_t {
    const auto& o = t.ops[id];
    if (o.kind == K::latch) {
      return id;
    }
    if (o.kind == K::mux && o.b.id != Tape::none && t.ops[o.b.id].kind == K::latch && requires_.contains(o.b.id)) {
      return o.b.id;
    }
    return Tape::none;
  };
  // clones, per excluding set of literals (the literals of the latch being fed that some other latch
  // requires the opposite of)
  std::map<Literal_set, absl::flat_hash_map<uint32_t, uint32_t>> clones;
  size_t                                                         cloned = 0, bypassed = 0;
  for (const auto id : latches) {
    const Tape::Ref data = t.ops[id].b;
    if (data.id == Tape::none) {
      continue;
    }
    Literal_set excluding;
    for (const auto& l : requires_[id]) {
      if (required_by.contains({l.first, !l.second})) {
        excluding.push_back(l);
      }
    }
    if (excluding.empty()) {
      continue;
    }
    auto&      memo   = clones[excluding];
    const auto closed = [&](size_t latch) {  // provably closed while `id` is open
      return latch != id && std::ranges::any_of(excluding, [&](const auto& l) {
               return std::ranges::binary_search(requires_[latch], std::pair{l.first, !l.second});
             });
    };
    // post-order over the data cone; `memo[x] == x`: nothing under x changes
    bool                                   ok = true;
    std::vector<std::pair<uint32_t, bool>> stack{
        {data.id, false}
    };
    while (!stack.empty() && ok) {
      const auto [x, expanded] = stack.back();
      if (memo.contains(x)) {
        stack.pop_back();
        continue;
      }
      const Tape::Op o = t.ops[x];  // by value: cloning grows the tape
      if (const auto latch = latch_under(x); latch != Tape::none) {
        if (closed(latch)) {
          const Tape::Ref held = t.ops[latch].c;
          if (o.kind == K::latch) {
            memo[x] = held.id;
          } else {  // the reset mux, over the held value
            memo[x] = static_cast<uint32_t>(t.mux(V{o.a.id, o.a.width, o.a.unsign},
                                                  V{held.id, o.b.width, o.b.unsign},
                                                  V{o.c.id, o.c.width, o.c.unsign},
                                                  o.width,
                                                  o.unsign)
                                                .id);
            ++cloned;
          }
          ++bypassed;
        } else {
          memo[x] = x;  // another latch, open or not: as it is
        }
        stack.pop_back();
        continue;
      }
      if (Tape::is_free(o.kind)) {
        memo[x] = x;
        stack.pop_back();
        continue;
      }
      const bool pure = o.kind == K::resize || o.kind == K::concat || o.kind == K::unary_not || o.kind == K::reduce_or
                        || o.kind == K::count_bits || o.kind == K::bitfield_insert || o.kind == K::sign_extend
                        || o.kind == K::binary || o.kind == K::mux || o.kind == K::wire;
      if (!pure) {
        memo[x] = x;  // a memory read or a loop: not re-evaluated (if a closed latch is under it the ring stays, and is reported)
        stack.pop_back();
        continue;
      }
      if (!expanded) {
        stack.back().second = true;
        t.for_each_operand(o, [&](const Tape::Ref& r) {
          if (!memo.contains(r.id)) {
            stack.emplace_back(r.id, false);
          }
        });
        continue;
      }
      bool changed = false;
      t.for_each_operand(o, [&](const Tape::Ref& r) { changed = changed || memo.at(r.id) != r.id; });
      if (!changed) {
        memo[x] = x;
      } else {
        Tape::Op copy = o;
        for (Tape::Ref* r : {&copy.a, &copy.b, &copy.c}) {
          if (r->id != Tape::none) {
            r->id = memo.at(r->id);
          }
        }
        if (o.kind == K::concat) {
          copy.x = static_cast<uint32_t>(t.refs.size());
          for (uint32_t j = 0; j < o.n; ++j) {
            Tape::Ref lane = t.refs[o.x + j];
            lane.id        = memo.at(lane.id);
            t.refs.push_back(lane);
          }
        }
        t.ops.push_back(copy);
        memo[x] = static_cast<uint32_t>(t.ops.size() - 1);
        ++cloned;
      }
      stack.pop_back();
    }
    const auto replaced = memo.at(data.id);
    if (replaced == data.id) {
      continue;
    }
    const V bypass{replaced, data.width, data.unsign};
    t.ops[id].b = Tape::ref(bypass);
    for (auto& u : latch_updates_) {
      if (u.data.id == data.id && u.loc == static_cast<size_t>(t.ops[t.ops[id].c.id].imm)) {
        u.data = bypass;
      }
    }
  }
  if (livehd::debug::env<"LHD_SIM_LNAST_TIME">() != nullptr && bypassed != 0) {
    std::fprintf(stderr,
                 "lnast_llvm: %zu latch reads taken as held under an exclusive enable (%zu ops cloned)\n",
                 bypassed,
                 cloned);
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
  for (const auto& [name, op] : f.wire_ops) {
    const auto value = end.find(name);
    if (value == end.end()) {
      bail("net '" + f.prefix + name + "' is read at its settled value and never assigned");
    }
    k_->set_wire(op.id, k_->resize(value->second, op.width, op.unsign));
  }
  f.wire_ops.clear();
  for (size_t idx : f.owned) {
    const auto& r = regs_[idx];
    if (r.mem_size != 0) {
      if (r.reset_pin) {  // reset reloads the whole image (zero unless `image`) at the edge, over any write
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
        // A SYNC reset behind a closed clock gate waits for an edge the gate lets through, exactly as a
        // register's (and as the memory's writes, see write_enable). An async reset lands regardless.
        if (r.sync && !r.clock_name.empty()) {
          const V* clock = signal(r.clock_name, pre, end);
          if (clock == nullptr) {
            bail("memory '" + r.field.name + "' is clocked by unknown signal '" + r.clock_name + "'");
          }
          if (const V gate = all_of(clock_enables(*clock, "memory '" + r.field.name + "'")); gate.width != 0) {
            active = k_->binary(Op::bit_and, active, k_->resize(gate, 1, true), 1, true);
          }
        }
        V image{};
        if (r.image) {
          const uint32_t whole = static_cast<uint32_t>(r.mem_size * r.field.bits);
          image                = k_->constant_words(whole, twos_words(*r.image, whole), true);
        }
        if (!k_->array_clear_if(ensure_array(idx, pre), active, image)) {
          bail("memory reset");
        }
      }
      next_[idx] = V{};  // the memory is updated in place: nothing to publish
      continue;
    }
    if (r.latch) {
      finish_latch(f, idx, pre, end);
      continue;
    }
    auto nx = end.find(r.local + next_tag);
    V    n  = nx != end.end() ? nx->second : end.at(r.local);
    // A gated clock: the register takes the edge only while every gate on the way to the root clock
    // is on. Spelled as the hold mux, so it is published under that condition like any enable.
    V gate_all{};  // the gates on this register's clock, all of them on (none: width 0)
    if (!r.clock_name.empty()) {
      const V* clock = signal(r.clock_name, pre, end);
      if (clock == nullptr) {
        bail("register '" + r.field.name + "' is clocked by unknown signal '" + r.clock_name + "'");
      }
      if (const V gate = all_of(clock_enables(*clock, "register '" + r.field.name + "'")); gate.width != 0) {
        n        = k_->mux(gate, cur_[idx], k_->resize(n, r.field.bits, !r.field.sign), r.field.bits, !r.field.sign);
        gate_all = gate;
      }
    }
    if (r.reset_pin) {
      const V* rv = signal(*r.reset_pin, pre, end);  // an input or any signal settled this cycle
      if (rv == nullptr) {
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
      // A SYNC reset is a clocked event: behind a closed clock gate it waits for an edge the gate lets
      // through. An async reset lands whatever the gate does.
      V act = ai->second;
      if (r.sync && gate_all.width != 0) {
        act = k_->binary(Op::bit_and, act, k_->resize(gate_all, 1, true), 1, true);
      }
      reset_items_.push_back({
          idx,
          act,
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
// A latch is always guarded and in place: `if (enable) held = data`.
struct Root {
  Tape::Ref value;
  size_t    loc       = 0;
  bool      guarded   = false;
  bool      when_zero = false;  // update while the guard is zero
  Tape::Ref guard, data, hold;  // `hold`: the register's current value
  size_t    home   = Tape::none;
  size_t    kernel = 0;  // the kernel that publishes it
  uint32_t  q      = Tape::none;  // a register's root: the op that reads the register (its current value)
};

// What one kernel family computes: the ops it needs, in the order they are emitted, and where its
// results go. A view replays the tape at ONE level of the root clock, so whatever reads the clock is
// a constant in it: `fold_*` holds what that makes of each op (a clock gate is off, a latch on the
// other phase is closed and reads as its held value, ...).
struct View {
  std::string           tag;              // file/symbol suffix: "e" rise, "f" fall, "s" settle
  int                   level   = 0;      // the root clock while this view runs
  bool                  effects = false;  // memory writes happen in this view
  std::vector<Root>     roots;
  // Emission starts from the results one at a time (order_view): `rank`, when set, is the order of
  // those starts (per op: lower first; default: recorded order), and `groups` what came out -- the
  // start op and the first position of each run of ops emitted for one start.
  std::vector<uint32_t>                      rank;
  std::vector<std::pair<uint32_t, uint32_t>> groups;
  bool                                       deep = false;  // deepest operand cone first (order_view)
  std::vector<uint32_t> order;     // the live computed ops, in emission order
  std::vector<uint32_t> pos;       // per op: its index in `order` (none: not emitted)
  std::vector<uint32_t> cut;       // kernel k replays order[cut[k], cut[k + 1])
  std::vector<uint8_t>  spill;     // per op: read by another kernel of this view
  // Per op: the state location a spilled value crosses kernels through. The view's own: the same op
  // is a different value in another clock phase, and two views taking turns on one word would never
  // let the kernels that read it rest (activity gating).
  std::vector<size_t>   slot;
  uint32_t              first_bit = 0;  // activity gating: kernel k of this view owns dirty bit first_bit + k
  uint64_t              size = 0;  // words of code (cut_kernels)
  std::vector<uint8_t>  fold_state;  // per op: 0 not evaluated, 1 not a constant, 2 the constant fold_value
  std::vector<uint64_t> fold_value;
  // The kernel that computes (or, for an input or a constant, may as well publish) `id`.
  size_t                kernel_of(uint32_t id) const {
    if (pos[id] == Tape::none || cut.size() < 2) {
      return 0;
    }
    return static_cast<size_t>(std::upper_bound(cut.begin(), cut.end(), pos[id]) - cut.begin()) - 1;
  }
  size_t count_live() const { return order.size(); }
  // The constant op `id` is in this view, if fold() found one (lookup only: safe from the workers).
  std::optional<uint64_t> folded(uint32_t id) const {
    return id < fold_state.size() && fold_state[id] == 2 ? std::optional<uint64_t>(fold_value[id]) : std::nullopt;
  }
};

// Evaluate op `id` at the view's clock level when that makes it a constant. Deliberately small: the
// values that matter are one-bit gate/enable cones (`clk & en`, `!clk`), and a wrong fold would be a
// wrong simulation, so anything not obviously exact is "not a constant".
std::optional<uint64_t> fold(const Tape& t, View& v, uint32_t id, int depth = 0) {
  using K = Tape::K;
  if (v.fold_state.size() != t.ops.size()) {
    v.fold_state.assign(t.ops.size(), 0);
    v.fold_value.assign(t.ops.size(), 0);
  }
  if (v.fold_state[id] != 0) {
    return v.folded(id);
  }
  const auto&             o    = t.ops[id];
  const auto              mask = [](uint32_t w) { return w >= 64 ? ~uint64_t{0} : (uint64_t{1} << w) - 1; };
  std::optional<uint64_t> r;
  if (o.width >= 1 && o.width <= 64 && depth < 128) {
    const auto get = [&](const Tape::Ref& x) -> std::optional<uint64_t> {
      // an operand is usable when its view is the op it names, unsigned, at that op's own width
      if (x.id == Tape::none || x.width != t.ops[x.id].width || x.width > 64) {
        return std::nullopt;
      }
      return fold(t, v, x.id, depth + 1);
    };
    switch (o.kind) {
      case K::clock   : r = static_cast<uint64_t>(v.level); break;
      case K::constant: r = o.imm & mask(o.width); break;
      case K::resize:
        if (auto a = get(o.a); a && (o.a.unsign || ((*a >> (o.a.width - 1)) & 1) == 0)) {
          r = *a & mask(o.width);
        }
        break;
      case K::unary_not:
        if (auto a = get(o.a); a && o.a.width == o.width) {
          r = ~*a & mask(o.width);
        }
        break;
      case K::reduce_or:
        if (auto a = get(o.a)) {
          r = *a != 0 ? 1 : 0;
        }
        break;
      case K::binary: {
        const auto op   = static_cast<Op>(o.bop);
        const bool same = o.a.width == o.b.width && o.a.unsign && o.b.unsign;
        const auto a    = get(o.a);
        const auto b    = get(o.b);
        if (!same) {
          break;
        }
        if (op == Op::bit_and && o.a.width == o.width) {
          if ((a && *a == 0) || (b && *b == 0)) {
            r = 0;
          } else if (a && b) {
            r = *a & *b;
          }
        } else if (op == Op::bit_or && o.a.width == o.width) {
          if ((a && *a == mask(o.width)) || (b && *b == mask(o.width))) {
            r = mask(o.width);
          } else if (a && b) {
            r = *a | *b;
          }
        } else if (op == Op::bit_xor && o.a.width == o.width && a && b) {
          r = *a ^ *b;
        } else if ((op == Op::eq || op == Op::ne) && a && b) {
          r = ((*a == *b) == (op == Op::eq)) ? 1 : 0;
        }
        break;
      }
      case K::mux:
        if (auto sel = get(o.a)) {
          const auto& arm = *sel != 0 ? o.c : o.b;
          if (arm.width == o.width && arm.unsign == o.unsign) {
            r = get(arm);
          }
        }
        break;
      case K::latch:
        if (auto en = get(o.a); en && *en != 0 && o.b.width == o.width && o.b.unsign == o.unsign) {
          r = get(o.b);
        }
        break;
      case K::wire:
        if (o.a.id != Tape::none && o.a.width == o.width && o.a.unsign == o.unsign) {
          r = get(o.a);
        }
        break;
      default: break;
    }
  }
  v.fold_state[id] = r ? 2 : 1;
  v.fold_value[id] = r ? *r : 0;
  return r;
}

// The values op `id` reads IN THIS VIEW: none when it folded to a constant; a closed latch reads only
// its held value and an open one only its data; a mux with a constant select reads one arm.
template <class Fn>
void view_operands(const Tape& t, const View& v, uint32_t id, Fn&& fn) {
  using K       = Tape::K;
  const auto& o = t.ops[id];
  if (o.kind != K::array_write && o.kind != K::array_clear && v.folded(id)) {
    return;
  }
  if (o.kind == K::latch) {
    if (auto en = v.folded(o.a.id)) {
      fn(*en != 0 ? o.b : o.c);
      return;
    }
  } else if (o.kind == K::mux) {
    if (auto sel = v.folded(o.a.id)) {
      fn(*sel != 0 ? o.c : o.b);
      return;
    }
  }
  t.for_each_operand(o, fn);
}

// 0: the root publishes nothing in this view (its guard is a constant that is off); 1: a plain store
// of `data` (the guard is a constant that is on); 2: as recorded.
int root_mode(const View& v, const Root& root) {
  if (root.guarded) {
    if (auto g = v.folded(root.guard.id)) {
      return ((*g != 0) != root.when_zero) ? 1 : 0;
    }
  }
  return 2;
}
template <class Fn>
void root_operands(const View& v, const Root& root, Fn&& fn) {
  const auto mode = root_mode(v, root);
  if (mode == 0) {
    return;
  }
  if (!root.guarded) {
    fn(root.value);
    return;
  }
  if (mode == 2) {
    fn(root.guard);
  }
  fn(root.data);
}

// Activity gating: a kernel is a function of the state words it reads, so it need not run again until
// one of them changes. Every kernel owns one bit of the `dirty` words; the caller runs a kernel only
// when its bit is set, clearing it first. Whoever changes a state location sets the bits of the
// kernels that READ it and of the OTHER kernels that write it (a location with several writers -- a
// latch's held value is written in every phase it is open, a memory by each of its ports -- holds
// what the last one wrote, so the others have to assert their value again).
// A store compares before it marks: most of a large design holds its value most cycles.
// A register that is not written in place changes at the commit, not when its next value is stored:
// its marks go to the `next` words, which the commit kernel merges into `dirty`. (Set right away, a
// later kernel of the same view would run now, on the old value, and be clean when the value changes.)
struct Activity {
  bool                               on         = false;
  size_t                             bits       = 0;           // kernels
  size_t                             dirty_loc  = Tape::none;  // tape locations, then (after the layout) words
  size_t                             next_loc   = Tape::none;
  size_t                             dirty_word = 0;
  size_t                             next_word  = 0;
  std::vector<std::vector<uint32_t>> readers, writers;  // per state location: kernel bits
  std::vector<std::vector<uint32_t>> array_readers, array_writers;
  static constexpr uint32_t          nobody = ~uint32_t{0};

  size_t      words() const { return (bits + 63) / 64; }
  static void add(std::vector<Cgen_llvm::Dirty_mask>& out, size_t base, const std::vector<uint32_t>& list, uint32_t skip) {
    for (const auto bit : list) {
      if (bit == skip) {
        continue;
      }
      const auto word = base + bit / 64;
      auto       it   = std::ranges::find(out, word, &Cgen_llvm::Dirty_mask::word);
      if (it == out.end()) {
        out.push_back({word, 0});
        it = out.end() - 1;
      }
      it->mask |= uint64_t{1} << (bit % 64);
    }
  }
  // What kernel `self` marks when it changes location `loc`.
  std::vector<Cgen_llvm::Dirty_mask> marks(size_t loc, uint32_t self, bool next = false) const {
    std::vector<Cgen_llvm::Dirty_mask> out;
    if (on) {
      add(out, next ? next_word : dirty_word, readers[loc], nobody);
      add(out, next ? next_word : dirty_word, writers[loc], self);
    }
    return out;
  }
  std::vector<Cgen_llvm::Dirty_mask> array_marks(size_t array, uint32_t self) const {
    std::vector<Cgen_llvm::Dirty_mask> out;
    if (on) {
      add(out, dirty_word, array_readers[array], nobody);
      add(out, dirty_word, array_writers[array], self);
    }
    return out;
  }
  std::vector<Cgen_llvm::Dirty_mask> all() const {
    std::vector<Cgen_llvm::Dirty_mask> out;
    for (size_t w = 0; on && w < words(); ++w) {
      out.push_back({dirty_word + w, w + 1 == words() && bits % 64 != 0 ? (uint64_t{1} << (bits % 64)) - 1 : ~uint64_t{0}});
    }
    return out;
  }
  // The kernel bits reading `loc`, for a writer that is not a kernel (the testbench driving an input).
  std::vector<uint32_t> readers_of(size_t loc) const { return on ? readers[loc] : std::vector<uint32_t>{}; }
};

struct Kernel_job {
  std::string name, object;
  const View* view  = nullptr;
  size_t      index = 0;
  uint32_t    begin = 0, end = 0;  // positions in view->order
};

// A register root that publishes into a pending word: the register itself changes at the commit.
bool is_pending(const Root& root) { return root.home != Tape::none && root.loc != root.home; }

// Every value kernel `index` of `v` (order[begin, end)) reads: `fn(op id)` per operand of its ops and
// of the results it publishes. What that means for the kernel's inputs is the caller's (an input op is
// a state location; an op of another kernel arrives through its slot; a constant is made in place).
template <class Fn>
void kernel_operands(const Tape& t, const View& v, size_t index, uint32_t begin, uint32_t end, Fn&& fn) {
  for (uint32_t p = begin; p < end; ++p) {
    view_operands(t, v, v.order[p], [&](const Tape::Ref& r) { fn(r.id); });
  }
  for (const auto& root : v.roots) {
    if (root.kernel == index && root_mode(v, root) != 0) {
      root_operands(v, root, [&](const Tape::Ref& r) { fn(r.id); });
      if (root.guarded && root.loc != root.home && root_mode(v, root) == 2) {
        fn(root.hold.id);  // published as the mux: it reads the register
      }
    }
  }
}

// The size of a kernel, in machine words of code (cut_kernels: kernels are one to two times this).
// LLVM's instruction selection and scheduling are superlinear in the size of a basic block, and a
// kernel is mostly one block: boom as ONE kernel spent 364 s there; in pieces the same code is a few
// CPU seconds per worker. Small kernels also run faster on a large design (less is live at once).
//
// With activity gating (see Activity) a kernel is also the unit that can be skipped, and smaller is
// better again: one changed value wakes a whole kernel. Measured with gating on (cycles/s):
//   words per kernel   1250    600    300    150     75     40
//   minion            84.3k   102k   114k   134k   118k    87k    (ungated: 29.1k)
//   boom              23.2k  30.6k  30.9k  36.3k  34.8k  27.1k    (ungated: 34.3k)
// Below ~150 the values crossing kernels (a store, a load and a compare each) cost more than the
// skipped work saves.
uint32_t chunk_target(bool gated) {
  static const uint32_t forced = [] {
    const char* text = livehd::debug::env<"LHD_SIM_LNAST_CHUNK">();  // debug: words of code per kernel
    return text == nullptr ? 0u : static_cast<uint32_t>(std::strtoul(text, nullptr, 10));
  }();
  // Ungated, a kernel is only cut to bound the LLVM compile of a huge function (a 93k-instruction kernel took 338 s
  // to select): every cut costs the values crossing it a store, a load and a compare at run time. matched_filter's
  // loop-built tap chain is 15k ops of 450-bit values (108k words): in 1250-word kernels it ran 3.45 s per 10M
  // cycles, as one kernel 0.38 s. 10,000 words: gemmini8 Pyrope2 builds in 4 s (20,000: 18 s; LLVM compile is superlinear).
  return forced != 0 ? forced : gated ? 150u : 10000u;
}

// Words of code: an op on a 512-bit value is eight words.
uint64_t op_size(const Tape& t, uint32_t id) { return std::max<uint64_t>(1, (t.ops[id].width + 63) / 64); }
uint64_t view_size(const Tape& t, const View& v) {
  uint64_t size = 0;
  for (const auto id : v.order) {
    size += op_size(t, id);
  }
  return size;
}

// Emission order: depth first from each result, in the order the results were recorded. The walk
// records statements in dependency (Kahn) order, which is breadth first: every value of one logic
// level is computed before any of the next, so nearly everything is live at once and a kernel is
// mostly spills (dino: 878 of 2,990 instructions touched the stack). Depth first, a value is computed
// right before the cone that consumes it. Only what a root needs is emitted, so this is also the
// dead-code pass, and the order is the view's own: the tape is a dataflow graph (a latch's data may
// be recorded after its readers), so nothing here relies on op numbers. A loop is one unit, emitted
// whole. Throws when the view has a combinational cycle (two latches transparent in the same phase).
void order_view(const Tape& t, View& v) {
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
  // Memory order (the view that writes memories): a memory is written in place, so its reads and
  // writes keep the order they were recorded in. Per array, each write follows the write before it
  // and every live read recorded since; each read follows the last write recorded before it (a
  // byte-enable write is a chain of read-modify-writes of one entry: each lane must see the lane
  // before it).
  std::vector<uint32_t>                                roots;
  absl::flat_hash_map<uint32_t, std::vector<uint32_t>> after;        // unit -> units it must follow
  absl::flat_hash_map<uint32_t, std::vector<uint32_t>> after_reads;  // write unit -> earlier reads (if live)
  if (v.effects) {
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
      (void)fold(t, v, j);
      if (t.ops[j].kind == K::latch || t.ops[j].kind == K::mux) {
        (void)fold(t, v, t.ops[j].a.id);
      }
      view_operands(t, v, j, [&](const Tape::Ref& r) {
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
    if (root.guarded) {
      (void)fold(t, v, root.guard.id);
    }
    root_operands(v, root, [&](const Tape::Ref& r) { roots.push_back(unit[r.id]); });
  }
  std::ranges::sort(roots);
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  if (!v.rank.empty()) {
    std::ranges::stable_sort(roots, [&](uint32_t a, uint32_t b) { return v.rank[a] < v.rank[b]; });
  }
  v.groups.clear();
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
  // Of a unit's operands the deepest cone is emitted first (Sethi-Ullman): while the long cone is
  // computed, nothing else of this unit is being held, so values live for a shorter stretch of the
  // order and fewer of them cross a cut (boundary values: minion 26,678 -> 23,026 and 193k -> 201k
  // cycles/s, boom 8,476 -> 8,122). Used where the view is cut (`deep`); inside ONE kernel LLVM
  // schedules the block itself and the effect is noise either way (dino +6%, picorv32 -4%).
  std::vector<uint32_t> height;
  if (v.deep) {
    height.assign(n, 0);
    std::vector<uint8_t> seen(n, 0);
    for (auto root : roots) {
      if (seen[root] != 0 || Tape::is_free(t.ops[root].kind)) {
        continue;
      }
      seen[root] = 1;
      stack.push_back({root, {}, 0});
      operands(root, stack.back().ops, &live);
      while (!stack.empty()) {
        auto& f = stack.back();
        if (f.next < f.ops.size()) {
          const auto child = f.ops[f.next++];
          if (seen[child] == 0) {
            seen[child] = 1;
            stack.push_back({child, {}, 0});
            operands(child, stack.back().ops, &live);
          }
          continue;
        }
        uint32_t h = 0;
        for (const auto o : f.ops) {
          h = std::max(h, height[o]);
        }
        height[f.u] = h + 1;
        stack.pop_back();
      }
    }
  }
  const auto deepest_first = [&](std::vector<uint32_t>& ops) {
    if (!height.empty()) {
      std::ranges::stable_sort(ops, [&](uint32_t a, uint32_t b) { return height[a] > height[b]; });
    }
  };
  for (auto root : roots) {
    if (done[root] != 0 || Tape::is_free(t.ops[root].kind)) {
      continue;
    }
    v.groups.emplace_back(root, static_cast<uint32_t>(v.order.size()));
    stack.push_back({root, {}, 0});
    operands(root, stack.back().ops, &live);
    deepest_first(stack.back().ops);
    done[root] = 1;
    while (!stack.empty()) {
      auto& f = stack.back();
      if (f.next < f.ops.size()) {
        const auto child = f.ops[f.next++];
        if (done[child] == 0) {
          done[child] = 1;
          stack.push_back({child, {}, 0});
          operands(child, stack.back().ops, &live);
          deepest_first(stack.back().ops);
        } else if (done[child] == 1) {
          // name the latches on the ring: the units on the stack from `child` up
          std::string ring;
          size_t      from = stack.size();
          while (from > 0 && stack[from - 1].u != child) {
            --from;
          }
          size_t shown = 0, ops_on_ring = 0;
          for (size_t k = from > 0 ? from - 1 : 0; k < stack.size(); ++k) {
            ++ops_on_ring;
            const auto u = stack[k].u;
            if (t.ops[u].kind == K::latch && shown < 8) {
              const auto name  = t.names.find(u);
              const auto en    = v.folded(t.ops[u].a.id);
              ring            += std::format("{}{}({})",
                                  shown++ ? " -> " : "",
                                  name == t.names.end() ? std::format("latch#{}", u) : name->second,
                                  en ? (*en != 0 ? "open" : "closed") : "enable not constant");
            }
          }
          if (livehd::debug::env<"LHD_SIM_LNAST_DEBUG">() != nullptr) {  // the enable cone of each latch on the ring
            static const char* const kinds[]
                = {"input", "const",  "constw", "resize", "concat", "not",    "ror",    "count",  "insert",
                   "sext",  "binary", "mux",    "aread",  "awrite", "aclear", "lbegin", "lindex", "lphi",
                   "lend",  "lfinal", "clock",  "latch",  "wire",   "lget",   "lset"};
            std::function<void(uint32_t, int)> dump = [&](uint32_t id, int depth) {
              const auto& o  = t.ops[id];
              const auto  fc = v.folded(id);
              std::fprintf(stderr,
                           "%*s#%u %s/%u w%u %s%s\n",
                           depth * 2,
                           "",
                           id,
                           kinds[static_cast<int>(o.kind)],
                           o.bop,
                           o.width,
                           fc ? std::format("= {}", *fc).c_str() : "?",
                           t.names.contains(id) ? (" " + t.names.at(id)).c_str() : "");
              if (depth < 5 && o.kind != K::latch) {
                t.for_each_operand(o, [&](const Tape::Ref& r) { dump(r.id, depth + 1); });
              }
            };
            for (size_t k = from > 0 ? from - 1 : 0; k < stack.size(); ++k) {
              if (t.ops[stack[k].u].kind == K::latch) {
                std::fprintf(stderr, "ring latch #%u enable:\n", stack[k].u);
                dump(t.ops[stack[k].u].a.id, 1);
              }
            }
          }
          bail(std::format("combinational cycle of {} values while the clock is {}, through transparent latches: {}",
                           ops_on_ring,
                           v.level != 0 ? "high" : "low",
                           ring.empty() ? std::string("(none: a memory read/write order ring)") : ring));
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

// A register is written in place -- no pending word, no copy at the commit, and under an enable no
// store at all while it is off -- only when nothing reads its old value after the kernel that
// writes it. So the cone that READS a register should be emitted before the cone that computes its
// next value: a pipeline is emitted last stage first. Emission goes start by start (order_view);
// this derives, from the order just made, which starts must precede which (every start whose run
// reads register A before the start that publishes A), and returns how many registers have a
// reader behind their writer as things stand. `rank` gets the order to try next: a depth-first
// topological order of those constraints, otherwise as now (a ring of registers cannot be satisfied
// and keeps its order).
size_t rank_readers_first(const Tape& t, const View& v, std::vector<uint32_t>& rank) {
  const auto n_groups = v.groups.size();
  const auto group_of = [&](uint32_t pos) {
    const auto it = std::upper_bound(v.groups.begin(), v.groups.end(), pos, [](uint32_t p, const auto& g) { return p < g.second; });
    return static_cast<uint32_t>(it - v.groups.begin()) - 1;
  };
  absl::flat_hash_map<uint32_t, std::vector<uint32_t>> readers;    // register read op -> groups reading it
  absl::flat_hash_map<uint32_t, uint32_t>              last_read;  // register read op -> last position reading it
  absl::flat_hash_set<uint32_t>                        wanted;
  for (const auto& root : v.roots) {
    if (root.q != Tape::none) {
      wanted.insert(root.q);
    }
  }
  for (uint32_t p = 0; p < v.order.size(); ++p) {
    view_operands(t, v, v.order[p], [&](const Tape::Ref& r) {
      if (wanted.contains(r.id)) {
        auto&      list = readers[r.id];
        const auto g    = group_of(p);
        if (list.empty() || list.back() != g) {
          list.push_back(g);
        }
        last_read[r.id] = p;
      }
    });
  }
  std::vector<std::vector<uint32_t>> before(n_groups);  // group -> groups that should come first
  size_t                             behind = 0;
  for (const auto& root : v.roots) {
    if (root.q == Tape::none || root_mode(v, root) == 0) {
      continue;
    }
    uint32_t written = Tape::none;  // where the register's next value is complete
    root_operands(v, root, [&](const Tape::Ref& r) {
      if (v.pos[r.id] != Tape::none && (written == Tape::none || v.pos[r.id] > written)) {
        written = v.pos[r.id];
      }
    });
    if (written == Tape::none) {
      continue;  // computes nothing: published by a kernel ahead of all the others
    }
    if (auto it = last_read.find(root.q); it != last_read.end() && it->second > written) {
      ++behind;
    }
    if (auto it = readers.find(root.q); it != readers.end()) {
      const auto writer = group_of(written);
      for (const auto g : it->second) {
        if (g != writer) {
          before[writer].push_back(g);
        }
      }
    }
  }
  rank.assign(t.ops.size(), Tape::none);
  std::vector<uint8_t>                     state(n_groups, 0);
  std::vector<std::pair<uint32_t, size_t>> stack;
  uint32_t                                 next = 0;
  for (uint32_t first = 0; first < n_groups; ++first) {
    if (state[first] != 0) {
      continue;
    }
    state[first] = 1;
    stack.emplace_back(first, 0);
    while (!stack.empty()) {
      const auto g = stack.back().first;
      if (stack.back().second < before[g].size()) {
        const auto b = before[g][stack.back().second++];
        if (state[b] == 0) {
          state[b] = 1;
          stack.emplace_back(b, 0);
        }
        continue;
      }
      rank[v.groups[g].first] = next++;
      stack.pop_back();
    }
  }
  return behind;
}

void cut_kernels(const Tape& t, View& v, uint64_t target, bool gated) {
  const auto n = static_cast<uint32_t>(v.order.size());
  v.cut.clear();
  v.spill.assign(t.ops.size(), 0);
  const bool any_root = std::ranges::any_of(v.roots, [&](const Root& r) { return root_mode(v, r) != 0; });
  if (n == 0 && !any_root) {
    return;
  }
  absl::flat_hash_set<uint32_t> loop_tail;  // an op of a loop other than its last one: no cut after it
  for (const auto& [lb, le] : t.loops) {
    for (uint32_t j = lb; j < le; ++j) {
      loop_tail.insert(j);
    }
  }
  // Size is counted in machine words, not ops: an op on a 512-bit value is eight words of code.
  const auto            size_of = [&](uint32_t id) { return op_size(t, id); };
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
      view_operands(t, v, v.order[p], [&](const Tape::Ref& r) { last_use[r.id] = std::max(last_use[r.id], p); });
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
  v.size = size_to[n];
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
  // A result that computes nothing -- a register taking another register's value (a pipeline stage),
  // an input or a constant -- belongs to no kernel's logic. Left to kernel 0 (below) they all pile up
  // there: boom has 1,500, one block of stores and compares that took as long to compile as forty
  // other kernels and that ran every clock. Gated, they get kernels of their own, in recorded order
  // (a module's registers together), ahead of the others so the registers they read can still be
  // written in place by the kernels behind them.
  const auto copies = [&](const Root& root) {
    bool any = false, computed = false;
    root_operands(v, root, [&](const Tape::Ref& r) {
      any      = true;
      computed = computed || (!Tape::is_free(t.ops[r.id].kind) && !v.folded(r.id));
    });
    return any && !computed;
  };
  size_t copy_kernels = 0;
  if (gated) {
    uint64_t words = 0;
    for (const auto& root : v.roots) {
      words += copies(root) ? std::max<uint64_t>(1, (root.value.width + 63) / 64) : 0;
    }
    if (words > target) {
      copy_kernels = static_cast<size_t>((words + target - 1) / target);
      v.cut.insert(v.cut.begin(), copy_kernels, 0);  // empty op ranges: an op at position 0 is the first real kernel's
    }
  }
  // A value crosses kernels through a state word: mark what another kernel of this view reads.
  for (uint32_t p = 0; p < n; ++p) {
    const auto i    = v.order[p];
    const auto mine = v.kernel_of(i);
    view_operands(t, v, i, [&](const Tape::Ref& r) {
      if (!Tape::is_free(t.ops[r.id].kind) && v.kernel_of(r.id) != mine) {
        v.spill[r.id] = 1;
      }
    });
  }
  // A result is published by the last kernel that computes one of its operands; the others cross.
  std::vector<uint8_t> is_copy(v.roots.size(), 0);
  if (copy_kernels != 0) {
    // A register is written in place only when nothing reads it in a later kernel, so along a chain
    // of copies (a shift register: c' = b, b' = a) the reader goes first: c, then b.
    absl::flat_hash_map<size_t, std::vector<uint32_t>> read_by;  // state location -> the copies reading it
    for (uint32_t i = 0; i < v.roots.size(); ++i) {
      if (copies(v.roots[i])) {
        is_copy[i] = 1;
        root_operands(v, v.roots[i], [&](const Tape::Ref& r) {
          if (t.ops[r.id].kind == Tape::K::input) {
            read_by[static_cast<size_t>(t.ops[r.id].imm)].push_back(i);
          }
        });
      }
    }
    std::vector<uint8_t>                     seen(v.roots.size(), 0);
    std::vector<std::pair<uint32_t, size_t>> stack;  // (root, next reader to visit)
    uint64_t                                 copied = 0;
    const auto                               place  = [&](uint32_t i) {
      v.roots[i].kernel  = std::min<size_t>(copy_kernels - 1, static_cast<size_t>(copied / target));
      copied            += std::max<uint64_t>(1, (v.roots[i].value.width + 63) / 64);
    };
    for (uint32_t first = 0; first < v.roots.size(); ++first) {
      if (is_copy[first] == 0 || seen[first] != 0) {
        continue;
      }
      seen[first] = 1;
      stack.emplace_back(first, 0);
      while (!stack.empty()) {
        auto& [i, next] = stack.back();
        const auto home = v.roots[i].home != Tape::none ? v.roots[i].home : v.roots[i].loc;
        const auto it   = read_by.find(home);
        if (it != read_by.end() && next < it->second.size()) {
          const auto reader = it->second[next++];
          if (seen[reader] == 0) {
            seen[reader] = 1;
            stack.emplace_back(reader, 0);
          }
          continue;
        }
        place(i);
        stack.pop_back();
      }
    }
  }
  for (uint32_t index = 0; index < v.roots.size(); ++index) {
    auto& root = v.roots[index];
    if (is_copy[index] != 0) {
      continue;
    }
    root.kernel = 0;
    root_operands(v, root, [&](const Tape::Ref& r) { root.kernel = std::max(root.kernel, v.kernel_of(r.id)); });
    root_operands(v, root, [&](const Tape::Ref& r) {
      if (!Tape::is_free(t.ops[r.id].kind) && v.kernel_of(r.id) != root.kernel) {
        v.spill[r.id] = 1;
      }
    });
  }
}

// Replay order[begin, end) of `view` into one state kernel and write its object. Returns the
// instruction count; throws Unsupported.
size_t build_kernel(const Tape& t, const Kernel_job& job, const Activity& gate, size_t words) {
  const View& v                              = *job.view;
  const auto& slot                           = v.slot;
  const auto  self                           = static_cast<uint32_t>(v.first_bit + job.index);
  using K                                    = Tape::K;
  const auto here = [&](uint32_t id) { return v.pos[id] != Tape::none && v.pos[id] >= job.begin && v.pos[id] < job.end; };
  std::vector<Cgen_llvm::State_input>   layout;
  absl::flat_hash_map<uint32_t, size_t> in_index;
  const auto                            need = [&](uint32_t id) {
    const auto& d = t.ops[id];
    if (d.kind == K::input) {
      if (in_index.try_emplace(id, layout.size()).second) {
        layout.push_back({t.locs[d.imm], d.width, d.unsign, 0});
      }
    } else if (!Tape::is_free(d.kind) && !here(id) && !v.folded(id)) {
      if (in_index.try_emplace(id, layout.size()).second) {
        if (slot[id] == Tape::none) {
          bail("internal: a value crosses kernels without a state word");
        }
        layout.push_back({t.locs[slot[id]], d.width, d.unsign, 0});
      }
    }
  };
  kernel_operands(t, v, job.index, job.begin, job.end, need);
  std::vector<const Root*> my_roots;
  for (const auto& root : v.roots) {
    if (root.kernel == job.index && root_mode(v, root) != 0) {
      my_roots.push_back(&root);
    }
  }

  Cgen_llvm                             k(job.name, Cgen_llvm::State_layout{words, layout});
  absl::flat_hash_map<uint32_t, V>      map;
  absl::flat_hash_map<uint64_t, size_t> arrays;
  absl::flat_hash_map<uint32_t, size_t> lane_buffer;  // op -> the buffer holding its value (a buffered loop carry)
  absl::flat_hash_map<uint32_t, size_t> read_buffer;  // op -> a copy of it made ahead of a loop that reads its lanes
  for (const auto& [id, index] : in_index) {
    map[id] = k.input(index);
  }
  const auto val = [&](const Tape::Ref& r) {
    auto it = map.find(r.id);
    if (it == map.end()) {
      const auto& d = t.ops[r.id];
      V           made;
      if (auto c = v.folded(r.id)) {  // a constant in this view (the clock, and what it decides)
        made = k.constant(d.width, *c, d.unsign);
      } else if (d.kind == K::constant) {
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
    if (o.kind != K::array_write && o.kind != K::array_clear && v.folded(i)) {
      continue;  // a constant in this view: made on demand
    }
    switch (o.kind) {
      case K::input:
      case K::constant:
      case K::constant_words:
      case K::clock         : break;  // on demand
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
      case K::mux:
        if (auto sel = v.folded(o.a.id)) {
          made(i, k.resize(val(*sel != 0 ? o.c : o.b), o.width, o.unsign));
        } else {
          made(i, k.mux(val(o.a), val(o.b), val(o.c), o.width, o.unsign));
        }
        break;
      case K::latch:
        // closed in this view: the held value; open: the data; else whichever the enable says
        if (auto en = v.folded(o.a.id)) {
          made(i, k.resize(val(*en != 0 ? o.b : o.c), o.width, o.unsign));
        } else {
          made(i, k.mux(val(o.a), val(o.c), val(o.b), o.width, o.unsign));
        }
        break;
      case K::wire      : made(i, k.resize(val(o.a), o.width, o.unsign)); break;
      case K::array_read: made(i, k.array_read(array_of(o.imm), val(o.a), o.width, o.unsign)); break;
      case K::array_write:
        if (!k.array_write(array_of(o.imm), val(o.a), val(o.b), val(o.c), gate.array_marks(o.imm, self))) {
          bail("memory write");
        }
        break;
      case K::array_clear:
        if (o.b.id != Tape::none) {
          // A constant image (the usual case: a declared initializer) is one table copied on reset, linear
          // in the memory; shifting one size*bits constant per entry was quadratic.
          const auto& img = t.ops[o.b.id];
          if (img.kind == K::constant_words || img.kind == K::constant) {
            const auto&                 a = t.arrays[o.imm];
            const std::vector<uint64_t> packed
                = img.kind == K::constant ? std::vector<uint64_t>{img.imm}
                                          : std::vector<uint64_t>(t.words.begin() + img.x, t.words.begin() + img.x + img.n);
            if (!k.array_load_words_if(array_of(o.imm),
                                       val(o.a),
                                       Cgen_llvm::array_state_image(a.bits, a.size, packed),
                                       gate.array_marks(o.imm, self))) {
              bail("memory reset");
            }
          } else if (!k.array_load_if(array_of(o.imm), val(o.a), val(o.b), gate.array_marks(o.imm, self))) {
            bail("memory reset");
          }
        } else if (!k.array_clear_if(array_of(o.imm), val(o.a), gate.array_marks(o.imm, self))) {
          bail("memory reset");
        }
        break;
      case K::loop_begin: {
        // Array base pointers must dominate the body and what follows it (a loop is emitted whole,
        // in recorded order, so its ops follow here until its loop_end).
        std::vector<uint32_t> lane_sources;  // wide values the body reads lanes of and that exist before the loop
        for (uint32_t q = p; q < job.end && !(t.ops[v.order[q]].kind == K::loop_end && t.ops[v.order[q]].a.id == i); ++q) {
          const auto& body = t.ops[v.order[q]];
          if (body.kind == K::array_read || body.kind == K::array_write || body.kind == K::array_clear) {
            (void)array_of(body.imm);
          }
          if (body.kind == K::lane_get && map.contains(body.a.id) && t.ops[body.a.id].kind != K::input
              && !lane_buffer.contains(body.a.id) && !read_buffer.contains(body.a.id)) {
            lane_sources.push_back(body.a.id);
            read_buffer[body.a.id] = 0;
          }
        }
        // Stored once, ahead of the loop: a lane read is then a load, not a copy of the value a trip.
        // (A value read straight from the state needs no copy: dynamic_extract reads it in place.)
        for (const auto src : lane_sources) {
          const auto& d    = t.ops[src];
          read_buffer[src] = k.lane_buffer_create(val(Tape::Ref{src, d.width, d.unsign}));
        }
        std::vector<V> seeds, phis;
        for (uint32_t j = 0; j < o.n; ++j) {
          seeds.push_back(val(t.refs[o.x + j]));
        }
        // A carry that lives in a buffer for this loop (recognize_lanes): seeded here, ahead of the loop.
        if (auto plan = t.memory_carries.find(i); plan != t.memory_carries.end()) {
          for (uint32_t j = 0; j < o.n; ++j) {
            if (plan->second[j] != 0) {
              lane_buffer[i + 2 + j] = k.lane_buffer_create(seeds[j]);
            }
          }
        }
        V index;
        if (!k.loop_begin(o.imm, static_cast<int64_t>(t.words[o.c.id]), o.width, seeds, index, phis)) {
          bail("native loop");
        }
        map[i + 1] = index;
        for (uint32_t j = 0; j < o.n; ++j) {
          map[i + 2 + j] = phis[j];  // for a buffered carry: a value nothing that is needed reads
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
          const auto  buffered = lane_buffer.find(static_cast<uint32_t>(o.a.id) + 2 + j);
          const auto& d        = t.ops[i + 1 + j];
          map[i + 1 + j]       = buffered == lane_buffer.end() ? fin[j] : k.lane_buffer_load(buffered->second, d.width, d.unsign);
        }
        break;
      }
      case K::lane_get: {
        V at = val(o.b);
        if (o.imm != 0) {  // a constant further up the value
          const uint32_t w = std::max<uint32_t>(at.width, 64 - static_cast<uint32_t>(__builtin_clzll(o.imm))) + 1;
          at               = k.binary(Op::add, V{at.id, at.width, true}, k.constant(w, o.imm, true), w, true);
        }
        if (auto buffered = lane_buffer.find(o.a.id); buffered != lane_buffer.end()) {
          made(i, k.lane_buffer_extract(buffered->second, at, o.width, o.width, o.unsign));
        } else if (auto stored = read_buffer.find(o.a.id); stored != read_buffer.end()) {
          made(i, k.lane_buffer_extract(stored->second, at, o.width, o.width, o.unsign));
        } else {
          made(i, k.dynamic_extract(val(o.a), at, o.width, o.width, o.unsign));
        }
        break;
      }
      case K::lane_set:
        if (auto buffered = lane_buffer.find(o.a.id); buffered != lane_buffer.end()) {
          const auto buffer = buffered->second;
          if (!k.lane_buffer_insert(buffer, val(o.b), static_cast<uint32_t>(o.imm), val(o.c))) {
            bail("lane write");
          }
          lane_buffer[i] = buffer;          // the same buffer now holds this value
          map[i]         = map.at(o.a.id);  // nothing that is needed reads it as a value
        } else {
          made(i, k.dynamic_insert(val(o.a), val(o.b), static_cast<uint32_t>(o.imm), val(o.c), o.width, o.unsign));
        }
        break;
    }
  }
  std::string error;
  for (uint32_t p = job.begin; p < job.end; ++p) {
    const auto i = v.order[p];
    if (v.spill[i] != 0 && !v.folded(i)) {
      const auto& d = t.ops[i];
      if (!k.add_state_output(t.locs[slot[i]],
                              t.locs[slot[i]],
                              val(Tape::Ref{i, d.width, d.unsign}),
                              gate.marks(slot[i], self),
                              error)) {
        bail("kernel boundary value: " + error);
      }
    }
  }
  for (const auto* root : my_roots) {
    const auto word = t.locs[root->loc];
    const auto mode = root_mode(v, *root);
    bool       ok   = false;
    // What changes is the register (at the commit, when the value goes to a pending word first).
    const auto changes = is_pending(*root) ? root->home : root->loc;
    const auto was     = t.locs[changes];
    const auto marks   = gate.marks(changes, self, is_pending(*root));
    if (!root->guarded) {
      ok = k.add_state_output(word, was, val(root->value), marks, error);
    } else if (mode == 1) {  // the guard is a constant that is on
      ok = k.add_state_output(word, was, k.resize(val(root->data), root->value.width, root->value.unsign), marks, error);
    } else if (root->loc == root->home) {
      ok = k.add_state_output_if(val(root->guard),
                                 root->when_zero,
                                 word,
                                 k.resize(val(root->data), root->value.width, root->value.unsign),
                                 error,
                                 marks);
    } else {
      const V keep = val(root->hold), data = val(root->data);
      ok = k.add_state_output(word,
                              was,
                              k.mux(val(root->guard),
                                    root->when_zero ? data : keep,
                                    root->when_zero ? keep : data,
                                    root->value.width,
                                    root->value.unsign),
                              marks,
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

// A `latch & clk` AND models a clean clock gate (edges of `clk`, taken while the latch holds 1) only
// when the latch cannot change while `clk` is high: its enable must be 0 whenever `clk` is 1. Any other
// latch (transparent while the clock is high, or on an unrelated enable) makes the AND a glitchy clock
// that inou.cgen.sim evaluates as logic: decline instead of simulating clean edges the hardware lacks.
void Gen::check_and_gates() {
  for (const auto& [out, gate] : gates_) {
    if (!gate.from_and) {
      continue;
    }
    const auto& latch = k_->ops[gate.latch.id];
    bool        clean = false;
    if (latch.a.id != Tape::none) {
      View high;
      high.level = 1;  // the root clock high; a gated clock `clk` is forced high below
      high.fold_state.assign(k_->ops.size(), 0);
      high.fold_value.assign(k_->ops.size(), 0);
      high.fold_state[gate.clk.id] = 2;
      high.fold_value[gate.clk.id] = 1;
      const auto open              = fold(*k_, high, latch.a.id);
      clean                        = open && *open == 0;
    }
    if (!clean) {
      bail(std::format("a clock ANDed with latch '{}' that is not closed while that clock is high (not a clean clock gate)",
                       latch.imm < regs_.size() ? regs_[latch.imm].field.name : std::string{"(a clock gate's)"}));
    }
  }
}

Result Gen::run(const Sim_objects& out) {
  Result res;
  draw_unknown_   = !out.unknown_zero;
  warn_undefined_ = out.warn_undefined;
  try {
    Phase_timer timer;
    ln_  = top_ln_;
    top_ = build_frame(top_ln_, "");
    timer.lap("collect (decls/attrs/frames)");
    fr_  = top_;
    {
      std::string clocks;
      size_t      count = 0;
      for (const auto& f : top_->in) {
        if (top_->clocks.contains(f.name)) {
          clocks += (count++ ? ", " : "") + f.name;
        }
      }
      if (count > 1) {
        bail("several clock inputs (" + clocks + "): one root clock is modeled");
      }
      if (count == 0 && std::ranges::any_of(regs_, [](const Reg_info& r) { return r.latch; })) {
        // The state is latches and no flop or memory names a clock: the design's clock reaches the latch
        // enable as DATA, a port nothing here toggles. inou.cgen.sim derives the clock from the enable.
        bail("a latch with no clock port: its enable is driven by a plain input");
      }
    }

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
      top_in[f.name] = top_->clocks.contains(f.name) ? tape.clock() : tape.input(f.word, f.bits, !f.sign);
    }
    cur_.assign(regs_.size(), V{});
    held_.assign(regs_.size(), V{});
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size != 0) {
        continue;
      }
      const V stored = tape.input(regs_[i].field.word, regs_[i].field.bits, !regs_[i].field.sign);
      held_[i]       = stored;
      cur_[i]        = regs_[i].latch ? tape.latch(stored, i) : stored;  // a latch reads as its transparent value
      if (regs_[i].latch) {
        tape.names[static_cast<uint32_t>(cur_[i].id)] = regs_[i].field.name;
      }
    }
    next_.assign(regs_.size(), V{});
    inst_env_.clear();
    path_terms_.clear();
    path_made_.clear();
    Env env = top_in;
    for (size_t idx : top_->owned) {
      if (regs_[idx].mem_size == 0) {
        env[regs_[idx].local] = cur_[idx];
      }
    }
    Env pre = env;
    exec_block(top_->stmts, env);
    finish_frame(*top_, pre, env);
    finish_late_gates();
    for (const auto& site : undefined_sites_) {  // consecutive locations: the class polls one run of words
      tape.arrays[site.array].loc = tape.new_loc(64);
    }
    for (const auto id : latch_ops_) {
      if (tape.ops[id].a.id == Tape::none) {
        bail("internal: a latch was never completed");
      }
    }
    for (const auto& o : tape.ops) {
      if (o.kind == Tape::K::wire && o.a.id == Tape::none) {
        bail("internal: a settled read was never completed");
      }
    }
    {
      bool       ok  = tape.resolve_wires();
      const auto fix = [&](V& v) {
        if (v.width != 0 && tape.ops[v.id].kind == Tape::K::wire) {
          const auto to = tape.carried(static_cast<uint32_t>(v.id));
          ok            = ok && to != Tape::none;
          v.id          = to != Tape::none ? to : v.id;
        }
      };
      for (auto& v : next_) {
        fix(v);
      }
      for (auto& [name, v] : env) {
        fix(v);
      }
      for (auto& u : latch_updates_) {
        fix(u.guard);
        fix(u.data);
      }
      for (auto& item : reset_items_) {
        fix(item.active);
      }
      if (!ok) {
        bail("nets that are assigned only from each other (a combinational cycle of wires)");
      }
    }
    check_and_gates();  // every latch is complete and every settled read resolved
    if (livehd::debug::env<"LHD_SIM_LNAST_NO_LANES">() == nullptr) {  // debug A/B: lane access as plain arithmetic
      recognize_lanes(tape);
    }
    bypass_exclusive_latches();
    timer.lap("walk");

    // The views of one clock period: `rise` runs with the clock low and ends in the rising edge
    // (posedge registers, memory writes); `fall` runs with it high and ends in the falling edge
    // (negedge registers), and exists only when something happens there; `settle` runs with it low
    // again and publishes the outputs. A latch follows its data in whichever views it is open.
    View rise, fall, settle;
    rise.tag                      = "e";
    rise.level                    = 0;
    rise.effects                  = true;
    fall.tag                      = "f";
    fall.level                    = 1;
    settle.tag                    = "s";
    settle.level                  = 0;
    const bool          no_guards = livehd::debug::env<"LHD_SIM_LNAST_NO_GUARD">() != nullptr;  // debug A/B: plain stores only
    // A register's next value is stored straight into the register when no later kernel of its edge
    // view still reads the old value; otherwise into a pending location the commit kernel copies.
    // Decided once the kernels are cut: until then every register publishes into a pending location.
    std::vector<size_t> pending(regs_.size(), Tape::none);
    for (size_t i = 0; i < regs_.size(); ++i) {
      if (regs_[i].mem_size != 0 || regs_[i].latch) {
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
      root.q     = static_cast<uint32_t>(cur_[i].id);
      // `r' = c ? x : r` (either arm): a hold, published under its condition when it lands in place.
      if (const auto& m = tape.ops[next_[i].id]; m.kind == Tape::K::mux && m.width == next_[i].width && m.unsign == next_[i].unsign
                                                 && (m.b.id == cur_[i].id) != (m.c.id == cur_[i].id) && !no_guards) {
        root.guarded   = true;
        root.when_zero = m.c.id == cur_[i].id;
        root.guard     = m.a;
        root.data      = root.when_zero ? m.b : m.c;
        root.hold      = Tape::ref(cur_[i]);
      }
      (regs_[i].negedge ? fall : rise).roots.push_back(root);
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
    // A latch's held value follows in every view where the latch can be open: `if (enable) held =
    // data`, in place. After the fall that only matters when the enable is not just the clock phase:
    // a latch that is simply open while the clock is low is written again, before anything can read
    // what it held, by the next rise view.
    for (const auto& u : latch_updates_) {
      for (View* v : {&rise, &fall, &settle}) {
        const auto open = fold(tape, *v, static_cast<uint32_t>(u.guard.id));
        if ((open && *open == 0) || (v == &settle && open)) {
          continue;
        }
        Root root;
        root.value   = Tape::Ref{static_cast<uint32_t>(u.data.id), u.bits, u.unsign};
        root.loc     = u.loc;
        root.home    = u.loc;
        root.guarded = true;
        root.guard   = Tape::ref(u.guard);
        root.data    = Tape::ref(u.data);
        v->roots.push_back(root);
      }
    }

    // Each commit kernel reads the distinct reset conditions of its registers: keep those values
    // alive through its view.
    for (View* v : {&rise, &fall, &settle}) {
      v->slot.assign(tape.ops.size(), Tape::none);
    }
    auto give_slot = [&](View& v, uint32_t id) {
      if (v.slot[id] == Tape::none && !Tape::is_free(tape.ops[id].kind)) {
        v.slot[id] = tape.new_loc(tape.ops[id].width);
      }
    };
    std::vector<uint32_t> reset_conditions[2];  // [0]: rise, [1]: fall
    // One reset signal reaches the registers of many instances, and each instance derives its own
    // "is active" from it (`reset != 0`): the same condition, built once per instance. The commit
    // kernel tests each DISTINCT condition once, so equal ones are folded here (a small structural
    // compare: these cones are a compare or two over an input).
    {
      std::function<bool(uint32_t, uint32_t, int)> same = [&](uint32_t a, uint32_t b, int depth) -> bool {
        if (a == b) {
          return true;
        }
        const auto& x = tape.ops[a];
        const auto& y = tape.ops[b];
        using K       = Tape::K;
        if (depth > 6 || x.kind != y.kind || x.bop != y.bop || x.unsign != y.unsign || x.width != y.width || x.imm != y.imm
            || (x.kind != K::resize && x.kind != K::unary_not && x.kind != K::reduce_or && x.kind != K::binary && x.kind != K::mux
                && x.kind != K::constant)) {
          return false;
        }
        if (x.kind == K::constant) {
          return true;
        }
        for (const auto& [p, q] : {
                 std::pair{&x.a, &y.a},
                 std::pair{&x.b, &y.b},
                 std::pair{&x.c, &y.c}
        }) {
          if ((p->id == Tape::none) != (q->id == Tape::none)) {
            return false;
          }
          if (p->id != Tape::none && (p->width != q->width || p->unsign != q->unsign || !same(p->id, q->id, depth + 1))) {
            return false;
          }
        }
        return true;
      };
      std::vector<uint32_t> distinct[2];
      for (auto& item : reset_items_) {
        auto& list  = distinct[regs_[item.idx].negedge ? 1 : 0];
        auto  equal = std::ranges::find_if(list, [&](uint32_t id) { return same(id, static_cast<uint32_t>(item.active.id), 0); });
        if (equal == list.end()) {
          list.push_back(static_cast<uint32_t>(item.active.id));
        } else {
          item.active.id = *equal;
        }
      }
    }
    for (const auto& item : reset_items_) {
      auto&      list = reset_conditions[regs_[item.idx].negedge ? 1 : 0];
      auto&      view = regs_[item.idx].negedge ? fall : rise;
      const auto id   = static_cast<uint32_t>(item.active.id);
      if (std::ranges::find(list, id) == list.end()) {
        list.push_back(id);
        give_slot(view, id);
        if (view.slot[id] != Tape::none) {
          Root root;
          root.value = Tape::Ref{id, tape.ops[id].width, tape.ops[id].unsign};
          root.loc   = view.slot[id];
          view.roots.push_back(root);
        }
      }
    }
    // Activity gating (see Activity) pays for a compare at every store with the kernels it skips. A
    // small design is a kernel or two that run every clock anyway, and cut small enough to skip
    // anything it loses more at the cuts (picorv32 6.5M -> 2.3M cycles/s, dino 5.8M -> 4.4M): only a
    // large design is gated. LHD_SIM_LNAST_GATE (debug): 0 never, 1 always.
    bool gated = false;
    {
      uint64_t size = 0;
      for (View* v : {&rise, &fall, &settle}) {
        order_view(tape, *v);
        size += view_size(tape, *v);
      }
      const char* text = livehd::debug::env<"LHD_SIM_LNAST_GATE">();
      // Gating pays only where most kernels sleep most cycles: minion (219k words) gains 4.6x, boom (95k) a few percent,
      // while a dense design of 100-170k words (matched_filter and gemmini Pyrope2, every kernel active each cycle)
      // lost 6x to 30x in 150-word kernels. 200k words and above.
      gated            = text != nullptr ? text[0] != '0' : size > 200000;
      // Readers of a register before its writer (rank_readers_first): a few rounds, each from the
      // order the one before produced. What an order costs is counted where it is paid, after the
      // cut: a word of register that is not written in place and a word crossing kernels (a store,
      // a load and a compare) -- reordering trades the first for the second, and the recorded order
      // has the fewest crossings (bitcoin, four kernels: 4.15M cycles/s as recorded, 3.77M with one
      // more register in place and 29 more words crossing; boom and minion gain a fifth and a
      // third: 3,100 and 1,100 fewer words copied at every commit). The cheapest order seen stays. Only
      // for a view that will be cut: in ONE kernel every register is in place whatever the order.
      const auto target = chunk_target(gated);
      // A view that will be cut is ordered deepest cone first. LHD_SIM_LNAST_ORDER (debug): rec, deep.
      for (View* v : {&rise, &fall, &settle}) {
        const char* how = livehd::debug::env<"LHD_SIM_LNAST_ORDER">();
        v->deep         = how != nullptr ? std::string_view(how) == "deep" : view_size(tape, *v) > 4 * target;
        if (v->deep) {
          order_view(tape, *v);
        }
      }
      const auto cost = [&](View& v) {
        cut_kernels(tape, v, target, gated);
        uint64_t                              words = 0;
        absl::flat_hash_map<uint32_t, size_t> last;  // register read op -> last kernel reading it
        for (uint32_t p = 0; p < v.order.size(); ++p) {
          const auto i  = v.order[p];
          words        += v.spill[i] != 0 && !v.folded(i) ? op_size(tape, i) : 0;
          const auto k  = v.kernel_of(i);
          view_operands(tape, v, i, [&](const Tape::Ref& r) {
            if (tape.ops[r.id].kind == Tape::K::input) {
              auto& l = last[r.id];
              l       = std::max(l, k);
            }
          });
        }
        for (const auto& root : v.roots) {
          root_operands(v, root, [&](const Tape::Ref& r) {
            if (tape.ops[r.id].kind == Tape::K::input) {
              auto& l = last[r.id];
              l       = std::max(l, root.kernel);
            }
          });
        }
        for (const auto& root : v.roots) {
          if (root.q != Tape::none && root_mode(v, root) != 0) {
            if (auto it = last.find(root.q); it != last.end() && it->second > root.kernel) {
              // Four times a crossing word: it is copied at every commit whether or not anything
              // ran, and under an enable it turns "nothing to do" into a mux, a store and a compare.
              words += 4 * std::max<uint64_t>(1, (root.value.width + 63) / 64);
            }
          }
        }
        return words;
      };
      for (View* v : {&rise, &fall}) {
        if (view_size(tape, *v) <= 4 * target) {
          continue;
        }
        std::vector<uint32_t> rank, best_rank;
        const uint64_t        recorded = cost(*v);
        uint64_t              best     = recorded;
        size_t                behind   = rank_readers_first(tape, *v, rank);
        for (int round = 0; round < 3 && behind != 0; ++round) {
          v->rank = rank;
          order_view(tape, *v);
          const uint64_t now = cost(*v);
          if (now < best) {
            best      = now;
            best_rank = v->rank;
          }
          std::vector<uint32_t> next;
          const size_t          left = rank_readers_first(tape, *v, next);
          if (left >= behind) {
            break;
          }
          behind = left;
          rank   = std::move(next);
        }
        v->rank = best_rank;
        order_view(tape, *v);
        if (timer.on) {
          std::fprintf(stderr,
                       "lnast_llvm: ordering: cost %llu as recorded, %llu with readers first (%s)\n",
                       static_cast<unsigned long long>(recorded),
                       static_cast<unsigned long long>(best),
                       best_rank.empty() ? "kept as recorded" : "reordered");
        }
      }
    }
    for (View* v : {&rise, &fall, &settle}) {
      cut_kernels(tape, *v, chunk_target(gated), gated);
      for (uint32_t i = 0; i < v->spill.size(); ++i) {
        if (v->spill[i] != 0) {
          give_slot(*v, i);
        }
      }
    }

    // In place: the kernel that publishes register i's next value is the last kernel of its edge view
    // to read Q(i) (a kernel loads what it reads before it stores anything).
    size_t in_place = 0;
    for (View* v : {&rise, &fall}) {
      absl::flat_hash_map<uint32_t, std::ptrdiff_t> last_reader;  // input op -> last kernel reading it
      for (const auto i : v->order) {
        const auto mine = static_cast<std::ptrdiff_t>(v->kernel_of(i));
        view_operands(tape, *v, i, [&](const Tape::Ref& r) {
          if (tape.ops[r.id].kind == Tape::K::input) {
            auto& last = last_reader[r.id];
            last       = std::max<std::ptrdiff_t>(last, mine);
          }
        });
      }
      absl::flat_hash_map<size_t, std::ptrdiff_t> writer;  // pending location -> the kernel publishing it
      for (const auto& root : v->roots) {
        root_operands(*v, root, [&](const Tape::Ref& r) {
          if (tape.ops[r.id].kind == Tape::K::input) {
            auto& last = last_reader[r.id];
            last       = std::max<std::ptrdiff_t>(last, static_cast<std::ptrdiff_t>(root.kernel));
          }
        });
        writer[root.loc] = static_cast<std::ptrdiff_t>(root.kernel);
      }
      absl::flat_hash_map<size_t, size_t> redirect;  // pending location -> the register's own
      for (size_t i = 0; i < regs_.size(); ++i) {
        if (pending[i] == Tape::none || regs_[i].negedge != (v == &fall)) {
          continue;
        }
        const auto reader = last_reader.find(static_cast<uint32_t>(cur_[i].id));
        if (reader == last_reader.end() || reader->second <= writer.at(pending[i])) {
          redirect[pending[i]] = regs_[i].field.word;
          pending[i]           = Tape::none;
          ++in_place;
        }
      }
      for (auto& root : v->roots) {
        if (auto it = redirect.find(root.loc); it != redirect.end()) {
          root.loc = it->second;
        }
      }
    }

    // ---- activity gating: which kernels read and which write every state location (see Activity) ----
    Activity gate;
    {
      uint32_t kernels = 0;
      for (View* v : {&rise, &fall, &settle}) {
        v->first_bit  = kernels;
        kernels      += v->cut.empty() ? 0 : static_cast<uint32_t>(v->cut.size() - 1);
      }
      gate.bits = kernels;
      gate.on   = kernels != 0 && gated;
    }
    if (gate.on) {
      using K        = Tape::K;
      gate.dirty_loc = tape.new_loc(gate.words() * 64);
      gate.next_loc  = tape.new_loc(gate.words() * 64);
      gate.readers.resize(tape.locs.size());
      gate.writers.resize(tape.locs.size());
      gate.array_readers.resize(tape.arrays.size());
      gate.array_writers.resize(tape.arrays.size());
      for (View* v : {&rise, &fall, &settle}) {
        for (size_t c = 0; c + 1 < v->cut.size(); ++c) {
          const auto bit   = static_cast<uint32_t>(v->first_bit + c);
          const auto begin = v->cut[c], end = v->cut[c + 1];
          kernel_operands(tape, *v, c, begin, end, [&](uint32_t id) {
            const auto& d = tape.ops[id];
            if (d.kind == K::input) {
              gate.readers[d.imm].push_back(bit);
            } else if (!Tape::is_free(d.kind) && !(v->pos[id] != Tape::none && v->pos[id] >= begin && v->pos[id] < end)
                       && !v->folded(id)) {
              if (v->slot[id] == Tape::none) {
                bail("internal: a value crosses kernels without a state word");
              }
              gate.readers[v->slot[id]].push_back(bit);
            }
          });
          for (uint32_t p = begin; p < end; ++p) {
            const auto  i = v->order[p];
            const auto& o = tape.ops[i];
            if (o.kind == K::array_read) {
              gate.array_readers[o.imm].push_back(bit);
            } else if (o.kind == K::array_write || o.kind == K::array_clear) {
              gate.array_writers[o.imm].push_back(bit);
            }
            if (v->spill[i] != 0 && !v->folded(i)) {
              gate.writers[v->slot[i]].push_back(bit);
            }
          }
        }
        for (const auto& root : v->roots) {
          if (root_mode(*v, root) != 0) {
            gate.writers[is_pending(root) ? root.home : root.loc].push_back(static_cast<uint32_t>(v->first_bit + root.kernel));
          }
        }
      }
      for (auto* lists : {&gate.readers, &gate.writers, &gate.array_readers, &gate.array_writers}) {
        for (auto& list : *lists) {
          std::ranges::sort(list);
          list.erase(std::unique(list.begin(), list.end()), list.end());
        }
      }
      for (auto& f : top_->in) {  // the testbench writes these: it marks their readers
        if (!top_->clocks.contains(f.name)) {
          f.marks = gate.readers_of(f.word);
        }
      }
      if (timer.on) {
        size_t plain = 0, guarded = 0, pend = 0, spills = 0, unread = 0;
        for (View* v : {&rise, &fall, &settle}) {
          for (const auto& root : v->roots) {
            const auto mode = root_mode(*v, root);
            if (mode == 0) {
              continue;
            }
            const auto self = static_cast<uint32_t>(v->first_bit + root.kernel);
            if (gate.marks(is_pending(root) ? root.home : root.loc, self).empty()) {
              ++unread;
            } else if (is_pending(root)) {
              ++pend;
            } else if (root.guarded && mode == 2) {
              ++guarded;
            } else {
              ++plain;
            }
          }
          for (const auto i : v->order) {
            spills += v->spill[i] != 0 && !v->folded(i) ? 1 : 0;
          }
        }
        std::fprintf(stderr,
                     "lnast_llvm: gating compares: %zu plain stores, %zu guarded, %zu pending, %zu boundary values; %zu results "
                     "nobody waits on\n",
                     plain,
                     guarded,
                     pend,
                     spills,
                     unread);
      }
    }

    // ---- layout: the dirty words; inputs; registers written in place; per edge, the others twice
    // (current, then pending: that commit is one copy); memories; outputs; the words kernels pass
    // values through ----
    auto place = [&](size_t loc) {
      if (tape.locs[loc] == Tape::none) {
        tape.locs[loc]  = words_;
        words_         += (tape.loc_bits[loc] + 63) / 64;
      }
    };
    if (gate.on) {
      place(gate.dirty_loc);
      place(gate.next_loc);
      gate.dirty_word = tape.locs[gate.dirty_loc];
      gate.next_word  = tape.locs[gate.next_loc];
    }
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
    size_t q_base[2] = {0, 0}, d_base[2] = {0, 0}, q_words[2] = {0, 0};
    for (int e = 0; e < 2; ++e) {
      const auto mine
          = [&](size_t i) { return regs_[i].mem_size == 0 && pending[i] != Tape::none && regs_[i].negedge == (e == 1); };
      q_base[e] = words_;
      for (size_t i = 0; i < regs_.size(); ++i) {
        if (mine(i)) {
          place(regs_[i].field.word);
        }
      }
      q_words[e] = words_ - q_base[e];
      d_base[e]  = words_;
      for (size_t i = 0; i < regs_.size(); ++i) {
        if (mine(i)) {
          place(pending[i]);
        }
      }
    }
    for (size_t loc = 0; loc < tape.locs.size(); ++loc) {
      place(loc);  // memories, outputs, clock-gate latches and boundary words, in creation order
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
    for (const View* v : {&rise, &fall, &settle}) {
      auto& names = v == &rise ? res.edge_functions : v == &fall ? res.fall_functions : res.settle_functions;
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
            [&tape, &job, &gate, words]() -> std::pair<size_t, std::string> {
              try {
                return {build_kernel(tape, job, gate, words), {}};
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

    // ---- the commit kernels: next state becomes state; a register in reset takes its initial value ----
    for (int e = 0; e < 2; ++e) {
      const auto& conditions = reset_conditions[e];
      if (q_words[e] == 0 && conditions.empty()) {
        continue;
      }
      const View&                         v = e == 0 ? rise : fall;
      std::vector<Cgen_llvm::State_input> layout;
      for (const auto id : conditions) {
        const auto& d = tape.ops[id];
        layout.push_back({tape.locs[d.kind == Tape::K::input ? static_cast<size_t>(d.imm) : v.slot[id]], d.width, d.unsign, 0});
      }
      auto& name = e == 0 ? res.commit_function : res.fall_commit_function;
      name       = out.symbol + (e == 0 ? "_c" : "_d");
      Cgen_llvm      k(name, Cgen_llvm::State_layout{words, layout});
      std::string    error;
      // The reset conditions were stored by this edge's kernels: read them before the copy below.
      std::vector<V> active;
      for (size_t i = 0; i < conditions.size(); ++i) {
        const auto& d = tape.ops[conditions[i]];
        if (auto c = v.folded(conditions[i])) {
          active.push_back(k.constant(d.width, *c, d.unsign));
        } else {
          active.push_back(d.kind == Tape::K::constant ? k.constant(d.width, d.imm, d.unsign) : k.input(i));
        }
      }
      if (!k.copy_state(q_base[e], d_base[e], q_words[e], error)) {
        bail("commit: " + error);
      }
      // The registers just copied changed NOW: what their kernels marked for it becomes current.
      if (gate.on && q_words[e] != 0 && !k.merge_state_words(gate.dirty_word, gate.next_word, gate.words(), error)) {
        bail("commit: " + error);
      }
      for (size_t i = 0; i < conditions.size(); ++i) {
        std::vector<std::pair<size_t, V>> stores;
        for (const auto& item : reset_items_) {
          if (item.active.id == conditions[i] && regs_[item.idx].negedge == (e == 1)) {
            const auto& f = regs_[item.idx].field;
            stores.emplace_back(f.word, k.constant_words(f.bits, twos_words(item.init, f.bits), !f.sign));
          }
        }
        // A register in reset is not what its kernel stored: every kernel runs again (reset is rare).
        if (!k.state_store_if(active[i], stores, gate.all())) {
          bail("commit: register reset");
        }
      }
      const auto object = stem + (e == 0 ? "-c.llvm.o" : "-d.llvm.o");
      if (!k.write_object(object, error, true, object.substr(0, object.size() - 1) + "k")) {
        bail("llvm: " + error);
      }
      res.instructions += k.instruction_count();
      res.objects.push_back(object);
    }
    timer.lap("commit kernels");
    if (timer.on) {
      std::fprintf(stderr,
                   "lnast_llvm: %zu ops (%zu rise, %zu fall, %zu settle live; %llu + %llu + %llu words of code), %zu + %zu + %zu "
                   "kernels%s, %zu state words, %zu registers in place, %zu + %zu words copied at commit, %zu latches\n",
                   tape.ops.size(),
                   rise.count_live(),
                   fall.count_live(),
                   settle.count_live(),
                   static_cast<unsigned long long>(rise.size),
                   static_cast<unsigned long long>(fall.size),
                   static_cast<unsigned long long>(settle.size),
                   res.edge_functions.size(),
                   res.fall_functions.size(),
                   res.settle_functions.size(),
                   gate.on ? " (activity gated)" : "",
                   words,
                   in_place,
                   q_words[0],
                   q_words[1],
                   latch_ops_.size());
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
        } else if (r.image) {
          // Entry-major little-endian words, (entry_bits + 63) / 64 per entry: power_on puts each entry.
          const uint32_t whole = static_cast<uint32_t>(r.mem_size * r.field.bits);
          const auto     all   = twos_words(*r.image, whole);
          const size_t   nw    = (r.field.bits + 63) / 64;
          auto           bit   = [&](uint64_t i) { return ((all[i / 64] >> (i % 64)) & 1) != 0; };
          f.init_words.assign(nw * r.mem_size, 0);
          for (uint64_t e = 0; e < r.mem_size; ++e) {
            for (uint32_t b = 0; b < r.field.bits; ++b) {
              if (bit(e * r.field.bits + b)) {
                f.init_words[e * nw + b / 64] |= uint64_t{1} << (b % 64);
              }
            }
          }
        }
      } else if (r.mem_size != 0) {
        f.fill = r.reset_pin ? 2 : 0;  // a memory nothing initializes and nothing resets powers on zero
      } else {
        f.fill = r.reset_pin ? 2 : 1;
      }
      reg_fields.push_back(f);
    }
    for (const auto& u : unknown_sites_) {  // a drawn `?` literal: random at power-on whatever the init policy
      Field f;
      f.name = u.name;
      f.bits = u.bits;
      f.word = tape.locs[u.loc];
      f.fill = 2;
      reg_fields.push_back(f);
    }
    for (const auto& site : undefined_sites_) {
      res.undefined.push_back({site.what, site.where, tape.locs[tape.arrays[site.array].loc]});
    }
    for (const auto& h : hidden_) {  // a clock gate's enable latch: state like any other (checkpoints)
      Field f;
      f.name     = h.name;
      f.bits     = 1;
      f.word     = tape.locs[h.loc];
      f.has_init = true;
      f.init_words.assign(1, 0);
      reg_fields.push_back(f);
    }
    if (!out.json.empty()) {
      std::ofstream js(out.json);
      auto          dump = [&](const char* key, const std::vector<Field>& v, bool with_init) {
        js << "  \"" << key << "\": [";
        for (size_t i = 0; i < v.size(); ++i) {
          js << (i ? ", " : "") << "{\"name\":\"" << v[i].name << "\",\"bits\":" << v[i].bits << ",\"sign\":" << (v[i].sign ? 1 : 0)
             << ",\"word\":" << v[i].word;
          if (!v[i].marks.empty()) {  // activity gating: the kernel bits to set when this input changes
            js << ",\"marks\":\"";
            for (size_t m = 0; m < v[i].marks.size(); ++m) {
              js << (m ? " " : "") << v[i].marks[m];
            }
            js << "\"";
          }
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
      js << "{\n  \"words\": " << words << ",\n  \"gate_word\": " << gate.dirty_word
         << ",\n  \"gate_bits\": " << (gate.on ? gate.bits : 0) << ",\n  \"commit\": \"" << res.commit_function << "\",\n";
      list("edge", res.edge_functions);
      list("fall", res.fall_functions);
      js << "  \"fall_commit\": \"" << res.fall_commit_function << "\",\n";
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
    res.words     = words;
    res.gate_word = gate.dirty_word;
    res.gate_bits = gate.on ? gate.bits : 0;
    res.ok        = true;
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
