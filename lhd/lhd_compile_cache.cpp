//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lhd_compile_cache.hpp"

#include <unistd.h>

#if defined(__APPLE__)
#include <sys/clonefile.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "compile_salt.hpp"
#include "diag.hpp"
#include "eprp.hpp"
#include "graph_library_singleton.hpp"
#include "lhd_kernel_internal.hpp"
#include "lhd_prp_import.hpp"
#include "lnast.hpp"
#include "node_util.hpp"
#include "prp2lnast.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "semdiff.hpp"
#include "woothash.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

namespace lhd {

namespace fs = std::filesystem;

namespace {

struct Source_unit {
  std::string              name;
  std::string              path;      // user-tree path used in SourceIds
  std::string              abs_path;  // read-only resolver identity
  std::string              bytes;     // the hermetic per-run snapshot
  std::vector<std::string> imports;
  uint64_t                 semantic_hash{0};
  bool                     exact_snapshot_match{false};
  bool                     snapshot_unavailable{false};
  bool                     exact_prior_match{false};
  std::string              snapshot_file;
  std::shared_ptr<Lnast>   lnast;
};

struct Prior_unit {
  uint64_t                 semantic_hash{0};
  std::string              snapshot_file;
  std::string              path;
  std::vector<std::string> imports;
};

struct Prior_cache {
  bool                                          compatible{false};
  std::map<std::string, Prior_unit>             units;
  std::map<std::string, std::shared_ptr<Lnast>> lnasts;
  std::vector<std::string>                      order;
};

struct Graph_row {
  std::string name;
  std::string owner;
  std::string unit_key;
  uint64_t    interface_hash{0};
  bool        has_body{false};
  uint64_t    h0{0};
  uint64_t    h1{0};
};

struct Artifact_file {
  std::string path;
  uint64_t    size{0};
  int64_t     mtime{0};
};

// A diagnostic the GRAPH PIPELINE produced, carried with the generation so a
// warm restore can reproduce it. A total restore skips upass, tolg, cprop and
// pass.formal outright, so without replay a warm compile prints strictly fewer
// records than the cold compile of the same sources — measured on minion: 44
// cold against 24 warm. `pass.formal` warnings used to be handled by refusing
// to store the generation at all, which cost such a design every byte of graph
// reuse forever.
//
// The carried type is `livehd::diag::Diagnostic` itself, never a hand-picked
// subset: replay is a plain `sink().emit()`, so a field added to the record
// (the structured verdict/engine/attrs payload was already there) can never be
// silently dropped on the warm path. Every non-error severity rides — an `info`
// progress record from a pipeline pass (pass.bitfuzz) is skipped by a warm
// restore exactly like a warning, and errors cannot reach here at all (the
// store bails on has_errors()).

// `String(data, size)`, never `String(c_str())`: an embedded NUL would silently
// truncate the value, and every call would pay a strlen.
template <typename W>
void write_json_string(W& w, const char* key, std::string_view v) {
  w.Key(key);
  w.String(v.data(), static_cast<rapidjson::SizeType>(v.size()));
}

// The span is carried VERBATIM from the generation that produced it. After a
// comment-only edit the byte offsets of the edited file have shifted, so a
// replayed location can be stale by exactly that insertion — the srcmap
// exemption the warm==cold rule already grants (H5). Re-resolving it would need
// the post-parse trees the restore exists to avoid building.
//
// Field-for-field the same shape core/diag.cpp `append_span` writes into the
// JSONL stream, `file_id` included: a span that carries only a producer-local
// file id is NOT null, so dropping the member would flip is_null() and lose the
// location outright on the way back.
template <typename W>
void write_span(W& w, const livehd::diag::Span& span) {
  if (span.is_null()) {
    w.Null();
    return;
  }
  w.StartObject();
  const auto u64 = [&](const char* k, const std::optional<uint64_t>& v) {
    if (v) {
      w.Key(k);
      w.Uint64(*v);
    }
  };
  const auto u32 = [&](const char* k, const std::optional<uint32_t>& v) {
    if (v) {
      w.Key(k);
      w.Uint(*v);
    }
  };
  u64("source_id", span.source_id);
  u32("file_id", span.file_id);
  if (!span.file.empty()) {
    write_json_string(w, "file", span.file);
  }
  u64("start_byte", span.start_byte);
  u64("end_byte", span.end_byte);
  u32("start_line", span.start_line);
  u32("start_col", span.start_col);
  u32("end_line", span.end_line);
  u32("end_col", span.end_col);
  w.EndObject();
}

std::optional<livehd::diag::Span> read_span(const rapidjson::Value& v) {
  livehd::diag::Span span;
  if (v.IsNull()) {
    return span;
  }
  if (!v.IsObject()) {
    return std::nullopt;
  }
  if (v.HasMember("file") && v["file"].IsString()) {
    span.file = v["file"].GetString();
  }
  const auto u64 = [&](const char* k, std::optional<uint64_t>& dst) {
    if (v.HasMember(k) && v[k].IsUint64()) {
      dst = v[k].GetUint64();
    }
  };
  const auto u32 = [&](const char* k, std::optional<uint32_t>& dst) {
    if (v.HasMember(k) && v[k].IsUint()) {
      dst = v[k].GetUint();
    }
  };
  u64("source_id", span.source_id);
  u32("file_id", span.file_id);
  u64("start_byte", span.start_byte);
  u64("end_byte", span.end_byte);
  u32("start_line", span.start_line);
  u32("start_col", span.start_col);
  u32("end_line", span.end_line);
  u32("end_col", span.end_col);
  return span;
}

// Severity travels by NAME, not by the enum's underlying value: the code salt
// covers the front end and lowering, not core/diag.hpp, so a reordered enum
// would otherwise reinterpret every stored record instead of refusing it.
std::optional<livehd::diag::Severity> severity_from(std::string_view name) {
  using livehd::diag::Severity;
  for (const auto sev : {Severity::error, Severity::warning, Severity::note, Severity::info}) {
    if (livehd::diag::to_string(sev) == name) {
      return sev;
    }
  }
  return std::nullopt;
}

template <typename W>
void write_diag(W& w, const livehd::diag::Diagnostic& d) {
  w.StartObject();
  write_json_string(w, "severity", livehd::diag::to_string(d.severity));
  write_json_string(w, "pass", d.pass);
  write_json_string(w, "code", d.code);
  write_json_string(w, "category", d.category);
  write_json_string(w, "message", d.message);
  write_json_string(w, "hint", d.hint);
  w.Key("span");
  write_span(w, d.span);
  w.Key("see");
  w.StartArray();
  for (const auto& text : d.see) {
    w.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
  }
  w.EndArray();
  w.Key("notes");
  w.StartArray();
  for (const auto& note : d.notes) {
    w.StartObject();
    write_json_string(w, "message", note.message);
    w.Key("span");
    write_span(w, note.span);
    w.EndObject();
  }
  w.EndArray();
  w.Key("deferred");
  w.Bool(d.deferred);
  write_json_string(w, "verdict", d.verdict);
  write_json_string(w, "engine", d.engine);
  w.Key("duration_ms");
  w.Int64(d.duration_ms);
  w.Key("attrs");
  w.StartArray();
  for (const auto& [key, value] : d.attrs) {
    w.StartObject();
    write_json_string(w, "key", key);
    write_json_string(w, "value", value);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
}

std::optional<livehd::diag::Diagnostic> read_diag(const rapidjson::Value& v) {
  const auto str = [&](const char* k) { return v.HasMember(k) && v[k].IsString(); };
  if (!v.IsObject() || !str("severity") || !str("pass") || !str("code") || !str("category") || !str("message") || !str("hint")
      || !v.HasMember("span") || !v.HasMember("see") || !v["see"].IsArray() || !v.HasMember("notes") || !v["notes"].IsArray()
      || !v.HasMember("deferred") || !v["deferred"].IsBool() || !str("verdict") || !str("engine") || !v.HasMember("duration_ms")
      || !v["duration_ms"].IsInt64() || !v.HasMember("attrs") || !v["attrs"].IsArray()) {
    return std::nullopt;
  }
  livehd::diag::Diagnostic d;
  const auto               sev = severity_from(v["severity"].GetString());
  if (!sev) {
    return std::nullopt;
  }
  d.severity = *sev;
  d.pass     = v["pass"].GetString();
  d.code     = v["code"].GetString();
  d.category = v["category"].GetString();
  // Sink::emit debug-asserts the pinned category vocabulary, so a damaged
  // generation must be REFUSED here rather than abort the process on replay.
  if (!livehd::diag::is_known_category(d.category)) {
    return std::nullopt;
  }
  d.message = v["message"].GetString();
  d.hint    = v["hint"].GetString();
  auto span = read_span(v["span"]);
  if (!span) {
    return std::nullopt;
  }
  d.span = std::move(*span);
  for (const auto& see_ref : v["see"].GetArray()) {
    if (!see_ref.IsString()) {
      return std::nullopt;
    }
    d.see.emplace_back(see_ref.GetString());
  }
  for (const auto& note : v["notes"].GetArray()) {
    if (!note.IsObject() || !note.HasMember("message") || !note["message"].IsString() || !note.HasMember("span")) {
      return std::nullopt;
    }
    auto note_span = read_span(note["span"]);
    if (!note_span) {
      return std::nullopt;
    }
    d.notes.push_back(livehd::diag::Note{note["message"].GetString(), std::move(*note_span)});
  }
  d.deferred    = v["deferred"].GetBool();
  d.verdict     = v["verdict"].GetString();
  d.engine      = v["engine"].GetString();
  d.duration_ms = v["duration_ms"].GetInt64();
  for (const auto& attr : v["attrs"].GetArray()) {
    if (!attr.IsObject() || !attr.HasMember("key") || !attr["key"].IsString() || !attr.HasMember("value")
        || !attr["value"].IsString()) {
      return std::nullopt;
    }
    d.attrs.emplace_back(attr["key"].GetString(), attr["value"].GetString());
  }
  return d;
}

struct Graph_inventory {
  std::string                           closure_key;
  std::vector<std::string>              lnast_order;
  std::vector<Graph_row>                rows;
  std::vector<Artifact_file>            artifact_files;
  std::vector<livehd::diag::Diagnostic> pipeline_diags;
};

// Re-emit a generation's stored pipeline records. Only legal when the restore
// is TOTAL: the pipeline then ran over no graph in this process, so the
// replayed set is exactly what a cold run would have printed.
void replay_pipeline_diags(const Graph_inventory& inventory) {
  for (const auto& d : inventory.pipeline_diags) {
    livehd::diag::sink().emit(d);
  }
}

std::string            lnast_unit_dir(std::string_view snapshot_file);
std::shared_ptr<Lnast> load_compact_lnast(const std::string& dir, std::string_view expected_name);

uint64_t hash_bytes(std::string_view text) { return lh::woothash64(text.data(), text.size(), 1021); }

std::string slurp(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    throw Lhd_error{"missing_file",
                    std::format("could not read Pyrope source {}", path),
                    "check the import path and file permissions"};
  }
  ifs.seekg(0, std::ios::end);
  const auto end = ifs.tellg();
  if (end < 0) {
    throw Lhd_error{"missing_file", std::format("failed while reading Pyrope source {}", path), ""};
  }
  std::string text(static_cast<size_t>(end), '\0');
  ifs.seekg(0, std::ios::beg);
  if (!text.empty()) {
    ifs.read(text.data(), static_cast<std::streamsize>(text.size()));
  }
  if (!ifs) {
    throw Lhd_error{"missing_file", std::format("failed while reading Pyrope source {}", path), ""};
  }
  return text;
}

std::optional<std::string> try_slurp(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return std::nullopt;
  }
  ifs.seekg(0, std::ios::end);
  const auto end = ifs.tellg();
  if (end < 0) {
    return std::nullopt;
  }
  std::string text(static_cast<size_t>(end), '\0');
  ifs.seekg(0, std::ios::beg);
  if (!text.empty()) {
    ifs.read(text.data(), static_cast<std::streamsize>(text.size()));
  }
  if (!ifs) {
    return std::nullopt;
  }
  return text;
}

enum class File_compare { equal, different, unavailable };

File_compare compare_file_bytes(const std::string& path, std::string_view bytes) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) {
    return File_compare::unavailable;
  }
  ifs.seekg(0, std::ios::end);
  const auto end = ifs.tellg();
  if (end < 0) {
    return File_compare::unavailable;
  }
  if (static_cast<uint64_t>(end) != bytes.size()) {
    return File_compare::different;
  }
  ifs.seekg(0, std::ios::beg);
  std::array<char, 64 * 1024> chunk;
  size_t                      offset = 0;
  while (offset < bytes.size()) {
    const auto count = std::min(chunk.size(), bytes.size() - offset);
    ifs.read(chunk.data(), static_cast<std::streamsize>(count));
    if (!ifs) {
      return File_compare::unavailable;
    }
    if (!std::equal(chunk.data(), chunk.data() + count, bytes.data() + offset)) {
      return File_compare::different;
    }
    offset += count;
  }
  return File_compare::equal;
}

