//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "diag.hpp"
#include "lnast.hpp"
#include "lnast_builder.hpp"
#include "lnast_ntype.hpp"
#include "prp_ast_facade.hpp"

namespace prpparse {
class Parser;
struct Diag;
}  // namespace prpparse

// Names a statement WRITES and READS, for the nested-lambda capture slice
// (Prp2lnast::plan_streamed_captures).
struct Prp_stmt_rw {
  absl::flat_hash_set<std::string> writes;
  absl::flat_hash_set<std::string> reads;
  absl::flat_hash_set<std::string> declares;  // the statement opens the name's scope (no earlier writer is it)
  absl::flat_hash_set<std::string> callees;   // the callee names of the calls it makes
  // Never replayed: a lambda definition, or a `type`/`enum` declaration (a
  // hoisted comptime entity the nested lambda already sees; its name is in
  // `declared_type`).
  bool                             opaque = false;
  std::string                      declared_type;
};

class Prp2lnast {
protected:
  // Parsing (prpparse: a hand-written recursive-descent Pyrope parser whose Ast
  // is walked through the tree-sitter-shaped facade in prp_ast_facade.hpp).
  std::string                              prp_file;
  std::string                              src_filename;  // source path, for diagnostic spans
  std::string                              src_relpath;   // workspace-relative form for SourceId minting
  // Own the buffer + parser so the arena-allocated Ast (and the source bytes the
  // facade reads spans from) outlive the whole lowering.
  std::unique_ptr<prpparse::Source_buffer> prp_buf;
  std::unique_ptr<prpparse::Parser>        prp_parser;
  TSNode                                   ts_root_node;  // facade handle over the Ast root

  // Mint a SourceId for `node`'s span in the Lnast's locator (0 when the node
  // is null or no source path is known).
  hhds::SourceId mint_src(const TSNode& node) const;

  // RAII: while alive, every def-bearing LNAST node created is stamped with
  // the given statement-span id (Lnast::set_pending_srcid), so SSA temps and
  // op nodes are attributable. Nested statements narrow it; the destructor
  // restores the enclosing statement's id.
  class Pending_src {
  public:
    Pending_src(Lnast& ln, hhds::SourceId id) : ln_(ln), saved_(ln.pending_srcid()) {
      if (id != hhds::SourceId_invalid) {
        ln_.set_pending_srcid(id);
      }
    }
    ~Pending_src() { ln_.set_pending_srcid(saved_); }
    Pending_src(const Pending_src&)            = delete;
    Pending_src& operator=(const Pending_src&) = delete;

  private:
    Lnast&         ln_;
    hhds::SourceId saved_;
  };

  // Emit a structured diagnostic (the LiveHD docs §3) anchored at
  // `node`'s source span (best-effort byte + line/col, pre-sourcemap), then
  // abort the parse. `category` per §4 (e.g. "syntax", "name", "type").
  [[noreturn]] void report_error(const TSNode& node, std::string_view code, std::string_view category, std::string message,
                                 std::string_view hint = {}) const;
  // Location-less variant (span = null) for defensive sites with no TS node.
  [[noreturn]] void report_error(std::string_view code, std::string_view category, std::string message,
                                 std::string_view hint = {}) const;
  // Bridge a prpparse syntax diagnostic (fail-fast Parse_error) into the LiveHD
  // diag sink and abort the parse, so a syntax error surfaces like any other.
  [[noreturn]] void report_prpparse_error(const prpparse::Diag& d) const;
  // Variant anchored at the source span previously attached (via `attach_loc`)
  // to an LNAST node — for post-build checks that walk the tree and no longer
  // hold the originating TSNode. Falls back to the location-less form when the
  // node carries no loc.
  [[noreturn]] void report_error(const Lnast_nid& nid, std::string_view code, std::string_view category, std::string message,
                                 std::string_view hint = {}) const;
  // Located-error variant from a pre-captured span (2f-stream): the deferred
  // undefined-read check runs after the parse arena was reset per construct, so
  // the originating TSNode is gone — the read site's span was captured into
  // Read_site at record time and is replayed here.
  [[noreturn]] void report_error_at(uint32_t start_byte, uint32_t end_byte, uint32_t start_line, uint32_t start_col,
                                    uint32_t end_line, uint32_t end_col, std::string_view code, std::string_view category,
                                    std::string message, std::string_view hint = {}) const;

  // Emit a *warning* diagnostic (does NOT abort the parse — the program still
  // compiles). Anchored at `node`'s source span like report_error. Used for
  // lint-style findings such as a statement whose computed value is unused.
  void report_warning(const TSNode& node, std::string_view code, std::string_view category, std::string message,
                      std::string_view hint = {}) const;

  // Build a diag Span from a Tree-sitter node (byte range + 1-based line/col).
  [[nodiscard]] livehd::diag::Span span_of_node(const TSNode& node) const;
  // Stage an error diagnostic (pass = "inou.prp") into the sink and abort the
  // parse. Every report_error/report_prpparse_error overload funnels through
  // here so the stage+throw shape lives in exactly one place.
  [[noreturn]] void stage_error(livehd::diag::Span span, std::string_view code, std::string_view category, std::string message,
                                std::string_view hint) const;

  // True when `n` (the CST subtree of a statement-position expression) can have
  // an observable side effect — a function call, an assignment, an attribute
  // write (`::[attr=…]`), a lambda, or an embedded scope / control /
  // if / match. A pure expression (arithmetic, reads, tuples, constants) has
  // none, so discarding its value at statement position is useless.
  [[nodiscard]] bool expr_has_side_effects(TSNode n) const;

  // Stamp `node`'s span as the LNAST node's SourceId (minted through
  // the Lnast's Source_locator), overriding the statement-level pending id
  // with a finer anchor — an `if` condition, a call site, a range expression —
  // so the consuming diagnostic points at the exact construct. No-op when
  // `node` is null.
  void attach_loc(const Lnast_nid& idx, const TSNode& node);

  // Reject a bare-`0b…` binary literal: Pyrope requires an explicit sign on
  // binary constants (`0ub…` unsigned / `0sb…` signed). `text` is the literal
  // source text; `node` anchors the diagnostic span. No-op for any other text.
  void check_binary_literal_sign(std::string_view text, const TSNode& node) const;

  // Reject a typed scalar declaration whose literal initializer's kind
  // contradicts the declared type (bool/int/string are distinct — no implicit
  // conversion). Shared by variable declarations (`mut c:bool = 10`) and
  // function parameters (`comb f(b:bool = 3)`) so both report identically.
  // `inner_type` is the type node (bool_type/uint_type/…); `anchor` pins the
  // span. No-op (returns) unless both kinds are statically known and differ;
  // when they differ it reports and aborts (does not return).
  void check_decl_init_kind(std::string_view name, const Lnast_node& value, TSNode inner_type, const TSNode& anchor) const;

  // Primitive type token (`u32`/`s8`/`signed`/`unsigned`/`bool`/`string`)
  // as it appears in does/equals/case operand
  // position (plain `identifier` there — the grammar's *_type nodes only
  // exist in type contexts).
  static bool         is_prim_type_token(std::string_view txt);
  // Lower one `does`/`equals`/`case` operand. An integer type-call
  // (`int(max=…,min=…)` / `u8(min=…)`) lowers to a
  // `declare(tmp, prim_type_int(max,min), 'type')` and returns the tmp ref; a
  // bare type-token identifier returns a ref WITHOUT registering a read site
  // (constprop decodes the name to kind+envelope; a real variable of that
  // name — e.g. `u2` — still wins because the fold consults the symbol
  // table / type-info first). Anything else falls through to expr_to_node.
  std::optional<Lnast_node> int_type_literal(TSNode n);
  Lnast_node          does_operand_to_node(TSNode n);
  // Fold the integer-only expression subset admitted by integer type bounds,
  // generic defaults and statically known declaration values. Names resolve
  // through the visible bindings (Binding::int_value; `Z.[bits]` through
  // Binding::range; `cfg.w` through a file-scope comptime tuple), so a type
  // such as `signed(bits=W)` or `signed(max=(1 << W)-1)` is canonicalized
  // before uPass consumes its prim_type_int(max,min) node.
  std::optional<Dlop> resolve_type_int_value(TSNode n) const;
  // Shared by emit_type_expr (declare side) and does_operand_to_node
  // (operand side): classify an integer type keyword and refine its (max,min)
  // bounds from a `(max=…, min=…, bits=…)` constraint/argument tuple. Returns
  // false when `kw` is not an integer type keyword.
  bool                int_type_call_bounds(std::string_view kw, TSNode tup, std::string& max_txt, std::string& min_txt);
  // 2f-type_bound — lower any integer type bound in `type_cast_node` that
  // resolve_type_int_value cannot fold, into statements emitted AT THE CURRENT
  // STATEMENT POSITION, and stash the resulting refs in prelowered_int_bounds_.
  // Must run BEFORE the declaration's `attr_set` cluster head: the emitted
  // statements have to precede the `declare` that consumes them, and
  // rewrite_decls_to_declare merges a CONTIGUOUS attr_set/type_spec run.
  void                prelower_type_bounds(TSNode type_cast_node);
  // Same, for the TYPE node itself (a `f<T=unsigned(bits=N)>` argument). An
  // array type recurses into its element and prelowers each dimension that is
  // an expression this front end cannot fold (see prelowered_array_dims_).
  void                prelower_int_type_bounds(TSNode ty);
  // The dimension node of an `array_length` slot: the prelowered ref, the
  // folded `[n]`, or the written text (a bare name the runner folds).
  Lnast_node          array_dim_to_node(TSNode len);
  // The expression of an `array_length` slot that has to be lowered to
  // statements to fold (`[N+1]` over a generic); null for an empty slot, a
  // bare name, a range, or an expression that folds here.
  TSNode              prelowerable_array_dim(TSNode len) const;
  // One `<head>(%tmp, l, r)` statement; returns the fresh %tmp ref.
  Lnast_node          emit_bound_binop(Lnast_ntype::Lnast_ntype_int head, const Lnast_node& l, const Lnast_node& r);

