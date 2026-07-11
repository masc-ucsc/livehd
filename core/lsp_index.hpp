//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Process-global semantic index for the Pyrope LSP (task 2n Phase B).
//
// The upass runner records one Entry per user-variable definition site DURING
// its single traversal (while the symbol table + live bw_meta are in scope),
// joining the source-level name to its resolved type/range. The LSP layer reads
// the entries after the front-end returns to answer textDocument/hover.
//
// This lives in core/ (not lsp/) so the runner — deep in upass/ — can write to
// it through the dependency edge it already has on //core, with NO cycle back
// to lsp/. It mirrors the diag::sink() singleton: a single static instance,
// cleared per analysis run. The flag defaults OFF, so a normal CLI compile
// (which never calls set_enabled) pays only one bool check per recording site
// and stores nothing. Not thread-safe — the LSP analyses one buffer at a time
// (the workspace indexer is a separate process, §12), exactly like diag::sink().
namespace livehd::lsp_index {

// One recorded definition site. Coordinates are 1-based line/col with byte
// columns (the diag::Span convention); the LSP converts to 0-based at the wire.
struct Entry {
  uint32_t    start_line{0};
  uint32_t    start_col{0};
  uint32_t    end_line{0};
  uint32_t    end_col{0};
  std::string file;    // workspace-relative source path (Span.file); the run
                       // walks the buffer AND its imported siblings, so every
                       // entry must say which file its span lives in
  std::string name;    // source-level lexical name (SSA suffix stripped)
  std::string render;  // hover text, e.g. "a : u8(bw_min=0, bw_max=15)"
};

class Index {
public:
  void                      clear() { entries_.clear(); }
  void                      set_enabled(bool e) { enabled_ = e; }
  bool                      enabled() const { return enabled_; }
  void                      record(Entry e) {
    if (enabled_) {
      entries_.push_back(std::move(e));
    }
  }
  const std::vector<Entry>& entries() const { return entries_; }

private:
  bool               enabled_{false};
  std::vector<Entry> entries_;
};

// The single process-global index (mirrors livehd::diag::sink()).
Index& index();

}  // namespace livehd::lsp_index