std::string normalized_user_path(std::string_view p) { return fs::path(p).lexically_normal().string(); }

std::vector<Source_unit> capture_closure(const std::vector<std::string>& seeds, const Eprp_var& var, const Prior_cache* prior,
                                         const std::string& scope) {
  std::map<std::string, std::string> pending;  // logical unit -> path
  std::set<std::string>              loaded;
  std::set<std::string>              parsed_paths;
  std::map<std::string, std::string> logical_paths;
  import_detail::Resolver            resolver;

  for (const auto& ln : var.lnasts) {
    loaded.emplace(ln->get_top_module_name());
  }
  const auto preloaded = loaded;
  for (const auto& path : seeds) {
    const auto unit = import_detail::unit_name_of(path);
    const auto abs  = import_detail::abspath_of(path);
    if (const auto it = logical_paths.find(unit); it != logical_paths.end() && it->second != abs) {
      throw Lhd_error{"config",
                      std::format("Pyrope source unit '{}' names both {} and {}", unit, it->second, abs),
                      "rename one source file"};
    }
    logical_paths[unit] = abs;
    pending[unit]       = normalized_user_path(path);
  }

  std::vector<Source_unit> out;
  while (!pending.empty()) {
    auto node = pending.extract(pending.begin());
    auto name = std::move(node.key());
    auto path = std::move(node.mapped());
    auto abs  = import_detail::abspath_of(path);
    if (!parsed_paths.insert(abs).second) {
      loaded.insert(name);
      continue;
    }

    Source_unit unit;
    unit.name     = name;
    unit.path     = normalized_user_path(path);
    unit.abs_path = abs;
    unit.bytes    = slurp(path);
    if (prior != nullptr) {
      const auto old = prior->units.find(unit.name);
      if (old != prior->units.end() && old->second.path == unit.path) {
        const auto comparison = compare_file_bytes(scope + "/pyrope/" + old->second.snapshot_file, unit.bytes);
        if (comparison == File_compare::equal) {
          unit.exact_snapshot_match = true;
          unit.imports              = old->second.imports;
        } else if (comparison == File_compare::unavailable) {
          unit.snapshot_unavailable = true;
        }
      }
    }
    if (!unit.exact_snapshot_match) {
      try {
        unit.imports = Prp2lnast::scan_imports(unit.path, unit.bytes);
      } catch (...) {
        // The full parser below owns syntax diagnostics. An unterminated token
        // means this compile will fail before any missing import can matter.
        unit.imports.clear();
      }
    }
    loaded.insert(name);

    const std::string importer_dir = import_detail::dir_of(unit.path);
    for (const auto& raw : unit.imports) {
      if (raw.starts_with("lg:") || raw.starts_with("ln:")) {
        continue;
      }
      const auto names     = import_detail::candidates(raw);
      // An already-satisfied import must still run conflict DETECTION: skipping
      // it made "ambiguous stem" depend on unit-name traversal order, while the
      // cache-off discover_imports path errors deterministically. Resolution is
      // cheap (per-directory listings are cached in the resolver).
      const bool satisfied = std::any_of(names.begin(), names.end(), [&](const auto& n) { return loaded.contains(n); });
      for (const auto& candidate : names) {
        const auto resolved = resolver.find(importer_dir, candidate);
        if (resolved.empty()) {
          continue;
        }
        const auto resolved_abs = import_detail::abspath_of(resolved);
        if (const auto it = logical_paths.find(candidate); it != logical_paths.end() && it->second != resolved_abs) {
          throw Lhd_error{"config",
                          std::format("ambiguous import '{}': resolves to both {} and {}", candidate, it->second, resolved_abs),
                          "rename the file or import explicitly"};
        }
        logical_paths[candidate] = resolved_abs;
        if (!satisfied && !parsed_paths.contains(resolved_abs)) {
          pending.try_emplace(candidate, normalized_user_path(resolved));
        }
        break;
      }
    }
    out.push_back(std::move(unit));
  }

  std::sort(out.begin(), out.end(), [](const Source_unit& a, const Source_unit& b) { return a.name < b.name; });
  for (size_t i = 0; i < out.size(); ++i) {
    // The stable sorted ordinal is collision-free by construction. A digest
    // here would turn a harmless fingerprint collision into path aliasing.
    out[i].snapshot_file = std::format("unit_{:08}.prp", i);
  }

  // Match discover_imports exactly: all seed files are published first in
  // command-line order, then each import breadth is published in logical-name
  // order. The source inventory used to remain alphabetized here. That is a
  // valid closure representation, but LNAST order is load-bearing for uPass's
  // registries and CSE representatives; on XS it made an incremental cold
  // compile produce a different NewCSR graph from the cache-disabled oracle.
  // Snapshot ordinals above stay name-sorted so changing traversal order does
  // not create needless cache-file churn.
  std::map<std::string, size_t> by_name;
  for (size_t i = 0; i < out.size(); ++i) {
    by_name.emplace(out[i].name, i);
  }
  // `satisfied` mirrors the capture walk above: a name an ALREADY-LOADED lnast
  // provides needs no import breadth of its own. `placed` is the separate
  // ordering view -- a captured unit must appear exactly once in the result even
  // when a preloaded lnast happens to share its logical name (a seed .prp beside
  // an `ln:` import of the same module). Folding the two sets into one dropped
  // that unit from the returned closure, and with it from the inventory and the
  // closure key, so a later edit of it could not dirty the cache.
  std::set<std::string> satisfied = preloaded;
  std::set<std::string> placed;
  std::vector<size_t>   order;
  order.reserve(out.size());
  const auto place = [&](const std::string& name, size_t index) {
    if (!placed.insert(name).second) {
      return;
    }
    satisfied.insert(name);
    order.push_back(index);
  };
  for (const auto& path : seeds) {
    const auto name = import_detail::unit_name_of(path);
    if (const auto it = by_name.find(name); it != by_name.end()) {
      place(name, it->second);
    }
  }
  size_t next_scan = 0;
  while (next_scan < order.size()) {
    const size_t          scan_end = order.size();
    std::set<std::string> found;
    for (size_t pos = next_scan; pos < scan_end; ++pos) {
      for (const auto& raw : out[order[pos]].imports) {
        if (raw.starts_with("lg:") || raw.starts_with("ln:")) {
          continue;
        }
        const auto names = import_detail::candidates(raw);
        if (std::any_of(names.begin(), names.end(), [&](const auto& name) { return satisfied.contains(name); })) {
          continue;
        }
        for (const auto& name : names) {
          if (by_name.contains(name)) {
            found.insert(name);
            break;
          }
        }
      }
    }
    for (const auto& name : found) {
      place(name, by_name.at(name));
    }
    next_scan = scan_end;
  }
  // Defensive only: every captured source should be reachable from a seed,
  // but retain a deterministic complete inventory if a future resolver shape
  // introduces an alias that the logical-name walk above cannot reconstruct.
  for (const auto& [name, index] : by_name) {
    place(name, index);
  }
  std::vector<Source_unit> ordered;
  ordered.reserve(out.size());
  for (const auto index : order) {
    ordered.push_back(std::move(out[index]));
  }
  return ordered;
}

void digest_node(const Lnast& ln, const Lnast_nid& nid, std::string& bytes) {
  const auto type = static_cast<uint16_t>(ln.get_type(nid));
  bytes.append(reinterpret_cast<const char*>(&type), sizeof(type));
  const auto name = ln.get_name(nid);
  const auto size = static_cast<uint64_t>(name.size());
  bytes.append(reinterpret_cast<const char*>(&size), sizeof(size));
  bytes.append(name);
  bytes.push_back('(');
  for (const auto& child : ln.children(nid)) {
    digest_node(ln, child, bytes);
  }
  bytes.push_back(')');
}

uint64_t semantic_hash(const Lnast& ln) {
  std::string bytes;
  digest_node(ln, ln.get_root(), bytes);
  return hash_bytes(bytes);
}

bool semantic_identical_node(const Lnast& a, const Lnast_nid& an, const Lnast& b, const Lnast_nid& bn) {
  if (a.get_type(an) != b.get_type(bn) || a.get_name(an) != b.get_name(bn)) {
    return false;
  }
  auto ac = a.children(an);
  auto bc = b.children(bn);
  auto ai = ac.begin();
  auto bi = bc.begin();
  while (ai != ac.end() && bi != bc.end()) {
    if (!semantic_identical_node(a, *ai, b, *bi)) {
      return false;
    }
    ++ai;
    ++bi;
  }
  return ai == ac.end() && bi == bc.end();
}

bool semantic_identical(const Lnast& a, const Lnast& b) { return semantic_identical_node(a, a.get_root(), b, b.get_root()); }

std::string closure_key(const std::vector<Source_unit>& units) {
  std::string bytes;
  for (const auto& unit : units) {
    bytes += unit.name;
    bytes.push_back('\0');
    bytes += std::format("{:016x}", unit.semantic_hash);
    bytes.push_back('\0');
    for (const auto& import : unit.imports) {
      bytes += import;
      bytes.push_back('\0');
    }
    bytes.push_back('\xff');
  }
  return std::format("{:016x}", hash_bytes(bytes));
}

std::map<std::string, std::string> unit_merkle_keys(const std::vector<Source_unit>& units) {
  std::map<std::string, const Source_unit*> by_name;
  std::map<std::string, std::string>        keys;
  for (const auto& unit : units) {
    by_name.emplace(unit.name, &unit);
    keys.emplace(unit.name, std::format("{:016x}", unit.semantic_hash));
  }
  // Acyclic imports settle in at most N rounds. Cycles never reach lowering
  // successfully; their unstable keys merely prevent reuse on the failing run.
  for (size_t round = 0; round < units.size(); ++round) {
    auto next = keys;
    for (const auto& unit : units) {
      std::vector<std::string> deps;
      for (const auto& raw : unit.imports) {
        for (const auto& candidate : import_detail::candidates(raw)) {
          if (by_name.contains(candidate)) {
            deps.push_back(keys.at(candidate));
            break;
          }
        }
      }
      std::sort(deps.begin(), deps.end());
      std::string text = std::format("{:016x}", unit.semantic_hash);
      for (const auto& dep : deps) {
        text += '|';
        text += dep;
      }
      next[unit.name] = std::format("{:016x}", hash_bytes(text));
    }
    if (next == keys) {
      break;
    }
    keys = std::move(next);
  }
  return keys;
}

std::vector<std::string> clean_units(const std::vector<Source_unit>& units, const Prior_cache& prior) {
  if (!prior.compatible) {
    return {};
  }
  std::map<std::string, const Source_unit*> by_name;
  std::set<std::string>                     dirty;
  for (const auto& unit : units) {
    by_name.emplace(unit.name, &unit);
    const auto old = prior.units.find(unit.name);
    if (old == prior.units.end() || !unit.exact_prior_match) {
      dirty.emplace(unit.name);
    }
  }
  if (prior.units.size() != units.size()) {
    // A vanished exporter must invalidate every importer that named it. The
    // exact old reverse edges are not retained in memory here, so conservatively
    // rebuild the current closure; ghost pruning still removes the vanished def.
    return {};
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& unit : units) {
      if (dirty.contains(unit.name)) {
        continue;
      }
      for (const auto& raw : unit.imports) {
        bool dep_dirty = false;
        for (const auto& candidate : import_detail::candidates(raw)) {
          if (by_name.contains(candidate)) {
            dep_dirty = dirty.contains(candidate);
            break;
          }
        }
        if (dep_dirty) {
          dirty.emplace(unit.name);
          changed = true;
          break;
        }
      }
    }
  }
  std::vector<std::string> clean;
  for (const auto& unit : units) {
    if (!dirty.contains(unit.name)) {
      clean.push_back(unit.name);
    }
  }
  return clean;
}

// The one dotted-prefix ownership rule: `unit` owns `unit` and `unit.<x>`.
bool name_within_unit(std::string_view name, std::string_view unit) {
  return name == unit || (name.size() > unit.size() + 1 && name.substr(0, unit.size()) == unit && name[unit.size()] == '.');
}