  // Reject `a = 3` with no prior `mut`/`const`/declare (or param/output) visible
  // in scope. Runs on the producer tree (pre-upass), so it sees only source-level
  // declarations — no inliner/SSA-synthesized stores to false-positive on.
  void check_undeclared_writes() const;
  // `visible` carries names declared in ENCLOSING scopes (re-declaring one of
  // them here is shadowing); `seed_here` pre-populates the CURRENT scope (a func
  // body seeds its params/outputs — re-declaring those at the body top level is
  // the normal output-init pattern, not shadowing).
  // Minimal scoped symbol table done in ONE walk: each scope pushes a frame of
  // its declared names onto `scope_stack`; a `declare` reusing a name in an
  // ENCLOSING frame is shadowing, a `store` to a name in NO live frame is
  // undeclared. Sibling scopes (if/else arms, separate blocks) push independent
  // frames, so reusing a name across branches is fine. `barrier` is the lowest
  // visible frame index (a func body is a fresh namespace → barrier = its frame).
  // O(N * depth), no per-scope set copy.
  void check_writes_in_scope(const Lnast_nid& scope_stmts, std::vector<absl::flat_hash_set<std::string>>& scope_stack,
                             size_t barrier, const absl::flat_hash_set<std::string>& seed_here = {}) const;

  // Reject reading a name that is not visible at the read site (04-variables.md
  // "Variable scope": a variable is visible from its declaration to the end of
  // its scope, in program order — so a read before the declaration, after the
  // declaring block closed, or of a never-declared name is a compile error).
  // `read_sites_` is populated by `identifier_to_node` on the value
  // (for_lvalue=false) path, so it records only genuine reads with their
  // source TSNode (→ located diagnostic) plus the stmts frame being built at
  // read time (→ scope resolution); the func_call callee name uses
  // `create_ref` directly, so builtins like `cputs` are never recorded. Runs
  // on the producer tree, after the LNAST is built.
  void        check_undefined_reads() const;
  // 2c-wire — enforce the single-driver net rules on every `wire` declaration:
  // exactly one driver (one assignment, or one if/match that covers every path);
  // a second driver, an undriven wire, or an incompletely-driven wire are errors.
  // Runs on the pre-elaborate tree (before lnastfmt drops a dead first write).
  void        check_wire_drivers() const;
  void        check_wire_scope(const Lnast_nid& node) const;
  // Names readable anywhere regardless of program order: function names
  // (func_def) and type/enum declarations — comptime entities, forward
  // references allowed.
  static void collect_hoisted_names(const Lnast& ln, const Lnast_nid& node, absl::flat_hash_set<std::string>& hoisted);

  // LNAST output. `builder` co-owns `lnast` and is the canonical home for
  // the current `idx_stmts` cursor, tmp-ref minting, and frontend-agnostic
  // stmt emitters (cleanup_todo §3.4).
  std::shared_ptr<Lnast> lnast;
  std::shared_ptr<Lnast> root_lnast_;
  Lnast_builder          builder;

  // Pending overflow kind ("wrap"/"sat") to apply to the next assignment.
  // Set by process_{description,scope_statement} when the new grammar's
  // statement-level `wrap`/`sat` prefix is seen; consumed by process_assignment.
  // ts_node_type strings have static lifetime, so a string_view is safe.
  std::string_view pending_overflow_kind;

  // Tree-sitter currently doesn't always attach a `comb foo(...) { ... }`
  // body to the lambda's `code` field; for some inputs the body parses as
  // a separate sibling scope_statement next to the lambda. process_lambda
  // detects that pattern, uses the sibling as the body, and records its
  // start byte here so the enclosing walker (process_description /
  // process_scope_statement) skips it on the next iteration. Without
  // this, the body content would also emit as an orphan top-level stmts.
  std::unordered_set<uint32_t> consumed_lambda_body_starts;

  // Early-`return` desugar state (2f-return_leak). A function whose body has a
  // `return` inside a loop is lowered with a synthesized `mut <flag> = false`
  // declared at its body top; `return_flag_name_` holds that flag's name while
  // lowering the body ("" otherwise, i.e. the clean no-flag scope-rewrite mode).
  // `in_return_loop_` is true while lowering inside such a loop's body, so a
  // `return` there becomes `<flag> = true; break`. `synth_return_flag_count_`
  // uniquifies the flag name across (possibly nested) functions.
  std::string return_flag_name_;
  bool        in_return_loop_          = false;
  int         synth_return_flag_count_ = 0;

  // Counter for file-unique hoisted in-tuple method names (`call` →
  // `call__t1`). The bundle field keeps the source name; the func_def (and
  // hence the registry unit) gets the unique one, so two bundles may both
  // define `call` without colliding.
  int hoisted_lambda_count_ = 0;

  // (Removed: the parse-time `comptime_tuples_` shape tracker. All
  // for-loop / tuple-iteration unrolling now happens in the upass runner, which
  // reads tuple shapes from the live constprop bundle — see prp2lnast emits a
  // raw `for` node and uPass_runner::unroll_for / try_tuple_shape.)

  // Every variable READ lowered by `identifier_to_node` (for_lvalue=false):
  // the name, its source TSNode (diagnostic span), and the position the
  // builder was at — `scope` is the stmts frame being appended to and
  // `before` its last statement at read time (invalid = frame still empty).
  // check_undefined_reads (after the LNAST is built) resolves each site
  // lexically: declarations at/before `before` in `scope`, then outward
  // through the enclosing frames (a func_def boundary switches to its
  // params/outputs + enclosing comptime bindings only).
  enum class Generic_read : uint8_t { none, default_value, argument };
  struct Read_site {
    std::string  name;
    // Captured diagnostic span of the read (start/end byte + 1-based line/col),
    // taken at record time. Streaming (2f-stream) resets the parse arena between
    // top-level constructs, so the originating TSNode (Ast*) does NOT outlive the
    // walk — the undefined-read check runs afterward and emits from these fields.
    uint32_t     start_byte = 0, end_byte = 0;
    uint32_t     start_line = 0, start_col = 0, end_line = 0, end_col = 0;
    Lnast_nid    scope;
    Lnast_nid    before;
    // A named-type reference (`x:T`, `x:[N]T`) routed through the same
    // visibility check as value reads — an undefined type errors, while hoisted
    // types, forward refs, generic params, and imports resolve normally. Only
    // the error wording differs (unknown-type vs undefined-read).
    bool         is_type = false;
    // A free-function call CALLEE (`foo(...)`, not a method/UFCS, not a
    // built-in). Validated like a read so a never-defined callee errors; only
    // the wording differs (undefined-call).
    bool         is_call = false;
    // A read in a generic default or a call-site generic argument, which
    // names a type, lambda or comptime const declared EARLIER (user ruling
    // 2026-09-28 (33)); only the wording differs (unknown-generic-name).
    Generic_read generic = Generic_read::none;
  };
  std::vector<Read_site>                                                        read_sites_;
  // Per-scope (stmts node) declaration index: name -> EARLIEST child position that
  // declares it (the same declarations read_is_visible's stmt_declares matches).
  // Built once in check_undefined_reads so read_is_visible resolves a frame in
  // O(1) — `earliest_decl_idx <= boundary_idx` — instead of scanning the scope's
  // siblings up to the read per read (which was O(reads * scope) on big scopes).
  // Source order also matters: elif headers are emitted before ALL arm bodies,
  // so their IR positions alone would expose a later initializer to an earlier
  // arm. A zero source offset leaves unlocated/generated declarations unchanged.
  struct Read_declaration {
    int      index;
    uint32_t start_byte;
  };
  // Keyed by the scope node (hhds::Node_class is abseil-hashable).
  mutable absl::flat_hash_map<Lnast_nid, absl::flat_hash_map<std::string, Read_declaration>> read_scope_decls_;
  // Child position of each direct child of a stmts scope (the boundary nodes
  // read_is_visible compares against). Built in the same walk.
  mutable absl::flat_hash_map<Lnast_nid, int>                                   read_child_index_;
  // The `declare` nodes process_tick_statement synthesizes for a tick's implicit
  // loop variable (`clock`). They are exempt from the no-shadowing rule: no
  // source line wrote them, so "rename the inner/loop variable" is unactionable
  // advice for the one shape that trips it — a test parameter named like the
  // loop var, which `lhd sim` reports as "collides with a test parameter".
  absl::flat_hash_set<Lnast_nid>                                                tick_loop_var_decls_;
  // Names that behave as declarations at the root stmts of a directly
  // streamed lambda: its io/generic names plus the comptime captures its body
  // prologue declares.  A file wrapper leaves this empty.
  absl::flat_hash_set<std::string>                                              streamed_scope_names_;

