//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "livehd_lsp.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "diag.hpp"
#include "eprp_var.hpp"
#include "lnast.hpp"
#include "pass_lnastfmt.hpp"
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
        content_length = std::strtoul(val.c_str(), nullptr, 10);
        have_len       = true;
      }
    }
  }

  if (!have_len) {
    body.clear();
    return true;
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

// Run the Pyrope front-end on `text` and return the collected diagnostics.
std::vector<livehd::diag::Diagnostic> analyze(std::string_view virtual_path, std::string_view text) {
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_human_stderr(false);  // never write to the (redirected) human channel
  sink.set_jsonl_path("off");    // in-memory only
  sink.set_step("lsp");

  // Mirror the `inou.prp |> pass.upass |> pass.lnastfmt` shell pipeline so the
  // LSP surfaces every diagnostic those three stages produce, not just the
  // front-end's. All three emit into the shared core/diag sink (typecheck via
  // sink().emit(); lnastfmt/verifier via Pass::error/warn -> parser_error_int ->
  // sink().flush()), so collecting sink.records() after the run captures them.
  // Each stage is wrapped: a thrown error has already recorded its diagnostic,
  // and a stage failure stops the chain exactly as the `|>` pipeline would.
  std::shared_ptr<Lnast> lnast;
  try {
    // Full front-end (not parse_only) so name/type diagnostics surface too. The
    // first reported PARSE error still aborts the front-end (collect-and-continue
    // is future work per docs/contracts/diagnostics.md); upass/lnastfmt below run
    // only when the front-end produced a usable tree.
    Prp2lnast converter(virtual_path, module_name_of(virtual_path), /*parse_only=*/false, text);
    lnast = converter.get_lnast();
  } catch (const std::exception&) {
    // The diagnostic was already flushed into records() by the parser_error
    // constructor (parser_error_int -> sink().flush()). Nothing more to do.
  } catch (...) {
  }

  if (lnast) {
    Eprp_var var;
    var.add(lnast);

    bool upass_ok = false;
    try {
      Pass_upass::work(var);  // typecheck / constprop / bitwidth / verifier / assert / …
      upass_ok = true;
    } catch (...) {
      // upass aborted (a Pass::error throw or config error); its diagnostic is
      // already in the sink. Skip lnastfmt — the `|>` pipeline would stop here.
    }
    if (upass_ok) {
      try {
        Pass_lnastfmt::fmt_begin(var);  // LNAST structural validation
      } catch (...) {
      }
    }
  }

  return sink.records();  // copy out before the next analysis clears the sink
}

int severity_to_lsp(livehd::diag::Severity s) {
  switch (s) {
    case livehd::diag::Severity::error  : return 1;
    case livehd::diag::Severity::warning: return 2;
    case livehd::diag::Severity::note   : return 3;  // information
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
  // fallback it is an ASCII-correct approximation (see docs/contracts §7).
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
