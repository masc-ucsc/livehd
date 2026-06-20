# prpparse integration & upgrade notes (LiveHD side)

Maintainer notes for the `@prpparse` dependency — what it is, how LiveHD pulls it
in, how to bump the pin, and what to re-check so an upstream update merges
smoothly. Written 2026-06-20 alongside an upstream modernization pass (see
"Recent upstream change" below).

**Current pin (2026-06-20):** `@prpparse` and `@tree_sitter_pyrope` are both at
tree-sitter-pyrope `ee587553d30a2b1d434c97808ad765df1469afb1`
(integrity `sha256-0IZpOHnoe53LMAkviTThXWWBvj3Uh+V0uP+rtIzyI3E=`). That bump was
behavior-preserving on the parser side (the modernization below) but carried a
**C→C++ migration of the prpfmt formatter** — see "prpfmt C→C++" below for the
overlay change it forced. `@hhds` was *not* bumped (no new hhds API). No
`inou/prp` source change was needed (the changed prpparse pieces are all private;
the prpfmt C ABI `prpfmt_format_string` is unchanged, so `lhd/lhd_pyrope.cpp`
still compiles as-is).

## What prpparse is

`prpparse` is the hand-written, recursive-descent **Pyrope parser** that became
the `inou/prp` front-end, replacing the generated tree-sitter grammar (so neither
`@tree-sitter` nor `@tree-sitter-pyrope` is a front-end dependency anymore — the
tree-sitter runtime is still pulled separately, but only for the `lhd pyrope fmt`
*formatter*, `@tree_sitter_pyrope//:prpfmt`).

- **Source of truth:** `tree-sitter-pyrope/prpparse/` (a subdir of the
  tree-sitter-pyrope repo). C++23, bazel.
- **Output:** an `hhds` `Prp_tree` (materialized from an arena of lightweight
  `Ast` nodes). Consumed by `inou/prp/prp2lnast.{hpp,cpp}` via the node facade
  `inou/prp/prp_ast_facade.hpp`.
- **Design:** fail-fast (first syntax error throws `Parse_error`; no recovery,
  no ERROR nodes). `string_view` discipline (tokens/nodes hold byte offsets /
  views into the source buffer — no per-token allocation). Arena-allocated AST.
- It is kept a **differential-oracle** of `grammar.js`: it accepts exactly what
  the tree-sitter grammar accepts (parses all corpus files). That parity is the
  upstream regression gate (`make test-all` in tree-sitter-pyrope).

## How LiveHD consumes it (the load-bearing details)

In `MODULE.bazel`:

```python
http_archive(
    name       = "prpparse",
    build_file = "//packages:prpparse.BUILD",         # <-- overlay, see below
    strip_prefix = "tree-sitter-pyrope-<commit>/prpparse",
    urls       = ["https://github.com/masc-ucsc/tree-sitter-pyrope/archive/<commit>.zip"],
    integrity  = "sha256-...",
)
```

**The repo's own `prpparse/BUILD` is NOT used by LiveHD.** Bazel overlays
`//packages:prpparse.BUILD` onto the fetched archive. That overlay is
hand-maintained here and must be kept in sync with upstream's `prpparse/BUILD`
(srcs glob, `*.def` textual headers, the `@hhds` dep, and any copts). Key bits of
the overlay:

- `include_prefix = "prpparse"` → consumers `#include "prpparse/lexer.hpp"`,
  while prpparse's own bare internal includes (`#include "ast.hpp"`) still resolve
  same-directory. **Header-name hygiene:** upstream renamed its `diag.hpp` →
  `prp_diag.hpp` precisely so the repo-root `-iquote` bazel adds for this dep
  cannot shadow LiveHD's core `diag.hpp` / `parser.hpp`. Do not reintroduce a
  bare `diag.hpp`/`parser.hpp` in prpparse.
- `copts = ["-std=c++23", "-w"]` — treated as an external dep, so warnings are
  off. (See the hardening note below: the overlay does **not** carry the
  bounds-checking flags the standalone build now does.)