  struct Destination_state {
    std::shared_ptr<Lnast>                                                lnast;
    Lnast_builder                                                         builder;
    std::vector<Read_site>                                                read_sites;
    absl::flat_hash_map<Lnast_nid, absl::flat_hash_map<std::string, Read_declaration>> read_scope_decls;
    absl::flat_hash_map<Lnast_nid, int>                                   read_child_index;
    absl::flat_hash_set<Lnast_nid>                                        tick_loop_var_decls;
    absl::flat_hash_set<Lnast_nid>                                        decl_shape_seed_stores;
    absl::flat_hash_set<std::string>                                      streamed_scope_names;
    absl::node_hash_map<Lnast_nid, Prp_stmt_rw>                           capture_rw;
  };
  std::vector<Destination_state>              destination_stack_;
  // The current tree's capture_rw: the read/write sets of the statements a
  // capture slice already scanned. A complete statement never changes, except a
  // call patch_streamed_capture_calls extends (it clears the cache). A node map:
  // a slice holds pointers to the entries while it adds more.
  absl::node_hash_map<Lnast_nid, Prp_stmt_rw> capture_rw_;

  void push_streamed_destination(std::string_view name, std::string_view kind, bool timecheck_off, std::string_view lg_name);
  std::shared_ptr<Lnast> pop_streamed_destination();
  void                   finalize_current_lnast();
  // True iff `rs.name` is visible at the recorded site (see Read_site).
  bool                   read_is_visible(const Read_site& rs) const;

  // Names introduced by in-flight constructs whose declarations are not
  // statement-level LNAST nodes, readable from the point they are pushed:
  // tuple-literal fields (a field initializer can read EARLIER fields of the
  // same literal — 04-variables.md "Tuple scope") and lambda signature
  // params/outputs (a default value can read EARLIER params — `comb f(a,
  // b=a+5)`). identifier_to_node skips recording reads of these names.
  // Suspended (std::exchange) while a lambda BODY is processed: bodies see
  // params via the func_def signature in read_is_visible, not this stack.
  std::vector<std::vector<std::string>> inflight_name_scopes_;
  bool                                  name_in_inflight_scope(std::string_view name) const;

  // Record a named-type reference (`x:T`, array base `x:[N]T`) as a type
  // Read_site so check_undefined_reads validates that the type symbol exists
  // (an undefined `:potato` errors; hoisted/forward/generic/import names pass).
  void record_type_name_read(const TSNode& type_node);
  // Record every name the expression `e` reads as a value Read_site (see
  // identifier_to_node), for an expression that is NOT lowered where it is
  // written: a generic default or a call-site generic bind that the runner
  // re-reads by name, a port or tuple-field width bound that does not fold. An
  // undeclared name there is then an undefined-read at the read instead of a
  // nil the runner binds or a dropped width (ruling 2026-09-28 #24).
  void record_name_reads(TSNode e, Generic_read generic = Generic_read::none);
  // A value Read_site for identifier `id` (canonical `name`) at the current
  // builder position, or against streamed_scope_names_ when `no_frame`.
  void push_read_site(TSNode id, std::string name, bool no_frame, Generic_read generic = Generic_read::none);
  // Lowering the signature of a streamed lambda: it has no stmts frame yet, so
  // a read resolves against streamed_scope_names_ (see read_is_visible).
  bool is_streamed_signature() const;
  // What a CST expression reads, for the checks that inspect an expression
  // without lowering it (record_name_reads, is_comptime_expression,
  // comptime_default_node). `names` are the identifiers read as VALUES: a field
  // name after a dot, an attribute name, a named-argument / attribute /
  // assignment key, the declared name of a tuple-TYPE field, a callee and the
  // `std` of `std.<member>` are not. A lambda literal is not entered.
  struct Expr_reads {
    std::vector<TSNode> names;
    std::vector<TSNode> callees;  // the callee of every call
    std::vector<TSNode> lambdas;  // lambda literals
  };
  Expr_reads expr_reads(TSNode e, bool enter_attribute_reads) const;

  // A type position spelled `I<N>` (the removed signed sized spelling, `S<N>`
  // replaced it): a tailored rename hint.
  void check_type_name_spelling(const TSNode& node) const;
  void report_removed_int_type(livehd::diag::Span span, std::string_view text) const;

  // Top
  void process_description();
  // Post-build pass: rewrite statement-level assign/tuple_set → store.
  // Post-build pass: merge the declaration cluster
  // (attr_set(type)+attr_set(comptime)+type_spec) into one `declare(var, TYPE,
  // const(mode))`. Rebuilds the body into a fresh tree (replace_body) since
  // in-place subtree deletion is avoided on the LNAST tree. The value (if any)
  // stays a separate `store`.
  void rewrite_decls_to_declare();

  // Statements
  void                                  process_statement(TSNode n);
  void                                  process_scope_statement(TSNode n, Lnast_nid target_stmts);
  // Scope attributes `{ ::[abc="…", color=…] … }` (2opt-freq B): strict parse
  // of the block's attribute_sq into a region id (+ optional abc string
  // literal node). Region-id bookkeeping: string labels intern per file, auto
  // ids skip explicitly used ones.
  bool                                  parse_scope_attributes(TSNode attr_list_node, int& region_id, TSNode& abc_rv,
                                                               std::vector<std::pair<std::string, TSNode>>& options);
  int                                   alloc_region_id();
  absl::flat_hash_map<std::string, int> region_label_ids_;
  absl::flat_hash_set<int>              region_ids_used_;
  int                                   next_region_id_    = 1;
  int                                   region_marker_seq_ = 0;  // unique marker target per block
  // Shared body for process_description / process_scope_statement: walks ALL
  // children of `parent` (named + anonymous) so the grammar's hidden `wrap`/
  // `sat` overflow tokens are visible.
  void                                  walk_statement_block(TSNode parent);
  // Lower a scope's children from index `from`, desugaring early `return`
  // (2f-return_leak): a guarded `if cond { … return }` pushes the rest of the
  // scope into a synthesized `else`; a bare `return` drops the rest.
  void                                  lower_children_range(TSNode parent, uint32_t from);
  // Recover the hidden `wrap`/`sat` overflow keyword from the raw source gap
  // `[prev_end, gap_end)` before a statement (the prpparse CST does not
  // materialize it). Returns "wrap"/"sat"/"" (last identifier run in the gap,
  // comments stripped). Shared by lower_children_range and the streaming
  // top-level driver (2f-stream).
  std::string_view                      scan_overflow_in_gap(uint32_t prev_end, uint32_t gap_end) const;
  // 2f-stream top-level driver: lower one construct pulled from the parse stream,
  // tracking the overflow-prefix gap scan + prev_end across calls.
  void                                  lower_streamed_top_level(TSNode c, std::string_view& pending_overflow, uint32_t& prev_end);
  bool                                  is_guarded_return_if(TSNode s, TSNode& cond_out, TSNode& then_out);
  void                                  process_assignment(TSNode n);
  void                                  process_declaration_statement(TSNode n);
  void                                  process_while_statement(TSNode n);
  void                                  process_for_statement(TSNode n);
  void                                  process_loop_statement(TSNode n);
  // `tick`/`step` — the simulation cycle loop of a `test` block and its cycle
  // advance. A tick is NOT a comptime loop: its iteration count is assumed
  // unknown (see lnast_nodes.def), so it gets its own node rather than reusing
  // the always-unrolled `while`/`for` lowering.
  void                                  process_tick_statement(TSNode n);
  void                                  process_step_statement(TSNode n);
  // Name of the implicit tick loop variable (the 0-based cycle index): the
  // `clocks=(name=ratio)` lvalue if present, else `clock`. Must agree with
  // prp_sim.cpp's tick_one_entry.
  std::string                           tick_loop_var_name(TSNode tick);
  uint32_t                              tick_path_steps(TSNode n, uint32_t before);
  // An always-true RECOMPUTED ref (`1 == 1`) for `loop`/`while true` conditions
  // (a literal `const 'true'` cond makes the runner skip the in-loop body fold,
  // so the break-guard never resolves). `lower_infinite_loop` builds the shared
  // `while (1==1) { if (1==1) {body} else {break} }` shape from the body node.
  Lnast_node                            emit_always_true_ref();
  void                                  lower_infinite_loop(TSNode code, TSNode loc);
  void                                  process_control_statement(TSNode n);
  // Statement-table entry point (the table needs the plain `void(TSNode)`
  // member signature); forwards to the named variant with no override.
  void                                  process_lambda_statement(TSNode n);
  // `hoist_name` (when non-empty) overrides the func_def's emitted name —
  // used by tuple_to_node to hoist an in-tuple method (`comb call(ref
  // self,…){…}` inside a bundle literal) under a file-unique name while the
  // bundle field keeps the source method name.
  void                                  process_lambda_statement_named(TSNode n, std::string_view hoist_name);
  void                                  process_enum_assignment(TSNode n);
  // `std` is the built-in namespace: a lambda, type, enum or import alias of
  // that name is an error at its declaration (`name` is its name node).
  void                                  reject_std_declaration(TSNode name, std::string_view what);
  // One parsed entry of an enum definition (either source form).
  struct Enum_entry {
    std::string name;
    TSNode      type_node{};   // per-entry payload type (`Yellow:Rgb`); null when none
    TSNode      value_node{};  // explicit value expression; null when none
    bool        has_type  = false;
    bool        has_value = false;
  };
  // The entries of an `enum NAME = (…)` statement's `values` tuple.
  void        parse_enum_statement_entries(TSNode values, std::vector<Enum_entry>& entries);
  // The ordinal rule of payload-less entries (03-bundle.md "Enumerate"): true
  // (a sequence each explicit value resets) when some entry has an explicit
  // value, false (one-hot) when none has. A nested `enum(…)` entry is one-hot
  // hierarchical, never an ordinal seed. Shared by lower_enum_def and
  // enum_entry_value so the two never number an entry differently.
  static bool enum_entries_sequential(const std::vector<Enum_entry>& entries);

