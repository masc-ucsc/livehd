//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The ONE spelling of the simulator tune vector (sim_profile.md §3, §7): the
// resolved values of the G-class `sim.tune.*` knobs, i.e. the knobs that change
// the generated C++ but never a simulated value. Shared, header-only, by the
// codegen (inou.cgen.sim folds `tv1()` into the COLOR ROOT's generation key)
// and by lhd (the tune store records, compares and exports the same string),
// so the two can never disagree on what "the same vector" means.
//
// Deliberately free of hhds/LLVM includes: lhd's tune model links it without
// dragging the emitter in. Color_plan's kDefaultLiveWords / kDefaultFenceRatio
// / kNoFences alias the constants below.

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace livehd::sim {

// The built-in vector is L1, `d=on f=16` (ruling 2026-09-19): the large designs
// the simulator exists for win with dirty gating (minion 2-3x, xs_alu 4x, xs_rob
// ~100x), while the small always-toggling DUTs it slows (lhdtrack's LFSR
// harnesses, 1.2-3.6x) pin `sim.tune.dirty=off` in their scripts, and the tuner
// trials L0 for any design it profiles busy.
inline constexpr bool     kTuneDefaultDirty      = true;
inline constexpr uint64_t kTuneDefaultLiveWords  = 256;
inline constexpr int64_t  kTuneDefaultFenceRatio = 16;
inline constexpr int64_t  kTuneNoFences          = std::numeric_limits<int64_t>::max();
inline constexpr uint64_t kTuneMaxNumber         = uint64_t{1} << 20;

// Default-constructed == the built-in default vector.
struct Tune_vector {
  bool     dirty      = kTuneDefaultDirty;       // sim.tune.dirty
  int64_t  fence      = kTuneDefaultFenceRatio;  // sim.tune.fence: kTuneNoFences == "none", else a ratio in [0, 2^20]
  uint64_t live_words = kTuneDefaultLiveWords;   // sim.tune.live_words, in [1, 2^20]
  bool     llvm       = false;                   // sim.tune.backend

  bool operator==(const Tune_vector&) const = default;

  [[nodiscard]] std::string dirty_str() const { return dirty ? "on" : "off"; }
  [[nodiscard]] std::string fence_str() const { return fence == kTuneNoFences ? std::string("none") : std::to_string(fence); }
  [[nodiscard]] std::string live_words_str() const { return std::to_string(live_words); }
  [[nodiscard]] std::string backend_str() const { return llvm ? "llvm" : "slop"; }

  // Canonical text: "tv1:d=on;f=16;lw=256;be=slop". Byte-stable: it is a
  // generation-key input and the tune store's vector identity.
  [[nodiscard]] std::string tv1() const {
    return "tv1:d=" + dirty_str() + ";f=" + fence_str() + ";lw=" + live_words_str() + ";be=" + backend_str();
  }

  // The `--set` list that reproduces this vector exactly.
  [[nodiscard]] std::string reproduce() const {
    return "--set sim.tune.dirty=" + dirty_str() + " --set sim.tune.fence=" + fence_str() + " --set sim.tune.live_words="
           + live_words_str() + " --set sim.tune.backend=" + backend_str();
  }

  static std::optional<Tune_vector> parse(std::string_view text);
};

// ---- knob grammar (the `--set sim.tune.X=V` values). Each returns:
//   ok=false            -> `err` names the problem (usage error);
//   ok=true, !value     -> "auto" or empty: this source does not pin the knob;
//   ok=true, value      -> the pinned value.
template <class T>
struct Tune_knob {
  bool             ok = true;
  std::optional<T> value;
  std::string      err;
};

namespace tune_detail {
inline bool whole(std::string_view v, uint64_t lo, uint64_t hi, uint64_t& out) {
  if (v.empty()) {
    return false;
  }
  const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
  return ec == std::errc{} && end == v.data() + v.size() && out >= lo && out <= hi;
}
}  // namespace tune_detail

// auto|on|off, with true|false|1|0 accepted as aliases (TOML `dirty = true`).
inline Tune_knob<bool> parse_tune_dirty(std::string_view v) {
  if (v.empty() || v == "auto") {
    return {};
  }
  if (v == "on" || v == "true" || v == "1") {
    return {true, true, {}};
  }
  if (v == "off" || v == "false" || v == "0") {
    return {true, false, {}};
  }
  return {false, std::nullopt, "expects auto|on|off, got '" + std::string(v) + "'"};
}

// auto|none|N, N a whole number of sites per interface word in [0, 2^20].
inline Tune_knob<int64_t> parse_tune_fence(std::string_view v) {
  if (v.empty() || v == "auto") {
    return {};
  }
  if (v == "none") {
    return {true, kTuneNoFences, {}};
  }
  uint64_t n = 0;
  if (tune_detail::whole(v, 0, kTuneMaxNumber, n)) {
    return {true, static_cast<int64_t>(n), {}};
  }
  return {false, std::nullopt, "expects auto|none|N (N a whole number in [0, 1048576]), got '" + std::string(v) + "'"};
}

// auto|N, N a whole number of 64-bit words in [1, 2^20].
inline Tune_knob<uint64_t> parse_tune_live_words(std::string_view v) {
  if (v.empty() || v == "auto") {
    return {};
  }
  uint64_t n = 0;
  if (tune_detail::whole(v, 1, kTuneMaxNumber, n)) {
    return {true, n, {}};
  }
  return {false, std::nullopt, "expects auto|N (N a whole number in [1, 1048576]), got '" + std::string(v) + "'"};
}

// auto|slop|llvm (value = true for llvm).
inline Tune_knob<bool> parse_tune_backend(std::string_view v) {
  if (v.empty() || v == "auto") {
    return {};
  }
  if (v == "slop") {
    return {true, false, {}};
  }
  if (v == "llvm") {
    return {true, true, {}};
  }
  return {false, std::nullopt, "expects auto|slop|llvm, got '" + std::string(v) + "'"};
}

// Built-in defaults for whatever no source pinned. The fence default FOLLOWS
// dirty: fences only pay when dirty gating can skip what they isolate
// (sim_profile.md §1), so `auto` is none with dirty off and 16 with it on.
inline Tune_vector resolve_tune_defaults(std::optional<bool> dirty, std::optional<int64_t> fence,
                                         std::optional<uint64_t> live_words, std::optional<bool> llvm) {
  Tune_vector v;
  v.dirty      = dirty.value_or(kTuneDefaultDirty);
  v.fence      = fence.value_or(v.dirty ? kTuneDefaultFenceRatio : kTuneNoFences);
  v.live_words = live_words.value_or(kTuneDefaultLiveWords);
  v.llvm       = llvm.value_or(false);
  return v;
}

// Normalize the raw values Color_plan::discover / Cgen_sim CONSUME into the
// canonical vector: discover() reads a negative fence as kTuneDefaultFenceRatio
// and live_words 0 as kTuneDefaultLiveWords, so those spellings build the same
// plan and must print (and key) identically. The dirty-dependent `auto` fence
// is CALLER policy (resolve_tune_defaults), not normalization.
inline Tune_vector canonical_tune_vector(bool dirty, int64_t fence_ratio, uint64_t live_words, bool llvm) {
  Tune_vector v;
  v.dirty      = dirty;
  v.fence      = fence_ratio < 0 ? kTuneDefaultFenceRatio : fence_ratio;
  v.live_words = live_words == 0 ? kTuneDefaultLiveWords : live_words;
  v.llvm       = llvm;
  return v;
}

inline std::optional<Tune_vector> Tune_vector::parse(std::string_view text) {
  constexpr std::string_view kPrefix = "tv1:";
  if (!text.starts_with(kPrefix)) {
    return std::nullopt;
  }
  text.remove_prefix(kPrefix.size());
  Tune_vector v;
  bool        seen_d = false, seen_f = false, seen_lw = false, seen_be = false;
  while (!text.empty()) {
    const auto semi = text.find(';');
    const auto item = text.substr(0, semi);
    text            = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);
    const auto eq   = item.find('=');
    if (eq == std::string_view::npos) {
      return std::nullopt;
    }
    const auto key = item.substr(0, eq);
    const auto val = item.substr(eq + 1);
    if (key == "d") {
      const auto k = parse_tune_dirty(val);
      if (!k.ok || !k.value) {
        return std::nullopt;
      }
      v.dirty = *k.value;
      seen_d  = true;
    } else if (key == "f") {
      const auto k = parse_tune_fence(val);
      if (!k.ok || !k.value) {
        return std::nullopt;
      }
      v.fence = *k.value;
      seen_f  = true;
    } else if (key == "lw") {
      const auto k = parse_tune_live_words(val);
      if (!k.ok || !k.value) {
        return std::nullopt;
      }
      v.live_words = *k.value;
      seen_lw      = true;
    } else if (key == "be") {
      const auto k = parse_tune_backend(val);
      if (!k.ok || !k.value) {
        return std::nullopt;
      }
      v.llvm  = *k.value;
      seen_be = true;
    } else {
      return std::nullopt;
    }
  }
  if (!(seen_d && seen_f && seen_lw && seen_be)) {
    return std::nullopt;
  }
  return v;
}

}  // namespace livehd::sim
