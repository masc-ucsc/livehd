//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <functional>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lnast.hpp"
#include "lnast_builder.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "tree_sitter/api.h"

class Prp2lnast {
protected:
  // TS Parsing
  std::string prp_file;
  std::string src_filename;  // source path, for diagnostic spans
  TSParser*   parser   = nullptr;
  TSTree*     ts_tree  = nullptr;  // owned; freed in the dtor (and on ctor throw)
  TSNode      ts_root_node;

  // Emit a structured diagnostic (docs/contracts/diagnostics.md §3) anchored at
  // `node`'s source span (best-effort byte + line/col, pre-sourcemap), then
  // abort the parse. `category` per §4 (e.g. "syntax", "name", "type").
  [[noreturn]] void report_error(const TSNode& node, std::string_view code, std::string_view category,
                                 std::string message, std::string_view hint = {}) const;
  // Location-less variant (span = null) for defensive sites with no TS node.
  [[noreturn]] void report_error(std::string_view code, std::string_view category, std::string message,
                                 std::string_view hint = {}) const;
  // Variant anchored at the source span previously attached (via `attach_loc`)
  // to an LNAST node — for post-build checks that walk the tree and no longer
  // hold the originating TSNode. Falls back to the location-less form when the
  // node carries no loc.
  [[noreturn]] void report_error(const Lnast_nid& nid, std::string_view code, std::string_view category,
                                 std::string message, std::string_view hint = {}) const;

  // Persist `node`'s source span (byte range + 1-based line) and the source
  // filename onto the LNAST node at `idx`, so downstream passes (e.g. the upass
  // verifier reporting a comptime-false cassert) can point at the assertion.
  // No-op when `node` is null. This is the pre-sourcemap, per-node best-effort
  // location; the general mechanism is task 1f. See docs/contracts/diagnostics.md.
  void attach_loc(const Lnast_nid& idx, const TSNode& node);

  // If the tree-sitter parse produced a MISSING node (genuine syntax error),
  // report it and abort (does not return); a clean parse returns normally.
  void check_parse_errors() const;

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

  // Reject `a = 3` with no prior `mut`/`const`/declare (or param/output) visible
  // in scope. Runs on the producer tree (pre-upass), so it sees only source-level
  // declarations — no inliner/SSA-synthesized stores to false-positive on.
  void check_undeclared_writes() const;
  // `visible` carries names declared in ENCLOSING scopes (re-declaring one of
  // them here is shadowing); `seed_here` pre-populates the CURRENT scope (a func
  // body seeds its params/outputs — re-declaring those at the body top level is
  // the normal output-init pattern, not shadowing).
  void check_writes_in_scope(const Lnast_nid& scope_stmts, const std::unordered_set<std::string>& visible,
                             const std::unordered_set<std::string>& seed_here = {}) const;

  // Reject reading a name that is never defined ANYWHERE in the file (e.g.
  // `const y = x`). A GLOBAL check (scope-order agnostic): a name that is a
  // declare/store/attr_set target somewhere — a local, a function/type/enum
  // name, or a param/output — is "defined". `read_sites_` is populated by
  // `identifier_to_node` on the value (for_lvalue=false) path, so it records
  // only genuine reads with their source TSNode (→ located diagnostic); the
  // func_call callee name uses `create_ref` directly, so builtins like `cputs`
  // are never recorded. Runs on the producer tree, after the LNAST is built.
  void check_undefined_reads() const;
  void collect_defined_names(const Lnast_nid& node, std::unordered_set<std::string>& defined) const;

  // LNAST output. `builder` co-owns `lnast` and is the canonical home for
  // the current `idx_stmts` cursor, tmp-ref minting, and frontend-agnostic
  // stmt emitters (cleanup_todo §3.4).
  std::shared_ptr<Lnast> lnast;
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

  // Counter for file-unique hoisted in-tuple method names (`call` →
  // `call__t1`). The bundle field keeps the source name; the func_def (and
  // hence the registry unit) gets the unique one, so two bundles may both
  // define `call` without colliding.
  int hoisted_lambda_count_ = 0;

