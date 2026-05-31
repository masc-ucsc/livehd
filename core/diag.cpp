//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "diag.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace livehd::diag {

std::string_view to_string(Severity s) {
  switch (s) {
    case Severity::error: return "error";
    case Severity::warning: return "warning";
    case Severity::note: return "note";
  }
  return "error";
}

namespace {

void json_escape(std::string& out, std::string_view s) {
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
}

void append_kv_str(std::string& out, std::string_view key, std::string_view val) {
  out += '"';
  out += key;
  out += "\":\"";
  json_escape(out, val);
  out += '"';
}

void append_span(std::string& out, const Span& sp) {
  out += "\"span\":";
  if (sp.is_null()) {
    out += "null";
    return;
  }
  out += '{';
  bool first = true;
  auto comma  = [&] {
    if (!first) {
      out += ',';
    }
    first = false;
  };
  auto num = [&](std::string_view k, auto v) {
    comma();
    out += '"';
    out += k;
    out += "\":";
    out += std::to_string(v);
  };
  if (sp.source_id) {
    num("source_id", *sp.source_id);
  }
  if (sp.file_id) {
    num("file_id", *sp.file_id);
  }
  if (!sp.file.empty()) {
    comma();
    append_kv_str(out, "file", sp.file);
  }
  if (sp.start_byte) {
    num("start_byte", *sp.start_byte);
  }
  if (sp.end_byte) {
    num("end_byte", *sp.end_byte);
  }
  if (sp.start_line) {
    num("start_line", *sp.start_line);
  }
  if (sp.start_col) {
    num("start_col", *sp.start_col);
  }
  if (sp.end_line) {
    num("end_line", *sp.end_line);
  }
  if (sp.end_col) {
    num("end_col", *sp.end_col);
  }
  out += '}';
}

size_t dedup_key(const Diagnostic& d) {
  std::string k;
  k.reserve(d.code.size() + d.message.size() + 32);
  k += d.code;
  k += '\x01';
  k += d.message;
  k += '\x01';
  if (d.span.source_id) {
    k += std::to_string(*d.span.source_id);
  }
  k += '\x01';
  k += d.span.file;
  k += '\x01';
  if (d.span.start_byte) {
    k += std::to_string(*d.span.start_byte);
  }
  return std::hash<std::string>{}(k);
}

}  // namespace

std::string to_jsonl(const Diagnostic& d, uint64_t seq) {
  std::string out;
  out.reserve(256);
  out += "{\"schema_version\":1,";
  append_kv_str(out, "severity", to_string(d.severity));
  out += ',';
  append_kv_str(out, "code", d.code);
  out += ',';
  append_kv_str(out, "category", d.category);
  out += ',';
  append_kv_str(out, "pass", d.pass);
  out += ',';
  append_kv_str(out, "message", d.message);
  out += ',';
  append_span(out, d.span);
  if (!d.hint.empty()) {
    out += ',';
    append_kv_str(out, "hint", d.hint);
  }
  if (!d.see.empty()) {
    out += ",\"see\":[";
    for (size_t i = 0; i < d.see.size(); ++i) {
      if (i) {
        out += ',';
      }
      out += '"';
      json_escape(out, d.see[i]);
      out += '"';
    }
    out += ']';
  }
  if (!d.notes.empty()) {
    out += ",\"notes\":[";
    for (size_t i = 0; i < d.notes.size(); ++i) {
      if (i) {
        out += ',';
      }
      out += "{\"severity\":\"note\",";
      append_kv_str(out, "message", d.notes[i].message);
      out += ',';
      append_span(out, d.notes[i].span);
      out += '}';
    }
    out += ']';
  }
  out += ",\"seq\":";
  out += std::to_string(seq);
  out += '}';
  return out;
}

namespace {
// "file:line:col" (or "file" / "file @byte N"), empty when the span is null.
std::string loc_string(const Span& sp) {
  if (sp.is_null()) {
    return {};
  }
  std::string out;
  if (!sp.file.empty()) {
    out += sp.file;
  } else if (sp.file_id) {
    out += "file#";
    out += std::to_string(*sp.file_id);
  } else if (sp.source_id) {
    out += "src#";
    out += std::to_string(*sp.source_id);
  }
  if (sp.start_line) {
    out += ':';
    out += std::to_string(*sp.start_line);
    if (sp.start_col) {
      out += ':';
      out += std::to_string(*sp.start_col);
    }
  } else if (sp.start_byte) {
    out += " @byte ";
    out += std::to_string(*sp.start_byte);
  }
  return out;
}
}  // namespace