std::string owner_of(std::string_view artifact, const Result& res) {
  std::string owner;
  for (const auto& [unit, _] : res.compile_cache_unit_keys) {
    if (name_within_unit(artifact, unit) && unit.size() > owner.size()) {
      owner = unit;
    }
  }
  return owner;
}

std::string key_of(std::string_view owner, const Result& res) {
  for (const auto& [unit, key] : res.compile_cache_unit_keys) {
    if (unit == owner) {
      return key;
    }
  }
  return {};
}

std::string scope_name(const Options& opts, const std::vector<std::string>& seeds) {
  std::string raw = opts.top.empty() ? import_detail::unit_name_of(seeds.front()) : opts.top;
  std::string out;
  out.reserve(raw.size());
  for (const unsigned char c : raw) {
    out += std::isalnum(c) || c == '.' || c == '_' || c == '-' ? static_cast<char>(c) : '_';
  }
  if (out != raw) {
    out += std::format("_{:08x}", static_cast<uint32_t>(hash_bytes(raw)));
  }
  return out.empty() ? "default" : out;
}

std::string context_descriptor(const Options& opts) {
  std::string text = std::format("top={}|recipe={}", opts.top, opts.recipe.empty() ? "O1" : opts.recipe);
  // Seed identity: scope_name alone is a stem/--top, so two different designs
  // in one workdir would otherwise alias one scope and inherit each other's
  // prior_units — which ghost pruning may then delete from a shared lg: dir.
  for (const auto& f : opts.files) {
    text += std::format("|src:{}", normalized_user_path(f));
  }
  auto sets = opts.sets;
  std::sort(sets.begin(), sets.end());
  for (const auto& [key, value] : sets) {
    if (key == "compile.cache") {
      continue;
    }
    text += std::format("|{}={}", key, value);
  }
  for (const auto& d : opts.in_dirs) {
    text += std::format("|in:{}:{}", d.kind, d.path);
  }
  for (const auto& d : opts.ins) {
    text += std::format("|in:{}:{}", d.kind, d.path);
  }
  return text;
}

Prior_cache load_prior(const std::string& scope, const std::string& context, bool materialize_lnasts = false) {
  Prior_cache prior;
  auto        text = try_slurp(scope + "/inventory.json");
  if (!text) {
    return prior;
  }
  rapidjson::Document doc;
  doc.Parse(text->data(), text->size());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("schema_version") || !doc["schema_version"].IsInt()
      || doc["schema_version"].GetInt() != 3 || !doc.HasMember("code_salt") || !doc["code_salt"].IsUint64()
      || doc["code_salt"].GetUint64() != livehd::kCompileSrcSalt || !doc.HasMember("context") || !doc["context"].IsString()
      || context != doc["context"].GetString() || !doc.HasMember("units") || !doc["units"].IsArray()) {
    return prior;
  }
  for (const auto& u : doc["units"].GetArray()) {
    if (!u.IsObject() || !u.HasMember("name") || !u["name"].IsString() || !u.HasMember("semantic_hash")
        || !u["semantic_hash"].IsUint64() || !u.HasMember("snapshot") || !u["snapshot"].IsString() || !u.HasMember("path")
        || !u["path"].IsString() || !u.HasMember("imports") || !u["imports"].IsArray()) {
      return {};
    }
    Prior_unit unit;
    unit.semantic_hash = u["semantic_hash"].GetUint64();
    unit.snapshot_file = u["snapshot"].GetString();
    unit.path          = u["path"].GetString();
    for (const auto& import : u["imports"].GetArray()) {
      if (!import.IsString()) {
        return {};
      }
      unit.imports.emplace_back(import.GetString());
    }
    const std::string name = u["name"].GetString();
    if (!prior.units.emplace(name, std::move(unit)).second) {
      return {};
    }
    prior.order.push_back(name);
  }
  if (materialize_lnasts) {
    try {
      for (const auto& name : prior.order) {
        const auto& unit = prior.units.at(name);
        auto        ln   = load_compact_lnast(scope + "/ln/" + lnast_unit_dir(unit.snapshot_file), name);
        if (semantic_hash(*ln) != unit.semantic_hash) {
          return {};
        }
        prior.lnasts.emplace(name, std::move(ln));
      }
    } catch (...) {
      return {};
    }
  }
  prior.compatible = true;
  return prior;
}

void write_inventory(const std::string& path, const std::string& context, const std::vector<Source_unit>& units) {
  rapidjson::StringBuffer                    sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  w.StartObject();
  w.Key("schema_version");
  w.Int(3);
  w.Key("code_salt");
  w.Uint64(livehd::kCompileSrcSalt);
  w.Key("context");
  w.String(context.c_str());
  w.Key("units");
  w.StartArray();
  for (const auto& unit : units) {
    w.StartObject();
    w.Key("name");
    w.String(unit.name.c_str());
    w.Key("path");
    w.String(unit.path.c_str());
    w.Key("semantic_hash");
    w.Uint64(unit.semantic_hash);
    w.Key("snapshot");
    w.String(unit.snapshot_file.c_str());
    w.Key("imports");
    w.StartArray();
    for (const auto& import : unit.imports) {
      w.String(import.c_str());
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write compile cache inventory {}", path), "check --workdir permissions"};
  }
  ofs.write(sb.GetString(), static_cast<std::streamsize>(sb.GetSize()));
  ofs.put('\n');
  if (!ofs) {
    throw Lhd_error{"config", std::format("failed writing compile cache inventory {}", path), "check --workdir space"};
  }
}

// Failed or killed stores strand pid-suffixed temp trees (`*.new.<pid>`,
// `*.old.<pid>`) that later runs — carrying new pids — would never collect;
// on Backend-scale designs one stranded lg.new is a full library copy. Sweep
// any whose owning process is provably gone.
void collect_stale_temps(const fs::path& dir) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto name = it->path().filename().string();
    auto       pos  = name.rfind(".new.");
    if (pos == std::string::npos) {
      pos = name.rfind(".old.");
    }
    if (pos == std::string::npos) {
      continue;
    }
    const auto pid_text = name.substr(pos + 5);
    if (pid_text.empty() || !std::all_of(pid_text.begin(), pid_text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
      continue;
    }
    const auto pid = static_cast<pid_t>(std::strtol(pid_text.c_str(), nullptr, 10));
    if (pid == ::getpid()) {
      continue;
    }
    errno = 0;
    if (::kill(pid, 0) == 0 || errno != ESRCH) {
      continue;  // the owning store may still be live — leave its temp alone
    }
    std::error_code rm_ec;
    fs::remove_all(it->path(), rm_ec);
  }
}

void replace_dir(const fs::path& fresh, const fs::path& live) {
  std::error_code ec;
  const auto      backup = fs::path(live.string() + std::format(".old.{}", static_cast<long>(::getpid())));
  fs::remove_all(backup, ec);
  ec.clear();
  if (fs::exists(live, ec)) {
    ec.clear();
    fs::rename(live, backup, ec);
    if (ec) {
      throw Lhd_error{"config", std::format("could not retire compile cache directory {}: {}", live.string(), ec.message()), ""};
    }
  }
  ec.clear();
  fs::rename(fresh, live, ec);
  if (ec) {
    std::error_code restore_ec;
    if (fs::exists(backup, restore_ec)) {
      fs::rename(backup, live, restore_ec);
    }
    throw Lhd_error{"config", std::format("could not publish compile cache directory {}: {}", live.string(), ec.message()), ""};
  }
  fs::remove_all(backup, ec);
}

uint64_t graph_interface_hash(const hhds::GraphIO& io) {
  std::string bytes{io.get_name()};
  bytes.push_back('\0');
  auto add = [&](char kind, const auto& pins) {
    for (const auto& p : pins) {
      bytes += std::format("{}|{}|{}|{}|{}|{}", kind, p.name, p.port_id, p.bits, p.unsign ? 1 : 0, p.loop_break ? 1 : 0);
      bytes.push_back('\0');
    }
  };
  add('i', io.get_input_pin_decls());
  add('o', io.get_output_pin_decls());
  return hash_bytes(bytes);
}

std::vector<Graph_row> graph_rows(hhds::GraphLibrary& lib, const Result& res, bool& digestable) {
  std::vector<Graph_row>                                            rows;
  absl::flat_hash_map<hhds::Gid, livehd::semdiff::Canonical_digest> digest_memo;
  auto                                                              resolver = [&](hhds::Gid gid) -> hhds::Graph* {
    if (!lib.has_graph(gid)) {
      return nullptr;
    }
    auto graph = lib.get_graph(gid);
    return graph.get();
  };
  for (const auto gid : lib.all_io_gids()) {
    auto io = lib.find_io(gid);
    if (!io) {
      continue;
    }
    Graph_row row;
    row.name           = std::string(io->get_name());
    row.owner          = owner_of(row.name, res);
    row.unit_key       = key_of(row.owner, res);
    row.interface_hash = graph_interface_hash(*io);
    row.has_body       = lib.has_graph(gid);
    if (row.has_body) {
      auto graph  = lib.get_graph(gid);
      // One memo for the whole library is load-bearing: the per-call overload
      // re-walks every shared child once per root (O(defs x subtree)), which
      // reached 28 GB RSS on Backend. The batch overload computes every def's
      // Merkle digest once while retaining the identical digest contract.
      auto digest = livehd::semdiff::canonical_digest(graph.get(), resolver, digest_memo);
      if (!digest.valid) {
        digestable = false;
        return {};
      }
      row.h0 = digest.h0;
      row.h1 = digest.h1;
    }
    rows.push_back(std::move(row));
  }
  std::sort(rows.begin(), rows.end(), [](const Graph_row& a, const Graph_row& b) { return a.name < b.name; });
  return rows;
}

std::vector<Artifact_file> artifact_files(const fs::path& root) {
  std::vector<Artifact_file> files;
  std::error_code            ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || ec) {
      continue;
    }
    const auto rel = fs::relative(it->path(), root, ec);
    if (ec) {
      break;
    }
    // Separate error codes: a nested a(b(ec), ec) call lets the outer success
    // clear the inner failure, silently committing a garbage size/mtime row.
    std::error_code size_ec;
    std::error_code time_ec;
    const auto      size  = it->file_size(size_ec);
    const auto      mtime = it->last_write_time(time_ec);
    if (size_ec || time_ec) {
      ec = size_ec ? size_ec : time_ec;
      break;
    }
    files.push_back(Artifact_file{.path  = rel.generic_string(),
                                  .size  = static_cast<uint64_t>(size),
                                  .mtime = static_cast<int64_t>(mtime.time_since_epoch().count())});
  }
  if (ec) {
    throw Lhd_error{"config", std::format("could not inventory graph artifact {}: {}", root.string(), ec.message()), ""};
  }
  std::sort(files.begin(), files.end(), [](const Artifact_file& a, const Artifact_file& b) { return a.path < b.path; });
  return files;
}

bool safe_artifact_path(std::string_view text) {
  const fs::path path{text};
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  return std::none_of(path.begin(), path.end(), [](const fs::path& part) { return part == ".."; });
}

bool artifact_matches(const fs::path& root, const std::vector<Artifact_file>& expected, bool allow_extra) {
  std::error_code ec;
  for (const auto& row : expected) {
    if (!safe_artifact_path(row.path)) {
      return false;
    }
    const auto path = root / row.path;
    if (!fs::is_regular_file(path, ec) || ec || fs::file_size(path, ec) != row.size || ec
        || static_cast<int64_t>(fs::last_write_time(path, ec).time_since_epoch().count()) != row.mtime || ec) {
      return false;
    }
  }
  if (!allow_extra) {
    try {
      return artifact_files(root).size() == expected.size();
    } catch (...) {
      return false;
    }
  }
  return true;
}

