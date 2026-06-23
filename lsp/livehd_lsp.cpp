//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "livehd_lsp.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ci_string.hpp"  // Ci_str_map/Ci_str_set: case-insensitive name matching
#include "diag.hpp"
#include "eprp_var.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "lsp_index.hpp"
#include "str_tools.hpp"  // str_tools::ci_equal / ci_ends_with
#include "pass_upass.hpp"
#include "prp2lnast.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::lsp {

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

// The real stdout fd, dup'd in run_stdio(); all protocol bytes go here. fd 1 is
// repointed at stderr so stray library stdout cannot corrupt the JSON-RPC stream.
int g_out_fd = STDOUT_FILENO;

// Negotiated during `initialize` (utf-8 when the client offers it, else utf-16).
bool g_utf8 = false;

// True when the client announced pull-diagnostics support (textDocument/
// diagnostic). We then serve diagnostics ONLY via pull and suppress the
// publishDiagnostics push — otherwise a client that does both shows every
// diagnostic twice (once pushed, once pulled).
bool g_client_pull = false;

// uri -> last-known buffer text.
std::unordered_map<std::string, std::string> g_docs;

//============================ framed stdio ============================

void write_all(int fd, const char* p, size_t n) {
  while (n != 0) {
    ssize_t w = ::write(fd, p, n);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) {
        continue;
      }
      return;  // broken pipe / closed: give up on this write
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
}

void send_message(const std::string& body) {
  std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  write_all(g_out_fd, header.data(), header.size());
  write_all(g_out_fd, body.data(), body.size());
}

bool read_byte(char& out) {
  for (;;) {
    ssize_t r = ::read(STDIN_FILENO, &out, 1);
    if (r == 1) {
      return true;
    }
    if (r < 0 && errno == EINTR) {
      continue;
    }
    return false;  // EOF or error
  }
}

// Read one Content-Length-framed message body. Returns false on EOF/error.
bool read_message(std::string& body) {
  size_t content_length = 0;
  bool   have_len       = false;

  for (;;) {  // headers, one CRLF-terminated line at a time
    std::string line;
    for (;;) {
      char c;
      if (!read_byte(c)) {
        return false;
      }
      if (c == '\n') {
        break;
      }
      if (c != '\r') {
        line.push_back(c);
      }
    }
    if (line.empty()) {
      break;  // blank line ends the header block
    }
    auto pos = line.find(':');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string val = line.substr(pos + 1);
      // trim leading spaces in the value
      size_t      s   = val.find_first_not_of(" \t");
      if (s != std::string::npos) {
        val = val.substr(s);
      }
      if (key == "Content-Length") {
        // Parse defensively: strtoul wraps a leading '-' to a huge value, so a
        // "Content-Length: -5" would make resize() below throw std::length_error
        // (abort), and an implausibly large length would block the read loop
        // forever (hang). Reject negative / non-numeric / out-of-range and treat
        // it as a missing length rather than crashing.
        errno          = 0;
        char* end      = nullptr;
        const auto raw = std::strtoull(val.c_str(), &end, 10);
        if (end != val.c_str() && errno == 0 && !val.empty() && val.front() != '-') {
          content_length = static_cast<size_t>(raw);
          have_len       = true;
        }
      }
    }
  }

  // A framed body larger than this is implausible for an LSP message; cap it so
  // a bogus/huge Content-Length cannot resize() to an aborting size or stall the
  // read loop waiting for bytes that will never arrive.
  constexpr size_t kMaxBodyBytes = 64u * 1024 * 1024;  // 64 MiB
  if (!have_len || content_length > kMaxBodyBytes) {
    body.clear();
    return have_len ? false : true;
  }
  body.resize(content_length);
  size_t got = 0;
  while (got < content_length) {
    ssize_t r = ::read(STDIN_FILENO, body.data() + got, content_length - got);
    if (r <= 0) {
      if (r < 0 && errno == EINTR) {
        continue;
      }
      return false;
    }
    got += static_cast<size_t>(r);
  }
  return true;
}