// gcc/clang-style: `livehd:<file>:<line>:<col>:<severity>:<message>` (the
// location segment is omitted when the span is null, like `livehd:error:…`).
// `error` = compilation cannot proceed; `warning` = proceeds but a likely issue;
// `note` = a secondary location attached to the diagnostic. `help` = the hint.
std::string to_text(const Diagnostic& d) {
  // Prefix shared by the message line + its help/see continuations.
  std::string prefix = "livehd:";
  auto        loc    = loc_string(d.span);
  if (!loc.empty()) {
    prefix += loc;
    prefix += ':';
  }

  std::string out;
  out += prefix;
  out += to_string(d.severity);
  out += ':';
  out += d.message;
  if (!d.hint.empty()) {
    out += '\n';
    out += prefix;
    out += "help:";
    out += d.hint;
  }
  for (const auto& s : d.see) {
    out += '\n';
    out += prefix;
    out += "see:";
    out += s;
  }
  for (const auto& n : d.notes) {
    out += "\nlivehd:";
    auto nloc = loc_string(n.span);
    if (!nloc.empty()) {
      out += nloc;
      out += ':';
    }
    out += "note:";
    out += n.message;
  }
  return out;
}

void Sink::init_output() {
  if (configured_) {
    return;
  }
  configured_ = true;

  // LIVEHD_DIAG selects the channels (see diagnostics.md §2.1):
  //   unset / "both"  -> human to stderr AND JSONL to diag.jsonl   (default)
  //   "jsonl"         -> JSONL to diag.jsonl only (no human)
  //   "stderr" / "-"  -> human to stderr only (no file)
  //   "off" / "none"  -> silent
  //   <path>          -> human to stderr AND JSONL to <path>
  const char*      env = std::getenv("LIVEHD_DIAG");
  std::string_view v   = (env && env[0]) ? env : "both";
  if (v == "off" || v == "none") {
    human_stderr_ = false;
    json_out_     = Json::none;
  } else if (v == "jsonl") {
    human_stderr_ = false;
    json_out_     = Json::file;
    json_path_    = "diag.jsonl";
  } else if (v == "stderr" || v == "-") {
    human_stderr_ = true;
    json_out_     = Json::none;
  } else if (v == "both") {
    human_stderr_ = true;
    json_out_     = Json::file;
    json_path_    = "diag.jsonl";
  } else {
    human_stderr_ = true;
    json_out_     = Json::file;
    json_path_    = v;
  }
}

void Sink::open_json_file() {
  if (json_fp_ != nullptr || json_out_ != Json::file) {
    return;
  }
  // Truncate once per process so each run starts with a fresh error file.
  json_fp_ = std::fopen(json_path_.c_str(), "w");
}

void Sink::write_json(const std::string& line) {
  if (json_out_ == Json::stderr_) {
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
  } else if (json_out_ == Json::file) {
    open_json_file();
    if (json_fp_ != nullptr) {
      std::fputs(line.c_str(), json_fp_);
      std::fputc('\n', json_fp_);
      std::fflush(json_fp_);  // crash-safe: survive a later abort
    }
  }
}

void Sink::emit(Diagnostic d) {
  auto key = dedup_key(d);
  if (!seen_.insert(key).second) {
    return;  // already reported this (code, span, message) in the current step
  }
  switch (d.severity) {
    case Severity::error: ++error_count_; break;
    case Severity::warning: ++warn_count_; break;
    case Severity::note: ++note_count_; break;
  }
  init_output();
  if (json_out_ != Json::none) {
    write_json(to_jsonl(d, seq_));
  }
  if (human_stderr_) {
    std::fputs(to_text(d).c_str(), stderr);
    std::fputc('\n', stderr);
  }
  ++seq_;
  records_.push_back(std::move(d));
}

void Sink::fatal(Diagnostic d) {
  d.severity   = Severity::error;
  auto message = d.message;
  emit(std::move(d));
  throw std::runtime_error(message);
}

size_t Sink::count(Severity s) const {
  switch (s) {
    case Severity::error: return error_count_;
    case Severity::warning: return warn_count_;
    case Severity::note: return note_count_;
  }
  return 0;
}

void Sink::clear() {
  records_.clear();
  seen_.clear();
  step_.clear();
  seq_         = 0;
  error_count_ = 0;
  warn_count_  = 0;
  note_count_  = 0;
  staged_.reset();
  if (json_fp_ != nullptr) {
    std::fclose(json_fp_);
    json_fp_ = nullptr;
  }
}

void Sink::set_step(std::string_view step) {
  step_ = step;
  seen_.clear();
}

void Sink::set_jsonl_path(std::string_view path) {
  configured_ = true;
  if (json_fp_ != nullptr) {
    std::fclose(json_fp_);
    json_fp_ = nullptr;
  }
  if (path.empty() || path == "off" || path == "none") {
    json_out_ = Json::none;
  } else if (path == "stderr" || path == "-") {
    json_out_ = Json::stderr_;
  } else {
    json_out_  = Json::file;
    json_path_ = path;
  }
}

void Sink::set_human_stderr(bool on) {
  configured_   = true;
  human_stderr_ = on;
}

void Sink::stage(Diagnostic d) { staged_ = std::move(d); }

void Sink::flush(Severity sev, std::string_view text) {
  if (staged_) {
    emit(std::move(*staged_));
    staged_.reset();
  } else {
    emit(Diagnostic{.severity = sev,
                    .code     = (sev == Severity::warning ? "warning" : "error"),
                    .category = "internal",
                    .pass     = "parser",
                    .message  = std::string(text)});
  }
}

Sink& sink() {
  static Sink s;
  return s;
}

}  // namespace livehd::diag
