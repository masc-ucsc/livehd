// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "tmap.hpp"
namespace livehd::synth {
std::string read_worker_record(const char* path);
void        write_worker_record(const char* path, std::string_view data);
struct Worker_result {
  Mapped_fragment fragment;
  bool            stopped    = false;
  bool            failed     = false;
  // Historical identity only: the transport has reaped this child on return.
  uint64_t        worker_pid = 0;
};
// Shared isolated transport. Only the three admission/deadline callbacks in
// limits are used. The payload and complete response are bounded to 64 MiB.
struct Worker_exchange {
  std::string data, reason;
  bool        stopped = false, failed = false;
  uint64_t    worker_pid = 0;
};
Worker_exchange exchange_worker(const std::string& executable, const char* mode, std::string_view payload,
                                const Mapping_request& limits);
Worker_result   map_in_worker(const std::string& executable, const Mapping_request& request);
}  // namespace livehd::synth
