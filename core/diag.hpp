//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Unified compile error/warning surface. See the LiveHD docs.
//
// One Diagnostic == one compile error/warning/note. The Sink is a
// process-global collector that serializes each record as one JSONL line
// (machine-first, agent-consumable) and can render human text on demand.
//
// This is a leaf library (std + absl + hhds only) so pass/common, upass, and
// inou/prp can all route their errors through it without a dependency cycle.

#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "hhds/source_locator.hpp"

namespace livehd::diag {

enum class Severity { error, warning, note };

std::string_view to_string(Severity s);

// Source location: hhds owns the resolved-span type (the locator mints,
// resolves, and renders it — see hhds/source_locator.hpp). Every field is
// optional; a default-constructed Span is "unknown" and serializes as JSON
// null. Resolve one with `ln->span_of(nid)` (primary anchor) or
// `ln->spans_of(nid)` (primary + the combined id's related sites).
using Span = hhds::Source_span;

struct Note {
  std::string message;
  Span        span;
};

// The related (secondary) anchors of a resolved combined id as notes — the
// loose-Diagnostic counterpart of Builder::at(Resolved_spans).
inline std::vector<Note> notes_from(const hhds::Source_locator::Resolved_spans& rs, std::string_view message = "related source") {
  std::vector<Note> out;
  out.reserve(rs.related.size());
  for (const auto& r : rs.related) {
    out.push_back(Note{std::string(message), r});
  }
  return out;
}

// (code, category) pair for pass-local emit helpers that also take a variadic
// format: a distinct braced-init type — `error_at(nid, {"no-clock", "time"},
// fmt, ...)` — can never be swallowed by a `(nid, fmt, args...)` overload the
// way two leading string_view parameters can (a string literal converts to
// both string_view AND format_string, and perfect-forwarded identity args
// out-rank either conversion, silently rebinding or recursing).
struct Id {
  std::string_view code;
  std::string_view category;
};

// The pinned category vocabulary — Sink::emit asserts membership in debug
// builds, so a typo'd or invented category fails the first test that hits it.
//   syntax      - parse / structural form
//   name        - undeclared, out-of-scope, shadowing, duplicates
//   type        - kind mismatches, tuple/bundle shape, attribute rules
//   bitwidth    - range / width / overflow
//   comptime    - comptime evaluation (cassert, non-comptime where required)
//   time        - pipeline stages / register timing contract
//   io          - files, paths, options, emit targets
//   unsupported - recognized but not implemented
//   internal    - compiler invariant broke (a livehd bug, not a user error)
inline constexpr std::array<std::string_view, 9> kCategories
    = {"syntax", "name", "type", "bitwidth", "comptime", "time", "io", "unsupported", "internal"};

[[nodiscard]] inline bool is_known_category(std::string_view c) {
  for (const auto k : kCategories) {
    if (c == k) {
      return true;
    }
  }
  return false;
}

struct Diagnostic {
  Severity                 severity = Severity::error;
  std::string              code     = "error";     // stable, greppable id (kebab-case)
  std::string              category = "internal";  // syntax|name|type|bitwidth|unsupported|internal
  std::string              pass;                   // dotted origin, e.g. "upass.attributes"
  std::string              message;                // one line, no trailing period, no location
  Span                     span{};
  std::string              hint{};   // optional one-line suggestion
  std::vector<std::string> see{};    // optional doc/spec pointers
  std::vector<Note>        notes{};  // optional secondary locations
};

// One JSONL line (no trailing newline). `seq` is the per-run monotonic id.
std::string to_jsonl(const Diagnostic& d, uint64_t seq);

// Human-readable, clang/rust-style. Degrades to a single line when span is null;
// never fabricates a location. With a locator, the message line (and each note)
// is followed by a caret-underlined source excerpt rendered from the locator's
// hash-validated bytes (hhds::render_excerpt) — omitted when bytes are absent.
std::string to_text(const Diagnostic& d);
std::string to_text(const Diagnostic& d, const hhds::Source_locator* sl);

// Process-global diagnostic collector. Mirrors how `Pass::eprp` is a static
// singleton reachable from every pass.
class Sink {
public:
  // Append + accumulate, then write per the active output config.
  // Deduplicates (code, span, message) within a step.
  //
  // Output is controlled by the env var LIVEHD_DIAG (read once, overridable via
  // the setters below — the CLI will set them explicitly):
  //   unset / "both"  -> human (`to_text`) to stderr AND JSONL to diag.jsonl  (default)
  //   "jsonl"         -> JSONL to diag.jsonl only (no human)
  //   "stderr" / "-"  -> human to stderr only (no file)
  //   "off" / "none"  -> silent
  //   <path>          -> human to stderr AND JSONL to <path>
  // The JSON file is truncated once per process and flushed after every record
  // (crash-safe: a record survives even if the process later aborts).
  void emit(Diagnostic d);

  // Emit at error severity, then throw to abort the step.
  [[noreturn]] void fatal(Diagnostic d);

  // Stage a rich Diagnostic to be emitted by the next flush() — lets a call site
  // that has full (code/category/span) context hand it to the generic throw path
  // (Eprp::parser_error -> parser_error_int -> flush) without double-reporting.
  void stage(Diagnostic d);
  // Emit the staged Diagnostic if present, else a generic one built from `text`.
  // Called by the parser error/warn sinks so every thrown error reports once.
  void flush(Severity sev, std::string_view text);

  [[nodiscard]] bool   has_errors() const { return error_count_ > 0; }
  [[nodiscard]] size_t count(Severity s) const;