  // Shared lowering for `enum NAME[:T] = (…)` and `const NAME = enum(…)`.
  // Emits one value-carrier bundle per entry — the auto/explicit ordinal
  // (one-hot when no entry is explicit, sequential otherwise; 03-bundle.md
  // "Enumerate") or the constructed payload (`Yellow:Rgb = v` → `Rgb(v)`,
  // resolved by the runner's init-construction hook) — each tagged with a
  // `__enumentry` identity attr ('NAME.entry') that powers enum-aware `in`,
  // `string()` and interpolation. Returns the ref of the enum-type bundle
  // (entry name → carrier).
  //
  // Hierarchical enums (`bird = enum(eagle, parrot)`): one fresh one-hot bit
  // per node in DFS pre-order — `parent_bits` ORs the ancestor bits into each
  // descendant ("the lower hierarchy level bits are kept", 03-bundle.md). The
  // parent carrier is the children bundle plus a `__enumval` attr holding the
  // parent's own encoding (its bare bit, for int()/==/in). `bit_counter` is
  // shared across the whole tree (nullptr starts a fresh top-level counter).
  //
  // `enc` (optional) collects the value range of every entry, hierarchical
  // nodes included (see Enum_encoding).
  struct Enum_encoding {
    std::optional<Dlop> min;
    std::optional<Dlop> max;
    bool                known = true;  // false: a payload entry, or a value that does not fold here
    void                add(const std::optional<Dlop>& v) {
      if (!v || !v->is_integer() || v->has_unknowns()) {
        known = false;
        return;
      }
      if (!min || v->lt_op(*min)->is_known_true()) {
        min = *v;
      }
      if (!max || v->gt_op(*max)->is_known_true()) {
        max = *v;
      }
    }
  };
  //
  // A one-hot enum needs one bit per node; at most kMaxOneHotEnumBits fit the
  // integer encoding (`loc` anchors the error past that).
  Lnast_node lower_enum_def(std::string_view enum_name, TSNode enum_level_type, const std::vector<Enum_entry>& entries, TSNode loc,
                            int64_t parent_bits = 0, int* bit_counter = nullptr, Enum_encoding* enc = nullptr);
  // Declare the hidden integer encoding alias (Lnast::enum_encoding_type) of
  // enum `enum_name` from the range `lower_enum_def` collected into `enc`;
  // nothing for a payload enum. Shared by `enum NAME = (…)` and
  // `const NAME = enum(…)`.
  void       emit_enum_encoding_alias(std::string_view enum_name, TSNode enum_level_type, const Enum_encoding& enc, TSNode loc);
  // Parse the `enum(...)` argument list into Enum_entry rows (shared by the
  // `const X = enum(...)` expression path and nested hierarchical entries).
  void       parse_enum_definition_entries(TSNode enum_def_node, std::vector<Enum_entry>& entries);
  // Splice an `enum(...NAME)` spread operand into entries (const string → field
  // name; const tuple → its named fields). See const_rvalue_nodes_.
  void       expand_enum_spread(TSNode operand, std::vector<Enum_entry>& entries);
  void       process_type_statement(TSNode n);
  void       process_import_statement(TSNode n);

  static constexpr int kMaxOneHotEnumBits = 63;

  // Whether a type (an enum's level type, or an entry's type) is an INTEGER
  // type: `uN`/`sN`, `unsigned`/`signed`/`uint` with or without a constraint
  // (`unsigned(bits=4)`), or a visible `type X = …` alias of one. nullopt when
  // it is not (a payload type); else its folded (max, min), when it folds here.
  using Int_type_range = std::optional<std::pair<Dlop, Dlop>>;
  std::optional<Int_type_range> int_type_of(TSNode type_or_cast) const;

  // `pub` exports + the `import` builtin.
  // check_pub_value_decl: a `pub` value declaration must be file-scope `const`.
  void                       check_pub_value_decl(TSNode decl_node, std::string_view kind);
  // plain_string_literal_text: unquoted body of a plain comptime string
  // literal expression ('…' raw, or "…" with no interpolation); nullopt else.
  std::optional<std::string> plain_string_literal_text(TSNode n);
  // emit_import_call: the canonical marked-builtin call shape
  // `func_call(target, const "import", const '<unit>')` — what `lhd scan`
  // (collect_imports) matches and the upass resolver folds.
  void                       emit_import_call(const Lnast_node& target, std::string_view unit, TSNode loc_node);
  // lower_import_call: validate + lower the expression form `import("unit")`.
  void                       lower_import_call(TSNode call_node, TSNode arg_tuple, const Lnast_node& target);

  void     process_test_statement(TSNode n);
  unsigned simulation_test_depth_ = 0;
  void     process_spawn_statement(TSNode n);
  void     process_impl_statement(TSNode n);

  // Expressions: returns an Lnast_node (ref or const) naming the result
  Lnast_node expr_to_node(TSNode n);
  // The default of an io entry kept in its slot (an output's, a mod/pipe
  // input's). A streamed lambda's signature has no statement frame, so there
  // the default must be a comptime_default_node.
  Lnast_node io_default_to_node(TSNode def, TSNode param);
  // A default with no statement position to be computed in (the io slot of a
  // streamed signature, the reset value of a `-> (reg q)` output): a literal,
  // or a value that folds to a compile-time integer here. Anything else is an
  // undefined-read (a name nothing declares) or default-not-comptime.
  Lnast_node comptime_default_node(TSNode def, TSNode param);
  // User ruling 2026-09-28 (34): a `mod`/`pipe` default is a compile-time
  // constant, so reading another (runtime) input of the same lambda as a
  // VALUE is default-not-comptime. An attribute read (`a.[bits]`) is comptime.
  void       check_default_reads_no_input(TSNode def, TSNode param);
  // Whether the default `def` of an INPUT rides the body prologue (the
  // `__default` io sentinel plus `store(__default_<in>, def)`) instead of the
  // io slot: every `comb` default, and a `mod`/`pipe` default that does not
  // fold here (a generic, a comptime const, a comb call, an if-expression, an
  // attribute read of an input: ruling 34) -- the unit's own walk folds it
  // (a template's specialization once the generics are bound) and pass.upass
  // writes the constant back into the io slot, or reports default-not-comptime.
  bool       input_default_in_prologue(TSNode def, std::string_view lambda_kind, bool is_io_output, bool is_vararg);

  // The compile-time constant a default folds to HERE, without lowering
  // anything: a literal, a foldable integer expression (resolve_type_int_value)
  // or an enum entry. nullopt when it needs statements to compute.
  std::optional<Lnast_node> front_end_default_value(TSNode def);

  // The integer encoding of entry `entry` of a visible `enum NAME = (…)`
  // statement or `const NAME = enum(…)` binding (`Color.Green` as a comptime
  // default), assigned exactly as lower_enum_def does. nullopt for a payload or
  // hierarchical enum, or an explicit value that does not fold here.
  std::optional<Dlop> enum_entry_value(std::string_view enum_name, std::string_view entry);

  Lnast_node binary_expr_to_node(TSNode n);
  Lnast_node unary_expr_to_node(TSNode n);
  Lnast_node if_expr_to_node(TSNode n, bool need_result = true);
  Lnast_node match_expr_to_node(TSNode n, bool need_result = true);
  Lnast_node bit_selection_to_node(TSNode n);
  Lnast_node member_selection_to_node(TSNode n);

  // Bit-range mask synthesis shared between bit_selection reads
  // (`bit_selection_to_node`) and bit-range writes (the `bit_selection` arm of
  // `process_lvalue_for_assign`). `sel_node` is the `select` TS child of a
  // `bit_selection`. Returns the Lnast_node to use as the mask operand of
  // `get_mask` / `set_mask`: a `Dlop` when both range endpoints are
  // integer-literal (encoded as a bitmask via `Dlop::get_mask_value`), or a
  // ref to a freshly-emitted `range` / `shl` LNAST stmt for dynamic cases.
  Lnast_node compute_bit_mask_ref(TSNode sel_node, int* const_width = nullptr);
  Lnast_node emit_range_node(const Lnast_node& start, const Lnast_node& end);

