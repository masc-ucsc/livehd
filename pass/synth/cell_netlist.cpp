// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_netlist.hpp"

namespace livehd::synth {

namespace {
constexpr uint64_t kIdentity = 0xAAAAAAAAAAAAAAAAULL;  // f(x) = x0, replicated
}

Cell_decl declare_cell(hhds::GraphLibrary& outlib, const Cell_type& type) {
  Cell_decl desc;
  desc.name        = type.name;
  desc.output_name = type.output;
  desc.io          = outlib.find_io(desc.name);
  const bool fresh = !desc.io;
  if (fresh) {
    desc.io = outlib.create_io(desc.name);
  }
  hhds::Port_id pid = 1;
  for (const auto& input : type.inputs) {
    desc.input_names.push_back(input);
    if (fresh) {
      desc.io->add_input(desc.input_names.back(), pid);
      desc.io->set_bits(desc.input_names.back(), 1);
    }
    ++pid;
  }
  if (fresh) {
    desc.io->add_output(desc.output_name, pid);
    desc.io->set_bits(desc.output_name, 1);
  }
  return desc;
}

uint32_t Cell_library::add(Cell_type type) {
  const auto id = static_cast<uint32_t>(types_.size());
  if (type.inputs.size() <= 6) {
    const auto key = std::pair<size_t, uint64_t>{type.inputs.size(), type.truth};
    auto       it  = twin_.find(key);
    if (it == twin_.end() || type.area < types_[it->second].area) {
      twin_[key] = id;
    }
  }
  types_.push_back(std::move(type));
  return id;
}

bool Cell_library::is_identity(uint32_t id) const { return types_[id].inputs.size() == 1 && types_[id].truth == kIdentity; }

bool Cell_library::is_inverter(uint32_t id) const { return types_[id].inputs.size() == 1 && types_[id].truth == ~kIdentity; }

std::optional<uint32_t> Cell_library::inverting_twin(uint32_t id) const {
  const auto& t = types_[id];
  if (t.inputs.size() > 6) {
    return std::nullopt;
  }
  auto it = twin_.find(std::pair<size_t, uint64_t>{t.inputs.size(), ~t.truth});
  return it == twin_.end() ? std::nullopt : std::optional<uint32_t>{it->second};
}

const Cell_decl& Cell_library::decl(hhds::GraphLibrary& outlib, uint32_t id) const {
  if (decl_lib_ != &outlib) {
    decls_.clear();
    decl_lib_ = &outlib;
  }
  if (decls_.size() < types_.size()) {
    decls_.resize(types_.size());
  }
  auto& slot = decls_[id];
  if (!slot) {
    slot = declare_cell(outlib, types_[id]);
  }
  return *slot;
}

std::vector<uint32_t> Cell_netlist::readers() const {
  std::vector<uint32_t> count(signals.size(), 0);
  const auto            read = [&](uint32_t s) {
    if (s != kNone) {
      ++count[s];
    }
  };
  for (const auto& c : cells) {
    for (auto f : c.fanins) {
      read(f);
    }
  }
  for (auto o : outputs) {
    read(o);
  }
  for (const auto& l : latches) {
    read(l.d);
  }
  return count;
}

std::vector<uint32_t> Cell_netlist::cell_readers() const {
  std::vector<uint32_t> count(signals.size(), 0);
  for (const auto& c : cells) {
    for (auto f : c.fanins) {
      if (f != kNone) {
        ++count[f];
      }
    }
  }
  return count;
}

}  // namespace livehd::synth