  // Accumulated records this run (for tests, the verifier, and the renderer).
  [[nodiscard]] const std::vector<Diagnostic>& records() const { return records_; }

  // Reset all state (records, counters, dedup, seq, staged) and close any file.
  void clear();

  // Tag subsequent records with a pipeline step (resets the per-step dedup set).
  void set_step(std::string_view step);

  // Explicit overrides (take precedence over the env var; used by the CLI and
  // by tests). `path`: "" / "off" / "none" disables JSON, "-" / "stderr" -> stderr,
  // else a file path. `set_human_stderr` toggles the human channel.
  void set_jsonl_path(std::string_view path);
  void set_human_stderr(bool on);
  // Render the stderr channel as JSONL records instead of human text (the
  // CLI's --diag-fmt jsonl). set_human_stderr still gates the channel on/off.
  void set_stderr_jsonl(bool on);

  // Source-excerpt provider for the human channel. The pointed-to locator must
  // outlive its registration — prefer the RAII Locator_scope below, which the
  // emitting stages (parse, upass, tolg) wrap around their per-artifact
  // locator. Null = no excerpts (locations still print).
  void                                      set_locator(const hhds::Source_locator* sl) noexcept { locator_ = sl; }
  [[nodiscard]] const hhds::Source_locator* locator() const noexcept { return locator_; }

private:
  void init_output();  // lazily read env (once) unless overridden
  void open_json_file();
  void write_json(const std::string& line);

  std::vector<Diagnostic>     records_;
  absl::flat_hash_set<size_t> seen_;  // per-step dedup keys
  std::optional<Diagnostic>   staged_;
  std::string                 step_;
  uint64_t                    seq_         = 0;
  size_t                      error_count_ = 0;
  size_t                      warn_count_  = 0;
  size_t                      note_count_  = 0;

  enum class Json { uninit, none, stderr_, file };
  Json        json_out_ = Json::uninit;
  std::string json_path_;
  std::FILE*  json_fp_      = nullptr;  // owned when json_out_ == file
  bool        human_stderr_ = false;
  bool        stderr_jsonl_ = false;  // stderr channel renders JSONL, not text
  bool        configured_   = false;  // env read or setter called

  const hhds::Source_locator* locator_ = nullptr;  // excerpt provider (not owned)
};

// The active process-global sink.
Sink& sink();

// Scoped excerpt provider: registers `sl` with the sink for the lifetime of
// the scope and restores the previous one on exit (exception-safe — fatal()
// throws through it). Used per-artifact: the parse holds the unit's locator,
// the upass stages hold the current Lnast's, tolg the graph's.
class Locator_scope {
public:
  explicit Locator_scope(const hhds::Source_locator* sl) : prev_(sink().locator()) { sink().set_locator(sl); }
  Locator_scope(const Locator_scope&)            = delete;
  Locator_scope& operator=(const Locator_scope&) = delete;
  ~Locator_scope() { sink().set_locator(prev_); }

private:
  const hhds::Source_locator* prev_;
};

// Fluent construction for the common emit shapes. Code + category + pass are
// mandatory up front (the greppable identity of the diagnostic); span, message,
// hint, and notes chain. Terminal calls:
//   .emit()  - report and continue (the severity decides if it counts as error)
//   .fatal() - report, then throw std::runtime_error to abort the step
//   .stage() - park the record for the Eprp::parser_error flush path (the
//              caller throws parser_error itself; see Sink::stage)
//
//   diag::err("upass.bitwidth", "negative-shift", "bitwidth")
//       .at(ln->span_of(nid))
//       .msg("shift amount is always negative (range [{}, {}])", lo, hi)
//       .hint("a shift / bit-select count must be >= 0")
//       .emit();
class Builder {
public:
  Builder(Severity sev, std::string_view pass, std::string_view code, std::string_view category) {
    d_.severity = sev;
    d_.pass     = std::string(pass);
    d_.code     = std::string(code);
    d_.category = std::string(category);
  }

  Builder& at(Span span) {
    d_.span = std::move(span);
    return *this;
  }

  // Primary span + the combined id's related sites as notes, in one call
  // (`ln->spans_of(nid)` / `Source_locator::resolve_spans`).
  Builder& at(const hhds::Source_locator::Resolved_spans& rs, std::string_view note_msg = "related source") {
    d_.span = rs.primary;
    for (const auto& r : rs.related) {
      d_.notes.push_back(Note{std::string(note_msg), r});
    }
    return *this;
  }

  template <typename... Args>
  Builder& msg(std::format_string<Args...> format, Args&&... args) {
    d_.message = std::format(format, std::forward<Args>(args)...);
    return *this;
  }

  Builder& hint(std::string_view h) {
    d_.hint = std::string(h);
    return *this;
  }

  Builder& see(std::string_view s) {
    d_.see.emplace_back(s);
    return *this;
  }

  Builder& note(std::string_view m, Span span = {}) {
    d_.notes.push_back(Note{std::string(m), std::move(span)});
    return *this;
  }

  Diagnostic build() { return std::move(d_); }

  void              emit();
  [[noreturn]] void fatal();
  void              stage();

private:
  Diagnostic d_;
};

inline Builder err(std::string_view pass, std::string_view code, std::string_view category) {
  return Builder(Severity::error, pass, code, category);
}

inline Builder warn(std::string_view pass, std::string_view code, std::string_view category) {
  return Builder(Severity::warning, pass, code, category);
}

}  // namespace livehd::diag