  // The number of bits a `#[...]` select covers when compute_bit_mask_ref could
  // not fold it (a bound is a NAME, not a literal): a const when the bounds
  // fold through visible compile-time names, else a ref to the statements
  // computing it, for the runner to fold. nullopt for an open range or a width
  // that reads a runtime value.
  std::optional<Lnast_node> bit_range_lane_width(TSNode sel_node);

  Lnast_node attribute_read_to_node(TSNode n);
  Lnast_node dot_expression_to_node(TSNode n);
  Lnast_node function_call_expr_to_node(TSNode n);
  Lnast_node interpolated_string_to_node(TSNode n);
  // A double-quoted string body split into pieces: literal text (escapes
  // decoded; a hole with no expression — `{}` or `{/* c */}` — stays literal
  // text with its comments removed) or one `{expr[:spec]}` hole. The holes are
  // found with prpparse's own hole scanner (Lexer::istring_hole_end), so a
  // `}`/`{`/`:`/`/*` inside a comment, nested string or backtick name in a hole
  // never ends it, opens another, or starts a format spec.
  struct Istring_piece {
    std::string text;  // literal (when !is_hole)
    TSNode      expr{};
    std::string spec;  // format spec without the `:` (may be empty)
    bool        is_hole = false;
  };
  std::vector<Istring_piece> istring_pieces(TSNode n) const;
  Lnast_node tuple_to_node(TSNode n, bool is_square, bool field_types_on_target = false);
  Lnast_node identifier_to_node(TSNode n, bool for_lvalue);
  Lnast_node constant_text_to_node(std::string_view text);
  // `expr::[attr=…]` write-side attribute bracket in expression position.
  Lnast_node attribute_set_to_node(TSNode n);

  // Type handling
  void                 emit_type_spec(const Lnast_node& target, TSNode type_cast_node);
  // 2f-nested_type — stamp `path.<field>` type_specs for a tuple-shaped type,
  // descending into nested tuple fields (leaves only). See the definition.
  void                 emit_tuple_type_field_specs(std::string_view path, TSNode tuple_node);
  // The inner `tuple` node when `type_cast_node`'s type is a tuple SHAPE.
  TSNode               tuple_type_inner(TSNode type_cast_node) const;
  void                 emit_attribute_list(const Lnast_node& target, TSNode attribute_list_node);
  // Rulings on one attribute's value: `sync` is deprecated, a runtime `*_pin`
  // value needs `ref`. `rv` is null for a flag-only attribute.
  void                 check_attribute_value(TSNode item, std::string_view key, TSNode rv) const;
  // Catch typical attribute-name mistakes (`initial`→`init`, `clk`→`clock_pin`,
  // `bit`→`bits`, …) at parse time with a targeted hint. `has_value` is true
  // when the attribute carries `=value` — it disambiguates `[clock=x]` (meant
  // `clock_pin`) from the valid flag-only classification `clk::[clock]`.
  void                 reject_common_mistakes_attr_name(TSNode node, std::string_view name, bool has_value) const;
  void                 emit_type_expr(const Lnast_nid& type_index, TSNode type_node);
  // A bare built-in type word (`U8`, `Signed`, `Bool`, `Clock`, ...) as a type
  // child of `parent` (no constraint tuple); `anchor` locates a width error.
  void                 emit_bare_type_word(const Lnast_nid& parent, std::string_view word, TSNode anchor);
  // A bare type word used as a VALUE (`(a=U8, b=S20)`, `U8.[max]`): a
  // type-mode declare of a tmp, returned as a ref.
  Lnast_node           type_word_value(TSNode n, std::string_view word);
  // Comptime vector/matrix dimension extraction for a type_cast whose `type`
  // field is an `array_type` chain (e.g. `:[N][M]T`). Returns dims outer→inner
  // when every dimension is an integer-literal length; empty otherwise.
  std::vector<int64_t> extract_array_dims(TSNode type_cast_node) const;

  // func_def input/output arg helpers. The arg shape is
  //   assign(ref name, <default | const "nil" | const "ref">, [type-subtree])
  // The type subtree is omitted when no `:Type` annotation is present. A
  // composite tuple type `(a:T, b:U)` is encoded as a `tuple_add` whose
  // children are recursive `assign` arg nodes.
  // Parameter-attribute carrier (`a::[comptime]`) — captured during arg
  // walking and replayed at body entry as `attr_set` (plus a `cassert` for
  // `comptime`, which doubles as the parameter-constraint check).
  struct Param_attr {
    std::string param;
    std::string key;
    std::string value;  // empty -> "true"
  };
  // lambda_kind/is_io_output: set only when called from the lambda io driver
  // (Interface contract — fully-typed pipe/mod ios, per-output
  // declared landing cycle on mod). Tuple-literal contexts leave them unset
  // and skip every interface check.
  void emit_arg_assign(const Lnast_nid& tuple_parent, TSNode typed_ident, TSNode definition_or_null, bool is_ref_mod,
                       std::vector<Param_attr>* attrs_out = nullptr, std::string_view lambda_kind = {}, bool is_io_output = false,
                       bool is_vararg_mod = false);
  void emit_arg_type(const Lnast_nid& assign_parent, TSNode type_node);

  // Current lambda-kind context while processing a body ("comb" /
  // "pipe" / "mod" / ...; empty stack = file scope). Gates `stage[N]`
  // declarations (mod-only) and `x@[N]` timecheck emission (mod/pipe only).
  std::vector<std::string> lambda_kind_stack_;

  // Parse a stage_decl's optional timing_slot to the (min,max)
  // stages const texts: stage[N] -> ("N","N") with N >= 1; stage[A..=B] /
  // stage[A..<B] -> ascending literal range; bare `stage` / `stage[]` ->
  // ("nil","nil") (toolchain picks). Anything else is a compile error.
  std::pair<std::string, std::string> parse_stage_slot(TSNode storage_node);

  // Emit `timecheck(ref name, const N, const N)` recording an
  // `x@[N]` cycle check (flop-free, inert — consumed by the future pipe/mod
  // typecheck pass). `x@[]` emits nothing (explicit opt-out). Errors when the
  // enclosing lambda is a comb (every comb value is at cycle 0) or the slot
  // is not a literal N >= 0.
  void maybe_emit_timecheck(TSNode timing_slot, TSNode id_node);

  // Resolve a `pipe_lambda` node's `depth` field to the (min,max)
  // stages pair: pipe[N] -> (N,N); pipe[A..=B] -> (A,B); pipe[A..<B] ->
  // (A,B-1); bare pipe -> (1,0) (max 0 = unconstrained). pipe[0], zero-min
  // ranges, descending ranges and non-literal depths are compile errors.
  std::pair<int64_t, int64_t> parse_pipe_depth(TSNode pipe_lambda_node);

  // 2f-type_bound — max/min for an integer type bound this front end cannot
  // fold to a constant, most importantly a GENERIC width:
  //   mod m<N=5>(…) { reg r:unsigned(bits=N) = nil … }
  // `N` only has a value at SPECIALIZATION, long after prp2lnast, so no
  // front-end folder can ever see it. The bound has to reach LNAST as a REF
  // that the runner folds once the generic is bound (bake_decl_pre_step does
  // exactly this for an array `[N]` dimension already). Until this existed the
  // `bits=` arm simply did nothing on a fold miss: the declared width was
  // dropped with NO diagnostic and the register was silently mis-sized.
  //
  // Keyed by the constraint tuple's start byte. Filled by
  // prelower_type_bounds at the DECLARATION — the only site with a statement
  // position to emit the desugar into — and consumed by int_type_call_bounds.
  struct Prelowered_bounds {
    Lnast_node max{Lnast_node::create_invalid()};
    Lnast_node min{Lnast_node::create_invalid()};
  };
  absl::flat_hash_map<uint32_t, Prelowered_bounds> prelowered_int_bounds_;
  // Array dimensions written as an expression that does not fold here
  // (`[N+1]`, `[N*2]` with a generic N), keyed by the `array_length` start
  // byte: the ref to the statements prelower_int_type_bounds emitted. The
  // runner folds the ref in place like an integer bound (bake_decl_pre_step).
  absl::flat_hash_map<uint32_t, Lnast_node>        prelowered_array_dims_;
  // Constraint tuples prelower_type_bounds actually examined. A PORT/return
  // type, a tuple field type and a `f<signed(bits=N)>` generic argument have no
  // statement position to desugar an unfoldable bound into, so prelowering
  // never runs there. Without this the "not a compile-time value" error fired
  // at those sites too and turned `mod m<N=8>(a:unsigned(bits=N))` -- which
  // used to compile -- into a hard error.
  absl::flat_hash_set<uint32_t>                    prelower_visited_;
  // 2f-generic_port_width — PORT/return types of a GENERIC lambda whose integer
  // bound does not fold (`mod m<N=1>(a:unsigned(bits=N * 4))`). The signature
  // is lowered before the body stmts frame exists, so the desugar cannot be
  // emitted at the declaration like prelower_type_bounds does for a body `mut`.
  // The io store gets a placeholder `nil` bound now; flush_deferred_port_bounds
  // (run once the body frame opens) emits the desugar into the BODY PROLOGUE and
  // rewrites the io leaves IN PLACE to the resulting refs. The runner folds them
  // at specialization (uPass_runner::deferred_port_type), never this front end.
  struct Pending_port_bound {
    TSNode    type_cast;
    Lnast_nid store;  // io `store(ref name, default, prim_type_int(max,min)[, stages])`
  };
  std::vector<Pending_port_bound>               pending_port_bounds_;
  bool                                          lambda_has_generics_ = false;  // lowering a generic lambda's signature
  bool                                          int_type_has_unfoldable_bound(TSNode type_node) const;
  // The innermost element of an array type (`[N][M]unsigned(bits=N)` ->
  // `unsigned(bits=N)`); any other type node is returned as is.
  TSNode                                        array_elem_type(TSNode type_node) const;
  void                                          flush_deferred_port_bounds();
  void                                          defer_port_bound(TSNode type_cast, const Lnast_nid& store);
  // 2c-wire — declarations whose INLINE TUPLE type made emit_type_spec emit a
  // shape-seeding `store(<name>, %tuple_tmp)`. That store carries the TYPE's
  // field layout, not a user assignment, but it is structurally identical to
  // one, so the single-driver counter booked it as driver #1 and
  //     wire value:(data:u8) = nil
  //     value = source            // the ONLY user assignment
  // was rejected as `wire-multiple-drivers` -- with the span on the DECLARATION.
  // It also MASKED the opposite check: such a wire never driven at all looked
  // driven once, so `wire-undriven` could not fire for a tuple-typed wire.
  // Names are discounted by exactly ONE seed; two real assignments still count 2.
  // Keyed by the seed STORE node, and checked only against the scope that holds
  // it: a name-keyed set let a typed tuple `k` in one lambda hide a real
  // rebind of an unrelated `k` elsewhere in the file.
  absl::flat_hash_set<Lnast_nid>                decl_shape_seed_stores_;
  // File-scope import bindings (`const pk = import("unit")`, `import "unit" as
  // pk`): replayed at the top of every streamed lambda, which has no enclosing
  // tree to read them from.
  absl::flat_hash_map<std::string, std::string> capture_import_bindings_;
  absl::flat_hash_set<std::string>              streamed_function_names_;

