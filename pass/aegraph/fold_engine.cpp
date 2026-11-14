//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fold_engine.hpp"

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>

#include "debug_env.hpp"

namespace livehd {

namespace {
std::atomic<Fold_cost> g_cost{Fold_cost::synth};

struct Rules_disable {
  std::mutex  mutex;
  std::string list;
};
Rules_disable& rules_disable() {
  static Rules_disable r;
  return r;
}

struct Counters {
  std::mutex                      mutex;
  std::map<std::string, uint64_t> counts;
};

Counters& counters() {
  static Counters c;
  return c;
}

bool log_enabled() {
  static const bool on = livehd::debug::env<"LHD_FOLD_BODY_LOG">() != nullptr;
  return on;
}
}  // namespace

const char* fold_cost_name(Fold_cost cost) { return cost == Fold_cost::sim ? "sim" : "synth"; }

void      set_fold_default_cost(Fold_cost cost) { g_cost.store(cost, std::memory_order_relaxed); }
Fold_cost fold_default_cost() { return g_cost.load(std::memory_order_relaxed); }

void set_fold_rules_disable(std::string list) {
  auto&                 r = rules_disable();
  const std::lock_guard lock(r.mutex);
  r.list = std::move(list);
}

std::string fold_rules_disable() {
  auto&                 r = rules_disable();
  const std::lock_guard lock(r.mutex);
  return r.list;
}

// The default spelling is the one the aegraph engine had while it was one of several, so persistent caches
// written then stay valid.
std::string fold_descriptor() {
  auto text = std::string{"engine=aegraph;cost="} + fold_cost_name(fold_default_cost());
  if (const auto off = fold_rules_disable(); !off.empty()) {
    text += ";rules_disable=" + off;
  }
  return text;
}

uint64_t fold_salt_mix(uint64_t salt) {
  const auto text = fold_descriptor();
  uint64_t   h    = salt ^ 0xcbf29ce484222325ULL;  // FNV-1a over the descriptor, seeded by the salt
  for (const unsigned char c : text) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return h;
}

void fold_note(std::string_view site) {
  {
    auto&                 c = counters();
    const std::lock_guard lock(c.mutex);
    ++c.counts[std::string{site}];
  }
  if (log_enabled()) {
    std::fprintf(stderr, "fold_body engine=aegraph site=%.*s\n", static_cast<int>(site.size()), site.data());
  }
}

uint64_t fold_count(std::string_view site) {
  auto&                 c = counters();
  const std::lock_guard lock(c.mutex);
  const auto            it = c.counts.find(std::string{site});
  return it == c.counts.end() ? 0 : it->second;
}

void fold_counters_reset() {
  auto&                 c = counters();
  const std::lock_guard lock(c.mutex);
  c.counts.clear();
}

}  // namespace livehd