- `deps = ["@hhds//hhds:core"]`. `@hhds` is pinned in `MODULE.bazel` via
  `git_override`; the pin comment flags the `Source_locator::span_minter/reserve`
  API that prpparse relies on. **A prpparse bump may require an hhds bump.**
- Upstream's `tests/` are excluded by the overlay — they run only in the
  standalone tree-sitter-pyrope repo, not in LiveHD.

Consumers / tests in LiveHD: `inou/prp/prp2lnast.{hpp,cpp}`,
`inou/prp/prp_ast_facade.hpp`, and `inou/prp/prpparse_stream_test.cpp`
(`@prpparse//:prpparse`).

**Co-development** (edit prpparse locally without re-pinning): swap the
`http_archive` for the `new_local_repository`/`local_path_override` stanza already
commented in `MODULE.bazel`, pointing at `../tree-sitter-pyrope/prpparse`.

## To bump the prpparse pin

1. Update `<commit>` in both `urls` and `strip_prefix`, then recompute `integrity`
   (one-liner is in the `MODULE.bazel` comment):
   `curl -sL .../archive/<commit>.zip | openssl dgst -sha256 -binary | base64`
   **`@prpparse` and `@tree_sitter_pyrope` share this archive** (one roots at the
   `prpparse/` subdir, the other at the repo root) — bump *both* stanzas to the
   same commit + integrity in lockstep, or the formatter and the parser fall out
   of sync.
2. If the new prpparse uses new hhds API, bump the `@hhds` `git_override` commit too.
3. **Diff upstream `prpparse/BUILD` against `//packages:prpparse.BUILD`** and port
   any changes (new srcs/globs, new deps, copts). This is the easiest step to
   forget — the two BUILD files drift independently. Do the same for the prpfmt
   side: `//packages:prpfmt.BUILD` is hand-maintained against `prpfmt/Makefile`
   (there is no upstream `prpfmt/BUILD`); the C↔C++ split and `-std`/`_GNU_SOURCE`
   handling live there — see "prpfmt C→C++" below.
4. Verify:
   - LiveHD: `bazel test //inou/prp/... //lhd/tests:lhd_fmt_test` (the former
     includes `prpparse_stream_test`; the latter is the authoritative formatter
     test). NB: the full `//inou/prp/...` run is parallel-heavy — an occasional
     `rc=-9` (SIGKILL sandbox flake) on a `comptime`/`v2prp` case clears on a
     `--nocache_test_results` rerun in isolation; it is not a pin regression.
   - Upstream (in tree-sitter-pyrope): `make test-all` — bazel unit tests +
     corpus accept-parity vs tree-sitter (the differential oracle).

## Recent upstream change (2026-06-20 modernization cleanup)

A small C++23-hygiene/safety pass on prpparse, all **behavior-preserving**
(verified: `prpparse_cli --sexp` tree + exit-code byte-identical across all 426
corpus files — 415 accept / 11 reject, same split as tree-sitter). If a future
bump straddles these commits, the moved pieces are:

- `parse_tuple_item(bool* plain = nullptr)` → an overload pair
  `parse_tuple_item()` / `parse_tuple_item(bool& plain)` (raw out-pointer removed).
- `Bracket_guard` / `Scope_guard`: `Parser*` → `Parser&`, copy/move deleted.
- `parse_subexpr`: manual `toks_/pos_/ebd_` save/restore → an RAII `State_guard`
  (now exception-safe; was a latent leak-of-state if a hole sub-parse threw).
- `Lexer::error(std::string,std::string,...)` → `(const char*, const std::string&,...)`
  to match `Parser::error`.
- `prpparse/BUILD`: added `PRPPARSE_COPTS` = `-std=c++23` + COPTS +
  `-D_GLIBCXX_ASSERTIONS -fstack-protector-strong`. **Standalone build only** —
  see the next section.

These mirror a parallel C→C++20 modernization of the sibling `prpfmt` formatter
(in the same repo), where the wins were larger because that code was literal C.
prpparse was already idiomatic C++23, so only these few items applied.

## prpfmt C→C++ (the ee58755 bump's load-bearing overlay change)