  // ── Nested-lambda capture (rulings 2026-09-27 #4 and 2026-09-28 #24,
  //    06-functions.md) ────────────────────────────────────────────────────
  // Name lookup is lexical, but a nested lambda sees only the COMPTIME
  // bindings of its enclosing scopes: `comptime const`, a plain `const` whose
  // value is a compile-time constant (`const x = 2`, `const w = N * 4`, see
  // is_comptime_expression), the generics of an enclosing lambda, imports,
  // types and lambdas. Reading an enclosing RUNTIME binding (input/output, a
  // `const` computed from a runtime value, `mut`, `reg`, `wire`) from a nested
  // lambda is a compile error: the value must be passed as an input.
  //
  // One frame per lexical scope being lowered: frame 0 is the file scope, a
  // lambda pushes a BOUNDARY frame (its generics, inputs and outputs) and every
  // block body a plain frame, so a declaration is forgotten when its scope
  // closes. A read resolves innermost-first; crossing a boundary frame before
  // the declaring frame is found makes it a capture.
  //
  // How a capture gets its value into a STREAMED lambda (its own LNAST):
  //   * comptime const -- the statements that compute it (its declaration plus
  //     whatever earlier statements of the enclosing trees it depends on: a
  //     file-scope `mut` + `for`, a `comptime const W = N + 1`, ...) are copied
  //     into the lambda's body prologue, so the lambda computes exactly the
  //     value, and the declared type, the enclosing scope computes. Runtime
  //     names and temps the copy writes are renamed private.
  //   * enclosing generic / loop index -- per-specialization values: they
  //     become IMPLICIT generics of the lambda, bound `<G=G>` at every call in
  //     the enclosing scope, so each specialization of the parent binds its own.
  enum class Bind_kind : uint8_t { runtime, comptime, generic };
  // What this front end knows statically about a binding. Kept ON the binding,
  // so it closes with its scope and a later same-name binding never reads a
  // stale value:
  //   * int_value -- a `const NAME = <compile-time integer expression>`, or a
  //     `mut` with declaration-time-capture semantics (recorded on the decl,
  //     UPDATED by a later statement-level plain write of another resolvable
  //     expression, ERASED by any other write: a runtime rhs, a compound op, or
  //     any write inside an if/for/while/match/lambda body, see
  //     conditional_depth_). Integer type bounds and `@[NAME]`/`stage[NAME]`/
  //     `pipe[NAME]` timing slots read it (resolve_cycle_value).
  //   * range -- the declared (max, min) of a binding with an integer type
  //     whose bounds fold (`Z:u6`, `Z:unsigned(bits=5)`, an `a:u8` input), so
  //     an attribute read (`<N=Z.[bits]>`, `const W = a.[bits]`) folds at the
  //     declaration.
  //   * fold_const -- a plain `const` this front end did not prove comptime
  //     (user ruling 2026-09-28 (32), fold-based capture): like a comptime
  //     const, a nested lambda that reads it replays its computation, and it
  //     is visible there iff that replay reaches no runtime root (an input or
  //     output, a `reg`/`wire`, a runtime loop value, a `mod`/`pipe`
  //     instance), i.e. iff its value folds to a compile-time constant.
  //   * replayable -- a `mut`/`const`, whose declaring and writing statements
  //     a capture replay may copy. Every other runtime binding is a root.
  //   * decl_depth -- conditional_depth_ at the declaration: a later write at
  //     the SAME depth is an unconditional statement of the binding's own
  //     scope and keeps int_value; a deeper one (an if/loop body) erases it.
  struct Binding {
    Bind_kind                            kind;
    std::optional<int64_t>               int_value;
    std::optional<std::pair<Dlop, Dlop>> range;
    bool                                 typed{false};  // declared with a type (`range` is its folded integer bounds)
    bool                                 fold_const{false};
    bool                                 replayable{false};
    // Bound to an UNNAMED tuple literal (`const t = (1, 2)`): a destructuring
    // of it binds by position (qa.md: named-vs-unnamed is a property of the
    // value, not of its spelling), and a rename slot against it is an error.
    bool                                 unnamed_tuple{false};
    uint32_t                             decl_depth{0};
  };
  // Whether a nested lambda may see `b`: a comptime binding, or a fold_const
  // (decided by its slice, see visible_in_lambda).
  static bool binding_visible_in_lambda(const Binding& b) { return b.kind == Bind_kind::comptime || b.fold_const; }
  struct Capture_frame {
    bool                                          lambda_boundary{false};
    bool                                          streamed{false};  // boundary of a streamed lambda
    // A `test` block's scope: no lambda (its reads are unrestricted), but a
    // local re-declaring a visible enclosing comptime binding is shadowing.
    bool                                          test_scope{false};
    std::string                                   lambda_name;  // diagnostics (the test name for a test_scope)
    absl::flat_hash_map<std::string, Binding>     names;
    std::vector<std::string>                      implicit_generics;  // boundary: enclosing generics it reads
    // Boundary: enclosing comptime consts / fold_consts computed from an
    // enclosing RUNTIME value (-> that value), which a replay cannot
    // recompute. A streamed lambda fills it when it plans its prologue; a
    // lambda kept as a func_def decides each fold_const at its first read
    // (fold_decided).
    absl::flat_hash_map<std::string, std::string> uncapturable;
    absl::flat_hash_set<std::string>              fold_decided;
    // The enum declarations of this scope, in SOURCE ORDER, re-lowered into the
    // prologue of every streamed lambda nested in it: an enum is a comptime
    // bundle whose entries a body reads (`Color.Green`), and the streamed
    // lambda has no enclosing tree to read it from. The CST clones live in
    // retained_arena_ for the same parse_next lifetime reason as
    // const_rvalue_nodes_.
    std::vector<TSNode>                           enum_decls;
    // The named lambdas declared in this scope -> true for a `comb`. A call
    // to a comb with comptime arguments is a comptime value; a `mod`/`pipe`
    // call is an instance, a runtime value (see is_comptime_expression).
    absl::flat_hash_map<std::string, bool>        lambdas;
    // The `type`/`enum` names declared in this scope (check_signature_shadow).
    absl::flat_hash_set<std::string>              types;

    // The scalar INTEGER type aliases of this scope (`type Nib = u4`, `type W
    // = unsigned(bits=N)`) -> their (max, min) when it folds here; an enum
    // level type spelled through one is an integer level (int_type_of).
    absl::flat_hash_map<std::string, std::optional<std::pair<Dlop, Dlop>>> int_type_aliases;
  };
  std::vector<Capture_frame> capture_frames_{1};
  // What the destructuring statement being lowered knows about its right-hand
  // side (set by process_assignment around process_lvalue_for_assign).
  // `legacy`: a call or tuple literal (rename slots keep the call-prefix form
  // `(x=f.a) = (f(), g())`). `rooted`: any other value (a variable, a field):
  // a rename slot's path starts at the value's own fields. `unnamed_var`: a
  // variable bound to an unnamed tuple; a rename slot against it is an error.
  enum class Destructure_rhs : uint8_t { legacy, rooted, unnamed_var };
  Destructure_rhs destructure_rhs_{Destructure_rhs::legacy};
  class [[nodiscard]] Capture_frame_guard {
  public:
    Capture_frame_guard(std::vector<Capture_frame>& frames, Capture_frame frame) : frames_(frames) {
      frames_.emplace_back(std::move(frame));
    }
    ~Capture_frame_guard() { frames_.pop_back(); }
    Capture_frame_guard(const Capture_frame_guard&)            = delete;
    Capture_frame_guard& operator=(const Capture_frame_guard&) = delete;