  // Comptime-known tuples used by process_for_statement to unroll
  //   `for (e[, idx[, key]]) in NAME { … }` over a static shape, and to size
  //   `for i in ref NAME { … }` ref-iterations (write-back unroll).
  //
  // Recording paths:
  //   - `const NAME = <literal_tuple>` with all-literal entries → entries
  //     have `value_known=true` and `value_text` set; powers value-bound
  //     unrolls (tuple_exclude).
  //   - `mut NAME = ()` and `mut NAME = <literal_tuple>` → tracks the size,
  //     mutating ops below keep size in sync.
  //   - `NAME = NAME ++ <literal_tuple>` (or `NAME ++= <literal_tuple>`)
  //     where the rhs is a positional/named tuple → appends placeholder
  //     entries with `value_known=false` (size grows but the per-iter values
  //     aren't statically known).
  //   - Any other write to NAME invalidates by erasing the entry.
  //
  // Empty `key` means a positional slot. `value_text` is only consumed when
  // `value_known` is true.
  struct Comptime_tuple_entry {
    std::string key;         // empty string for positional entries
    std::string value_text;  // pyrope constant literal (verbatim source text)
    bool        value_known = false;
  };
  std::unordered_map<std::string, std::vector<Comptime_tuple_entry>> comptime_tuples_;

  // Every variable READ lowered by `identifier_to_node` (for_lvalue=false),
  // paired with its source TSNode. check_undefined_reads (after the LNAST is
  // built) reports any whose name is never defined anywhere in the file.
  std::vector<std::pair<std::string, TSNode>> read_sites_;

  // Stack of "formal parameter widths in scope" — pushed by process_lambda_statement
  // before emitting the body, popped after. Each frame maps a typed argument
  // name (`x:u6`, `y:s8`, …) to its declared bits. parse_int_const consults
  // this so `for i in 0..<x.[bits]` can unroll at parse time even though
  // `x.[bits]` isn't a literal in the source — the producer knows the width
  // from the formal-parameter type. Outer frames are visible to inner scopes
  // (lexical lookup); inner frames shadow on name collision.
  std::vector<std::unordered_map<std::string, int64_t>> param_bits_stack_;

  // Top
  void process_description();
  // Task 1t — post-build pass: rewrite statement-level assign/tuple_set → store.
  // Task 1t — post-build pass: merge the declaration cluster
  // (attr_set(type)+attr_set(comptime)+type_spec) into one `declare(var, TYPE,
  // const(mode))`. Rebuilds the body into a fresh tree (replace_body) since
  // in-place subtree deletion is avoided on the LNAST tree. The value (if any)
  // stays a separate `store`.
  void rewrite_decls_to_declare();

  // Statements
  void process_statement(TSNode n);
  void process_scope_statement(TSNode n, Lnast_nid target_stmts);
  // Shared body for process_description / process_scope_statement: walks ALL
  // children of `parent` (named + anonymous) so the grammar's hidden `wrap`/
  // `sat` overflow tokens and `gate` field bubble-ups are visible.
  void walk_statement_block(TSNode parent);
  // Wrap a statement in a synthesized `if cond { stmt }` (or `if !cond { stmt }`
  // for `unless`). For decl-form assignments (`mut x = e when cond`) the decl
  // is hoisted to the surrounding scope with a `nil` initializer, so `x` is
  // visible regardless of the gate result.
  void process_gated_statement(TSNode stmt, TSNode gate);
  void process_assignment(TSNode n);
  void process_declaration_statement(TSNode n);
  void process_assert_statement(TSNode n);
  void process_while_statement(TSNode n);
  void process_for_statement(TSNode n);
  void process_loop_statement(TSNode n);
  void process_control_statement(TSNode n);
  void process_function_call_statement(TSNode n);
  // Statement-table entry point (the table needs the plain `void(TSNode)`
  // member signature); forwards to the named variant with no override.
  void process_lambda_statement(TSNode n);
  // `hoist_name` (when non-empty) overrides the func_def's emitted name —
  // used by tuple_to_node to hoist an in-tuple method (`comb call(ref
  // self,…){…}` inside a bundle literal) under a file-unique name while the
  // bundle field keeps the source method name.
  void process_lambda_statement_named(TSNode n, std::string_view hoist_name);
  void process_enum_assignment(TSNode n);
  // One parsed entry of an enum definition (either source form).
  struct Enum_entry {
    std::string name;
    TSNode      type_node{};   // per-entry payload type (`Yellow:Rgb`); null when none
    TSNode      value_node{};  // explicit value expression; null when none
    bool        has_type  = false;
    bool        has_value = false;
  };
  // Shared lowering for `enum NAME[:T] = (…)` and `const NAME = enum(…)`.
  // Emits one value-carrier bundle per entry — the auto/explicit ordinal
  // (one-hot when no entry is explicit, sequential otherwise; 03-bundle.md
  // "Enumerate") or the constructed payload (`Yellow:Rgb = v` → `Rgb(v)`,
  // resolved by the runner's init-construction hook) — each tagged with a
  // `__enumentry` identity attr ('NAME.entry') that powers enum-aware `in`,
  // `string()` and interpolation. Returns the ref of the enum-type bundle
  // (entry name → carrier).
  Lnast_node lower_enum_def(std::string_view enum_name, TSNode enum_level_type, const std::vector<Enum_entry>& entries);
  void process_type_statement(TSNode n);
  void process_import_statement(TSNode n);
  void process_test_statement(TSNode n);
  void process_spawn_statement(TSNode n);
  void process_impl_statement(TSNode n);

