//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Generated-C++ runtime text shared by the two simulator emitters: inou.cgen.sim
// (module headers, kernel TUs) and prp_sim (the drv.cpp driver). Both emit these
// strings VERBATIM, under the same include guards, so a TU that sees a DUT header
// and the driver's own copy compiles one definition, and every TU of one program
// agrees token for token (the inline variables and templates below are ODR-shared
// program-wide). One constant per text is the point: two hand-kept copies would be
// an ODR violation waiting to happen.
//
// Both texts assume slop.hpp (Slop / Slop_u) is already included.

#include <string_view>

namespace livehd::sim {

// `?` literals (sim_profile.md ruling 3). One parse per (width, spelling key)
// for the whole program, drawn on FIRST CALL (after main() has seeded hlop and
// applied `--set sim.unknown_zero=true`). A function template's local static is
// ONE object across every TU, so every site that emits the same literal gets
// the same value -- an unknown net has one value per run.
//
// `__lhd_unknown_zero` is the RUN-TIME spelling of sim.unknown_zero=true: every
// `?` bit resolves to 0, byte-for-byte the value a build generated with
// sim.unknown_zero=true folds at compile time (same '?' -> '0' substitution as
// the codegen's sim_const_text). `__lhd_unknown_draws` counts the `?` literals
// resolved in this process: a random-filled run whose count is 0 is still
// deterministic, which the tuner's oracle uses (sim_profile.md ruling 10).
inline constexpr std::string_view kUnknownLiteralHelper
    = "#ifndef LHD_SIM_UNKNOWN_LITERAL_V2\n"
      "#define LHD_SIM_UNKNOWN_LITERAL_V2\n"
      "#include <cstring>\n"
      "#include <string>\n"
      "// Run-time sim.unknown_zero: drv.bin's `--set sim.unknown_zero=true` sets it\n"
      "// before any test runs. `?` literals resolved so far are counted below.\n"
      "inline bool               __lhd_unknown_zero  = false;\n"
      "inline unsigned long long __lhd_unknown_draws = 0;\n"
      "template <int W, unsigned long long K>\n"
      "inline Slop<W> __lhd_unknown_literal(const char* __txt) {\n"
      "  static const Slop<W> __v = [__txt]() -> Slop<W> {\n"
      "    if (std::strchr(__txt, '?') == nullptr) return Slop<W>::from_pyrope(__txt);\n"
      "    ++__lhd_unknown_draws;\n"
      "    if (!__lhd_unknown_zero) return Slop<W>::from_pyrope(__txt);\n"
      "    std::string __z(__txt);\n"
      "    for (auto& __c : __z) if (__c == '?') __c = '0';\n"
      "    return Slop<W>::from_pyrope(__z.c_str());\n"
      "  }();\n"
      "  return __v;\n"
      "}\n"
      "#endif\n";

// Canonical state hashing for the tune profiler (sim_profile.md §6.1) and the
// end-of-test digest. A value hashes by its low N bits only: neither Slop's
// type tag nor the lazily non-canonical storage above bit N-1 reaches the hash,
// so Slop<8>(-56), Slop<8>(200) and Slop_u<8>(200) all hash alike, and the hash
// is invariant under sim.slop_u. A value of <= 64 bits hashes to its own bits
// (exact change detection); wider values and memories fold their words.
//
// `__lhd_tune_support` is the table a COLOR ROOT publishes (static
// `__tune_support()`) next to its `__tune_sources(uint64_t*)` hasher: the
// plan's single-period fan-in "support" of every live site, grouped into
// classes (sim_profile.md §8.1 I_s). Source k (written as word k by
// __tune_sources) sets bit `source_bucket[k]` of a words-wide activity mask when
// its hash changed; class c is idle in a sampled pair iff
// (class_bits[c*words + w] & mask[w]) == 0 for every w, and then contributes
// class_ge[c] of idle GE -- and likewise class_sites / class_cost /
// class_cost_flat of the other three weights (Color_plan::Support_class: one
// per site; simulation words, a compact loop its body x lanes; the same with no
// body multiplier). Every executable site is in exactly one class, so each
// total_* is the sum of its class_* column. Sentinels: source_occ[k] == occs
// marks a top input; class_occ[c] == occs marks a class spanning several
// occurrences (merged by equal rows past the table cap, or one of the trailing
// fold groups whose row is the OR of its members -- conservative; see
// Color_plan::Support::fold_*); a class whose bits are all zero reads only
// constants and is always idle.
inline constexpr std::string_view kTuneHashHelper
    = "#ifndef LHD_SIM_TUNE_HASH_V1\n"
      "#define LHD_SIM_TUNE_HASH_V1\n"
      "#include <array>\n"
      "#include <cstddef>\n"
      "#include <cstdint>\n"
      "#include <type_traits>\n"
      "#include <vector>\n"
      "inline std::uint64_t __lhd_tune_fmix(std::uint64_t k) {\n"
      "  k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ULL; k ^= k >> 33;\n"
      "  return k;\n"
      "}\n"
      "inline std::uint64_t __lhd_tune_fold(std::uint64_t h, std::uint64_t w) {\n"
      "  return (h ^ __lhd_tune_fmix(w)) * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL;\n"
      "}\n"
      "template <int N, class T>\n"
      "inline std::uint64_t __lhd_tune_value(const T& v) {\n"
      "  constexpr std::size_t K = T::packed_word_count;\n"
      "  constexpr std::size_t C = (static_cast<std::size_t>(N) + 63) / 64;  // canonical words\n"
      "  static_assert(C >= 1 && C <= K, \"__lhd_tune_value: unexpected packed layout\");\n"
      "  std::uint64_t  small[K <= 64 ? K : 1];\n"
      "  std::vector<std::uint64_t> big;\n"
      "  std::uint64_t* w = small;\n"
      "  if constexpr (K > 64) { big.resize(K); w = big.data(); }\n"
      "  v.copy_packed_words(w);\n"
      "  if constexpr (N % 64 != 0) w[C - 1] &= (std::uint64_t{1} << (N % 64)) - 1;\n"
      "  if constexpr (C == 1) {\n"
      "    return w[0];\n"
      "  } else {\n"
      "    std::uint64_t h = static_cast<std::uint64_t>(N);\n"
      "    for (std::size_t i = 0; i < C; ++i) h = __lhd_tune_fold(h, w[i]);\n"
      "    return h;\n"
      "  }\n"
      "}\n"
      "template <int N>\n"
      "inline std::uint64_t __lhd_tune_h(const Slop<N>& v) { return __lhd_tune_value<N>(v); }\n"
      "template <int N>\n"
      "inline std::uint64_t __lhd_tune_h(const Slop_u<N>& v) { return __lhd_tune_value<N>(v); }\n"
      "template <class I>\n"
      "  requires std::is_integral_v<I>\n"
      "inline std::uint64_t __lhd_tune_h(I v) { return static_cast<std::uint64_t>(v); }\n"
      "template <class T, std::size_t M>\n"
      "inline std::uint64_t __lhd_tune_h(const std::array<T, M>& a) {\n"
      "  std::uint64_t h = M;\n"
      "  for (const auto& e : a) h = __lhd_tune_fold(h, __lhd_tune_h(e));\n"
      "  return h;\n"
      "}\n"
      "template <class M>\n"
      "  requires requires(const M& m) { m.entries(); }\n"
      "inline std::uint64_t __lhd_tune_h(const M& m) {\n"
      "  std::uint64_t h = m.entries().size();\n"
      "  for (const auto& e : m.entries()) h = __lhd_tune_fold(h, __lhd_tune_h(e));\n"
      "  return h;\n"
      "}\n"
      "struct __lhd_tune_support {\n"
      "  std::uint32_t        words;          // activity-mask words per class\n"
      "  std::uint32_t        sources;        // words __tune_sources writes\n"
      "  std::uint32_t        classes;\n"
      "  std::uint32_t        occs;\n"
      "  bool                 exact;          // one bucket per source (no aliasing)\n"
      "  std::uint64_t        total_ge;       // sum of class_ge\n"
      "  const std::uint64_t* class_bits;     // [classes * words]\n"
      "  const std::uint64_t* class_ge;       // [classes]\n"
      "  const std::uint32_t* class_occ;      // [classes] occurrence index of the class\n"
      "  const std::uint32_t* source_bucket;  // [sources] bit index in [0, 64 * words)\n"
      "  const std::uint32_t* source_occ;     // [sources] occurrence index; == occs for a top input\n"
      "  const char* const*   occ_path;       // [occs] instance path from the root (\"\" = the root)\n"
      "  const char* const*   occ_def;        // [occs] LGraph definition name\n"
      "  const std::uint64_t* occ_ge;         // [occs] GE of the occurrence's own live sites\n"
      "  std::uint64_t        total_sites;      // sum of class_sites (every executable site)\n"
      "  std::uint64_t        total_cost;       // sum of class_cost\n"
      "  std::uint64_t        total_cost_flat;  // sum of class_cost_flat\n"
      "  const std::uint64_t* class_sites;      // [classes] executable sites of the class\n"
      "  const std::uint64_t* class_cost;       // [classes] simulation words (loops: body x lanes)\n"
      "  const std::uint64_t* class_cost_flat;  // [classes] simulation words, no body multiplier\n"
      "  const std::uint64_t* occ_cost;         // [occs] cost of the occurrence's own live sites\n"
      "};\n"
      "#endif\n";

}  // namespace livehd::sim
