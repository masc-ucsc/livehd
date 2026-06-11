//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Unified compile error/warning surface. See the LiveHD docs.
//
// One Diagnostic == one compile error/warning/note. The Sink is a
// process-global collector that serializes each record as one JSONL line
// (machine-first, agent-consumable) and can render human text on demand.
//
// This is a leaf library (std + absl only) so pass/common, upass, and inou/prp
// can all route their errors through it without a dependency cycle.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"

namespace livehd::diag {

enum class Severity { error, warning, note };

std::string_view to_string(Severity s);

// Source location. Every field is optional; a default-constructed Span is
// "unknown" and serializes as JSON null. Pre-sourcemap ([[1f]]) sites populate
// `file`/`start_byte`/`end_byte` best-effort (inou/prp) or leave it null
// (upass/lgraph). Post-sourcemap sites add `source_id` + resolved line/col.
struct Span {
  std::optional<uint64_t> source_id  = {};  // hhds SourceId ([[1f]])
  std::optional<uint32_t> file_id    = {};
  std::string             file       = {};  // pre-1f fallback path
  std::optional<uint64_t> start_byte = {};
  std::optional<uint64_t> end_byte   = {};
  std::optional<uint32_t> start_line = {};
  std::optional<uint32_t> start_col  = {};
  std::optional<uint32_t> end_line   = {};
  std::optional<uint32_t> end_col    = {};

  [[nodiscard]] bool is_null() const {
    return !source_id && !file_id && file.empty() && !start_byte && !start_line;
  }
};

struct Note {
  std::string message;
  Span        span;
};

struct Diagnostic {
  Severity                 severity = Severity::error;
  std::string              code     = "error";     // stable, greppable id (kebab-case)
  std::string              category = "internal";  // syntax|name|type|bitwidth|unsupported|internal
  std::string              pass;                    // dotted origin, e.g. "upass.attributes"
  std::string              message;                 // one line, no trailing period, no location
  Span                     span{};
  std::string              hint{};                   // optional one-line suggestion
  std::vector<std::string> see{};                    // optional doc/spec pointers
  std::vector<Note>        notes{};                  // optional secondary locations
};

// One JSONL line (no trailing newline). `seq` is the per-run monotonic id.
std::string to_jsonl(const Diagnostic& d, uint64_t seq);

// Human-readable, clang/rust-style. Degrades to a single line when span is null;
// never fabricates a location.
std::string to_text(const Diagnostic& d);

// Process-global diagnostic collector. Mirrors how `Pass::eprp` is a static
// singleton reachable from every pass.
class Sink {
public:
  // Append + accumulate, then write per the active output config.
  // Deduplicates (code, span, message) within a step.
  //
  // Output is controlled by the env var LIVEHD_DIAG (read once, overridable via
  // the setters below — the CLI [[1y]] will set them explicitly):
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
  Json        json_out_     = Json::uninit;
  std::string json_path_;
  std::FILE*  json_fp_      = nullptr;  // owned when json_out_ == file
  bool        human_stderr_ = false;
  bool        configured_   = false;    // env read or setter called
};

// The active process-global sink.
Sink& sink();

}  // namespace livehd::diag