  private:
    std::vector<Capture_frame>& frames_;
  };
  // Initializer bindings live only through their if/elif/else, match or while.
  // Use a constant-true if scope so outer writes take the normal merge path.
  class [[nodiscard]] Initializer_scope_guard {
  public:
    Initializer_scope_guard(Prp2lnast& lower, TSNode n);
    ~Initializer_scope_guard();
    Initializer_scope_guard(const Initializer_scope_guard&)            = delete;
    Initializer_scope_guard& operator=(const Initializer_scope_guard&) = delete;

  private:
    Prp2lnast&                         lower_;
    const Lnast*                      destination_;
    std::optional<Capture_frame_guard> capture_;
  };
  struct Capture_lookup {
    Bind_kind             kind;
    size_t                frame;              // the declaring frame
    std::optional<size_t> innermost_crossed;  // innermost lambda boundary between the read and `frame`
    const Binding*        binding;
  };
  Binding&                      note_binding(std::string_view name, Bind_kind kind);
  std::optional<Capture_lookup> lookup_capture(std::string_view name) const;
  Binding*                      find_binding(std::string_view name);
  // Report a read of an enclosing RUNTIME binding from a nested lambda, or of
  // an enclosing comptime const the lambda cannot capture (see uncapturable).
  void                          check_capture_read(std::string_view name, const TSNode& at) const;
  // check_capture_read + make an enclosing generic an implicit generic of every
  // streamed lambda the read crosses.
  void                          note_capture_read(std::string_view name, const TSNode& at);
  void                          note_implicit_generic(const Capture_lookup& hit, std::string_view name);
  // Whether the enclosing binding `hit` is visible where it is looked up: a
  // comptime binding, or a fold_const whose value folds there (ruling
  // 2026-09-28 (32); a fold_const read directly from a test block is).
  bool                          visible_in_lambda(std::string_view name, const Capture_lookup& hit);
  // A declaration in a nested lambda (or a test block) of a name an enclosing
  // scope binds visibly: that binding is visible (and replayed) here, so this
  // is shadowing, which Pyrope forbids.
  void                          check_capture_shadow(std::string_view name, const TSNode& at);
  // User ruling 2026-09-28 (35): a lambda input/output/generic (and ruling
  // (31) a test parameter) named like a visible enclosing const, type or
  // lambda is shadowing. Runs while the signature is lowered (the lambda's
  // boundary frame, or the test's frame, is the innermost).
  void                          check_signature_shadow(std::string_view name, const TSNode& at, std::string_view role);
  // Rvalue tmps that carry a comptime entity (an import namespace, an enum
  // type, a lambda value): a plain `const` bound to one is a comptime binding.
  absl::flat_hash_set<std::pair<const Lnast*, std::string>> comptime_rvalue_tmps_;
  void                                                      note_comptime_rvalue(const Lnast_node& tmp);
  bool                                                      is_comptime_rvalue(const Lnast_node& value) const;
  // True when the initializer `e` of a plain `const` is a compile-time
  // constant, which makes the const a comptime binding (ruling 2026-09-28 #24).
  bool                                                      is_comptime_expression(TSNode e) const;
  // The named lambda `name` resolves to, innermost scope first: a comb.
  bool                                                      resolves_to_comb(std::string_view name) const;
  // A bare NAMED lambda (`comb`/`mod`/`pipe` declared in an enclosing scope)
  // read as an operand value. Every lambda call needs parentheses
  // (06-functions.md), so `f == 3` is an error: write `f() == 3`.
  void                                                      check_lambda_used_as_value(TSNode operand);
  // A call a capture replay may copy (the value it computes is comptime when
  // its arguments are): a comb, a type cast, a `std` function, a lambda value.
  // A `mod`/`pipe` (or unknown) callee is an instance, a runtime root.
  bool                                                      callee_is_replayable(std::string_view callee) const;
  // (max, min) of an integer type whose bounds fold here (see Binding::range).
  std::optional<std::pair<Dlop, Dlop>>                      folded_int_type_range(TSNode ty) const;

  // Streamed-lambda prologue plan, computed before the signature (it can make
  // the lambda generic) and emitted once the body stmts exist.
  struct Capture_copy {
    std::shared_ptr<Lnast> src;
    Lnast_nid              nid;
    size_t                 depth;  // destination_stack_ index of `src`
  };
  struct Capture_plan {
    std::vector<Capture_copy>                                  stmts;  // program order
    // Per destination depth: the private names of the runtime names/temps the
    // copied statements write.
    std::vector<absl::flat_hash_map<std::string, std::string>> rename;
    std::vector<std::string>                                   seeds;  // the comptime names made visible
    // The seeds whose value this front end folded (an untyped comptime
    // integer): bound to it, never replayed (see plan_streamed_captures).
    std::vector<std::pair<std::string, int64_t>>               values;
  };
  Capture_plan plan_streamed_captures(TSNode lambda_node, TSNode code);
  void         emit_capture_plan(const Capture_plan& plan, const Lnast_nid& body_idx);
  // The statements a capture replay may copy: every statement visible at the
  // cursor of each enclosing tree (destination_stack_, depth = its index)
  // and, with `include_current`, of the tree being lowered (depth =
  // destination_stack_.size(), for a lambda kept as a func_def in it).
  struct Capture_visible {
    struct Stmt {
      Lnast_nid nid;
      size_t    depth;
    };
    std::vector<Stmt>                stmts;  // program order, outermost tree first
    std::vector<const Prp_stmt_rw*>  rws;    // nullptr: never replayed (a lambda, a type)
    absl::flat_hash_set<std::string> type_names;
  };
  Capture_visible   collect_capture_visible(bool include_current);
  // The backward capture slice of `seeds` over `vis`: which statements a
  // replay copies (the returned mask). A seed whose computation reaches a
  // runtime root lands in `uncapturable` (seed -> root) and is sliced out; the
  // enclosing generics the copies read land in `generics`. The `by_value`
  // names are bound to their folded value, never recomputed.
  std::vector<bool> slice_captures(const Capture_visible& vis, const std::vector<std::string>& seeds,
                                   const absl::flat_hash_set<std::string>&        by_value,
                                   absl::flat_hash_map<std::string, std::string>& uncapturable,
                                   std::vector<std::string>&                      generics) const;
  // A read of a fold_const `name` from a lambda kept as a func_def (no
  // streamed plan): decide it now, into that lambda's `uncapturable`.
  void              resolve_kept_lambda_fold_const(std::string_view name, const Capture_lookup& hit);

  // Implicit-generic binds of each streamed lambda, keyed "<defining scope
  // unit>\n<entity>" (see streamed_actuals_key): sibling scopes may each define
  // a same-named helper with different captures.
  absl::flat_hash_map<std::string, std::vector<std::string>> streamed_capture_actuals_;
  static std::string streamed_actuals_key(std::string_view scope_unit, std::string_view callee);
  void               append_streamed_capture_actuals(const Lnast_nid& fcall, std::string_view callee);
  void               patch_streamed_capture_calls(const std::shared_ptr<Lnast>& target, std::string_view callee,
                                                  const std::vector<std::string>& captures);

  // `const NAME = <string | tuple literal>` → the RHS CST node, kept so an
  // `enum(...NAME, …)` spread can splice NAME (a string becomes a field name; a
  // tuple's named fields are spliced in place) at lowering time — `enum(...a,
  // b=3, ...c)` lowers exactly like `enum("field", b=3, const foo=4)`
  // (2f-enum group D). Recorded only for top-level `const` decls with a simple
  // identifier lvalue. The RHS subtree is DEEP-COPIED into `retained_arena_`
  // (below) so it survives the streaming arena reset between constructs — the
  // spread can be in a LATER top-level statement than the const.
  absl::flat_hash_map<std::string, TSNode> const_rvalue_nodes_;
  // True while an enclosing scope's enum declaration (Capture_frame::enum_decls)
  // is re-lowered into a streamed lambda's prologue.
  bool                                     replaying_capture_enum_{false};
  // Persistent arena holding the cloned `const_rvalue_nodes_` RHS subtrees. The
  // streaming parser recycles its own arena per construct (2f-stream), so any
  // CST node a later statement still needs is cloned here instead, keyed off the
  // same `prp_buf` bytes (which outlive the parse). Small: only const string /
  // tuple rvalues that an `enum(...)` spread might reference.
  prpparse::Ast_arena                      retained_arena_;

