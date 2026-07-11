//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "livehd_lsp.hpp"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "diag.hpp"
#include "eprp_var.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "lsp_index.hpp"
#include "pass_upass.hpp"
#include "prp2lnast.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "source_path.hpp"
#include "str_tools.hpp"

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

// Percent-encode an absolute filesystem path into a file:// URI.
std::string path_to_uri(std::string_view abs_path) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string           out   = "file://";
  for (const char c : abs_path) {
    const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'
                      || c == '~' || c == '/';
    if (keep) {
      out.push_back(c);
    } else {
      out.push_back('%');
      out.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
      out.push_back(hex[static_cast<unsigned char>(c) & 0xF]);
    }
  }
  return out;
}

//============================ semantic state (2n Phase C) ============================

// Cross-file navigation state rebuilt by every analyze() run, alongside the
// livehd::lsp_index def entries the upass runner records. Everything here is
// copied OUT of the front-end objects before analyze() returns — no Lnast,
// Prp2lnast, or pass-internal symbol table is retained.
struct Pub_def {            // one `pub` export (name-identifier-anchored span)
  std::string        unit;  // exporting file unit ("lib_add")
  std::string        name;  // exported member ("my_add")
  std::string        kind;  // "value" | "comb" | "mod" | "pipe" | "fluid"
  livehd::diag::Span span;
};
struct Unit_info {                          // one walked Lnast unit (file-level or extracted lambda)
  std::string                 unit;         // top module name ("lib_add", "lib_add.my_add")
  std::string                 lambda_kind;  // "" for a file-level tree
  std::string                 file;         // workspace-relative source path
  livehd::diag::Span          span;         // whole-definition span (null for file-level trees)
  std::vector<Lnast_io_entry> inputs;       // io_meta copy (params hover/definition)
  std::vector<Lnast_io_entry> outputs;
};
struct Import_bind {   // `X = import("unit")` / `import("unit.entity")` in the buffer
  std::string local;   // bound name ("lib", "RegFile")
  std::string target;  // import string, quotes stripped ("lib_add", "lib_add.my_add")
};
struct Semantic_state {
  std::vector<Pub_def>                          pubs;
  std::vector<Unit_info>                        units;
  std::vector<Import_bind>                      imports;
  absl::flat_hash_map<std::string, std::string> rel2abs;     // Span.file -> absolute path
  std::string                                   buffer_rel;  // workspace-relative path of the analyzed buffer
};
Semantic_state g_sem;

// The buffer's import bindings, walked on the freshly-parsed tree (before the
// upass pipeline). prp2lnast lowers `const X = import("unit")` as an import
// fcall into a `%tmp` followed by `store(X, %tmp)`, so the tmp is chased one
// store; a direct user-name fcall target is accepted too.
void collect_import_bindings(const std::shared_ptr<Lnast>& ln, std::vector<Import_bind>& out) {
  absl::flat_hash_map<std::string, std::string> tmp_target;  // %tmp -> import string
  const auto                                    unquote = [](std::string s) {
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front()) {
      s = s.substr(1, s.size() - 2);
    }
    return s;
  };
  for (const auto& nid : ln->tree().pre_order()) {
    const auto t = ln->get_type(nid);
    if (Lnast_ntype::is_func_call(t)) {
      const auto target = ln->get_first_child(nid);
      const auto fname  = target.is_invalid() ? target : ln->get_sibling_next(target);
      if (fname.is_invalid() || ln->get_name(fname) != "import") {
        continue;
      }
      const auto arg = ln->get_sibling_next(fname);
      if (arg.is_invalid()) {
        continue;
      }
      const std::string unit = unquote(std::string(ln->get_name(arg)));
      if (unit.empty() || unit.starts_with("lg:") || unit.starts_with("ln:")) {
        continue;  // artifact imports have no on-disk .prp to jump to
      }
      const std::string tname(ln->get_name(target));
      if (Lnast::is_tmp(tname)) {
        tmp_target[tname] = unit;
      } else if (!tname.empty()) {
        out.push_back({tname, unit});
      }
    } else if (Lnast_ntype::is_store(t) || Lnast_ntype::is_dp_assign(t)) {
      // Plain 2-child `store(dst, src)` only — a leveled store writes a tuple
      // path, never an import binding.
      const auto dst = ln->get_first_child(nid);
      if (dst.is_invalid()) {
        continue;
      }
      const auto src = ln->get_sibling_next(dst);
      if (src.is_invalid() || !ln->get_sibling_next(src).is_invalid()) {
        continue;
      }
      const auto it = tmp_target.find(std::string(ln->get_name(src)));
      if (it == tmp_target.end()) {
        continue;
      }
      const std::string dname(ln->get_name(dst));
      if (!dname.empty() && !Lnast::is_tmp(dname)) {
        out.push_back({dname, it->second});
      }
    }
  }
}

