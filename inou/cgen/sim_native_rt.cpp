// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sim_native_rt.hpp"

#include <algorithm>
#include <limits>

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "sim_llvm_target.hpp"

namespace livehd::sim {
class Native_objects::Impl {
public:
  std::unique_ptr<llvm::orc::LLJIT> linker;
  std::string                       failure;
  Impl() {
    initialize_llvm_target();
    auto created = llvm::orc::LLJITBuilder().create();
    if (!created) {
      failure = llvm::toString(created.takeError());
    } else {
      linker = std::move(*created);
    }
  }
};

Native_objects::Native_objects() : impl_(std::make_shared<Impl>()) {}
Native_objects::~Native_objects() = default;

bool Native_objects::load(const std::string& path, std::string& error) {
  if (!impl_->linker) {
    error = impl_->failure;
    return false;
  }
  auto bytes = llvm::MemoryBuffer::getFile(path);
  if (!bytes) {
    error = path + ": " + bytes.getError().message();
    return false;
  }
  if (auto failure = impl_->linker->addObjectFile(std::move(*bytes))) {
    error = path + ": " + llvm::toString(std::move(failure));
    return false;
  }
  return true;
}

Native_function Native_objects::lookup(const std::string& name, std::string& error) {
  if (!impl_->linker) {
    error = impl_->failure;
    return nullptr;
  }
  auto address = impl_->linker->lookup(name);
  if (!address) {
    error = name + ": " + llvm::toString(address.takeError());
    return nullptr;
  }
  return address->toPtr<Native_function>();
}

std::unique_ptr<Native_instance> Native_objects::instantiate(const std::string& descriptor, std::string& error) {
  if (!impl_->linker) {
    error = impl_->failure;
    return nullptr;
  }
  auto address = impl_->linker->lookup(descriptor);
  if (!address) {
    error = descriptor + ": " + llvm::toString(address.takeError());
    return nullptr;
  }
  const auto& image = *address->toPtr<const Native_state_image*>();
  if (image.abi != native_state_abi || image.words > std::numeric_limits<size_t>::max() / sizeof(uint64_t)
      || image.public_words > image.words || image.initial_count > image.words || image.entry == nullptr
      || (image.initial_count != 0 && image.initial == nullptr)) {
    error = "invalid native state descriptor: " + descriptor;
    return nullptr;
  }
  for (uint64_t i = 0; i < image.initial_count; ++i) {
    if (image.initial[i].word >= image.words || (i != 0 && image.initial[i - 1].word >= image.initial[i].word)) {
      error = "invalid native state initialization: " + descriptor;
      return nullptr;
    }
  }
  auto instance           = std::make_unique<Native_instance>(image.words);
  instance->objects_      = impl_;
  instance->public_words_ = image.public_words;
  if (image.initial_count != 0) {
    instance->initial_ = {image.initial, static_cast<size_t>(image.initial_count)};
  }
  instance->actions_.push_back({image.entry, 0, 0});
  instance->reset();
  return instance;
}

void Native_instance::reset() {
  std::ranges::fill(state_, 0);
  for (const auto& word : initial_) {
    state_[word.word] = word.value;
  }
}

bool Native_instance::schedule(std::vector<Native_action> actions, std::string& error) {
  for (const auto& action : actions) {
    if (action.function == nullptr || (action.dirty_mask != 0 && action.dirty_word >= state_.size())) {
      error = "invalid native simulator action";
      return false;
    }
  }
  actions_ = std::move(actions);
  return true;
}

void Native_instance::step() {
  for (const auto& action : actions_) {
    if (action.dirty_mask != 0) {
      auto& dirty = state_[action.dirty_word];
      if ((dirty & action.dirty_mask) == 0) {
        continue;
      }
      dirty &= ~action.dirty_mask;
    }
    action.function(state_.data(), resources_.data());
  }
}
}  // namespace livehd::sim