The same archive backs `@tree_sitter_pyrope` (the `lhd pyrope fmt` formatter via
`//packages:prpfmt.BUILD`). As of ee58755 the formatter is **C++** (`prpfmt/ir.cc`
+ `prpfmt/prpfmt.cc`, was `ir.c`/`prpfmt.c`); only the generated tree-sitter
parser/scanner (`src/parser.c` + `src/scanner.c`) stay C. The overlay had to
change two things:

- **srcs:** `prpfmt/ir.c` → `prpfmt/ir.cc`, `prpfmt/prpfmt.c` → `prpfmt/prpfmt.cc`.
- **copts:** drop the old `-std=gnu11`. The overlay now has *mixed* C/C++ TUs in
  one `cc_library`; a per-target `-std=` is passed to *both* languages and would
  reject one of them. Rely on `.bazelrc`'s global `BAZEL_CONLYOPTS=-std=gnu17`
  (C) and `--cxxopt=-std=c++23` (C++) instead, and add `-D_GNU_SOURCE` so the
  POSIX `open_memstream`/`clock_gettime` the formatter calls stay visible under
  the strict-ANSI `-std=c++23` (they were visible before only because `-std=gnu11`
  is a GNU dialect). `extern "C" tree_sitter_pyrope()` and the `extern "C"` guard
  in `prpfmt_api.h` keep the C-parser link and the LiveHD embed working.

The genrule (`gen_ts_symbols`) and `includes`/`deps` are unchanged. The grammar
(`grammar.js`/`src/parser.c`) did **not** change across this bump, so the
formatter's accept/reject set and `ts_symbols.h` are identical — the one corpus
file the formatter rejects (`lead_word_op_cont.prp`) is a *pre-existing*
front-end/grammar divergence (prpparse's hand-written lexer accepts line-leading
`or`/`and` continuations; the tree-sitter grammar does not), not a bump
regression. Authoritative check: `//lhd/tests:lhd_fmt_test`.

## Hardening parity — LiveHD-side change (APPLIED in the ee58755 bump)

Upstream's `prpparse/BUILD` bounds-checks the parser (`-D_GLIBCXX_ASSERTIONS`,
traps OOB on `std::vector`/`string_view`/`array`) and adds
`-fstack-protector-strong`. Those flags do **not** flow through the upstream
`prpparse/BUILD` (LiveHD uses `//packages:prpparse.BUILD`), so they were ported
into the overlay's `copts` (warnings stay off; hardening is orthogonal to `-w`):

```python
copts = [
    "-std=c++23",
    "-w",
    "-D_GLIBCXX_ASSERTIONS",
    "-fstack-protector-strong",
],
```

`-D_FORTIFY_SOURCE=2` (prpfmt's third hardening flag) needs an optimized build,
so leave it to `-c opt` / `.bazelrc` rather than the unconditional copts. The
repo `.bazelrc` already defines sanitizer configs; exercise them periodically:

```
bazel test --config=asan  //inou/prp:prpparse_stream_test
bazel test --config=ubsan //inou/prp:prpparse_stream_test
```

(ASan/UBSan also cover the one spot `_GLIBCXX_ASSERTIONS` cannot — the lexer's raw
`const char*` numeric/operator matchers, which index with hand-rolled `< n`
guards rather than std:: containers.)

## Invariants to preserve across updates

- **string_view discipline:** `Token::text` and `Ast` spans are views/offsets into
  the `Source_buffer`, which outlives the parse. Don't introduce owning copies.
- **Arena stability:** the AST arena is a `std::deque<Ast>` precisely because raw
  `Ast*` escape and must stay valid for the parse's lifetime. Don't switch it to a
  reallocating container (e.g. `std::vector<Ast>`).
- **Fail-fast / exceptions:** the parser throws `Parse_error`; LiveHD builds with
  `-fexceptions` (`.bazelrc`). Keep it.
- **Header prefix discipline:** consumers see `prpparse/<hdr>`; no bare
  `diag.hpp`/`parser.hpp` in prpparse (clashes with LiveHD core).