//============================ JSON helpers ============================

void write_id(Writer& w, const rapidjson::Value& req) {
  w.Key("id");
  if (!req.HasMember("id")) {
    w.Null();
    return;
  }
  const auto& id = req["id"];
  if (id.IsString()) {
    w.String(id.GetString(), id.GetStringLength());
  } else if (id.IsInt()) {
    w.Int(id.GetInt());
  } else if (id.IsInt64()) {
    w.Int64(id.GetInt64());
  } else if (id.IsUint()) {
    w.Uint(id.GetUint());
  } else if (id.IsUint64()) {
    w.Uint64(id.GetUint64());
  } else {
    w.Null();
  }
}

std::string method_of(const rapidjson::Document& d) {
  if (d.HasMember("method") && d["method"].IsString()) {
    return d["method"].GetString();
  }
  return {};
}

const rapidjson::Value* member(const rapidjson::Value& v, const char* k) {
  if (v.IsObject() && v.HasMember(k)) {
    return &v[k];
  }
  return nullptr;
}

//============================ uri / source ============================

bool ends_with(std::string_view s, std::string_view suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// Decode a file:// URI to a filesystem-ish path (used only for diagnostic spans).
std::string uri_to_path(std::string_view uri) {
  std::string_view s = uri;
  if (s.rfind("file://", 0) == 0) {
    s.remove_prefix(7);
  }
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
          return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
          return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
          return c - 'A' + 10;
        }
        return -1;
      };
      int hi = hex(s[i + 1]);
      int lo = hex(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string module_name_of(std::string_view path) {
  auto             slash = path.find_last_of('/');
  std::string_view base  = (slash == std::string_view::npos) ? path : path.substr(slash + 1);
  auto             dot   = base.find('.');
  if (dot != std::string_view::npos) {
    base = base.substr(0, dot);
  }
  return base.empty() ? std::string("top") : std::string(base);
}

//============================ analysis ============================

// 2i-import S1 — raw `import("…")` strings of a parsed unit (quotes stripped).
std::vector<std::string> import_strings(const std::shared_ptr<Lnast>& ln) {
  std::vector<std::string> out;
  for (const auto& nid : ln->tree().pre_order()) {
    if (!Lnast_ntype::is_func_call(ln->get_type(nid))) {
      continue;
    }
    auto target = ln->get_first_child(nid);
    auto fname  = ln->get_sibling_next(target);
    if (fname.is_invalid() || ln->get_name(fname) != "import") {
      continue;
    }
    auto mod = ln->get_sibling_next(fname);
    if (mod.is_invalid()) {
      continue;
    }
    std::string t{ln->get_name(mod)};
    if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) {
      t = t.substr(1, t.size() - 2);
    }
    if (!t.empty()) {
      out.push_back(std::move(t));
    }
  }
  return out;
}