// Pub exports + per-unit io/span facts of every walked unit (buffer +
// discovered siblings + the lambda trees func_extract spawned during upass).
void collect_semantic_state(const std::vector<std::shared_ptr<Lnast>>& lnasts, Semantic_state& sem) {
  for (const auto& ln : lnasts) {
    Unit_info ui;
    ui.unit        = std::string(ln->get_top_module_name());
    ui.lambda_kind = std::string(ln->get_lambda_kind());
    ui.span        = ln->span_of(ln->get_root());
    if (!ui.span.file.empty()) {
      ui.file = ui.span.file;
    } else if (ln->source_locator().file_count() > 0) {
      ui.file = std::string(ln->source_locator().file_path(0));  // file-level tree: root has no srcid
    }
    ui.inputs  = ln->io_meta().inputs;
    ui.outputs = ln->io_meta().outputs;
    for (const auto& p : ln->get_pub_list()) {
      sem.pubs.push_back({ui.unit, p.name, p.kind, ln->source_locator().resolve_span(p.srcid)});
    }
    sem.units.push_back(std::move(ui));
  }
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
void discover_sibling_imports(Eprp_var& var, std::string_view importer_path,
                              absl::flat_hash_map<std::string, std::string>& rel2abs) {
  auto dir_of = [](std::string_view p) -> std::string {
    auto s = p.rfind('/');
    return s == std::string_view::npos ? std::string(".") : std::string(p.substr(0, s));
  };
  auto abspath_of = [](std::string_view p) -> std::string {
    std::error_code ec;
    auto            a = std::filesystem::absolute(std::filesystem::path(p), ec);
    return ec ? std::string(p) : a.lexically_normal().string();
  };
  // Resolve `<stem>.prp` in `dir` case-sensitively (mirrors the compile-time
  // discover_imports): scan the directory and return the REAL on-disk filename
  // (original case preserved) so two import spellings of one file collapse to a
  // path. Empty when absent.
  auto find_prp = [](const std::string& dir, const std::string& stem) -> std::string {
    // A stem may be path-qualified (`subdir/mod`): the directory portion rides
    // verbatim onto `dir` and only the final component is matched exactly.
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
      if (fn.size() == leaf.size() + 4 && str_tools::ends_with(fn, ".prp")
          && (std::string_view(fn).substr(0, leaf.size()) == leaf)) {
        return it->path().string();
      }
    }
    return {};
  };

  absl::flat_hash_map<std::string, std::string>         unit_dir;      // unit -> source dir (case-sensitive)
  std::unordered_set<std::string> parsed_paths;  // abs paths already loaded (exact filesystem paths)
  for (const auto& ln : var.lnasts) {
    unit_dir[std::string(ln->get_top_module_name())] = dir_of(importer_path);
  }
  parsed_paths.insert(abspath_of(importer_path));

  while (true) {
    absl::flat_hash_set<std::string> loaded;
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
          std::string path = find_prp(dir, c);
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
        // Span.file of everything minted from this sibling is
        // workspace_relative(path) — remember how to get back to the disk file
        // so definition Locations can carry a real file:// URI.
        rel2abs[livehd::srcloc::workspace_relative(path)] = abspath_of(path);
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

  // 2n Phase C: navigation state rebuilt per run (pubs, units, import
  // bindings, path mapping) — hover/definition read it after the front-end.
  g_sem            = {};
  g_sem.buffer_rel = livehd::srcloc::workspace_relative(virtual_path);
  {
    std::error_code ec;
    const auto      abs             = std::filesystem::absolute(std::filesystem::path(virtual_path), ec);
    g_sem.rel2abs[g_sem.buffer_rel] = ec ? std::string(virtual_path) : abs.lexically_normal().string();
  }

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
    discover_sibling_imports(var, virtual_path, g_sem.rel2abs);
    collect_import_bindings(lnast, g_sem.imports);
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
    // Pubs + unit io/span facts, including the lambda trees upass spawned
    // (var.lnasts grew during the walk). Copied out — nothing front-end-owned
    // survives past this return.
    collect_semantic_state(var.lnasts, g_sem);
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
  w.Key("definitionProvider");  // 2n Phase C (flag + handler land together)
  w.Bool(true);
  w.Key("declarationProvider");  // same resolver — gd/gD both answer
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

// params.position -> 0-based line/character (both 0 when absent).
void position_of(const rapidjson::Document& req, uint32_t& line0, uint32_t& char0) {
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
}

//============================ cursor -> symbol (2n Phase C) ============================

// The identifier (plus its dotted qualifier chain and any enclosing quoted
// string) under a 0-based cursor position. Refs carry no srcid in LNAST
// (srcid_carries excludes them — statement-granularity provenance), so the
// name under the cursor is recovered lexically from the buffer text.
struct Cursor_tok {
  std::string name;       // identifier under the cursor ("" when not on one)
  std::string root;       // first segment of a dotted chain ("" when unqualified)
  std::string chain;      // full chain up to and including the token (== name when unqualified)
  uint32_t    line0 = 0;  // token coordinates: 0-based, ecol0 exclusive
  uint32_t    scol0 = 0;
  uint32_t    ecol0 = 0;
  std::string str;  // enclosing quoted-string content ("" when none)
};

bool is_ident_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

Cursor_tok token_at(std::string_view text, uint32_t line0, uint32_t char0) {
  Cursor_tok tok;
  tok.line0 = line0;
  size_t ls = 0;
  for (uint32_t l = 0; l < line0; ++l) {
    const auto nl = text.find('\n', ls);
    if (nl == std::string_view::npos) {
      return tok;
    }
    ls = nl + 1;
  }
  const auto             le   = text.find('\n', ls);
  const std::string_view line = text.substr(ls, (le == std::string_view::npos ? text.size() : le) - ls);
  size_t                 pos  = std::min<size_t>(char0, line.size());

  // Enclosing quoted string (single line, no escape handling — Pyrope import
  // strings carry neither newlines nor escapes).
  {
    char   q  = 0;
    size_t qs = 0;
    for (size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (q == 0 && (c == '"' || c == '\'')) {
        q  = c;
        qs = i;
      } else if (q != 0 && c == q) {
        if (pos > qs && pos <= i) {
          tok.str = std::string(line.substr(qs + 1, i - qs - 1));
          break;
        }
        q = 0;
      }
    }
  }

  // Identifier under (or immediately left of) the cursor.
  size_t p = pos;
  if ((p >= line.size() || !is_ident_char(line[p])) && p > 0 && is_ident_char(line[p - 1])) {
    --p;
  }
  if (p >= line.size() || !is_ident_char(line[p])) {
    return tok;
  }
  size_t s = p;
  size_t e = p + 1;
  while (s > 0 && is_ident_char(line[s - 1])) {
    --s;
  }
  while (e < line.size() && is_ident_char(line[e])) {
    ++e;
  }
  if (line[s] >= '0' && line[s] <= '9') {
    return tok;  // a number literal, not a name
  }
  tok.name  = std::string(line.substr(s, e - s));
  tok.scol0 = static_cast<uint32_t>(s);
  tok.ecol0 = static_cast<uint32_t>(e);

  // Dotted qualifier chain to the left (`lib.my_add` with the cursor on
  // `my_add` -> root "lib", chain "lib.my_add").
  std::vector<std::string_view> segs;
  size_t                        q2 = s;
  while (q2 > 0 && line[q2 - 1] == '.') {
    size_t e2 = q2 - 1;
    size_t s2 = e2;
    while (s2 > 0 && is_ident_char(line[s2 - 1])) {
      --s2;
    }
    if (s2 == e2) {
      break;
    }
    segs.push_back(line.substr(s2, e2 - s2));
    q2 = s2;
  }
  if (!segs.empty()) {
    tok.root = std::string(segs.back());
    std::string chain;
    for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
      chain += *it;
      chain += '.';
    }
    chain     += tok.name;
    tok.chain  = std::move(chain);
  } else {
    tok.chain = tok.name;
  }
  return tok;
}

//============================ definition resolution ============================

struct Def_loc {
  std::string        file;  // workspace-relative Span.file
  livehd::diag::Span span;
};

const Pub_def* find_pub(std::string_view unit, std::string_view name) {
  for (const auto& p : g_sem.pubs) {
    if (p.unit == unit && p.name == name) {
      return &p;
    }
  }
  return nullptr;
}

const Unit_info* find_unit(std::string_view unit) {
  for (const auto& u : g_sem.units) {
    if (u.unit == unit) {
      return &u;
    }
  }
  return nullptr;
}

std::string binding_target(std::string_view local) {
  for (const auto& ib : g_sem.imports) {
    if (ib.local == local) {
      return ib.target;
    }
  }
  return {};
}

livehd::diag::Span point_span(std::string file, uint32_t line1, uint32_t col1) {
  livehd::diag::Span sp;
  sp.file       = std::move(file);
  sp.start_line = line1;
  sp.start_col  = col1;
  sp.end_line   = line1;
  sp.end_col    = col1;
  return sp;
}

// Resolve an import string ("unit" / "unit.entity" / "dir/unit") to a jump
// target: the pub entity's name identifier when the dotted form names one,
// else the start of the imported file.
void push_import_target(const std::string& target, std::vector<Def_loc>& out) {
  const auto slash  = target.rfind('/');
  const auto dot    = target.rfind('.');
  const bool dotted = dot != std::string::npos && (slash == std::string::npos || dot > slash);
  const auto unit   = dotted ? target.substr(0, dot) : target;
  if (dotted) {
    if (const auto* p = find_pub(unit, target.substr(dot + 1)); p != nullptr && p->span.start_line) {
      out.push_back({p->span.file, p->span});
      return;
    }
  }
  for (const auto& cand : {unit, target}) {
    if (const auto* u = find_unit(cand); u != nullptr && !u->file.empty()) {
      out.push_back({u->file, point_span(u->file, 1, 1)});
      return;
    }
  }
}

// The earliest recorded definition of `name` in the current buffer — the
// declaration statement (later entries are SSA re-definitions).
bool push_entry_decl(std::string_view name, std::vector<Def_loc>& out) {
  const livehd::lsp_index::Entry* best = nullptr;
  for (const auto& e : livehd::lsp_index::index().entries()) {
    if (e.name != name || e.file != g_sem.buffer_rel || e.start_line == 0) {
      continue;
    }
    if (best == nullptr || e.start_line < best->start_line || (e.start_line == best->start_line && e.start_col < best->start_col)) {
      best = &e;
    }
  }
  if (best == nullptr) {
    return false;
  }
  livehd::diag::Span sp;
  sp.file       = best->file;
  sp.start_line = best->start_line;
  sp.start_col  = best->start_col;
  sp.end_line   = best->end_line;
  sp.end_col    = best->end_col;
  out.push_back({best->file, sp});
  return true;
}

std::vector<Def_loc> resolve_definition(const Cursor_tok& tok) {
  std::vector<Def_loc> out;

  // (1) inside an import string: jump into the imported file
  if (!tok.str.empty()) {
    push_import_target(tok.str, out);
    if (!out.empty()) {
      return out;
    }
  }
  if (tok.name.empty()) {
    return out;
  }

  // (2) qualified member `root.…name` through an import namespace binding
  if (!tok.root.empty()) {
    const auto target = binding_target(tok.root);
    if (!target.empty() && target.find('.') == std::string::npos) {
      if (const auto* p = find_pub(target, tok.name); p != nullptr && p->span.start_line) {
        out.push_back({p->span.file, p->span});
      }
    }
    if (out.empty()) {
      push_entry_decl(tok.chain, out);  // a tuple-field definition in this buffer
    }
    return out;
  }

  // (3) unqualified: the import's original definition first (the point of
  // go-to-declaration on an imported binding), then the local declaration
  const auto target = binding_target(tok.name);
  if (!target.empty()) {
    push_import_target(target, out);
  }
  push_entry_decl(tok.name, out);
  if (!out.empty()) {
    return out;
  }

  // (4) a pub / lambda defined in this buffer (function names have no
  // symbol-table def entry — they live in the function registry)
  for (const auto& p : g_sem.pubs) {
    if (p.name == tok.name && p.span.file == g_sem.buffer_rel && p.span.start_line) {
      out.push_back({p.span.file, p.span});
      return out;
    }
  }
  for (const auto& u : g_sem.units) {
    if (u.lambda_kind.empty() || u.file != g_sem.buffer_rel || !u.span.start_line) {
      continue;
    }
    const auto dot = u.unit.rfind('.');
    if (dot != std::string::npos && std::string_view(u.unit).substr(dot + 1) == tok.name) {
      out.push_back({u.file, point_span(u.file, *u.span.start_line, u.span.start_col ? *u.span.start_col : 1)});
      return out;
    }
  }

  // (5) an IO param of the innermost lambda whose definition covers the cursor
  const Unit_info* best = nullptr;
  for (const auto& u : g_sem.units) {
    if (u.file != g_sem.buffer_rel || !u.span.start_line) {
      continue;
    }
    const uint32_t cl = tok.line0 + 1;  // spans are 1-based
    if (cl < *u.span.start_line || (u.span.end_line && cl > *u.span.end_line)) {
      continue;
    }
    bool has = false;
    for (const auto* v : {&u.inputs, &u.outputs}) {
      for (const auto& e : *v) {
        has = has || e.name == tok.name;
      }
    }
    if (has && (best == nullptr || *u.span.start_line > *best->span.start_line)) {
      best = &u;
    }
  }
  if (best != nullptr) {
    out.push_back({best->file, point_span(best->file, *best->span.start_line, best->span.start_col ? *best->span.start_col : 1)});
  }
  return out;
}

// Workspace-relative Span.file -> the URI the client can open. The current
// buffer answers with the request's own URI (exact match, no re-encoding).
std::string uri_for_rel(const std::string& rel, const std::string& buffer_uri) {
  if (rel.empty() || rel == g_sem.buffer_rel) {
    return buffer_uri;
  }
  const auto it = g_sem.rel2abs.find(rel);
  if (it == g_sem.rel2abs.end()) {
    return {};
  }
  return path_to_uri(it->second);
}

// textDocument/definition + textDocument/declaration (2n Phase C). Both answer
// the declaration site(s) of the symbol under the cursor; an imported binding
// answers the original (cross-file) definition first, then the local binding.
void handle_definition(const rapidjson::Document& req) {
  std::string uri   = uri_of(req);
  uint32_t    line0 = 0;
  uint32_t    char0 = 0;
  position_of(req, line0, char0);

  // A non-.prp doc never runs analyze(), so lsp_index/g_sem would still
  // describe the previously analyzed buffer — answer null, don't cross wires.
  auto doc_it = g_docs.find(uri);
  if (doc_it == g_docs.end() || !ends_with(uri_to_path(uri), ".prp")) {
    respond_null(req);
    return;
  }
  (void)diagnostics_for(uri, doc_it->second);  // rebuilds lsp_index + g_sem

  const auto locs = resolve_definition(token_at(doc_it->second, line0, char0));

  rapidjson::StringBuffer sb;
  Writer                  w(sb);
  w.StartObject();
  w.Key("jsonrpc");
  w.String("2.0");
  write_id(w, req);
  w.Key("result");
  if (locs.empty()) {
    w.Null();
  } else {
    w.StartArray();
    for (const auto& d : locs) {
      const std::string u = uri_for_rel(d.file, uri);
      if (u.empty()) {
        continue;  // span in a file the analysis didn't read from disk
      }
      w.StartObject();
      w.Key("uri");
      w.String(u.data(), static_cast<rapidjson::SizeType>(u.size()));
      write_range(w, d.span);
      w.EndObject();
    }
    w.EndArray();
  }
  w.EndObject();
  send_message(sb.GetString());
}

// textDocument/hover (task 2n Phase B; identifier-based lookup + cross-file
// kinds added with Phase C). Re-analyze the buffer (the upass runner fills
// livehd::lsp_index during the run), then answer, in order: the reaching
// definition of the identifier under the cursor; the def entry whose statement
// span covers the cursor; a pub/lambda (function) name; an IO param.
void handle_hover(const rapidjson::Document& req) {
  std::string uri   = uri_of(req);
  uint32_t    line0 = 0;
  uint32_t    char0 = 0;
  position_of(req, line0, char0);

  // Drive the front-end on the current buffer; populates lsp_index as a side
  // effect (diagnostics discarded here — the pull/push path owns those). If the
  // doc is not open — or is not .prp, which analyze() never runs on — answer
  // null rather than from a stale index/g_sem of another buffer.
  auto doc_it = g_docs.find(uri);
  if (doc_it == g_docs.end() || !ends_with(uri_to_path(uri), ".prp")) {
    respond_null(req);
    return;
  }
  (void)diagnostics_for(uri, doc_it->second);

  const auto tok = token_at(doc_it->second, line0, char0);

  std::string render;
  uint32_t    rsl = 0;  // reported range, 0-based, rec exclusive
  uint32_t    rsc = 0;
  uint32_t    rel = 0;
  uint32_t    rec = 0;

  const auto& entries = livehd::lsp_index::index().entries();

  // (a) identifier-based: the reaching definition (max recorded position at or
  // before the cursor), else the first one (a use above a hoisted def).
  if (!tok.name.empty()) {
    const livehd::lsp_index::Entry* reach = nullptr;
    const livehd::lsp_index::Entry* first = nullptr;
    for (const auto& e : entries) {
      if (e.file != g_sem.buffer_rel || e.start_line == 0) {
        continue;
      }
      // A qualified access (`lib.my_add`) can never be a bare local: match the
      // full chain only, or a same-named local would shadow the member.
      if (tok.root.empty() ? (e.name != tok.name) : (e.name != tok.chain)) {
        continue;
      }
      if (first == nullptr) {
        first = &e;
      }
      const uint32_t sl = e.start_line - 1;
      const uint32_t sc = e.start_col ? e.start_col - 1 : 0;
      if (sl > line0 || (sl == line0 && sc > char0)) {
        continue;  // starts past the cursor
      }
      if (reach == nullptr || e.start_line > reach->start_line
          || (e.start_line == reach->start_line && e.start_col >= reach->start_col)) {
        reach = &e;  // >= : same statement recorded twice (declare+store) — later facts win
      }
    }
    if (const auto* hit = (reach != nullptr) ? reach : first; hit != nullptr) {
      render = hit->render;
      rsl    = tok.line0;
      rsc    = tok.scol0;
      rel    = tok.line0;
      rec    = tok.ecol0;
    }
  }

  // (b) a function / pub name (no symbol-table def entry): `my_add : comb`.
  // Cross-file member (`lib.my_add`) prefers the exporter's own def entry so
  // the full type renders; a lambda kind is the fallback.
  if (render.empty() && !tok.name.empty()) {
    const Pub_def* p = nullptr;
    if (!tok.root.empty()) {
      const auto target = binding_target(tok.root);
      if (!target.empty() && target.find('.') == std::string::npos) {
        p = find_pub(target, tok.name);
      }
    } else {
      const auto target = binding_target(tok.name);  // `const f = import("unit.entity")`
      if (!target.empty()) {
        const auto dot = target.rfind('.');
        if (dot != std::string::npos) {
          p = find_pub(target.substr(0, dot), target.substr(dot + 1));
        }
      }
      if (p == nullptr) {
        for (const auto& q : g_sem.pubs) {
          if (q.name == tok.name && q.span.file == g_sem.buffer_rel) {
            p = &q;
            break;
          }
        }
      }
    }
    if (p != nullptr) {
      const livehd::lsp_index::Entry* src = nullptr;
      for (const auto& e : entries) {  // exporter-side def entry (pub values)
        if (e.name == p->name && e.file == p->span.file) {
          src = &e;
        }
      }
      if (src != nullptr && p->kind == "value") {
        render = src->render;
      } else {
        render = tok.name + " : " + p->kind + " " + p->unit + "." + p->name;
      }
    } else if (tok.root.empty()) {
      for (const auto& u : g_sem.units) {  // non-pub lambda in this buffer
        if (u.lambda_kind.empty() || u.file != g_sem.buffer_rel) {
          continue;
        }
        const auto dot = u.unit.rfind('.');
        if (dot != std::string::npos && std::string_view(u.unit).substr(dot + 1) == tok.name) {
          render = tok.name + " : " + u.lambda_kind;
          break;
        }
      }
    }
    if (!render.empty()) {
      rsl = tok.line0;
      rsc = tok.scol0;
      rel = tok.line0;
      rec = tok.ecol0;
    }
  }

  // (c) an IO param of the innermost lambda covering the cursor: `a : u8`.
  if (render.empty() && !tok.name.empty() && tok.root.empty()) {
    const Lnast_io_entry* io_hit = nullptr;
    uint32_t              io_ln  = 0;
    for (const auto& u : g_sem.units) {
      if (u.file != g_sem.buffer_rel || !u.span.start_line) {
        continue;
      }
      const uint32_t cl = line0 + 1;
      if (cl < *u.span.start_line || (u.span.end_line && cl > *u.span.end_line)) {
        continue;
      }
      for (const auto* v : {&u.inputs, &u.outputs}) {
        for (const auto& e : *v) {
          if (e.name == tok.name && (io_hit == nullptr || *u.span.start_line > io_ln)) {
            io_hit = &e;
            io_ln  = *u.span.start_line;
          }
        }
      }
    }
    if (io_hit != nullptr) {
      render = tok.name + " : ";
      if (io_hit->kind == Io_kind::boolean) {
        render += "bool";
      } else if (io_hit->kind == Io_kind::string) {
        render += "string";
      } else if (io_hit->bits > 0) {
        render += io_hit->is_signed ? 's' : 'u';
        render += std::to_string(io_hit->bits);
        if (io_hit->has_range) {
          render += "(bw_min=" + std::to_string(io_hit->range_min) + ", bw_max=" + std::to_string(io_hit->range_max) + ")";
        }
      } else {
        render += "int";
      }
      rsl = tok.line0;
      rsc = tok.scol0;
      rel = tok.line0;
      rec = tok.ecol0;
    }
  }

  // (d) statement-span coverage (the Phase B behavior, now file-filtered) —
  // only off an identifier (operators/assignment glyphs of a statement show
  // its dst). An unresolved identifier stays null rather than answering with
  // the covering statement's dst (or an inliner temp's) unrelated type.
  if (render.empty() && tok.name.empty()) {
    for (const auto& e : entries) {
      if (e.file != g_sem.buffer_rel) {
        continue;
      }
      const uint32_t sl          = e.start_line ? e.start_line - 1 : 0;
      const uint32_t sc          = e.start_col ? e.start_col - 1 : 0;
      const uint32_t el          = e.end_line ? e.end_line - 1 : sl;
      const uint32_t ec          = e.end_col ? e.end_col - 1 : sc;
      const bool     after_start = (line0 > sl) || (line0 == sl && char0 >= sc);
      const bool     before_end  = (line0 < el) || (line0 == el && char0 <= ec);
      if (after_start && before_end) {
        render = e.render;
        rsl    = sl;
        rsc    = sc;
        rel    = el;
        rec    = ec;
        break;
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
  if (render.empty()) {
    w.Null();
  } else {
    w.StartObject();
    w.Key("contents");
    w.StartObject();
    w.Key("kind");
    w.String("plaintext");
    w.Key("value");
    w.String(render.data(), static_cast<rapidjson::SizeType>(render.size()));
    w.EndObject();
    w.Key("range");
    w.StartObject();
    w.Key("start");
    write_position(w, rsl, rsc);
    w.Key("end");
    write_position(w, rel, rec);
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
    } else if (method == "textDocument/definition" || method == "textDocument/declaration") {
      handle_definition(doc);  // 2n Phase C
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