void write_graph_inventory(const std::string& path, const Result& res, const std::vector<Graph_row>& rows,
                           const std::vector<std::shared_ptr<Lnast>>& lnasts, const std::vector<Artifact_file>& files,
                           const std::vector<livehd::diag::Diagnostic>& pipeline_diags) {
  rapidjson::StringBuffer                    sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  w.StartObject();
  w.Key("schema_version");
  w.Int(3);
  w.Key("code_salt");
  w.Uint64(livehd::kCompileSrcSalt);
  w.Key("context");
  w.String(res.compile_cache_context.c_str());
  w.Key("closure_key");
  w.String(res.compile_cache_closure_key.c_str());
  w.Key("lnast_order");
  w.StartArray();
  for (const auto& ln : lnasts) {
    if (ln) {
      w.String(std::string(ln->get_top_module_name()).c_str());
    }
  }
  w.EndArray();
  w.Key("graphs");
  w.StartArray();
  for (const auto& row : rows) {
    w.StartObject();
    w.Key("name");
    w.String(row.name.c_str());
    w.Key("owner");
    w.String(row.owner.c_str());
    w.Key("unit_key");
    w.String(row.unit_key.c_str());
    w.Key("interface_hash");
    w.Uint64(row.interface_hash);
    w.Key("has_body");
    w.Bool(row.has_body);
    if (row.has_body) {
      w.Key("h0");
      w.Uint64(row.h0);
      w.Key("h1");
      w.Uint64(row.h1);
    }
    w.EndObject();
  }
  w.EndArray();
  w.Key("pipeline_diags");
  w.StartArray();
  for (const auto& d : pipeline_diags) {
    write_diag(w, d);
  }
  w.EndArray();
  w.Key("artifact_files");
  w.StartArray();
  for (const auto& file : files) {
    w.StartObject();
    w.Key("path");
    w.String(file.path.c_str());
    w.Key("size");
    w.Uint64(file.size);
    w.Key("mtime");
    w.Int64(file.mtime);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    throw Lhd_error{"config", std::format("could not write graph cache inventory {}", path), ""};
  }
  ofs.write(sb.GetString(), static_cast<std::streamsize>(sb.GetSize()));
  ofs.put('\n');
  if (!ofs) {
    throw Lhd_error{"config", std::format("failed writing graph cache inventory {}", path), ""};
  }
}

std::optional<Graph_inventory> read_graph_inventory(const Result& res) {
  auto text = try_slurp(res.compile_cache_scope + "/lg/graph_inventory.json");
  if (!text) {
    return std::nullopt;
  }
  rapidjson::Document doc;
  doc.Parse(text->data(), text->size());
  if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("schema_version") || !doc["schema_version"].IsInt()
      || doc["schema_version"].GetInt() != 3 || !doc.HasMember("code_salt") || !doc["code_salt"].IsUint64()
      || doc["code_salt"].GetUint64() != livehd::kCompileSrcSalt || !doc.HasMember("context") || !doc["context"].IsString()
      || res.compile_cache_context != doc["context"].GetString() || !doc.HasMember("closure_key") || !doc["closure_key"].IsString()
      || !doc.HasMember("lnast_order") || !doc["lnast_order"].IsArray() || !doc.HasMember("graphs") || !doc["graphs"].IsArray()
      || !doc.HasMember("pipeline_diags") || !doc["pipeline_diags"].IsArray() || !doc.HasMember("artifact_files")
      || !doc["artifact_files"].IsArray()) {
    return std::nullopt;
  }
  Graph_inventory inventory;
  inventory.closure_key = doc["closure_key"].GetString();
  for (const auto& name : doc["lnast_order"].GetArray()) {
    if (!name.IsString()) {
      return std::nullopt;
    }
    inventory.lnast_order.emplace_back(name.GetString());
  }
  for (const auto& g : doc["graphs"].GetArray()) {
    if (!g.IsObject() || !g.HasMember("name") || !g["name"].IsString() || !g.HasMember("interface_hash")
        || !g["interface_hash"].IsUint64() || !g.HasMember("has_body") || !g["has_body"].IsBool()) {
      return std::nullopt;
    }
    Graph_row row;
    row.name           = g["name"].GetString();
    row.owner          = g.HasMember("owner") && g["owner"].IsString() ? g["owner"].GetString() : "";
    row.unit_key       = g.HasMember("unit_key") && g["unit_key"].IsString() ? g["unit_key"].GetString() : "";
    row.interface_hash = g["interface_hash"].GetUint64();
    row.has_body       = g["has_body"].GetBool();
    if (row.has_body) {
      if (!g.HasMember("h0") || !g["h0"].IsUint64() || !g.HasMember("h1") || !g["h1"].IsUint64()) {
        return std::nullopt;
      }
      row.h0 = g["h0"].GetUint64();
      row.h1 = g["h1"].GetUint64();
    }
    inventory.rows.push_back(std::move(row));
  }
  for (const auto& d : doc["pipeline_diags"].GetArray()) {
    auto stored = read_diag(d);
    if (!stored) {
      return std::nullopt;
    }
    inventory.pipeline_diags.push_back(std::move(*stored));
  }
  for (const auto& f : doc["artifact_files"].GetArray()) {
    if (!f.IsObject() || !f.HasMember("path") || !f["path"].IsString() || !safe_artifact_path(f["path"].GetString())
        || !f.HasMember("size") || !f["size"].IsUint64() || !f.HasMember("mtime") || !f["mtime"].IsInt64()) {
      return std::nullopt;
    }
    inventory.artifact_files.push_back(Artifact_file{f["path"].GetString(), f["size"].GetUint64(), f["mtime"].GetInt64()});
  }
  if (inventory.artifact_files.empty()) {
    return std::nullopt;
  }
  return inventory;
}

template <typename T>
void write_pod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(value));
  if (!os) {
    throw Lhd_error{"config", "failed writing compact compile-cache LNAST", "check --workdir space"};
  }
}

template <typename T>
T read_pod(std::istream& is) {
  T value{};
  is.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!is) {
    throw Lhd_error{"config", "truncated compact compile-cache LNAST", "the unit will be rebuilt cold"};
  }
  return value;
}

void write_string(std::ostream& os, std::string_view text) {
  write_pod<uint32_t>(os, static_cast<uint32_t>(text.size()));
  os.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!os) {
    throw Lhd_error{"config", "failed writing compact compile-cache LNAST string", "check --workdir space"};
  }
}

std::string read_string(std::istream& is) {
  const auto size = read_pod<uint32_t>(is);
  if (size > (1U << 28)) {
    throw Lhd_error{"config", "invalid compact compile-cache LNAST string length", "the unit will be rebuilt cold"};
  }
  std::string text(size, '\0');
  is.read(text.data(), static_cast<std::streamsize>(size));
  if (!is) {
    throw Lhd_error{"config", "truncated compact compile-cache LNAST string", "the unit will be rebuilt cold"};
  }
  return text;
}

void write_compact_node(std::ostream& os, const Lnast& ln, const Lnast_nid& nid) {
  write_pod<uint16_t>(os, static_cast<uint16_t>(ln.get_type(nid)));
  const auto name_id = ln.get_name_id(nid);
  const auto srcid   = ln.get_srcid(nid);
  uint8_t    flags   = 0;
  if (name_id != 0) {
    flags |= 1;
  }
  if (srcid != hhds::SourceId_invalid) {
    flags |= 2;
  }
  write_pod<uint8_t>(os, flags);
  if (name_id != 0) {
    write_pod<int32_t>(os, name_id);
  }
  if (srcid != hhds::SourceId_invalid) {
    write_pod<hhds::SourceId>(os, srcid);
  }
  uint32_t child_count = 0;
  for ([[maybe_unused]] const auto& child : ln.children(nid)) {
    ++child_count;
  }
  write_pod<uint32_t>(os, child_count);
  for (const auto& child : ln.children(nid)) {
    write_compact_node(os, ln, child);
  }
}

// Iterative on purpose: tree.bin is untrusted cache data, and a corrupt file
// encoding a deep single-child chain must surface as the contracted "rebuilt
// cold" refusal, never as a recursion stack overflow.
Lnast_nid read_compact_node(std::istream& is, Lnast& ln, const Lnast_nid& parent, const std::map<int32_t, std::string>& names,
                            uint64_t& node_budget) {
  struct Frame {
    Lnast_nid nid;
    uint32_t  remaining;
  };
  Lnast_nid          root{};
  std::vector<Frame> stack;
  auto               read_one = [&](const Lnast_nid& under) -> void {
    if (node_budget == 0) {
      throw Lhd_error{"config", "compact compile-cache LNAST exceeds its node budget", "the unit will be rebuilt cold"};
    }
    --node_budget;
    const auto type    = static_cast<Lnast_ntype::Lnast_ntype_int>(read_pod<uint16_t>(is));
    const auto flags   = read_pod<uint8_t>(is);
    int32_t    name_id = 0;
    if ((flags & 1) != 0) {
      name_id = read_pod<int32_t>(is);
      if (!names.contains(name_id)) {
        throw Lhd_error{"config", "compact compile-cache LNAST references an unknown name", "the unit will be rebuilt cold"};
      }
    }
    hhds::SourceId srcid = hhds::SourceId_invalid;
    if ((flags & 2) != 0) {
      srcid = read_pod<hhds::SourceId>(is);
    }
    if ((flags & ~uint8_t{3}) != 0) {
      throw Lhd_error{"config", "compact compile-cache LNAST has invalid node flags", "the unit will be rebuilt cold"};
    }
    const auto child_count = read_pod<uint32_t>(is);
    auto       nid         = under.is_invalid() ? ln.set_root(type) : ln.add_child(under, type);
    if (root.is_invalid()) {
      root = nid;
    }
    if (name_id != 0) {
      ln.set_name(nid, names.at(name_id));
    }
    if (srcid != hhds::SourceId_invalid) {
      ln.set_srcid(nid, srcid);
    }
    if (child_count != 0) {
      stack.push_back(Frame{nid, child_count});
    }
  };
  read_one(parent);
  while (!stack.empty()) {
    auto& top = stack.back();
    if (top.remaining == 0) {
      stack.pop_back();
      continue;
    }
    --top.remaining;
    const auto under = top.nid;  // copy: read_one may grow `stack`
    read_one(under);
  }
  return root;
}

std::string lnast_unit_dir(std::string_view snapshot_file) {
  const auto dot = snapshot_file.rfind('.');
  return std::string(snapshot_file.substr(0, dot));
}

std::string lowered_lnast_dir(size_t index) { return std::format("unit_{:08}", index); }

constexpr std::string_view kCompactLnastMagic{"lhd.cln5"};

// The header pairs the magic with the stored body's semantic hash. The defer
// hit path compares it against the inventory row, so a snapshot that was
// republished without its LNAST (torn store, concurrent writer) can never
// authorize the stale tree: the pairing check is on the artifact itself.
bool compact_lnast_header_valid(const fs::path& dir, uint64_t expected_hash) {
  std::ifstream is(dir / "tree.bin", std::ios::binary);
  std::string   got(kCompactLnastMagic.size(), '\0');
  is.read(got.data(), static_cast<std::streamsize>(got.size()));
  if (!is || got != kCompactLnastMagic) {
    return false;
  }
  uint64_t stored_hash = 0;
  is.read(reinterpret_cast<char*>(&stored_hash), sizeof(stored_hash));
  return is && stored_hash == expected_hash;
}