// 2i-import S1 — the LSP analyzes a single buffer, so cross-file imports would
// otherwise all read as unresolved (the A7 false-squiggle). Mirror the
// compile-time resolver: pull in importer-directory-relative sibling `.prp`
// files from disk so imports resolve. Transitive to a fixpoint; reads siblings
// from disk (the open buffer is only this file). Best-effort: a sibling that
// fails to parse is skipped (the import simply stays unresolved for this file).
void discover_sibling_imports(Eprp_var& var, std::string_view importer_path) {
  auto dir_of = [](std::string_view p) -> std::string {
    auto s = p.rfind('/');
    return s == std::string_view::npos ? std::string(".") : std::string(p.substr(0, s));
  };
  auto abspath_of = [](std::string_view p) -> std::string {
    std::error_code ec;
    auto            a = std::filesystem::absolute(std::filesystem::path(p), ec);
    return ec ? std::string(p) : a.lexically_normal().string();
  };
  // Resolve `<stem>.prp` in `dir` case-insensitively (mirrors the compile-time
  // discover_imports): scan the directory and return the REAL on-disk filename
  // (original case preserved) so two import spellings of one file collapse to a
  // single path, even on a case-insensitive filesystem. Empty when absent.
  auto find_prp_ci = [](const std::string& dir, const std::string& stem) -> std::string {
    // A stem may be path-qualified (`subdir/mod`): the directory portion rides
    // verbatim onto `dir` and only the final component is case-folded.
    std::string scan_dir = dir;
    std::string leaf     = stem;
    if (const auto s = stem.rfind('/'); s != std::string::npos) {
      scan_dir = dir + "/" + stem.substr(0, s);
      leaf     = stem.substr(s + 1);
    }
    std::error_code it_ec;
    for (std::filesystem::directory_iterator it(scan_dir, it_ec), end; !it_ec && it != end; it.increment(it_ec)) {
      if (!it->is_regular_file()) {
        continue;
      }
      const auto fn = it->path().filename().string();
      if (fn.size() == leaf.size() + 4 && str_tools::ci_ends_with(fn, ".prp")
          && str_tools::ci_equal(std::string_view(fn).substr(0, leaf.size()), leaf)) {
        return it->path().string();
      }
    }
    return {};
  };

  Ci_str_map<std::string>         unit_dir;      // unit -> source dir (case-insensitive)
  std::unordered_set<std::string> parsed_paths;  // abs paths already loaded (exact filesystem paths)
  for (const auto& ln : var.lnasts) {
    unit_dir[std::string(ln->get_top_module_name())] = dir_of(importer_path);
  }
  parsed_paths.insert(abspath_of(importer_path));

  while (true) {
    Ci_str_set loaded;
    for (const auto& ln : var.lnasts) {
      loaded.insert(std::string(ln->get_top_module_name()));
    }
    std::vector<std::pair<std::string, std::string>> to_parse;  // (logical name, path)
    for (const auto& ln : var.lnasts) {
      auto dit = unit_dir.find(std::string(ln->get_top_module_name()));
      if (dit == unit_dir.end()) {
        continue;  // pre-loaded unit with no on-disk origin
      }
      const std::string dir = dit->second;
      for (const auto& raw : import_strings(ln)) {
        if (raw.starts_with("lg:") || raw.starts_with("ln:")) {
          continue;  // artifact imports resolve elsewhere, not on-disk source
        }
        // A trailing `.entry` after the last '/' selects a pub member, so the
        // file is the stem; otherwise the whole string is the file path.
        std::vector<std::string> names;
        auto                     slash = raw.rfind('/');
        auto                     dot   = raw.rfind('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
          names.push_back(raw.substr(0, dot));
        }
        names.push_back(raw);
        bool already = false;
        for (const auto& c : names) {
          if (loaded.count(c)) {
            already = true;
            break;
          }
        }
        if (already) {
          continue;
        }
        for (const auto& c : names) {
          std::string path = find_prp_ci(dir, c);
          if (!path.empty()) {
            if (parsed_paths.insert(abspath_of(path)).second) {
              to_parse.emplace_back(c, path);
            }
            break;
          }
        }
      }
    }
    if (to_parse.empty()) {
      break;
    }
    for (const auto& [name, path] : to_parse) {
      try {
        Prp2lnast converter(path, name);
        var.add(converter.get_lnast());
        unit_dir[name] = dir_of(path);
      } catch (...) {
        // sibling did not parse — skip it (import stays unresolved for this file)
      }
    }
  }
}