  // Functions (comb/mod/pipe) declared with a `ref` parameter (e.g. `ref self`).
  // Such a call mutates the caller, so using its RESULT in a right-hand-side
  // expression (`mut a3 = a1.set_x(...)`) is illegal — only the in-place
  // statement form is allowed. Recorded by name as definitions are lowered
  // (top-down, so a defined-before-use call is caught). See find_ref_param_call /
  // process_assignment (2f-ufcs).
  absl::flat_hash_set<std::string> ref_param_funcs_;
  // First call (anywhere in `n`) to a `ref`-param function, else a null node.
  TSNode                           find_ref_param_call(TSNode n) const;
  // A UFCS call `x.f(…)` writes its receiver back when `f` takes `ref self`;
  // a callee this front end does not know (a tuple method, a lambda declared
  // later) may, so it counts as a write too.
  bool                             ufcs_writes_receiver(std::string_view callee) const;
  // A call writes `name` (a `ref` actual, a `ref self` receiver): the value is
  // no longer statically known (Binding::int_value).
  void                             note_call_write(std::string_view name);

  // Nesting depth of conditional / loop / nested-lambda bodies currently being
  // lowered. >0 means writes are not unconditional statement-level writes, so
  // they must not record or update a Binding::int_value (a conditional re-bind
  // makes a mut runtime-valued). Bumped via a Conditional_scope RAII guard
  // around if/for/while/match arm bodies and lambda bodies.
  uint32_t conditional_depth_ = 0;

  // True while lowering a `cassert`/`assert` condition (or message).
  // `.[bw_max]`/`.[bw_min]` reads are debug-only: legal here, a compile error
  // anywhere else (each elaboration may compute a different legal range, so
  // branching on one would not converge).
  bool in_assert_lowering_ = false;

  // Inside a `tick` body: a nested `tick` is rejected at parse time so the user
  // sees "cannot be nested" instead of upass.semacheck's shadowing error on the
  // implicit `clock` loop variable, a name their source never declares.
  bool in_tick_statement_ = false;

  // docs 05b: exactly one `step` per tick iteration. Before a tick body lowers,
  // a walk counts the `step`s on every PATH through it (an `if`/`match` arm is
  // an alternative: the body's count is its largest arm, so `step; if c {step}`
  // is two on one path); the start byte of each step that is a path's second
  // or later lands here, and process_step_statement reports it.
  absl::flat_hash_set<uint32_t> tick_extra_steps_;

  struct Conditional_scope {
    uint32_t* d;
    explicit Conditional_scope(uint32_t* dd) : d(dd) { ++*d; }
    ~Conditional_scope() { --*d; }
    Conditional_scope(const Conditional_scope&)            = delete;
    Conditional_scope& operator=(const Conditional_scope&) = delete;
  };

  // Resolve a timing-index CST node to a compile-time integer: a `constant`
  // node parses via Dlop::from_pyrope (is_integer + is_i); an `identifier`
  // node reads its visible Binding::int_value; anything else is std::nullopt (the
  // caller emits its own "literal or compile-time constant" diagnostic).
  std::optional<int64_t>     resolve_cycle_value(TSNode n) const;
  // The Binding::int_value of the binding identifier `id` reads (a read of an
  // enclosing runtime binding from a nested lambda is reported, see
  // capture_frames_).
  std::optional<int64_t>     visible_const_int(TSNode id) const;
  // A comptime const `n` reads whose value this front end did not fold (it is
  // computed by a loop, an `if`, a call...), for a diagnostic that says so
  // instead of asking for the comptime const it already is.
  std::optional<std::string> unfolded_comptime_read(TSNode n) const;

  struct Call_arg {
    bool        is_assign = false;
    bool        is_ref    = false;
    bool        is_ufcs   = false;  // the receiver of `obj.method(...)`
    bool        is_spread = false;  // `...rest` — expand the bundle's fields at the call
    std::string assign_key;
    Lnast_node  value;
  };
  std::vector<Call_arg> collect_call_args(TSNode arg_tuple);
  void                  add_call_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Call_arg>& call_args);
  // Explicit call-site generic bindings (`f<int,string>(…)` — grammar field
  // `generic`): type-arg refs collected BEFORE the fcall node exists, then
  // attached as `store(__generic_arg, type_ref [, const name])` children ahead
  // of the actuals. `name` is set for a NAMED bind (`f<T=u8>`, todo 3g C) and
  // empty for a positional one.
  struct Generic_call_arg {
    Lnast_node  value;  // the bound type/constant/lambda (a ref or const)
    std::string name;   // generic parameter name, or empty (positional)
  };
  std::vector<Generic_call_arg> collect_generic_args(TSNode call_node);
  void add_generic_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Generic_call_arg>& generic_args);

  // Lvalue helpers. `rhs_is_fcall` tells the lvalue_list path to bind by
  // name (return-field name) rather than position; otherwise positional
  // binding is used (the right behaviour for tuple-literal RHS such as
  // `(a, b) = (b+1, a)`). `rhs_fcall_name` carries the RHS callee text so
  // the rename form `(x = dox.b) = dox(...)` can validate the prefix
  // before the dot matches the call's function name. `overflow_kind` is
  // "wrap"/"sat" when the enclosing assignment carries that modifier — the
  // only context where a type cast on a non-declaring lvalue (`wrap c:u4 = v`)
  // is legal; otherwise a type on a re-assignment is rejected. When set, the
  // scalar leaf lowers the value through a `wrap|sat(v=…, type=<lhs>)` library
  // call before the store (replaces the old attr_set(wrap) tag).
  // `rvalue_comptime` says the initializer is a compile-time constant
  // (is_comptime_expression): a plain `const` it declares is a comptime binding.
  Lnast_node process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node,
                                       bool rhs_is_fcall = false, std::string_view rhs_fcall_name = {},
                                       std::string_view overflow_kind = {}, bool rhs_name_bindable = false,
                                       std::optional<int64_t> resolved_rvalue_int = std::nullopt, bool rvalue_comptime = false);

  // Helpers
  // Source text of a node, with a whole-node plain escaped identifier
  // canonicalized (`` `foo` `` -> `foo`, `` `foo[bar]` `` kept): `foo` == foo.
  std::string_view        get_text(const TSNode& n) const;
  // Decoded backtick names (`` `d\\e` `` is the identifier `d\e`: a backtick
  // name reads the string escapes). A deque, so a returned view stays valid.
  mutable std::deque<std::string> decoded_names_;
  // The exact source bytes (no canonicalization). Only for KEYWORD-literal
  // tests on an identifier node: an escaped keyword (`` `true` ``) is an
  // ordinary name, never the literal it spells.
  std::string_view        get_raw_text(const TSNode& n) const {
    return text_between(ts_node_start_byte(n), ts_node_end_byte(n));
  }
  static std::string_view trim(std::string_view s);
  std::string_view        text_between(uint32_t start, uint32_t end) const;

  // Get rvalue text even when the rvalue field is a hidden token (numbers,
  // bool/string literals, '?'). Returns the text between the '=' operator's
  // end and the end of the enclosing parent span.
  std::string rvalue_text_fallback(TSNode parent, TSNode after_field) const;
  // Get binary_expression right operand text when hidden.
  std::string binary_right_text(TSNode bin, TSNode operator_node) const;
  // Get binary_expression left operand text when hidden.
  std::string binary_left_text(TSNode bin, TSNode operator_node) const;

  // TS API helpers
  inline TSNode   child_by_field(const TSNode& n, const char* field) const;
  inline uint32_t child_count(const TSNode& n) const { return ts_node_child_count(n); }
  inline TSNode   child(const TSNode& n, uint32_t i) const { return ts_node_child(n, i); }

public:
  Prp2lnast(std::string_view filename, std::string_view module_name);

  // Analyze an in-memory buffer (e.g. an editor's unsaved buffer / LSP request).
  // `filename` is a virtual path used only for diagnostic spans; `source` is the
  // buffer text (read verbatim — the file at `filename` is NOT opened).
  Prp2lnast(std::string_view filename, std::string_view module_name, std::string_view source);

  ~Prp2lnast();

  std::shared_ptr<Lnast> get_lnast() { return std::move(lnast); }
  void                   dump_tree_sitter() const;
  void                   dump_tree_sitter(TSNode n, int level) const;
  void                   dump() const;

  // Fast import discovery for `lhd scan` / depfile + BUILD generators: runs ONLY
  // the prpparse LEXER (no recursive-descent parse, no LNAST build, no
  // elaboration) and returns the raw import module strings (sorted, unique).
  // Linear in file size — milliseconds even on multi-MB sources, where a full
  // get_lnast() takes minutes. Handles both import forms:
  //   foo = import("[path/]file[.member...]")   (call; the literal's raw text)
  //   import file[.member...] as alias          (statement; the dotted module path)
  // Comments and string bodies are correctly ignored (they are not keyword
  // tokens). Throws Parse_error only on an unterminated string/backtick.
  static std::vector<std::string> scan_imports(const std::string& path);
  // Same lexer-only scan over already-captured bytes. `path` is the virtual
  // user-tree path used for diagnostics; no disk read occurs. Incremental
  // compile uses this after its one-shot source snapshot so dependency
  // discovery and the later parse observe exactly the same file contents.
  static std::vector<std::string> scan_imports(std::string_view path, std::string_view source);
};