void save_compact_lnast(const std::shared_ptr<Lnast>& ln, const fs::path& dir, bool metadata_only = false) {
  if (!ln) {
    throw Lhd_error{"config", "cannot store a null compile-cache LNAST", ""};
  }
  fs::create_directories(dir);
  std::ofstream os(dir / "tree.bin", std::ios::binary | std::ios::trunc);
  if (!os.is_open()) {
    throw Lhd_error{"config",
                    std::format("could not write compile-cache LNAST for {}", ln->get_top_module_name()),
                    "check --workdir permissions"};
  }
  std::shared_ptr<Lnast> stub;
  const Lnast*           body = ln.get();
  if (metadata_only) {
    stub            = std::make_shared<Lnast>(ln->get_top_module_name());
    const auto root = stub->set_root(Lnast_ntype::create_top());
    stub->add_child(root, Lnast_ntype::create_stmts());
    body = stub.get();
  }
  os.write(kCompactLnastMagic.data(), static_cast<std::streamsize>(kCompactLnastMagic.size()));
  write_pod<uint64_t>(os, semantic_hash(*body));
  write_string(os, ln->get_top_module_name());

  uint8_t meta_flags = 0;
  if (ln->is_template()) {
    meta_flags |= 1;
  }
  if (ln->is_verilog_origin()) {
    meta_flags |= 2;
  }
  if (ln->get_skip_timecheck()) {
    meta_flags |= 4;
  }
  if (ln->is_package_unit()) {
    meta_flags |= 8;
  }
  write_pod<uint8_t>(os, meta_flags);
  write_string(os, ln->get_lambda_kind());
  write_string(os, ln->get_lg_name());
  auto write_strings = [&](const std::vector<std::string>& strings) {
    write_pod<uint32_t>(os, static_cast<uint32_t>(strings.size()));
    for (const auto& text : strings) {
      write_string(os, text);
    }
  };
  write_strings(ln->get_generics());
  write_strings(ln->get_generic_defaults());
  write_strings(ln->get_external_modules());
  write_strings(ln->get_imported_packages());
  auto write_string_map = [&](const auto& values) {
    std::map<std::string, std::string> ordered(values.begin(), values.end());
    write_pod<uint32_t>(os, static_cast<uint32_t>(ordered.size()));
    for (const auto& [key, value] : ordered) {
      write_string(os, key);
      write_string(os, value);
    }
  };
  write_string_map(ln->get_package_const_exprs());
  write_string_map(ln->get_package_const_types());
  write_string_map(ln->get_io_type_names());

  const auto& pubs = ln->get_pub_list();
  write_pod<uint32_t>(os, static_cast<uint32_t>(pubs.size()));
  for (const auto& pub : pubs) {
    write_string(os, pub.name);
    write_string(os, pub.kind);
    write_pod<hhds::SourceId>(os, pub.srcid);
    write_string(os, pub.lg);
  }

  const auto& pub_values = ln->get_pub_values();
  write_pod<uint32_t>(os, static_cast<uint32_t>(pub_values.size()));
  for (const auto& [name, value] : pub_values) {
    write_string(os, name);
    write_string(os, value);
  }

  auto write_io = [&](const Lnast_io_entry& entry) {
    write_string(os, entry.name);
    write_pod<int64_t>(os, entry.bits);
    uint8_t flags  = 0;
    flags         |= entry.is_signed ? 1 : 0;
    flags         |= entry.is_ref ? 2 : 0;
    flags         |= entry.is_varargs ? 4 : 0;
    flags         |= entry.has_range ? 8 : 0;
    write_pod<uint8_t>(os, flags);
    write_pod<int32_t>(os, static_cast<int32_t>(entry.kind));
    write_pod<int64_t>(os, entry.stages_min);
    write_pod<int64_t>(os, entry.stages_max);
    write_string(os, entry.type_name);
    write_pod<int64_t>(os, entry.range_min);
    write_pod<int64_t>(os, entry.range_max);
  };
  const auto& io = ln->io_meta();
  write_pod<uint32_t>(os, static_cast<uint32_t>(io.inputs.size()));
  for (const auto& entry : io.inputs) {
    write_io(entry);
  }
  write_pod<uint32_t>(os, static_cast<uint32_t>(io.outputs.size()));
  for (const auto& entry : io.outputs) {
    write_io(entry);
  }

  const auto& bw = ln->bw_meta().ranges;
  if (metadata_only) {
    write_pod<uint32_t>(os, 0);
  } else {
    // Ordered copy: absl hash iteration is per-process randomized and would
    // make tree.bin byte-nondeterministic for an identical LNAST.
    std::map<std::string, BitwidthEntry> ordered(bw.begin(), bw.end());
    write_pod<uint32_t>(os, static_cast<uint32_t>(ordered.size()));
    for (const auto& [name, entry] : ordered) {
      write_string(os, name);
      write_pod<int64_t>(os, entry.min);
      write_pod<int64_t>(os, entry.max);
      write_pod<uint8_t>(os, entry.unbounded ? 1 : 0);
    }
  }

  std::map<int32_t, std::string> names;
  for (const auto& nid : body->depth_preorder()) {
    const auto id = body->get_name_id(nid);
    if (id != 0) {
      names.try_emplace(id, body->get_name(nid));
    }
  }
  write_pod<uint32_t>(os, static_cast<uint32_t>(names.size()));
  for (const auto& [id, name] : names) {
    write_pod<int32_t>(os, id);
    write_string(os, name);
  }
  write_compact_node(os, *body, body->get_root());
  os.close();
  if (!os) {
    throw Lhd_error{"config",
                    std::format("failed closing compile-cache LNAST for {}", ln->get_top_module_name()),
                    "check --workdir space"};
  }
  if (!metadata_only) {
    ln->source_locator().save(dir.string());
  }
}

std::shared_ptr<Lnast> load_compact_lnast(const std::string& dir, std::string_view expected_name) {
  std::ifstream is(fs::path(dir) / "tree.bin", std::ios::binary);
  if (!is.is_open()) {
    throw Lhd_error{"config", std::format("missing compact compile-cache LNAST in {}", dir), "the unit will be rebuilt cold"};
  }
  std::string got(kCompactLnastMagic.size(), '\0');
  is.read(got.data(), static_cast<std::streamsize>(got.size()));
  if (!is || got != kCompactLnastMagic) {
    throw Lhd_error{"config", std::format("invalid compact compile-cache LNAST in {}", dir), "the unit will be rebuilt cold"};
  }
  const auto header_hash = read_pod<uint64_t>(is);
  const auto name        = read_string(is);
  if (name != expected_name) {
    throw Lhd_error{"config",
                    std::format("compile-cache LNAST '{}' does not match expected '{}'", name, expected_name),
                    "the unit will be rebuilt cold"};
  }
  auto ln = std::make_shared<Lnast>(name);

  const auto meta_flags = read_pod<uint8_t>(is);
  if ((meta_flags & ~uint8_t{15}) != 0) {
    throw Lhd_error{"config", "compact compile-cache LNAST has invalid metadata flags", "the unit will be rebuilt cold"};
  }
  ln->set_template((meta_flags & 1) != 0);
  ln->set_verilog_origin((meta_flags & 2) != 0);
  ln->set_skip_timecheck((meta_flags & 4) != 0);
  ln->set_package_unit((meta_flags & 8) != 0);
  ln->set_lambda_kind(read_string(is));
  ln->set_lg_name(read_string(is));
  auto read_strings = [&]() {
    const auto count = read_pod<uint32_t>(is);
    if (count > (1U << 20)) {
      throw Lhd_error{"config", "invalid compact compile-cache string-vector count", "the unit will be rebuilt cold"};
    }
    std::vector<std::string> strings;
    strings.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      strings.push_back(read_string(is));
    }
    return strings;
  };
  ln->set_generics(read_strings());
  ln->set_generic_defaults(read_strings());
  for (const auto& value : read_strings()) {
    ln->add_external_module(value);
  }
  for (const auto& value : read_strings()) {
    ln->add_imported_package(value);
  }
  auto read_string_map = [&]() {
    const auto count = read_pod<uint32_t>(is);
    if (count > (1U << 26)) {
      throw Lhd_error{"config", "invalid compact compile-cache string-map count", "the unit will be rebuilt cold"};
    }
    absl::flat_hash_map<std::string, std::string> values;
    values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      auto key   = read_string(is);
      auto value = read_string(is);
      if (!values.emplace(std::move(key), std::move(value)).second) {
        throw Lhd_error{"config", "duplicate compact compile-cache string-map key", "the unit will be rebuilt cold"};
      }
    }
    return values;
  };
  ln->set_package_const_exprs(read_string_map());
  ln->set_package_const_types(read_string_map());
  for (const auto& [port, alias] : read_string_map()) {
    ln->add_io_type_name(port, alias);
  }

  struct Pub_record {
    std::string    name;
    std::string    kind;
    hhds::SourceId srcid;
    std::string    lg;
  };
  std::vector<Pub_record> pubs;
  const auto              pub_count = read_pod<uint32_t>(is);
  if (pub_count > (1U << 20)) {
    throw Lhd_error{"config", "invalid compact compile-cache pub count", "the unit will be rebuilt cold"};
  }
  pubs.reserve(pub_count);
  for (uint32_t i = 0; i < pub_count; ++i) {
    pubs.push_back({read_string(is), read_string(is), read_pod<hhds::SourceId>(is), read_string(is)});
  }

  const auto pub_value_count = read_pod<uint32_t>(is);
  if (pub_value_count > (1U << 26)) {
    throw Lhd_error{"config", "invalid compact compile-cache pub-value count", "the unit will be rebuilt cold"};
  }
  std::vector<std::pair<std::string, std::string>> pub_values;
  pub_values.reserve(pub_value_count);
  for (uint32_t i = 0; i < pub_value_count; ++i) {
    pub_values.emplace_back(read_string(is), read_string(is));
  }
  ln->set_pub_values(std::move(pub_values));

  auto read_io = [&]() {
    Lnast_io_entry entry;
    entry.name       = read_string(is);
    entry.bits       = read_pod<int64_t>(is);
    const auto flags = read_pod<uint8_t>(is);
    if ((flags & ~uint8_t{15}) != 0) {
      throw Lhd_error{"config", "invalid compact compile-cache IO flags", "the unit will be rebuilt cold"};
    }
    entry.is_signed  = (flags & 1) != 0;
    entry.is_ref     = (flags & 2) != 0;
    entry.is_varargs = (flags & 4) != 0;
    entry.has_range  = (flags & 8) != 0;
    entry.kind       = static_cast<Io_kind>(read_pod<int32_t>(is));
    entry.stages_min = read_pod<int64_t>(is);
    entry.stages_max = read_pod<int64_t>(is);
    entry.type_name  = read_string(is);
    entry.range_min  = read_pod<int64_t>(is);
    entry.range_max  = read_pod<int64_t>(is);
    return entry;
  };
  auto read_io_vector = [&](std::vector<Lnast_io_entry>& entries) {
    const auto count = read_pod<uint32_t>(is);
    if (count > (1U << 24)) {
      throw Lhd_error{"config", "invalid compact compile-cache IO count", "the unit will be rebuilt cold"};
    }
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      entries.push_back(read_io());
    }
  };
  read_io_vector(ln->io_meta().inputs);
  read_io_vector(ln->io_meta().outputs);
  ln->io_meta().invalidate_index();

  const auto bw_count = read_pod<uint32_t>(is);
  if (bw_count > (1U << 27)) {
    throw Lhd_error{"config", "invalid compact compile-cache bitwidth count", "the unit will be rebuilt cold"};
  }
  for (uint32_t i = 0; i < bw_count; ++i) {
    const auto    bw_name = read_string(is);
    BitwidthEntry entry;
    entry.min       = read_pod<int64_t>(is);
    entry.max       = read_pod<int64_t>(is);
    entry.unbounded = read_pod<uint8_t>(is) != 0;
    ln->bw_meta().ranges.emplace(bw_name, entry);
  }

  std::map<int32_t, std::string> names;
  const auto                     name_count = read_pod<uint32_t>(is);
  if (name_count > (1U << 26)) {
    throw Lhd_error{"config", "invalid compact compile-cache name count", "the unit will be rebuilt cold"};
  }
  for (uint32_t i = 0; i < name_count; ++i) {
    const auto id = read_pod<int32_t>(is);
    if (id == 0 || !names.emplace(id, read_string(is)).second) {
      throw Lhd_error{"config", "invalid compact compile-cache name table", "the unit will be rebuilt cold"};
    }
  }
  uint64_t node_budget = 1ULL << 32;
  (void)read_compact_node(is, *ln, Lnast_nid{}, names, node_budget);
  if (is.peek() != std::char_traits<char>::eof()) {
    throw Lhd_error{"config", "trailing data in compact compile-cache LNAST", "the unit will be rebuilt cold"};
  }
  if (semantic_hash(*ln) != header_hash) {
    throw Lhd_error{"config", "compact compile-cache LNAST does not match its header hash", "the unit will be rebuilt cold"};
  }
  (void)ln->source_locator().load_lazy(dir);
  for (auto& pub : pubs) {
    ln->add_pub(pub.name, pub.kind, pub.srcid, pub.lg);
  }
  return ln;
}

void hardlink_tree(const fs::path& src, const fs::path& dst) {
  std::error_code ec;
  fs::create_directories(dst, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not create compile-cache directory {}: {}", dst.string(), ec.message()), ""};
  }
  for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
    const auto rel = fs::relative(it->path(), src, ec);
    if (ec) {
      break;
    }
    const auto target = dst / rel;
    if (it->is_directory()) {
      fs::create_directories(target, ec);
    } else if (it->is_regular_file()) {
      fs::create_hard_link(it->path(), target, ec);
    }
    if (ec) {
      break;
    }
  }
  if (ec) {
    throw Lhd_error{"config", std::format("could not reuse compile-cache unit {}: {}", src.string(), ec.message()), ""};
  }
}

void copy_tree_files(const fs::path& src, const fs::path& dst) {
  std::error_code ec;
  fs::create_directories(dst, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not create compile-cache directory {}: {}", dst.string(), ec.message()), ""};
  }
  for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
    const auto rel = fs::relative(it->path(), src, ec);
    if (ec) {
      break;
    }
    const auto target = dst / rel;
    if (it->is_directory()) {
      fs::create_directories(target, ec);
    } else if (it->is_regular_file()) {
#if defined(__APPLE__)
      if (::clonefile(it->path().c_str(), target.c_str(), 0) == 0) {
        continue;  // APFS copy-on-write clone: independently mutable, O(1)
      }
#endif
      fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
      if (!ec) {
        std::error_code read_ec;
        const auto      mtime = fs::last_write_time(it->path(), read_ec);
        if (read_ec) {
          ec = read_ec;  // never stamp a garbage timestamp: it poisons artifact_matches forever
        } else {
          fs::last_write_time(target, mtime, ec);
        }
      }
    }
    if (ec) {
      break;
    }
  }
  if (ec) {
    throw Lhd_error{"config", std::format("could not snapshot graph library {}: {}", src.string(), ec.message()), ""};
  }
}