  // Expressions: returns an Lnast_node (ref or const) naming the result
  Lnast_node expr_to_node(TSNode n);
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
  // `get_mask` / `set_mask`: a `Const` when both range endpoints are
  // integer-literal (encoded as a bitmask via `Dlop::get_mask_value`), or a
  // ref to a freshly-emitted `range` / `shl` LNAST stmt for dynamic cases.
  Lnast_node compute_bit_mask_ref(TSNode sel_node);
  Lnast_node emit_range_node(const Lnast_node& start, const Lnast_node& end);
  Lnast_node attribute_read_to_node(TSNode n);
  Lnast_node dot_expression_to_node(TSNode n);
  Lnast_node function_call_expr_to_node(TSNode n);
  Lnast_node interpolated_string_to_node(TSNode n);
  Lnast_node tuple_to_node(TSNode n, bool is_square);
  Lnast_node identifier_to_node(TSNode n, bool for_lvalue);
  Lnast_node constant_text_to_node(std::string_view text);
  Lnast_node type_specification_to_node(TSNode n);

  // Type handling
  void emit_type_spec(const Lnast_node& target, TSNode type_cast_node);
  void emit_attribute_list(const Lnast_node& target, TSNode attribute_list_node);
  void emit_type_expr(const Lnast_nid& type_index, TSNode type_node);
  // Task 1t — the integer type-call `int(max=,min=,bits=)` / `uint(bits=)`.
  // Today the grammar lexes `int`/`uint`/`uN` as keyword tokens, so a
  // parenthesized argument list does NOT match `function_call_type`; it lands
  // as an ERROR node wrapping the keyword plus a bare `(…)` tuple. This helper
  // recognizes that shape on a type_cast and emits a single
  // `type_spec(target, prim_type_int(max,min))`, the canonical integer type.
  // Returns true when it handled the type (caller skips the legacy lowering).
  bool emit_int_type_call(const Lnast_node& target, TSNode type_cast_node);
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
  // (Task 1r-A interface contract — fully-typed pipe/mod ios, per-output
  // declared landing cycle on mod). Tuple-literal contexts leave them unset
  // and skip every interface check.
  void emit_arg_assign(const Lnast_nid& tuple_parent, TSNode typed_ident, TSNode definition_or_null, bool is_ref_mod,
                       std::vector<Param_attr>* attrs_out = nullptr, std::string_view lambda_kind = {},
                       bool is_io_output = false);
  void emit_arg_type(const Lnast_nid& assign_parent, TSNode type_node);

  // Task 1r — current lambda-kind context while processing a body ("comb" /
  // "pipe" / "mod" / ...; empty stack = file scope). Gates `stage[N]`
  // declarations (mod-only) and `x@[N]` timecheck emission (mod/pipe only).
  std::vector<std::string> lambda_kind_stack_;

