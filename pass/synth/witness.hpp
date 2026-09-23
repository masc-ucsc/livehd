// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <fstream>

#include "unate.hpp"

namespace livehd::synth {
struct Witness_record {
  int64_t     record = -1;
  std::string status = "not_run";
};

// Ordered endpoints of the Boolean cut, without asserting RTL clock/reset
// semantics. State refers to an entry in states; -1 means a primary/opaque port.
struct Source_boundary {
  struct Port {
    std::string name, kind;
    int64_t     state = -1;
  };
  struct State {
    std::string name, init;
    uint32_t    input = 0, output = 0;  // CO and CI indices, respectively
  };
  std::vector<Port>  inputs, outputs;
  std::vector<State> states;
};

// Self-contained JSONL witnesses for regions searched or reused this invocation.
// Write into private staging and publish with the corresponding report. A size
// refusal is explicit and never writes a partial JSON record.
class Witness_archive {
public:
  Witness_archive(const std::filesystem::path& path, std::string producer_version, uint64_t max_bytes,
                  const Search_options& search = {});
  Witness_record append(std::string_view region, size_t recipe_index, std::string_view variant, const Logic_network& source,
                        const Unate_network& network, const Recipe& recipe);
  Witness_record append_source(std::string_view region, const Logic_network& source, const Source_boundary* boundary = nullptr);
  // Capture only records appended since a byte checkpoint. Cache attachments
  // retain original record ids; replay assigns fresh ids in the current archive.
  std::string    capture_since(uint64_t begin) const;
  Witness_record replay(std::string_view record, std::string_view region);
  void           close();
  Witness_record skip(std::string status) {
    ++omitted_;
    return {-1, std::move(status)};
  }
  uint64_t records() const { return records_; }
  uint64_t omitted() const { return omitted_; }
  uint64_t bytes() const { return bytes_; }

private:
  std::filesystem::path path_;
  std::ofstream         output_;
  std::string           version_;
  Search_options        search_;
  uint64_t              max_bytes_, bytes_ = 0, records_ = 0, omitted_ = 0;
};

std::string pack_evidence(std::string_view decision, std::string_view witnesses);
bool        valid_evidence(std::string_view evidence, std::string_view expected_region = {});
// Returns a report wrapper explicitly labeling all decision metrics historical.
std::string replay_evidence(Witness_archive& archive, std::string_view region, std::string_view cached_region,
                            std::string_view evidence);
}  // namespace livehd::synth
