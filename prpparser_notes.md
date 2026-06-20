# prpparse integration & upgrade notes (LiveHD side)

Maintainer notes for the `@prpparse` dependency — what it is, how LiveHD pulls it
in, how to bump the pin, and what to re-check so an upstream update merges
smoothly. Written 2026-06-20 alongside an upstream modernization pass (see
"Recent upstream change" below).

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
2. If the new prpparse uses new hhds API, bump the `@hhds` `git_override` commit too.
3. **Diff upstream `prpparse/BUILD` against `//packages:prpparse.BUILD`** and port
   any changes (new srcs/globs, new deps, copts). This is the easiest step to
   forget — the two BUILD files drift independently.
4. Verify:
   - LiveHD: `bazel test //inou/prp/...` (includes `prpparse_stream_test`).
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

## Hardening parity — optional LiveHD-side change

Upstream's `prpparse/BUILD` now bounds-checks the parser
(`-D_GLIBCXX_ASSERTIONS`, traps OOB on `std::vector`/`string_view`/`array`) and
adds `-fstack-protector-strong`. **Those flags do not reach LiveHD builds**,
because LiveHD uses `//packages:prpparse.BUILD`, not the upstream `prpparse/BUILD`.
To get the same safety posture for `@prpparse` inside LiveHD, add them to the
overlay (warnings stay off; hardening is orthogonal to `-w`):

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