// Run the Pyrope front-end on `text` and return the collected diagnostics.
std::vector<livehd::diag::Diagnostic> analyze(std::string_view virtual_path, std::string_view text) {
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_human_stderr(false);  // never write to the (redirected) human channel
  sink.set_jsonl_path("off");    // in-memory only
  sink.set_step("lsp");

  // task 2n Phase B: enable + reset the per-buffer semantic index. The upass
  // runner records definition sites into it during this run (gated on
  // enabled(), which only the LSP ever sets); hover reads it afterwards.
  livehd::lsp_index::index().set_enabled(true);
  livehd::lsp_index::index().clear();

  // Run `inou.prp |> pass.upass` — no lnastfmt stage (its user-facing
  // checks moved into upass/semacheck, so lnastfmt is a compiler-internal
  // structural validator with nothing to tell an end user). Every diagnostic
  // producer (prp2lnast, semacheck, typecheck, constprop, bitwidth, verifier)
  // emits into the shared core/diag sink, so collecting sink.records() after
  // the run captures them. Each stage is wrapped: a thrown error has already
  // recorded its diagnostic, and a stage failure stops the chain exactly as
  // the `|>` pipeline would.
  std::shared_ptr<Lnast> lnast;
  try {
    // Full front-end so name/type diagnostics surface too. The
    // first reported PARSE error still aborts the front-end (collect-and-continue
    // is future work per the LiveHD docs); upass below runs
    // only when the front-end produced a usable tree.
    Prp2lnast converter(virtual_path, module_name_of(virtual_path), text);
    lnast = converter.get_lnast();
  } catch (const std::exception&) {
    // The diagnostic was already flushed into records() by the parser_error
    // constructor (parser_error_int -> sink().flush()). Nothing more to do.
  } catch (...) {
  }

  if (lnast) {
    Eprp_var var;
    var.add(lnast);
    // 2i-import S1 — resolve cross-file imports from this buffer's own directory
    // (sibling .prp read from disk) so the editor stops flagging them. Order the
    // exporters before the importing buffer (discovery appends siblings BFS, so
    // reverse approximates leaf-first) → imports resolve in upass's single pass;
    // import_defer makes any residual (an import cycle) defer silently instead
    // of emitting a false `import-unresolved`.
    discover_sibling_imports(var, virtual_path);
    std::reverse(var.lnasts.begin(), var.lnasts.end());
    // Diagnostics-only run: tolg:0 (no LNAST→LGraph lowering — also the label
    // default, set explicitly for clarity) and toln:0 (don't materialize the
    // rewritten post-upass LNAST; nothing here consumes it).
    var.add("tolg", "false");
    var.add("toln", "false");
    var.add("import_defer", "true");

    try {
      Pass_upass::work(var);  // semacheck / typecheck / constprop / bitwidth / verifier / assert / …
    } catch (...) {
      // upass aborted (a diag fatal/parser_error throw or config error); its diagnostic is
      // already in the sink — the `|>` pipeline would stop here too.
    }
  }

  return sink.records();  // copy out before the next analysis clears the sink
}

int severity_to_lsp(livehd::diag::Severity s) {
  switch (s) {
    case livehd::diag::Severity::error  : return 1;
    case livehd::diag::Severity::warning: return 2;
    case livehd::diag::Severity::note   : return 3;  // information
    case livehd::diag::Severity::info   : return 3;  // information (progress)
  }
  return 1;
}

void write_position(Writer& w, uint32_t line0, uint32_t char0) {
  w.StartObject();
  w.Key("line");
  w.Uint(line0);
  w.Key("character");
  w.Uint(char0);
  w.EndObject();
}

void write_range(Writer& w, const livehd::diag::Span& sp) {
  // diag spans are 1-based line/col with byte columns; LSP wants 0-based. With
  // utf-8 negotiated the byte column is the character offset; with the utf-16
  // fallback it is an ASCII-correct approximation.
  uint32_t sl = sp.start_line ? *sp.start_line - 1 : 0;
  uint32_t sc = sp.start_col ? *sp.start_col - 1 : 0;
  uint32_t el = sp.end_line ? *sp.end_line - 1 : sl;
  uint32_t ec = sp.end_col ? *sp.end_col - 1 : sc;
  w.Key("range");
  w.StartObject();
  w.Key("start");
  write_position(w, sl, sc);
  w.Key("end");
  write_position(w, el, ec);
  w.EndObject();
}

