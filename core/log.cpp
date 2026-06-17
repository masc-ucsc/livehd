//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "log.hpp"

#include <array>
#include <cstdio>
#include <print>
#include <utility>

#include "iassert.hpp"

namespace livehd::log {

namespace {

// The logging channel vocabulary: the LiveHD block/pass tree. A channel sets
// the level for its whole dotted subtree (configure()/enabled() resolve by the
// longest matching ancestor). Channels for passes use the SAME token as their
// `--set <token>.<flag>` options (upass/cprop/pass.abc/…) so the two read the
// same; non-pass source areas use their directory path. "pass" is both the
// generic Pass-framework channel and the subtree root for the pass.* passes.
// Add a channel = add a line here (and it becomes a valid `--set <it>.log`).
constexpr std::string_view kChannels[] = {
    // non-pass source areas
    "core",
    "core.lnast",
    "core.lgraph",
    "core.bundle",
    "lnast",
    "inou.prp",
    "inou.slang",
    "inou.cgen",
    "inou.yosys",
    "tolg",  // upass/tolg (standalone: NOT under the "upass" runner subtree)
    // pass framework + the pass.* passes it roots
    "pass",
    "pass.abc",
    "pass.color",
    "pass.partition",
    "pass.liberty",
    // the pass/upass runner and its phases
    "upass",
    "upass.verifier",
    "upass.bitwidth",
    "upass.ssa",
    "upass.runner",
    "upass.constprop",
    "upass.semacheck",
    "upass.attributes",
    "upass.inliner",
    // standalone passes (channel == the `--set` pass token)
    "cprop",
    "bitwidth",
    "cgen",
    "lec",
};

// Explicit (channel -> level) from configure(); resolved into a per-channel
// effective level (value-init = Level::off, so nothing logs by default).
std::vector<std::pair<std::string, Level>> g_explicit;
std::array<Level, std::size(kChannels)>     g_effective{};

int find_channel(std::string_view channel) {
  for (size_t i = 0; i < std::size(kChannels); ++i) {
    if (kChannels[i] == channel) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Effective level of `channel`: the level of its longest configured ancestor
// (the channel itself, then each dotted prefix on a dot boundary), else off.
Level resolve(std::string_view channel) {
  Level  best     = Level::off;
  size_t best_len = 0;
  for (const auto& [name, lvl] : g_explicit) {
    if (channel.size() < name.size() || channel.substr(0, name.size()) != name) {
      continue;
    }
    if (channel.size() != name.size() && channel[name.size()] != '.') {
      continue;  // not a dot boundary (e.g. "upass" must not match "upassX")
    }
    if (name.size() >= best_len) {  // longer (more specific) ancestor wins
      best_len = name.size();
      best     = lvl;
    }
  }
  return best;
}

void rebuild() {
  for (size_t i = 0; i < std::size(kChannels); ++i) {
    g_effective[i] = resolve(kChannels[i]);
  }
}

}  // namespace

Level parse_level(std::string_view name, bool& ok) {
  ok = true;
  if (name == "off") {
    return Level::off;
  }
  if (name == "error") {
    return Level::error;
  }
  if (name == "warn" || name == "warning") {
    return Level::warn;
  }
  if (name == "info") {
    return Level::info;
  }
  if (name == "debug") {
    return Level::debug;
  }
  if (name == "trace") {
    return Level::trace;
  }
  ok = false;
  return Level::off;
}

std::string_view level_name(Level lvl) {
  switch (lvl) {
    case Level::off: return "off";
    case Level::error: return "error";
    case Level::warn: return "warn";
    case Level::info: return "info";
    case Level::debug: return "debug";
    case Level::trace: return "trace";
  }
  return "off";
}

bool is_channel(std::string_view channel) { return find_channel(channel) >= 0; }

const std::vector<std::string_view>& channels() {
  static const std::vector<std::string_view> v(std::begin(kChannels), std::end(kChannels));
  return v;
}

void reset() {
  g_explicit.clear();
  rebuild();
}

void configure(std::string_view channel, Level lvl) {
  for (auto& [name, l] : g_explicit) {
    if (name == channel) {
      l = lvl;  // last --set wins
      rebuild();
      return;
    }
  }
  g_explicit.emplace_back(std::string{channel}, lvl);
  rebuild();
}

int id(std::string_view channel) {
  int idx = find_channel(channel);
  if (idx < 0) {
    std::println(stderr, "LHD_LOG: unregistered channel '{}' (add it to kChannels in core/log.cpp)", channel);
  }
  I(idx >= 0, "LHD_LOG on an unregistered channel (see stderr)");
  return idx;
}

bool enabled(int channel_id, Level lvl) {
  if (lvl == Level::off || channel_id < 0 || channel_id >= static_cast<int>(std::size(kChannels))) {
    return false;
  }
  return static_cast<uint8_t>(g_effective[channel_id]) >= static_cast<uint8_t>(lvl);
}

void emit(std::string_view channel, std::string_view level, std::string_view msg) {
  std::println(stderr, "[lhd:{}:{}] {}", level, channel, msg);
}

}  // namespace livehd::log
