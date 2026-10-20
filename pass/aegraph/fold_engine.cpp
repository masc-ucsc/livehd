//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fold_engine.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

namespace livehd {

namespace {
std::atomic<Fold_engine> g_engine{Fold_engine::classic};
std::atomic<Fold_cost>   g_cost{Fold_cost::synth};

struct Counters {
  std::mutex                                         mutex;
  std::map<std::pair<std::string, Fold_engine>, uint64_t> counts;
};

Counters& counters() {
  static Counters c;
  return c;
}

bool log_enabled() {
  static const bool on = std::getenv("LHD_FOLD_BODY_LOG") != nullptr;
  return on;
}
}  // namespace

std::optional<Fold_engine> parse_fold_engine(std::string_view text) {
  if (text == "classic") {
    return Fold_engine::classic;
  }
  if (text == "aegraph") {
    return Fold_engine::aegraph;
  }
  if (text == "aegraph+classic") {
    return Fold_engine::aegraph_classic;
  }
  return std::nullopt;
}

const char* fold_engine_name(Fold_engine engine) {
  switch (engine) {
    case Fold_engine::classic: return "classic";
    case Fold_engine::aegraph: return "aegraph";
    case Fold_engine::aegraph_classic: return "aegraph+classic";
  }
  return "classic";
}

const char* fold_cost_name(Fold_cost cost) { return cost == Fold_cost::sim ? "sim" : "synth"; }

void        set_fold_engine(Fold_engine engine) { g_engine.store(engine, std::memory_order_relaxed); }
Fold_engine fold_engine() { return g_engine.load(std::memory_order_relaxed); }
void        set_fold_default_cost(Fold_cost cost) { g_cost.store(cost, std::memory_order_relaxed); }
Fold_cost   fold_default_cost() { return g_cost.load(std::memory_order_relaxed); }

std::string fold_descriptor() {
  const auto engine = fold_engine();
  if (engine == Fold_engine::classic) {
    return {};
  }
  return std::string{"engine="} + fold_engine_name(engine) + ";cost=" + fold_cost_name(fold_default_cost());
}

uint64_t fold_salt_mix(uint64_t salt) {
  const auto text = fold_descriptor();
  if (text.empty()) {
    return salt;
  }
  uint64_t h = salt ^ 0xcbf29ce484222325ULL;  // FNV-1a over the descriptor, seeded by the salt
  for (const unsigned char c : text) {
    h ^= c;
    h *= 0x100000001b3ULL;
  }
  return h;
}

void fold_note(std::string_view site, Fold_engine engine) {
  {
    auto&                 c = counters();
    const std::lock_guard lock(c.mutex);
    ++c.counts[{std::string{site}, engine}];
  }
  if (log_enabled()) {
    std::fprintf(stderr, "fold_body engine=%s site=%.*s\n", fold_engine_name(engine), static_cast<int>(site.size()), site.data());
  }
}

uint64_t fold_count(std::string_view site, Fold_engine engine) {
  auto&                 c = counters();
  const std::lock_guard lock(c.mutex);
  const auto            it = c.counts.find({std::string{site}, engine});
  return it == c.counts.end() ? 0 : it->second;
}

uint64_t fold_count(std::string_view site) {
  auto&                 c = counters();
  const std::lock_guard lock(c.mutex);
  uint64_t              total = 0;
  for (const auto& [key, n] : c.counts) {
    if (key.first == site) {
      total += n;
    }
  }
  return total;
}

void fold_counters_reset() {
  auto&                 c = counters();
  const std::lock_guard lock(c.mutex);
  c.counts.clear();
}

}  // namespace livehd