void write_diagnostic(Writer& w, const livehd::diag::Diagnostic& d) {
  w.StartObject();
  write_range(w, d.span);
  w.Key("severity");
  w.Int(severity_to_lsp(d.severity));
  if (!d.code.empty()) {
    w.Key("code");
    w.String(d.code.data(), static_cast<rapidjson::SizeType>(d.code.size()));
  }
  w.Key("source");
  w.String("livehd");
  std::string msg = d.message;
  if (!d.hint.empty()) {
    msg += "\nhint: ";
    msg += d.hint;
  }
  w.Key("message");
  w.String(msg.data(), static_cast<rapidjson::SizeType>(msg.size()));
  w.EndObject();
}

void publish_diagnostics(const std::string& uri, const std::vector<livehd::diag::Diagnostic>& recs) {
  if (g_client_pull) {
    return;  // client uses pull diagnostics; pushing too would double-report
  }
  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  w.Key("method");
  w.String("textDocument/publishDiagnostics");
  w.Key("params");
  w.StartObject();
  w.Key("uri");
  w.String(uri.data(), static_cast<rapidjson::SizeType>(uri.size()));
  w.Key("diagnostics");
  w.StartArray();
  for (const auto& d : recs) {
    write_diagnostic(w, d);
  }
  w.EndArray();
  w.EndObject();
  w.EndObject();
  send_message(sb.GetString());
}

// diagnostics for a uri's current buffer (empty for non-.prp).
std::vector<livehd::diag::Diagnostic> diagnostics_for(const std::string& uri, std::string_view text) {
  std::string path = uri_to_path(uri);
  if (!ends_with(path, ".prp")) {
    return {};
  }
  return analyze(path, text);
}

void analyze_and_publish(const std::string& uri) {
  auto             it   = g_docs.find(uri);
  std::string_view text = (it == g_docs.end()) ? std::string_view{} : std::string_view(it->second);
  publish_diagnostics(uri, diagnostics_for(uri, text));
}

//============================ request handlers ============================

void handle_initialize(const rapidjson::Document& req) {
  // positionEncoding negotiation: pick utf-8 only if the client lists it.
  // Also detect pull-diagnostics support so we don't double-report (§push/pull).
  g_utf8        = false;
  g_client_pull = false;
  if (const auto* params = member(req, "params")) {
    if (const auto* caps = member(*params, "capabilities")) {
      if (const auto* general = member(*caps, "general")) {
        if (const auto* encs = member(*general, "positionEncodings"); encs && encs->IsArray()) {
          for (const auto& e : encs->GetArray()) {
            if (e.IsString() && std::string_view(e.GetString()) == "utf-8") {
              g_utf8 = true;
            }
          }
        }
      }
      if (const auto* td = member(*caps, "textDocument")) {
        if (member(*td, "diagnostic") != nullptr) {
          g_client_pull = true;
        }
      }
    }
  }

  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  write_id(w, req);
  w.Key("result");
  w.StartObject();
  w.Key("capabilities");
  w.StartObject();
  w.Key("positionEncoding");
  w.String(g_utf8 ? "utf-8" : "utf-16");
  w.Key("textDocumentSync");
  w.StartObject();
  w.Key("openClose");
  w.Bool(true);
  w.Key("change");
  w.Int(1);  // TextDocumentSyncKind.Full
  w.Key("save");
  w.StartObject();
  w.Key("includeText");
  w.Bool(true);
  w.EndObject();
  w.EndObject();
  w.Key("diagnosticProvider");
  w.StartObject();
  w.Key("interFileDependencies");
  w.Bool(false);
  w.Key("workspaceDiagnostics");
  w.Bool(false);
  w.EndObject();
  w.Key("hoverProvider");  // task 2n Phase B (flag + handler land together)
  w.Bool(true);
  w.EndObject();  // capabilities
  w.Key("serverInfo");
  w.StartObject();
  w.Key("name");
  w.String("livehd-lsp");
  w.Key("version");
  w.String("0.1");
  w.EndObject();
  w.EndObject();  // result
  w.EndObject();
  send_message(sb.GetString());
}

void respond_null(const rapidjson::Document& req) {
  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  write_id(w, req);
  w.Key("result");
  w.Null();
  w.EndObject();
  send_message(sb.GetString());
}