  // Task 1r — parse a stage_decl's optional timing_slot to the (min,max)
  // stages const texts: stage[N] -> ("N","N") with N >= 1; stage[A..=B] /
  // stage[A..<B] -> ascending literal range; bare `stage` / `stage[]` ->
  // ("nil","nil") (toolchain picks). Anything else is a compile error.
  std::pair<std::string, std::string> parse_stage_slot(TSNode storage_node);

  // Task 1r — emit `timecheck(ref name, const N, const N)` recording an
  // `x@[N]` cycle check (flop-free, inert — consumed by the future pipe/mod
  // typecheck pass). `x@[]` emits nothing (explicit opt-out). Errors when the
  // enclosing lambda is a comb (every comb value is at cycle 0) or the slot
  // is not a literal N >= 0.
  void maybe_emit_timecheck(TSNode timing_slot, TSNode id_node);

  // Task 1q — resolve a `pipe_lambda` node's `depth` field to the (min,max)
  // stages pair: pipe[N] -> (N,N); pipe[A..=B] -> (A,B); pipe[A..<B] ->
  // (A,B-1); bare pipe -> (1,0) (max 0 = unconstrained). pipe[0], zero-min
  // ranges, descending ranges and non-literal depths are compile errors.
  std::pair<int64_t, int64_t> parse_pipe_depth(TSNode pipe_lambda_node);

  // Task 1r/1q — file-/body-scope `const NAME = <int literal>` bindings,
  // recorded as the declaration is lowered (process_lvalue_for_assign scalar
  // branch). Lets `@[NAME]`, `stage[NAME]`, and `pipe[NAME]` timing slots
  // accept a compile-time-resolvable const in place of a bare literal.
  // No-shadowing is already enforced, so a name is unambiguous along the
  // visible chain; a later binding may overwrite an earlier same-name one.
  std::unordered_map<std::string, int64_t> const_int_bindings_;

  // Resolve a timing-index CST node to a compile-time integer: a `constant`
  // node parses via Dlop::from_pyrope (is_integer + is_i); an `identifier`
  // node looks up const_int_bindings_; anything else is std::nullopt (the
  // caller emits its own "literal or compile-time constant" diagnostic).
  std::optional<int64_t> resolve_cycle_value(TSNode n) const;

  struct Call_arg {
    bool        is_assign = false;
    bool        is_ref    = false;
    bool        is_ufcs   = false;  // task 1k — the receiver of `obj.method(...)`
    std::string assign_key;
    Lnast_node  value;
  };
  std::vector<Call_arg> collect_call_args(TSNode arg_tuple);
  void                  add_call_args_to_fcall(const Lnast_nid& fcall_idx, const std::vector<Call_arg>& call_args);

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
  // call before the store (Task 1t — replaces the old attr_set(wrap) tag).
  Lnast_node process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node,
                                       bool rhs_is_fcall = false, std::string_view rhs_fcall_name = {},
                                       std::string_view overflow_kind = {});

  // Helpers
  std::string_view        get_text(const TSNode& n) const;
  std::string             get_text_str(const TSNode& n) const { return std::string(get_text(n)); }
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
  inline TSNode   named_child(const TSNode& n, uint32_t i) const { return ts_node_named_child(n, i); }
  inline uint32_t named_child_count(const TSNode& n) const { return ts_node_named_child_count(n); }

public:
  Prp2lnast(std::string_view filename, std::string_view module_name);

  // Analyze an in-memory buffer (e.g. an editor's unsaved buffer / LSP request).
  // `filename` is a virtual path used only for diagnostic spans; `source` is the
  // buffer text (read verbatim — the file at `filename` is NOT opened).
  Prp2lnast(std::string_view filename, std::string_view module_name, std::string_view source);

  ~Prp2lnast();

  std::shared_ptr<Lnast> get_lnast() { return std::move(lnast); }
  void                   dump_tree_sitter() const;
  void                   dump_tree_sitter(TSTreeCursor* tc, int level) const;
  void                   dump() const;
};
