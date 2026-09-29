//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Shared slang SourceRange -> LiveHD provenance conversion (todo/ 1s
// subtasks C & D). One place extracts (workspace-relative path, byte span,
// line:col) from a slang SourceManager; the diagnostic client (C) wraps it into
// a diag::Span and the LNAST importer (D) mints a hhds::SourceId from the same
// info, so future provenance changes touch this one function.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "diag.hpp"
#include "hhds/source_locator.hpp"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "source_path.hpp"

namespace livehd::slang_loc {

struct Loc_info {
  std::string path;  // workspace-relative; empty when unresolvable
  uint64_t    start_byte = 0;
  uint64_t    end_byte   = 0;
  uint32_t    start_line = 0;
  uint32_t    start_col  = 0;
  uint32_t    end_line   = 0;
  uint32_t    end_col    = 0;
  bool        valid      = false;
};

// Resolve a slang SourceRange (macro expansions folded back to the original
// file) into a workspace-relative path + byte/line/col. The single extraction
// shared by the diagnostic client and the LNAST importer.
inline Loc_info extract(const slang::SourceManager& sm, slang::SourceRange range) {
  Loc_info   out;
  const auto start = sm.getFullyOriginalLoc(range.start());
  const auto end   = sm.getFullyOriginalLoc(range.end());
  if (!start.valid()) {
    return out;
  }
  const auto fname = sm.getFileName(start);
  if (fname.empty()) {
    return out;
  }
  out.path       = livehd::srcloc::workspace_relative(fname);
  out.start_byte = start.offset();
  out.end_byte   = end.valid() ? end.offset() : start.offset();
  out.start_line = static_cast<uint32_t>(sm.getLineNumber(start));
  out.start_col  = static_cast<uint32_t>(sm.getColumnNumber(start));
  out.end_line   = end.valid() ? static_cast<uint32_t>(sm.getLineNumber(end)) : out.start_line;
  out.end_col    = end.valid() ? static_cast<uint32_t>(sm.getColumnNumber(end)) : out.start_col;
  out.valid      = !out.path.empty();
  return out;
}

inline livehd::diag::Span to_span(const Loc_info& li) {
  livehd::diag::Span span;
  if (!li.valid) {
    return span;
  }
  span.file       = li.path;
  span.start_byte = li.start_byte;
  span.end_byte   = li.end_byte;
  span.start_line = li.start_line;
  span.start_col  = li.start_col;
  span.end_line   = li.end_line;
  span.end_col    = li.end_col;
  return span;
}

// Convenience: a diag::Span directly from a SourceRange (subtask C).
inline livehd::diag::Span span_of(const slang::SourceManager& sm, slang::SourceRange range) { return to_span(extract(sm, range)); }

inline livehd::diag::Span span_of(const slang::SourceManager& sm, slang::SourceLocation loc) {
  return span_of(sm, slang::SourceRange(loc, loc));
}

// Mint a hhds::SourceId for `range` into `loc` (subtask D), registering the
// file's content the first time the path is seen so the ln: save/load round
// trip keeps full line:col and a reload-time diagnostic still points at the
// original .v span. Returns SourceId_invalid when the range cannot be resolved.
// One file already ingested in THIS compilation: the bytes plus everything
// derived from them, so a later locator adopts all three by pointer.
struct Ingested {
  std::shared_ptr<const std::string>           content;
  uint64_t                                     hash = 0;
  std::shared_ptr<const std::vector<uint64_t>> offsets;
};
using Ingest_cache = absl::flat_hash_map<std::string, Ingested>;

// `cache` (optional, but ALWAYS pass it from a multi-module read) is what keeps
// this linear. `getSourceText(buffer)` returns the WHOLE slang buffer, not the
// range, and the `file_content(path) == nullptr` guard below only dedups within
// ONE locator -- while the reader builds one Lnast, and so one locator, PER
// MODULE (slang_structure.cpp, `make_shared<Lnast>` in lower_module). Without
// the cache every module re-copied, re-hashed and re-line-scanned the entire
// source: O(modules x file_bytes). Measured on the lhdsuite v2v_xs_backend
// gate, which hands `lhd lec` its 1040 emitted modules CONCATENATED into one
// 190.5 MB file -- 1040 x 190.5 MB is ~198 GB, and the read died of
// std::bad_alloc on a 64 GB machine after 88 s. A/B on 20 vs 40 of those
// modules showed the gap growing 4.2x for a 2x module count, confirming it is
// the module count and not the design size.
inline hhds::SourceId mint(hhds::Source_locator& loc, const slang::SourceManager& sm, slang::SourceRange range,
                           Ingest_cache* cache = nullptr) {
  const auto li = extract(sm, range);
  if (!li.valid) {
    return hhds::SourceId_invalid;
  }
  if (loc.file_content(li.path) == nullptr) {
    if (cache != nullptr) {
      if (const auto it = cache->find(li.path); it != cache->end()) {
        loc.adopt_file_content(li.path, it->second.content, it->second.hash, it->second.offsets);
        return loc.mint(li.path, li.start_byte, li.end_byte, li.start_line);
      }
    }
    const auto start = sm.getFullyOriginalLoc(range.start());
    const auto text  = sm.getSourceText(start.buffer());
    if (!text.empty()) {
      loc.set_file_content(li.path, std::string(text));  // derives hash + line table
      if (cache != nullptr) {
        cache->emplace(li.path,
                       Ingested{loc.file_content(li.path), loc.file_content_hash(li.path),
                                loc.file_line_offsets_shared(li.path)});
      }
    }
  }
  return loc.mint(li.path, li.start_byte, li.end_byte, li.start_line);
}

}  // namespace livehd::slang_loc