void materialize_artifact_files(const fs::path& src, const fs::path& dst, const std::vector<Artifact_file>& files) {
  std::error_code ec;
  fs::create_directories(dst, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not create graph artifact directory {}: {}", dst.string(), ec.message()), ""};
  }
  for (const auto& row : files) {
    if (!safe_artifact_path(row.path)) {
      throw Lhd_error{"config", "unsafe path in graph artifact inventory", "the cache generation will be refused"};
    }
    const auto from = src / row.path;
    const auto to   = dst / row.path;
    fs::create_directories(to.parent_path(), ec);
    if (ec) {
      break;
    }
#if defined(__APPLE__)
    if (::clonefile(from.c_str(), to.c_str(), 0) == 0) {
      continue;
    }
#endif
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      break;
    }
    std::error_code read_ec;
    const auto      mtime = fs::last_write_time(from, read_ec);
    if (read_ec) {
      ec = read_ec;
      break;
    }
    fs::last_write_time(to, mtime, ec);
    if (ec) {
      break;
    }
  }
  if (ec) {
    throw Lhd_error{"config", std::format("could not materialize graph artifact {}: {}", dst.string(), ec.message()), ""};
  }
}

void store_cache(Options& opts, Result& res, const std::string& scope, const std::string& context,
                 const std::vector<Source_unit>& units, const Prior_cache& prior) {
  const std::string suffix            = std::format(".new.{}", static_cast<long>(::getpid()));
  auto              publish_inventory = [&] {
    const auto inv_tmp = scope + "/inventory.json" + suffix;
    write_inventory(inv_tmp, context, units);
    if (::rename(inv_tmp.c_str(), (scope + "/inventory.json").c_str()) != 0) {
      throw Lhd_error{"config",
                      std::format("could not atomically publish {}/inventory.json", scope),
                      "check --workdir permissions"};
    }
  };
  auto write_snapshot = [&](const Source_unit& unit, const fs::path& path) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      throw Lhd_error{"config", std::format("could not write compile snapshot for {}", unit.path), ""};
    }
    ofs.write(unit.bytes.data(), static_cast<std::streamsize>(unit.bytes.size()));
    ofs.close();
    if (!ofs) {
      throw Lhd_error{"config", std::format("failed writing compile snapshot for {}", unit.path), ""};
    }
  };

  // The manifest is the commit point. When the logical-unit layout is stable,
  // replace only changed objects and publish the new inventory last. Readers
  // racing an object replacement either see the old complete generation or
  // refuse the transient mismatch; no run needs 1,108 new hardlinks merely to
  // record a comment in one file.
  bool stable_layout = prior.compatible && prior.units.size() == units.size();
  if (stable_layout) {
    for (const auto& unit : units) {
      const auto old = prior.units.find(unit.name);
      if (old == prior.units.end() || old->second.snapshot_file != unit.snapshot_file) {
        stable_layout = false;
        break;
      }
    }
  }
  if (stable_layout) {
    for (const auto& unit : units) {
      const auto& old = prior.units.at(unit.name);
      // ORDER IS LOAD-BEARING: publish the compact LNAST before its source
      // snapshot. A crash between the two then leaves NEW tree + OLD snapshot,
      // which the hit path refuses (byte mismatch -> honest reparse). The
      // reverse tearing (NEW snapshot + OLD tree) would byte-match the user's
      // edit and validate the stale tree against the old inventory row -- a
      // permanent silent false hit.
      if (!unit.exact_prior_match) {
        const fs::path  live = fs::path(scope) / "ln" / lnast_unit_dir(old.snapshot_file);
        const fs::path  temp = live.string() + suffix;
        std::error_code unit_ec;
        fs::remove_all(temp, unit_ec);
        save_compact_lnast(unit.lnast, temp);
        replace_dir(temp, live);
      }
      if (!unit.exact_snapshot_match) {
        const fs::path live = fs::path(scope) / "pyrope" / old.snapshot_file;
        const fs::path temp = live.string() + suffix;
        write_snapshot(unit, temp);
        if (::rename(temp.c_str(), live.c_str()) != 0) {
          throw Lhd_error{"config", std::format("could not atomically publish compile snapshot for {}", unit.path), ""};
        }
      }
    }
    publish_inventory();
    (void)opts;
    (void)res;
    return;
  }

  const fs::path  py_new = scope + "/pyrope" + suffix;
  const fs::path  ln_new = scope + "/ln" + suffix;
  std::error_code ec;
  fs::remove_all(py_new, ec);
  fs::remove_all(ln_new, ec);
  fs::create_directories(py_new, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not create compile snapshot {}: {}", py_new.string(), ec.message()), ""};
  }
  fs::create_directories(ln_new, ec);
  if (ec) {
    throw Lhd_error{"config", std::format("could not create compile LNAST cache {}: {}", ln_new.string(), ec.message()), ""};
  }
  std::atomic_size_t next{0};
  std::mutex         error_mu;
  std::exception_ptr error;
  auto               store_one = [&]() {
    while (true) {
      const auto index = next.fetch_add(1);
      if (index >= units.size()) {
        return;
      }
      {
        std::lock_guard guard(error_mu);
        if (error) {
          return;
        }
      }
      try {
        const auto&     unit = units[index];
        std::error_code unit_ec;
        const auto      old     = prior.units.find(unit.name);
        const auto old_snapshot = old == prior.units.end() ? fs::path{} : fs::path(scope) / "pyrope" / old->second.snapshot_file;
        const auto new_snapshot = py_new / unit.snapshot_file;
        if (unit.exact_snapshot_match && old != prior.units.end() && fs::is_regular_file(old_snapshot, unit_ec) && !unit_ec) {
          fs::create_hard_link(old_snapshot, new_snapshot, unit_ec);
          if (unit_ec) {
            throw Lhd_error{"config", std::format("could not reuse compile snapshot for {}: {}", unit.path, unit_ec.message()), ""};
          }
        } else {
          write_snapshot(unit, new_snapshot);
        }

        const auto unit_dir = lnast_unit_dir(unit.snapshot_file);
        if (unit.exact_prior_match && old != prior.units.end()) {
          hardlink_tree(fs::path(scope) / "ln" / lnast_unit_dir(old->second.snapshot_file), ln_new / unit_dir);
        } else {
          save_compact_lnast(unit.lnast, ln_new / unit_dir);
        }
      } catch (...) {
        std::lock_guard guard(error_mu);
        if (!error) {
          error = std::current_exception();
        }
        return;
      }
    }
  };
  const auto worker_count = std::min<size_t>(8, std::max<size_t>(1, std::thread::hardware_concurrency()));
  livehd::run_workers(worker_count, [&](size_t) { store_one(); });
  if (error) {
    // Do not strand multi-gigabyte temp trees in the persistent scope: later
    // runs carry a different pid suffix and would never collect these.
    fs::remove_all(py_new, ec);
    fs::remove_all(ln_new, ec);
    std::rethrow_exception(error);
  }
  // ln/ before pyrope/ for the same tearing reason as the stable path above:
  // a crash between the two renames must leave the OLD snapshots in place so
  // the next run misses honestly instead of pairing new bytes with old trees.
  replace_dir(ln_new, scope + "/ln");
  replace_dir(py_new, scope + "/pyrope");

  publish_inventory();
  (void)opts;
  (void)res;
}

}  // namespace

size_t compile_cache_parse_sources(Options& opts, Result& res, Eprp_var& var, const std::vector<std::string>& seed_files,
                                   bool defer_clean_lnasts) {
  if (seed_files.empty()) {
    return 0;
  }
  const auto scope          = std::format("{}/incr/scopes/compile/{}", opts.workdir, scope_name(opts, seed_files));
  const auto context        = context_descriptor(opts);
  res.compile_cache_scope   = scope;
  res.compile_cache_context = context;
  ensure_dir(scope);
  collect_stale_temps(scope);
  collect_stale_temps(fs::path(scope) / "pyrope");
  collect_stale_temps(fs::path(scope) / "ln");

  Prior_cache prior;
  {
    Phase_timer phase(res, "compile.cache.lookup");
    prior = load_prior(scope, context);
  }
  if (!prior.compatible && fs::exists(scope + "/inventory.json")) {
    ++res.compile_cache.refused;
  }
  res.compile_cache_prior_units.clear();
  for (const auto& [name, row] : prior.units) {
    res.compile_cache_prior_units.push_back(name);
  }
  std::vector<Source_unit> units;
  {
    Phase_timer phase(res, "compile.cache.sync");
    units = capture_closure(seed_files, var, prior.compatible ? &prior : nullptr, scope);
  }

  auto load_prior_unit = [&](const std::string& name, const Prior_unit& row) -> std::shared_ptr<Lnast> {
    if (auto found = prior.lnasts.find(name); found != prior.lnasts.end()) {
      return found->second;
    }
    try {
      auto ln = load_compact_lnast(scope + "/ln/" + lnast_unit_dir(row.snapshot_file), name);
      if (semantic_hash(*ln) != row.semantic_hash) {
        return {};
      }
      prior.lnasts.emplace(name, ln);
      return ln;
    } catch (...) {
      return {};
    }
  };

  {
    Phase_timer phase(res, "inou.prp");
    for (auto& unit : units) {
      bool hit = false;
      if (unit.snapshot_unavailable) {
        ++res.compile_cache.refused;
      }
      if (prior.compatible) {
        const auto row = prior.units.find(unit.name);
        if (row != prior.units.end() && unit.exact_snapshot_match) {
          // Exact byte equality with the preserved snapshot authorizes input
          // reuse. A Tier-B-only consumer does not
          // deserialize the cached forest: the header probe checks the stored
          // tree's OWN semantic hash against the inventory row, so a torn or
          // concurrently-republished generation can never pair new bytes with
          // a stale tree; the full artifact is decoded only when needed.
          if (compact_lnast_header_valid(fs::path(scope) / "ln" / lnast_unit_dir(row->second.snapshot_file),
                                         row->second.semantic_hash)
              && (defer_clean_lnasts || (unit.lnast = load_prior_unit(unit.name, row->second)))) {
            unit.semantic_hash     = row->second.semantic_hash;
            unit.exact_prior_match = true;
            hit                    = true;
          } else {
            // The user's bytes match the preserved snapshot, so a missing or
            // mismatched cached forest is cache damage, not a source edit.
            // Refuse it explicitly and recover by parsing the captured bytes.
            ++res.compile_cache.refused;
          }
        }
      }
      if (hit) {
        ++res.compile_cache.hits;
        continue;
      }
      const auto t0 = std::chrono::steady_clock::now();
      Prp2lnast  converter(unit.path, unit.name, unit.bytes);
      unit.lnast         = converter.get_lnast();
      unit.semantic_hash = semantic_hash(*unit.lnast);
      if (prior.compatible) {
        const auto row         = prior.units.find(unit.name);
        auto       ln          = row != prior.units.end() && row->second.semantic_hash == unit.semantic_hash
                                     ? load_prior_unit(unit.name, row->second)
                                     : std::shared_ptr<Lnast>{};
        // The digest only proposes a comment-only semantic hit. The exact,
        // ordered type/name traversal decides, so a digest collision can only
        // cause this comparison -- never stale graph restoration.
        unit.exact_prior_match = row != prior.units.end() && ln && row->second.semantic_hash == unit.semantic_hash
                                 && semantic_identical(*unit.lnast, *ln);
      }
      const std::chrono::duration<double, std::milli> dt  = std::chrono::steady_clock::now() - t0;
      res.compile_cache.redone_ms                        += dt.count();
      ++res.compile_cache.misses;
    }
  }

  res.compile_cache_closure_key = closure_key(units);
  const auto unit_keys          = unit_merkle_keys(units);
  res.compile_cache_unit_keys.assign(unit_keys.begin(), unit_keys.end());
  res.compile_cache_clean_units = clean_units(units, prior);

  if (!defer_clean_lnasts) {
    for (auto& unit : units) {
      var.add(unit.lnast);
    }
  } else {
    // Keep precisely the dirty transitive cone live, not merely the directly
    // edited roots. An unchanged importer of a changed unit must elaborate
    // again even though its own source bytes hit Tier A. Clean owners are
    // supplied as compact post-upass metadata by Tier B.
    const std::set<std::string> clean(res.compile_cache_clean_units.begin(), res.compile_cache_clean_units.end());
    for (auto& unit : units) {
      if (clean.contains(unit.name)) {
        continue;
      }
      if (!unit.lnast) {
        const auto row = prior.units.find(unit.name);
        if (row != prior.units.end()) {
          unit.lnast = load_prior_unit(unit.name, row->second);
        }
      }
      if (!unit.lnast) {
        // A compact body that passed the cheap header probe but cannot be
        // decoded is cache damage. Recover from the already-captured hermetic
        // bytes and republish this Tier-A unit instead of trusting the body.
        ++res.compile_cache.refused;
        if (unit.exact_prior_match) {
          unit.exact_prior_match = false;
          --res.compile_cache.hits;
          ++res.compile_cache.misses;
        }
        Prp2lnast converter(unit.path, unit.name, unit.bytes);
        unit.lnast         = converter.get_lnast();
        unit.semantic_hash = semantic_hash(*unit.lnast);
      }
      var.add(unit.lnast);
    }
  }

  try {
    Phase_timer phase(res, "compile.cache.store");
    store_cache(opts, res, scope, context, units, prior);
  } catch (...) {
    ++res.compile_cache.store_failed;
    throw;
  }
  return units.size();
}