void respond_method_not_found(const rapidjson::Document& req) {
  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  write_id(w, req);
  w.Key("error");
  w.StartObject();
  w.Key("code");
  w.Int(-32601);  // MethodNotFound
  w.Key("message");
  w.String("method not found");
  w.EndObject();
  w.EndObject();
  send_message(sb.GetString());
}

// Extract params.textDocument.uri (empty if absent).
std::string uri_of(const rapidjson::Document& req) {
  if (const auto* params = member(req, "params")) {
    if (const auto* td = member(*params, "textDocument")) {
      if (const auto* uri = member(*td, "uri"); uri && uri->IsString()) {
        return uri->GetString();
      }
    }
  }
  return {};
}

void handle_did_open(const rapidjson::Document& req) {
  std::string uri = uri_of(req);
  if (uri.empty()) {
    return;
  }
  std::string text;
  if (const auto* params = member(req, "params")) {
    if (const auto* td = member(*params, "textDocument")) {
      if (const auto* t = member(*td, "text"); t && t->IsString()) {
        text = t->GetString();
      }
    }
  }
  g_docs[uri] = std::move(text);
  analyze_and_publish(uri);
}

void handle_did_change(const rapidjson::Document& req) {
  std::string uri = uri_of(req);
  if (uri.empty()) {
    return;
  }
  // Full sync: the last contentChange carries the entire document text.
  if (const auto* params = member(req, "params")) {
    if (const auto* changes = member(*params, "contentChanges"); changes && changes->IsArray() && !changes->Empty()) {
      const auto& last = (*changes)[changes->Size() - 1];
      if (const auto* t = member(last, "text"); t && t->IsString()) {
        g_docs[uri] = t->GetString();
      }
    }
  }
  analyze_and_publish(uri);
}

void handle_did_save(const rapidjson::Document& req) {
  std::string uri = uri_of(req);
  if (uri.empty()) {
    return;
  }
  if (const auto* params = member(req, "params")) {
    if (const auto* t = member(*params, "text"); t && t->IsString()) {  // includeText
      g_docs[uri] = t->GetString();
    }
  }
  analyze_and_publish(uri);
}

void handle_did_close(const rapidjson::Document& req) {
  std::string uri = uri_of(req);
  if (uri.empty()) {
    return;
  }
  g_docs.erase(uri);
  publish_diagnostics(uri, {});  // clear squiggles
}

// Pull diagnostics (LSP 3.17 textDocument/diagnostic): full report for one doc.
void handle_pull_diagnostic(const rapidjson::Document& req) {
  std::string                           uri = uri_of(req);
  std::vector<livehd::diag::Diagnostic> recs;
  if (!uri.empty()) {
    auto             it   = g_docs.find(uri);
    std::string_view text = (it == g_docs.end()) ? std::string_view{} : std::string_view(it->second);
    recs                  = diagnostics_for(uri, text);
  }
  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  write_id(w, req);
  w.Key("result");
  w.StartObject();
  w.Key("kind");
  w.String("full");
  w.Key("items");
  w.StartArray();
  for (const auto& d : recs) {
    write_diagnostic(w, d);
  }
  w.EndArray();
  w.EndObject();
  w.EndObject();
  send_message(sb.GetString());
}

