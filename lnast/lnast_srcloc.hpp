//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Source-provenance plumbing shared by every artifact that carries
// hhds::attrs::srcid ([[1f]]): Lnast trees, the upass staging rebuilds, and
// the LNAST→LGraph lowering. The locator owns the file/span/line payload; IR
// nodes carry one uint64 SourceId, resolved here only at diagnostic-emission
// time — losing a locator degrades diags, never semantics.

#include <span>
#include <string>
#include <vector>

#include "diag.hpp"
#include "hhds/source_locator.hpp"

namespace livehd::srcloc {

// Re-mint `id` (an entry of `src`) into `dst`, so a node copied across
// artifacts keeps a resolvable provenance id. Anchors re-mint to the SAME id
// in practice (ids hash the payload); a combine flattens to its concrete
// anchors (first-parent-first) and re-combines, which preserves the primary
// anchor and the note ordering even when the nesting shape differs.
// Returns SourceId_invalid when `id` does not resolve in `src`.
inline hhds::SourceId import_srcid(hhds::Source_locator& dst, const hhds::Source_locator& src, hhds::SourceId id) {
  if (id == hhds::SourceId_invalid || &dst == &src) {
    return id;
  }
  if (dst.has(id)) {
    return id;  // already present (shared base, or a previous import)
  }
  const auto rs = src.resolve_all(id);
  if (rs.anchors.empty()) {
    return hhds::SourceId_invalid;
  }
  std::vector<hhds::SourceId> ids;
  ids.reserve(rs.anchors.size());
  for (const auto& a : rs.anchors) {
    // Carry the file's bytes (a shared-pointer copy, which also derives the
    // content hash + line offsets) — or, for files ingested without content,
    // just the line-offset table — so resolved spans keep full line:col and
    // sourcesContent fidelity in the destination locator too.
    if (dst.file_content(a.path) == nullptr) {
      if (auto content = src.file_content(a.path); content != nullptr) {
        dst.set_file_content(a.path, std::move(content));
      } else if (dst.file_line_offsets(a.path) == nullptr) {
        if (const auto* offs = src.file_line_offsets(a.path); offs != nullptr) {
          dst.set_file_line_offsets(a.path, *offs);
        }
      }
    }
    if (a.kind == hhds::Source_locator::Anchor_kind::Line_only) {
      ids.push_back(dst.mint_line(a.path, a.line));
    } else {
      ids.push_back(dst.mint(a.path, a.start_byte, a.end_byte, a.line));
    }
  }
  if (ids.size() == 1) {
    return ids.front();
  }
  return dst.combine(std::span<const hhds::SourceId>(ids.data(), ids.size()));
}

// Resolve one anchor into a diag Span: file + byte span + line, and — when the
// locator has the file's line-offset table — full 1-based line:col intervals
// (end is exclusive, matching tree-sitter / LSP conventions).
inline livehd::diag::Span span_of_anchor(const hhds::Source_locator& sl, const hhds::Source_locator::Anchor& a,
                                         hhds::SourceId id) {
  livehd::diag::Span span;
  span.source_id = id;
  span.file      = std::string(a.path);
  if (a.line != 0) {
    span.start_line = a.line;
    span.end_line   = a.line;
  }
  if (a.kind != hhds::Source_locator::Anchor_kind::Line_only) {
    span.start_byte = a.start_byte;
    span.end_byte   = a.end_byte;
    if (const auto lc = sl.to_line_col(a.path, a.start_byte)) {
      span.start_line = lc->line;
      span.start_col  = lc->col;
    }
    if (const auto lc = sl.to_line_col(a.path, a.end_byte)) {
      span.end_line = lc->line;
      span.end_col  = lc->col;
    }
  }
  return span;
}

// Primary-anchor Span for `id` (null Span when unresolvable). A combined id
// resolves to its first parent — the diag's display anchor.
inline livehd::diag::Span span_of(const hhds::Source_locator& sl, hhds::SourceId id) {
  if (id == hhds::SourceId_invalid) {
    return {};
  }
  const auto a = sl.resolve(id);
  if (!a) {
    return {};
  }
  return span_of_anchor(sl, *a, id);
}

// Secondary anchors of a combined id as Diagnostic::notes (empty for a plain
// anchor). The primary anchor is anchors[0] = the Span; the rest are the
// related sites (inline call chain, merged writes, ...).
inline std::vector<livehd::diag::Note> notes_of(const hhds::Source_locator& sl, hhds::SourceId id,
                                                std::string_view message = "related source") {
  std::vector<livehd::diag::Note> out;
  if (id == hhds::SourceId_invalid) {
    return out;
  }
  const auto rs = sl.resolve_all(id);
  for (size_t i = 1; i < rs.anchors.size(); ++i) {
    out.push_back(livehd::diag::Note{std::string(message), span_of_anchor(sl, rs.anchors[i], id)});
  }
  return out;
}

}  // namespace livehd::srcloc
