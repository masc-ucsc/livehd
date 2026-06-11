//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// ECMA-426 (source map v3) egress projection ([[1f]]-G). A textual egress
// artifact (cgen Verilog, generated Pyrope) records one Segment per emitted
// statement; to_json renders the standard {version,file,sources,mappings}
// object so stock JS/TS-ecosystem tooling can jump generated → original.
//
// Lossy by design: one display anchor per segment (a combined SourceId
// exports its primary parent). LiveHD-only data — the full SourceId per
// segment — rides under "x_livehd" so LiveHD tools can rejoin the locator.
// This is an output projection only; the locator's own serialized form
// (srcmap.txt, hhds-srcloc) stays the canonical store.
//
// Leaf header: std only — no hhds dependency (the caller resolves SourceIds
// through its locator and hands over plain positions).

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::sourcemap {

// All positions 0-based, per the source-map v3 spec (renderers display 1-based).
struct Segment {
  uint32_t gen_line = 0;
  uint32_t gen_col  = 0;
  uint32_t src_idx  = 0;  // index into the sources list
  uint32_t src_line = 0;
  uint32_t src_col  = 0;
  uint64_t source_id = 0;  // LiveHD SourceId (x_livehd extra; 0 = none)
};

namespace detail {

inline void append_vlq(std::string& out, int64_t value) {
  static constexpr char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint64_t              v    = value < 0 ? (static_cast<uint64_t>(-value) << 1) | 1 : static_cast<uint64_t>(value) << 1;
  do {
    uint32_t digit = v & 0x1f;
    v >>= 5;
    if (v != 0) {
      digit |= 0x20;  // continuation
    }
    out.push_back(b64[digit]);
  } while (v != 0);
}

inline void append_json_string(std::string& out, std::string_view s) {
  static constexpr char hex[] = "0123456789abcdef";
  out.push_back('"');
  for (char c : s) {
    switch (c) {
      case '"' : out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {  // JSON forbids raw control chars
          out += "\\u00";
          out.push_back(hex[(c >> 4) & 0xf]);
          out.push_back(hex[c & 0xf]);
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  out.push_back('"');
}

}  // namespace detail

// Render the version-3 source map. `segments` may arrive unsorted; they are
// ordered by (gen_line, gen_col) as the format requires. `sources_content` is
// aligned with `sources` (nullptr = JSON null); browser-style viewers can only
// display originals embedded there, so callers should fill what they have.
inline std::string to_json(std::string_view generated_file, const std::vector<std::string>& sources,
                           const std::vector<std::shared_ptr<const std::string>>& sources_content,
                           std::vector<Segment> segments) {
  std::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
    return a.gen_line != b.gen_line ? a.gen_line < b.gen_line : a.gen_col < b.gen_col;
  });

  std::string mappings;
  uint32_t    line     = 0;
  int64_t     prev_col = 0, prev_src = 0, prev_sline = 0, prev_scol = 0;
  bool        first_in_line = true;
  for (const Segment& s : segments) {
    while (line < s.gen_line) {
      mappings.push_back(';');
      ++line;
      prev_col      = 0;  // generated column resets per line; the rest carry over
      first_in_line = true;
    }
    if (!first_in_line) {
      mappings.push_back(',');
    }
    detail::append_vlq(mappings, static_cast<int64_t>(s.gen_col) - prev_col);
    detail::append_vlq(mappings, static_cast<int64_t>(s.src_idx) - prev_src);
    detail::append_vlq(mappings, static_cast<int64_t>(s.src_line) - prev_sline);
    detail::append_vlq(mappings, static_cast<int64_t>(s.src_col) - prev_scol);
    prev_col      = s.gen_col;
    prev_src      = s.src_idx;
    prev_sline    = s.src_line;
    prev_scol     = s.src_col;
    first_in_line = false;
  }

  std::string out;
  out += "{\"version\":3,\"file\":";
  detail::append_json_string(out, generated_file);
  out += ",\"sources\":[";
  for (size_t i = 0; i < sources.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    detail::append_json_string(out, sources[i]);
  }
  out += "]";
  if (std::any_of(sources_content.begin(), sources_content.end(), [](const auto& c) { return c != nullptr; })) {
    out += ",\"sourcesContent\":[";
    for (size_t i = 0; i < sources.size(); ++i) {
      if (i != 0) {
        out.push_back(',');
      }
      if (i < sources_content.size() && sources_content[i] != nullptr) {
        detail::append_json_string(out, *sources_content[i]);
      } else {
        out += "null";
      }
    }
    out += "]";
  }
  out += ",\"names\":[],\"mappings\":";
  detail::append_json_string(out, mappings);
  // LiveHD extra: the full SourceId per segment (same sorted order as the
  // mappings), so LiveHD tooling can rejoin the locator's combine parents.
  out += ",\"x_livehd\":{\"source_ids\":[";
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += std::to_string(segments[i].source_id);
  }
  out += "]}}";
  out.push_back('\n');
  return out;
}

}  // namespace livehd::sourcemap