// textDocument/hover (task 2n Phase B). Re-analyze the buffer (the upass runner
// fills livehd::lsp_index during the run), then return the type+range of the
// variable whose definition span covers the cursor. Per the 2n design the span
// is statement-granularity, so selectionRange == range. result:null off a name.
void handle_hover(const rapidjson::Document& req) {
  std::string uri   = uri_of(req);
  uint32_t    line0 = 0;
  uint32_t    char0 = 0;
  if (const auto* params = member(req, "params")) {
    if (const auto* pos = member(*params, "position")) {
      if (const auto* l = member(*pos, "line"); l && l->IsUint()) {
        line0 = l->GetUint();
      }
      if (const auto* c = member(*pos, "character"); c && c->IsUint()) {
        char0 = c->GetUint();
      }
    }
  }

  // Drive the front-end on the current buffer; populates lsp_index as a side
  // effect (diagnostics discarded here — the pull/push path owns those). If the
  // doc is not open, answer null rather than from a stale index of another buffer.
  auto doc_it = g_docs.find(uri);
  if (doc_it == g_docs.end()) {
    respond_null(req);
    return;
  }
  (void)diagnostics_for(uri, doc_it->second);

  // First index entry whose 0-based [start,end] span covers the cursor.
  const livehd::lsp_index::Entry* hit = nullptr;
  for (const auto& e : livehd::lsp_index::index().entries()) {
    const uint32_t sl = e.start_line ? e.start_line - 1 : 0;
    const uint32_t sc = e.start_col ? e.start_col - 1 : 0;
    const uint32_t el = e.end_line ? e.end_line - 1 : sl;
    const uint32_t ec = e.end_col ? e.end_col - 1 : sc;
    const bool     after_start = (line0 > sl) || (line0 == sl && char0 >= sc);
    const bool     before_end  = (line0 < el) || (line0 == el && char0 <= ec);
    if (after_start && before_end) {
      hit = &e;
      break;
    }
  }

  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  write_id(w, req);
  w.Key("result");
  if (hit == nullptr) {
    w.Null();
  } else {
    w.StartObject();
    w.Key("contents");
    w.StartObject();
    w.Key("kind");
    w.String("plaintext");
    w.Key("value");
    w.String(hit->render.data(), static_cast<rapidjson::SizeType>(hit->render.size()));
    w.EndObject();
    const uint32_t sl = hit->start_line ? hit->start_line - 1 : 0;
    const uint32_t sc = hit->start_col ? hit->start_col - 1 : 0;
    const uint32_t el = hit->end_line ? hit->end_line - 1 : sl;
    const uint32_t ec = hit->end_col ? hit->end_col - 1 : sc;
    w.Key("range");
    w.StartObject();
    w.Key("start");
    write_position(w, sl, sc);
    w.Key("end");
    write_position(w, el, ec);
    w.EndObject();
    w.EndObject();
  }
  w.EndObject();
  send_message(sb.GetString());
}

}  // namespace

int run_stdio() {
  // Protect the protocol stream: keep a private copy of the real stdout and
  // repoint fd 1 at stderr so any stray printf/std::cout from the front-end
  // lands in the log, never in the JSON-RPC stream.
  g_out_fd = ::dup(STDOUT_FILENO);
  if (g_out_fd < 0) {
    g_out_fd = STDOUT_FILENO;
  } else {
    ::dup2(STDERR_FILENO, STDOUT_FILENO);
  }

  bool        shutdown_requested = false;
  std::string body;
  while (read_message(body)) {
    if (body.empty()) {
      continue;
    }
    rapidjson::Document doc;
    doc.Parse(body.data(), body.size());
    if (doc.HasParseError() || !doc.IsObject()) {
      continue;
    }

    const std::string method = method_of(doc);
    const bool        has_id = doc.HasMember("id");

    if (method == "initialize") {
      handle_initialize(doc);
    } else if (method == "initialized") {
      // no-op
    } else if (method == "shutdown") {
      shutdown_requested = true;
      respond_null(doc);
    } else if (method == "exit") {
      return shutdown_requested ? 0 : 1;
    } else if (method == "textDocument/didOpen") {
      handle_did_open(doc);
    } else if (method == "textDocument/didChange") {
      handle_did_change(doc);
    } else if (method == "textDocument/didSave") {
      handle_did_save(doc);
    } else if (method == "textDocument/didClose") {
      handle_did_close(doc);
    } else if (method == "textDocument/diagnostic") {
      handle_pull_diagnostic(doc);
    } else if (method == "textDocument/hover") {
      handle_hover(doc);  // task 2n Phase B
    } else if (method == "$/cancelRequest" || method == "$/setTrace") {
      // no-op (analysis is synchronous)
    } else if (has_id) {
      respond_method_not_found(doc);  // unknown request: must answer
    }
    // unknown notification: ignore
  }
  return 0;
}

}  // namespace livehd::lsp