void compile_cache_materialize_sources(Result& res, Eprp_var& var) {
  auto current = load_prior(res.compile_cache_scope, res.compile_cache_context, true);
  if (!current.compatible || current.lnasts.size() != current.units.size()) {
    ++res.compile_cache.refused;
    throw Lhd_error{"config",
                    "could not materialize the current hermetic compile-cache source generation",
                    "remove the damaged compile scope or rerun with --set lhd.incremental=false"};
  }
  // The disk generation must be THIS run's: a concurrent compile sharing the
  // scope may have published a different generation between our store and this
  // re-read, and silently lowering someone else's sources would be a
  // miscompile. Recompute the closure key from the loaded rows and compare.
  std::vector<Source_unit> rows;
  rows.reserve(current.units.size());
  for (const auto& name : current.order) {
    const auto& unit = current.units.at(name);
    Source_unit row;
    row.name          = name;
    row.semantic_hash = unit.semantic_hash;
    row.imports       = unit.imports;
    rows.push_back(std::move(row));
  }
  if (closure_key(rows) != res.compile_cache_closure_key) {
    ++res.compile_cache.refused;
    throw Lhd_error{"config",
                    "compile-cache scope changed while this compile was running",
                    "another lhd process shares this --workdir scope; rerun, or use separate workdirs"};
  }
  // The inventory array preserves capture_closure's deterministic source
  // order. Prior_cache::units/lnasts are maps for lookup only; iterating the
  // map here alphabetized 500+ restored roots before a graph-cache refusal.
  // That changed registry and CSE representative order relative to a cold
  // parse (XS ExeUnitImp split shared Get_mask nodes), so a full fallback was
  // semantically equal but not H5-identical. Rebuild the forest in the exact
  // captured order.
  var.lnasts.clear();
  for (const auto& name : current.order) {
    auto it = current.lnasts.find(name);
    if (it == current.lnasts.end()) {
      ++res.compile_cache.refused;
      throw Lhd_error{"config",
                      "compile-cache source order references a missing LNAST",
                      "remove the damaged compile scope or rerun with --set lhd.incremental=false"};
    }
    var.add(std::move(it->second));
  }
}

bool compile_cache_restore_lg_artifact(Options& opts, Result& res, const std::string& lib_path) {
  (void)opts;
  if (res.compile_cache_scope.empty() || res.compile_cache_closure_key.empty()
      || res.compile_cache_clean_units.size() != res.compile_cache_unit_keys.size() || res.compile_cache_unit_keys.empty()) {
    return false;
  }
  Phase_timer phase(res, "compile.cache.lg_artifact");
  auto        expected = read_graph_inventory(res);
  if (!expected || expected->closure_key != res.compile_cache_closure_key) {
    return false;
  }
  const fs::path cached = fs::path(res.compile_cache_scope) / "lg";
  if (!artifact_matches(cached, expected->artifact_files, true)) {
    ++res.compile_cache.refused;
    return false;
  }
  if (!artifact_matches(lib_path, expected->artifact_files, false)) {
    // Wholesale replace_dir may only claim a directory this scope exclusively
    // owns. A shared emit lg: dir with files outside our inventory (another
    // compile's modules) falls back to the normal path, which merges into the
    // loaded library and prunes with ownership scoping.
    {
      std::error_code probe_ec;
      if (fs::exists(lib_path, probe_ec) && !probe_ec) {
        try {
          std::set<std::string> allowed;
          for (const auto& row : expected->artifact_files) {
            allowed.insert(row.path);
          }
          for (const auto& row : artifact_files(lib_path)) {
            if (!allowed.contains(row.path)) {
              return false;
            }
          }
        } catch (...) {
          return false;
        }
      }
    }
    const fs::path  fresh = lib_path + std::format(".new.{}", static_cast<long>(::getpid()));
    std::error_code ec;
    fs::remove_all(fresh, ec);
    try {
      materialize_artifact_files(cached, fresh, expected->artifact_files);
      if (!artifact_matches(fresh, expected->artifact_files, false)) {
        fs::remove_all(fresh, ec);
        ++res.compile_cache.refused;
        return false;
      }
      replace_dir(fresh, lib_path);
    } catch (...) {
      fs::remove_all(fresh, ec);
      ++res.compile_cache.refused;
      return false;
    }
  }
  res.compile_cache.hits
      += std::count_if(expected->rows.begin(), expected->rows.end(), [](const Graph_row& row) { return row.has_body; });
  // Every source unit is clean here, so pass.formal runs over nothing in this
  // process: the stored set IS the cold set.
  replay_pipeline_diags(*expected);
  return true;
}

bool compile_cache_restore_graphs(Options& opts, Result& res, Eprp_var& var, const std::string& lib_path) {
  (void)opts;
  if (res.compile_cache_scope.empty() || res.compile_cache_closure_key.empty()) {
    return false;
  }
  Phase_timer phase(res, "compile.cache.lg_lookup");
  auto        expected = read_graph_inventory(res);
  if (!expected) {
    return false;
  }
  const std::set<std::string> clean(res.compile_cache_clean_units.begin(), res.compile_cache_clean_units.end());
  const bool                  full_clean
      = expected->closure_key == res.compile_cache_closure_key && clean.size() == res.compile_cache_unit_keys.size();
  res.compile_cache_overlay_graphs.clear();
  // A PARTIAL restore re-runs the pipeline over the dirty cone only, so its live
  // records and the stored ones would overlap on an unknown boundary: the stored
  // records carry no per-graph attribution, so there is no sound way to replay
  // just the restored half — and a generation stored from a partial run would
  // itself hold an incomplete set. Refuse (I5: a principled refusal is COUNTED,
  // not hidden) and let the full rebuild reproduce every record. Per-graph
  // attribution is what would lift this.
  //
  // Decided BEFORE the cache library is opened: the verdict depends only on
  // full_clean and the carried records, so digesting every cached graph first
  // (graph_rows) just to refuse was pure waste on every incremental compile of
  // a design that warns.
  if (!full_clean && !expected->pipeline_diags.empty()) {
    for (const auto& row : expected->rows) {
      if (row.has_body && !row.owner.empty() && clean.contains(row.owner) && !row.unit_key.empty()
          && row.unit_key == key_of(row.owner, res)) {
        res.compile_cache_overlay_graphs.push_back(row.name);
      }
    }
    ++res.compile_cache.refused;
    return false;
  }
  try {
    check_ir_body_magic(res.compile_cache_scope + "/lg", "graph_", kHhdsGraphBodyMagic, "compile cache lg:");
    auto& cache      = livehd::Hhds_graph_library::instance(res.compile_cache_scope + "/lg");
    bool  digestable = true;
    auto  actual     = graph_rows(cache, res, digestable);
    if (!digestable || actual.size() != expected->rows.size()) {
      ++res.compile_cache.refused;
      return false;
    }
    for (size_t i = 0; i < actual.size(); ++i) {
      const auto& a = actual[i];
      const auto& e = expected->rows[i];
      if (a.name != e.name || a.interface_hash != e.interface_hash || a.has_body != e.has_body || a.h0 != e.h0 || a.h1 != e.h1) {
        ++res.compile_cache.refused;
        return false;
      }
      // Ownership is derived, never trusted from the manifest: changing it can
      // otherwise make a dirty graph masquerade as belonging to a clean unit.
      if (a.owner != e.owner) {
        ++res.compile_cache.refused;
        return false;
      }
    }

    if (!full_clean && clean.empty()) {
      return false;  // nothing restorable — leave the destination library untouched
    }
    auto eligible = [&](const Graph_row& row) {
      return full_clean
             || (!row.owner.empty() && clean.contains(row.owner) && !row.unit_key.empty()
                 && row.unit_key == key_of(row.owner, res));
    };
    std::set<std::string> restored_owners;
    for (const auto& row : expected->rows) {
      if (row.has_body && eligible(row) && !row.owner.empty()) {
        restored_owners.emplace(row.owner);
      }
    }
    auto owner_graph_restored = [&](std::string_view name) {
      const auto owner = owner_of(name, res);
      return clean.contains(owner) && restored_owners.contains(owner);
    };

    // The stale wipe is MANIFEST-SCOPED exactly like compile_cache_prune_graphs:
    // a shared emit lg: dir holds modules that OTHER compiles' units own, and a
    // warm restore must preserve them where a cold compile would (cold merges
    // into the loaded library and only owner-scoped pruning deletes).
    std::set<std::string> owners;
    for (const auto& [unit, key] : res.compile_cache_unit_keys) {
      owners.emplace(unit);
    }
    for (const auto& unit : res.compile_cache_prior_units) {
      owners.emplace(unit);
    }
    const auto owned = [&owners](std::string_view name) {
      return std::any_of(owners.begin(), owners.end(), [&](const auto& unit) { return name_within_unit(name, unit); });
    };
    auto&                    dst = livehd::Hhds_graph_library::instance(lib_path);
    std::vector<std::string> stale;
    for (const auto gid : dst.all_io_gids()) {
      if (auto io = dst.find_io(gid); io && owned(io->get_name())) {
        stale.emplace_back(io->get_name());
      }
    }
    for (const auto& name : stale) {
      dst.delete_graphio(name);
    }
    // Restored names are committed to `res` only when this function completes:
    // graph_pipeline_and_emits excludes those names from the recipe passes, the
    // latch-contract check, and pass.formal, so a half-restored list surviving
    // a failure would exempt the freshly re-lowered fallback graphs from all
    // three — and the store would then cache the unoptimized result.
    std::vector<std::string> restored_names;
    std::set<std::string>    eligible_names;
    for (size_t i = 0; i < actual.size(); ++i) {
      const auto& row = actual[i];
      if (row.has_body && eligible(expected->rows[i])) {
        eligible_names.insert(row.name);
        restored_names.push_back(row.name);
      }
    }
    // Merge performs the cross-library Sub-Gid remap that a definition-local
    // copy cannot: collision-probed name Gids can differ between the cached
    // and fresh libraries. Load the validated generation, then remove stale
    // dirty bodies before lowering recreates them. Existing foreign bodies in
    // a shared destination are keep-ours; bodyless declarations remain
    // available to restored definitions.
    dst.load_merge(res.compile_cache_scope + "/lg");
    for (const auto& row : actual) {
      if (row.has_body && !eligible_names.contains(row.name)) {
        dst.delete_graphio(row.name);
      }
    }
    const size_t restored_bodies = eligible_names.size();
    // Scoped load: a cold compile's var holds only this closure's graphs, so
    // foreign modules living in a shared destination library must not leak
    // into var (they would ride every emit). They stay in the library itself.
    for (const hhds::Gid id : dst.all_gids()) {
      auto g = dst.get_graph(id);
      if (g && !owner_of(g->get_name(), res).empty()) {
        var.add(g);
      }
    }
    res.compile_cache.hits += restored_bodies;

    if (!full_clean && restored_bodies != 0) {
      // Replace clean post-parse file trees with their cached post-upass file
      // and derived-lambda trees. They remain visible to constprop/inlining for
      // dirty callers, while upass/tolg skip rebuilding them. The cold order is
      // handed to pass.upass, which can restore it after dirty lambda bodies
      // have been extracted (those bodies do not exist yet here).
      std::map<std::string, std::shared_ptr<Lnast>> cached;
      for (size_t i = 0; i < expected->lnast_order.size(); ++i) {
        const auto& name = expected->lnast_order[i];
        if (owner_graph_restored(name)) {
          auto ln = load_compact_lnast(res.compile_cache_scope + "/lg/lowered_ln/" + lowered_lnast_dir(i), name);
          cached.emplace(name, std::move(ln));
        }
      }
      // Name -> queue, not name -> tree: duplicate top-module names legally
      // coexist (non-imported collisions are tolerated upstream), and a plain
      // map would silently DROP every tree after the first per name while also
      // flipping cold's last-wins registry to first-wins.
      std::map<std::string, std::deque<std::shared_ptr<Lnast>>> current;
      for (auto& ln : var.lnasts) {
        current[std::string(ln->get_top_module_name())].push_back(ln);
      }
      auto take_current = [&](const std::string& name) -> std::shared_ptr<Lnast> {
        auto it = current.find(name);
        if (it == current.end() || it->second.empty()) {
          return {};
        }
        auto ln = std::move(it->second.front());
        it->second.pop_front();
        if (it->second.empty()) {
          current.erase(it);
        }
        return ln;
      };
      std::vector<std::shared_ptr<Lnast>> ordered;
      ordered.reserve(expected->lnast_order.size() + var.lnasts.size());
      for (const auto& name : expected->lnast_order) {
        if (owner_graph_restored(name)) {
          auto it = cached.find(name);
          if (it == cached.end()) {
            ++res.compile_cache.refused;
            return false;
          }
          it->second->set_upass_converged(true);
          it->second->set_graph_restored(true);
          ordered.push_back(std::move(it->second));
          cached.erase(it);
          (void)take_current(name);
        } else if (auto ln = take_current(name)) {
          // A dirty file-level root occupies its former cold position. Cached
          // derived trees of that owner are skipped and re-extracted by upass.
          ordered.push_back(std::move(ln));
        }
      }
      // New roots have no cached position. Their original parse order is the
      // deterministic tie-break after the prior generation's units.
      for (const auto& ln : var.lnasts) {
        if (auto rest = take_current(std::string(ln->get_top_module_name()))) {
          ordered.push_back(std::move(rest));
        }
      }
      var.lnasts           = std::move(ordered);
      var.lnast_order_hint = expected->lnast_order;
    }
    res.compile_cache_restored_graphs = std::move(restored_names);
    const size_t total_bodies = std::count_if(actual.begin(), actual.end(), [](const Graph_row& row) { return row.has_body; });
    const bool   total        = full_clean && restored_bodies == total_bodies && restored_bodies != 0;
    if (total) {
      // Total restore: pass.formal is handed no graph, so replaying the stored
      // set reproduces the cold diagnostics exactly. A partial restore was
      // already refused above when the generation carries any.
      replay_pipeline_diags(*expected);
    }
    return total;
  } catch (...) {
    ++res.compile_cache.refused;
    return false;
  }
}

