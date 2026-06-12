//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Shared slang SourceRange -> LiveHD provenance conversion (todo/ 1s
// subtasks C & D). One place extracts (workspace-relative path, byte span,
// line:col) from a slang SourceManager; the diagnostic client (C) wraps it into
// a diag::Span and the LNAST importer (D) mints a hhds::SourceId from the same
// info, so future provenance changes touch this one function.

#include <cstdint>
#include <string>

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
inline hhds::SourceId mint(hhds::Source_locator& loc, const slang::SourceManager& sm, slang::SourceRange range) {
  const auto li = extract(sm, range);
  if (!li.valid) {
    return hhds::SourceId_invalid;
  }
  if (loc.file_content(li.path) == nullptr) {
    const auto start = sm.getFullyOriginalLoc(range.start());
    const auto text  = sm.getSourceText(start.buffer());
    if (!text.empty()) {
      loc.set_file_content(li.path, std::string(text));
    }
  }
  return loc.mint(li.path, li.start_byte, li.end_byte, li.start_line);
}

}  // namespace livehd::slang_loc
