// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "tmap.hpp"

namespace livehd::synth {

// Owns a private ABC session. No caller frame/network is modified. Maps each
// request independently and proves the exported cell fragment against ALL rail
// assignments with a bounded SAT miter, including invalid dual-rail codes.
class Abc_tmap final : public Tmap_backend {
public:
  explicit Abc_tmap(std::string worker_executable = {}) : worker_executable_(std::move(worker_executable)) {}
  struct Worker_statistics {
    uint64_t calls = 0, stopped = 0, failures = 0;
  };
  const Worker_statistics& worker_statistics() const { return workers_; }
  ~Abc_tmap() override;
  Abc_tmap(const Abc_tmap&)                  = delete;
  Abc_tmap&       operator=(const Abc_tmap&) = delete;
  Mapped_fragment map(const Mapping_request& request) override;
  // In process (not killable by a worker), guarded by the request admission.
  Mapped_block    map_block(const Block_request& request) override;
  // ABC `[&dch;] &if -K k -a` on the strashed logic (insight only; never the netlist).
  std::vector<std::pair<uint64_t, uint32_t>> lut_reference(const Logic_network& logic, uint32_t k, bool choices) override;

private:
  std::string       worker_executable_;
  Worker_statistics workers_;
  void*             frame_ = nullptr;
  std::string       library_;
};
// Internal lhd self-exec entry; request/response files use bounded binary records.
int abc_tmap_worker_main(const char* request_file, const char* response_file);
}  // namespace livehd::synth