bool compile_cache_overlay_clean_graphs(Result& res, Eprp_var& var, const std::string& lib_path) {
  if (res.compile_cache_overlay_graphs.empty()) {
    return true;
  }
  Phase_timer phase(res, "compile.cache.lg_overlay");
  try {
    auto expected = read_graph_inventory(res);
    if (!expected) {
      ++res.compile_cache.refused;
      return false;
    }
    check_ir_body_magic(res.compile_cache_scope + "/lg", "graph_", kHhdsGraphBodyMagic, "compile cache lg:");
    auto& cache      = livehd::Hhds_graph_library::instance(res.compile_cache_scope + "/lg");
    bool  digestable = true;
    auto  actual     = graph_rows(cache, res, digestable);
    if (!digestable || actual.size() != expected->rows.size()) {
      ++res.compile_cache.refused;
      return false;
    }
    for (size_t i = 0; i < actual.size(); ++i) {
      const auto& a = actual[i];
      const auto& e = expected->rows[i];
      if (a.name != e.name || a.interface_hash != e.interface_hash || a.has_body != e.has_body || a.h0 != e.h0 || a.h1 != e.h1
          || a.owner != e.owner) {
        ++res.compile_cache.refused;
        return false;
      }
    }

    const std::set<std::string> wanted(res.compile_cache_overlay_graphs.begin(), res.compile_cache_overlay_graphs.end());
    std::vector<std::string>    var_names;
    var_names.reserve(var.graphs.size());
    for (const auto& graph : var.graphs) {
      var_names.emplace_back(graph ? std::string(graph->get_name()) : std::string{});
    }
    auto& dst = livehd::Hhds_graph_library::instance(lib_path);
    for (const auto& name : wanted) {
      dst.delete_graphio(name);
    }
    // load_merge remaps every cached Sub Gid through the destination's actual
    // name table. Non-overlay bodies already present in dst are keep-ours.
    dst.load_merge(res.compile_cache_scope + "/lg");
    // load_merge replaces each selected Graph object after its stable name is
    // reintroduced. Refresh Eprp_var's shared_ptrs before emit/store; retaining
    // the deleted fresh objects would make downstream traversal assert even
    // though Sub Gids in dirty parents already resolve to the merged bodies.
    for (size_t i = 0; i < var.graphs.size(); ++i) {
      if (!wanted.contains(var_names[i])) {
        continue;
      }
      auto io = dst.find_io(var_names[i]);
      if (!io || !io->has_graph()) {
        ++res.compile_cache.refused;
        return false;
      }
      var.graphs[i] = io->get_graph();
    }
    res.compile_cache_overlay_graphs.clear();
    return true;
  } catch (...) {
    ++res.compile_cache.refused;
    return false;
  }
}

void compile_cache_prune_graphs(const Eprp_var& var, const Result& res, const std::string& lib_path) {
  auto&                 lib = livehd::Hhds_graph_library::instance(lib_path);
  std::set<std::string> live;
  for (const auto& graph : var.graphs) {
    if (!graph) {
      continue;
    }
    live.emplace(graph->get_name());
    for (const auto node : graph->body().nodes()) {
      if (livehd::graph_util::type_op_of(node) == Ntype_op::Sub) {
        if (auto child = node.get_subnode_io()) {
          live.emplace(child->get_name());
        }
      }
    }
  }
  // Pruning is MANIFEST-scoped: this compile may only delete artifacts of its
  // own units — the current closure plus units of this same scope's PREVIOUS
  // generation (an import edit legitimately drops a unit from the closure, and
  // its modules are then ghosts). A shared emit lg: dir also accumulates
  // modules that OTHER compiles' units own; those are someone else's live
  // closure and deleting them would corrupt every later consumer that links
  // against the library.
  std::set<std::string> owners;
  for (const auto& [unit, key] : res.compile_cache_unit_keys) {
    owners.emplace(unit);
  }
  for (const auto& unit : res.compile_cache_prior_units) {
    owners.emplace(unit);
  }
  for (const auto& ln : var.lnasts) {  // covers cache-disabled compiles too
    if (ln) {
      owners.emplace(ln->get_top_module_name());
    }
  }
  const auto owned = [&owners](std::string_view name) {
    for (const auto& unit : owners) {
      if (name_within_unit(name, unit)) {
        return true;
      }
    }
    return false;
  };
  std::vector<std::string> stale;
  for (const auto gid : lib.all_io_gids()) {
    if (auto io = lib.find_io(gid); io && !live.contains(std::string(io->get_name())) && owned(io->get_name())) {
      stale.emplace_back(io->get_name());
    }
  }
  for (const auto& name : stale) {
    lib.delete_graphio(name);
  }
}

void compile_cache_store_graphs(Options& opts, Result& res, const Eprp_var& var, const std::string& lib_path) {
  if (res.compile_cache_scope.empty() || res.compile_cache_closure_key.empty() || var.graphs.empty()) {
    return;
  }
  // A failing generation must stay cold. A warm restore skips the whole graph
  // pipeline — pass.formal included — so caching a run whose formal stage
  // refuted (or that errored in any way) would flip the rerun to a silent
  // pass instead of reproducing the same verdicts.
  if (livehd::diag::sink().has_errors()) {
    return;
  }
  // Same reproduce-the-verdicts rule for WARNING-level outcomes, but carried
  // rather than refused: a restored graph never re-enters upass/tolg/cprop/
  // pass.formal, so a warm run would otherwise print strictly fewer records
  // than the cold run of the same sources. Refusing to store instead (the old
  // `pass.formal` rule) cost such a design every byte of graph reuse forever —
  // minion's 24 `onehot-deferred` warnings made its whole compile cache
  // parse-only.
  //
  // [diag_mark, diag_end) is EXACTLY the window a warm restore skips, and both
  // ends are load-bearing. Before the mark is the front end (Tier A: re-emitted
  // by whichever unit reparses) plus the deferred-source materialization, which
  // runs warm too. After the end are the EMITS — cgen verilog/sim/pyrope and
  // lg.save — which also run warm, and whose diagnostics depend on the `--emit`
  // slots that `context_descriptor` deliberately leaves out of the cache key:
  // storing them would replay an `inou.cgen.sim` warning onto a later run that
  // asked for no sim emit at all.
  //
  // Every non-error severity rides, not just warnings: an `info` progress
  // record from a pipeline pass (pass.bitfuzz) is skipped by a warm restore in
  // exactly the same way. Errors cannot reach here (has_errors() bailed above);
  // the guard keeps it that way if that ever changes.
  std::vector<livehd::diag::Diagnostic> pipeline_diags;
  {
    const auto&  records = livehd::diag::sink().records();
    const size_t begin   = std::min(res.compile_cache_diag_mark, records.size());
    const size_t end     = std::min(res.compile_cache_diag_end, records.size());
    for (size_t i = begin; i < end; ++i) {
      const auto& record = records[i];
      if (record.severity != livehd::diag::Severity::error) {
        pipeline_diags.push_back(record);
      }
    }
  }
  Phase_timer phase(res, "compile.cache.lg_store");
  const auto  temp = res.compile_cache_scope + std::format("/lg.new.{}", static_cast<long>(::getpid()));
  try {
    std::error_code ec;
    fs::remove_all(temp, ec);
    auto&      src = livehd::Hhds_graph_library::instance(lib_path);
    const bool already_saved
        = std::any_of(opts.emit_dirs.begin(), opts.emit_dirs.end(), [](const auto& out) { return out.kind == "lg"; });
    if (!already_saved) {
      // An lg: emit has already serialized this exact final library. Other
      // output shapes keep it memory-only, so serialize once before snapshot.
      livehd::Hhds_graph_library::save(lib_path);
    }
    bool digestable = true;
    auto rows       = graph_rows(src, res, digestable);
    if (!digestable) {
      ++res.compile_cache.refused;
      fs::remove_all(temp, ec);
      return;
    }
    const auto files = artifact_files(lib_path);
    // Snapshot the already-serialized final GraphLibrary. Rebuilding a second
    // in-memory library with copy_from() and serializing it again dominated
    // cold Backend time; a byte copy is exact, independently owned, and keeps
    // a later overwrite of the user's lg: output from mutating the cache.
    copy_tree_files(lib_path, temp);
    if (!var.lnasts.empty()) {
      const auto lowered_dir = fs::path(temp) / "lowered_ln";
      fs::create_directories(lowered_dir);
      for (size_t i = 0; i < var.lnasts.size(); ++i) {
        // Restored clean graphs skip both upass and tolg, so bulky CONCRETE
        // `mod`/`pipe` statement bodies — always Sub instances, never inlined
        // or comptime-evaluated — are stored metadata-only (their authoritative
        // form is the cached LGraph). A TEMPLATE module is different: a dirty
        // neighbor needs its body to recreate a concrete specialization, so it
        // must remain available to the function registry. A clean `comb` keeps
        // its body for the same reason (inline/comptime calls). This mirrors the
        // ln: import rule in pyrope_parse.
        const auto lk        = var.lnasts[i]->get_lambda_kind();
        const bool meta_only = !var.lnasts[i]->is_template() && (lk == "mod" || lk == "pipe");
        save_compact_lnast(var.lnasts[i], lowered_dir / lowered_lnast_dir(i), meta_only);
      }
    }
    write_graph_inventory(temp + "/graph_inventory.json", res, rows, var.lnasts, files, pipeline_diags);
    replace_dir(temp, res.compile_cache_scope + "/lg");
  } catch (...) {
    std::error_code cleanup_ec;
    fs::remove_all(temp, cleanup_ec);  // do not strand a full library copy in the scope
    ++res.compile_cache.store_failed;
    throw;
  }
}

}  // namespace lhd
