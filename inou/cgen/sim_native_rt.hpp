// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "sim_native_abi.hpp"

namespace livehd::sim {
class Native_instance;

// Shared object loader. Loading relocatable machine code needs neither a C++
// compiler nor a generated host translation unit or executable link command.
class Native_objects {
public:
  Native_objects();
  ~Native_objects();
  Native_objects(const Native_objects&)                             = delete;
  Native_objects&                  operator=(const Native_objects&) = delete;
  bool                             load(const std::string& path, std::string& error);
  Native_function                  lookup(const std::string& name, std::string& error);
  // The instance keeps its object code alive even after this loader is gone.
  std::unique_ptr<Native_instance> instantiate(const std::string& descriptor, std::string& error);

private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

struct Native_action {
  Native_function function   = nullptr;
  // mask==0 means unconditional. Clear before invoking so a kernel can mark
  // a later phase (or its next invocation) dirty without losing the update.
  size_t          dirty_word = 0;
  uint64_t        dirty_mask = 0;
};

// State, clock-phase scheduling and kernel dispatch use one implementation
// for every design. Actions include native register/memory commit functions.
// The generator supplies actions in Color_plan execution/barrier order.
class Native_instance {
public:
  explicit Native_instance(size_t words) : state_(words), public_words_(words) {}
  Native_instance(const Native_instance&)                   = delete;
  Native_instance& operator=(const Native_instance&)        = delete;
  Native_instance(Native_instance&&) noexcept               = default;
  Native_instance&    operator=(Native_instance&&) noexcept = default;
  // Only published boundary storage is visible; private words are addressed
  // by offsets baked into the native object, without per-field link symbols.
  std::span<uint64_t> state() { return std::span(state_).first(public_words_); }
  void                reset();
  void                bind_resources(std::vector<void*> resources) { resources_ = std::move(resources); }
  bool                schedule(std::vector<Native_action> actions, std::string& error);
  void                step();

private:
  friend class Native_objects;
  std::shared_ptr<void>                objects_;
  std::vector<uint64_t>                state_;
  size_t                               public_words_;
  std::span<const Native_initial_word> initial_;
  std::vector<void*>                   resources_;
  std::vector<Native_action>           actions_;
};
}  // namespace livehd::sim
