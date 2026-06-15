//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Direct SystemVerilog -> LNAST lowering context (todo/verilog/2s.html
// subtask A). One Slang_context per slang Compilation; the per-concern
// visitor code lives in:
//   slang_structure.cpp - modules, ports, processes, instances, generate
//   slang_stmt.cpp      - statements (if/case/loops/blocks)
//   slang_expr.cpp      - rvalue expressions
//   slang_lvalue.cpp    - assignment targets
//   slang_types.cpp     - type mapping + the one materialize-conversion seam
// modeled on CIRCT's ImportVerilog Context split. Every module becomes its
// own Lnast in the extracted unit form (top -> [io, stmts], lambda_kind
// stamped) that upass/func_extract produces for a pyrope lambda, so SSA
// harvests io_meta from it and tolg lowers it with the exact Verilog name.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

// clang-format off
#include "slang/ast/Compilation.h"
#include "slang/ast/EvalContext.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/statements/ConditionalStatements.h"
#include "slang/ast/statements/LoopStatements.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"

#include "lnast_builder.hpp"
// clang-format on

class Slang_context {
public:
  struct Options {
    int  unroll_limit   = 4000;  // slang-side loop unroll cap (yosys-slang default)
    bool keep_timecheck = false;
  };

  Slang_context() = default;

  void set_source_manager(const slang::SourceManager* sm) { sm_ = sm; }
  void set_options(const Options& o) { options_ = o; }

  void process_root(const slang::ast::RootSymbol& root);

  std::vector<std::shared_ptr<Lnast>> pick_lnast();

private:
  // ── shared state ───────────────────────────────────────────────────────────
  const slang::SourceManager* sm_ = nullptr;
  Options                     options_;
  Lnast_builder               builder_;

  // module-definition body -> finished Lnast (nullptr while in flight).
  absl::flat_hash_map<const slang::ast::InstanceBodySymbol*, std::shared_ptr<Lnast>> lowered_;
  absl::flat_hash_map<const slang::ast::InstanceBodySymbol*, std::string>            module_names_;
  absl::flat_hash_set<std::string>                                                   module_names_used_;
  std::vector<std::shared_ptr<Lnast>>                                                ordered_lnasts_;

  // ── per-module state (reset by lower_module) ───────────────────────────────
  const slang::ast::InstanceBodySymbol*   body_ = nullptr;
  std::optional<slang::ast::EvalContext>  eval_ctx_;
  absl::flat_hash_map<const slang::ast::Symbol*, std::string> sym_lname_;
  absl::flat_hash_set<std::string>                            used_names_;
  absl::flat_hash_set<const slang::ast::Symbol*>              input_syms_;
  absl::flat_hash_set<const slang::ast::Symbol*>              output_syms_;
  absl::flat_hash_set<const slang::ast::Symbol*>              reg_syms_;   // clocked state vars
  absl::flat_hash_set<const slang::ast::Symbol*>              latch_syms_; // level-sensitive latch state vars (subset of reg_syms_)
  absl::flat_hash_set<const slang::ast::Symbol*>              mem_syms_;   // unpacked arrays lowered as memories
  absl::flat_hash_set<const slang::ast::Symbol*>              mem_wensize_emitted_;  // memories whose wensize attr was emitted
  absl::flat_hash_set<const slang::ast::Symbol*>              declared_;   // declare stmt already emitted
  std::string                                                 genblk_prefix_;
  bool                                                        module_failed_ = false;

  // ── per-process state ──────────────────────────────────────────────────────
  enum class Proc_kind : uint8_t { none, comb, seq };
  struct Assign_style {
    bool                  nonblocking = false;
    slang::SourceLocation loc;
  };
  Proc_kind                                                          proc_kind_ = Proc_kind::none;
  absl::flat_hash_map<const slang::ast::Symbol*, Assign_style>       proc_assign_style_;
  absl::flat_hash_set<const slang::ast::Symbol*>                     proc_blocking_written_;
  // loop-unroll budget shared across the nested loops of one process/ctx
  int                                                                unroll_budget_ = 0;

  // ── structure (slang_structure.cpp) ───────────────────────────────────────
  bool        lower_module(const slang::ast::InstanceSymbol& symbol);
  std::string module_name_of(const slang::ast::InstanceSymbol& symbol);
  void        emit_module_io(const slang::ast::InstanceSymbol& symbol, const Lnast_nid& in_tup, const Lnast_nid& out_tup);
  void        collect_state_vars(const slang::ast::InstanceBodySymbol& body);
  // Module bodies emit DRIVERS (continuous assigns, processes, instances) in
  // dataflow dependency order, not source order: LNAST/tolg resolve reads
  // sequentially, while verilog wires are order-free nets. Combinational
  // cycles fall back to source order + settled reads (LNAST-tier only).
  void lower_members(const slang::ast::Scope& scope);
  bool in_comb_cycle_ = false;  // current driver is part of a comb loop
  void        lower_process(const slang::ast::ProceduralBlockSymbol& pbs);
  void        lower_comb_process(const slang::ast::Statement& body);
  void        lower_ff_process(const slang::ast::SignalEventControl& clock, const slang::ast::Statement& body,
                               std::vector<const slang::ast::Statement*>& prologue);
  void        lower_instance(const slang::ast::InstanceSymbol& inst);
  void        lower_continuous_assign(const slang::ast::ContinuousAssignSymbol& ca);
  void        declare_value_symbol(const slang::ast::ValueSymbol& sym, bool force_reg);
  void        declare_reg(const slang::ast::ValueSymbol& sym);
  absl::flat_hash_set<const slang::ast::Symbol*> reg_declared_;

  // Unpacked-array (memory) info per declared array symbol (2s-D).
  struct Mem_info {
    int64_t lower       = 0;  // declared range lower bound (index bias)
    int     elem_bits   = 1;
    bool    elem_signed = false;
    int64_t size        = 0;
  };
  absl::flat_hash_map<const slang::ast::Symbol*, Mem_info> mem_info_;
  // Emit the comp_type_array declare for an unpacked array (reg or mut).
  // Returns false (with a diagnostic) for shapes the reader cannot lower.
  bool declare_unpacked(const slang::ast::ValueSymbol& sym, bool is_reg);

  // Unpacked-array PORTS are not memories: an `output T arr[N-1:0]` port lowers
  // to a FLAT packed [N*elem_bits-1:0] IO bus (Verilator/yosys flatten unpacked
  // ports the same way, so LEC lines up), and element access `arr[i]` becomes a
  // bit-slice at `(i-lower)*elem_bits`. Symbols here carry their dims in
  // mem_info_ but route through bit-slice get/set instead of store/tuple_get.
  absl::flat_hash_set<const slang::ast::Symbol*> flat_port_syms_;
  // Unpacked arrays indexed by a NON-constant selector somewhere in the module.
  // A comb plain-vector array that is NEVER runtime-indexed is safe to flatten
  // to a packed bus (constant element offsets, set_mask composition); a
  // runtime-indexed one must stay a memory (dynamic-shift flattening mismatches
  // — see the `tuplish` regression). Populated by a pre-pass in lower_module.
  absl::flat_hash_set<const slang::ast::Symbol*> runtime_indexed_arrays_;
  // Flat bit-slice read/write of an unpacked-array port element (reuses the
  // packed set_mask / shift+mask machinery).
  std::string flat_port_read(const slang::ast::ElementSelectExpression& es, const Mem_info& mi);
  void        flat_port_write(const slang::ast::ElementSelectExpression& es, const Mem_info& mi, const std::string& rhs);

  // ── statements (slang_stmt.cpp) ────────────────────────────────────────────
  void lower_statement(const slang::ast::Statement& stmt);
  void lower_conditional(const slang::ast::ConditionalStatement& stmt);
  void lower_case(const slang::ast::CaseStatement& stmt);
  struct Tinfo;  // fwd (defined below)
  std::string case_item_match(const std::string& sel, const Tinfo& si, const slang::ast::Expression& item,
                              slang::ast::CaseStatementCondition cond_kind);
  void lower_for_loop(const slang::ast::ForLoopStatement& stmt);
  void lower_while_loop(const slang::ast::Statement& stmt);  // While/DoWhile/Repeat
  void lower_foreach(const slang::ast::ForeachLoopStatement& stmt);
  bool unroll_tick(const slang::ast::Statement& stmt);  // false = budget exhausted (diag emitted)

  // ── expressions (slang_expr.cpp) ───────────────────────────────────────────
  // The result of expression lowering is an lname or pyrope const literal;
  // its (bits, signed) always match the slang expr.type so conversions stay
  // at the materialize seam.
  std::string lower_rvalue(const slang::ast::Expression& expr);
  std::string lower_binary(const slang::ast::BinaryExpression& expr);
  std::string lower_unary(const slang::ast::UnaryExpression& expr);
  std::string lower_select(const slang::ast::Expression& expr);  // Element/Range select rvalue
  std::string lower_concat(const slang::ast::ConcatenationExpression& expr);
  // Packed `'{...}` assignment pattern (simple/structured/replicated): elements
  // are already resolved positionally MSB-first, so concatenate them like a
  // concat. `type` must be integral (packed struct/array); unpacked targets are
  // diagnosed by the caller.
  std::string lower_assignment_pattern(const slang::ast::Expression& expr, std::span<const slang::ast::Expression* const> elems);
  std::string lower_conditional_expr(const slang::ast::ConditionalExpression& expr);
  std::string lower_call(const slang::ast::CallExpression& expr);
  // Inline a (synthesizable, input-only) user function: bind args, lower the
  // body, capture the return value. Returns the result temp.
  std::string inline_call(const slang::ast::CallExpression& expr, const slang::ast::SubroutineSymbol& sub);
  // function-inlining context (consumed by the Return statement handler)
  bool                              in_function_call_ = false;
  const slang::ast::VariableSymbol* func_ret_sym_     = nullptr;
  int                               inline_depth_     = 0;
  std::string read_symbol(const slang::ast::ValueSymbol& sym, slang::SourceRange range);
  std::string booleanize(std::string v);
  std::string lower_unpacked_read(const slang::ast::Expression& expr);  // memory/array element read

  // ── bool/int kind discipline ───────────────────────────────────────────────
  // LiveHD's typechecker kinds comparison/logical results as bool with no
  // implicit bool<->int interop; Verilog comparison results are 1-bit ints.
  // Expression temps may stay bool (if-conds, &&/||) but anything reaching an
  // integer context (stores, arithmetic, concat, ports) materializes to 0/1.
  absl::flat_hash_set<std::string> bool_values_;
  bool        is_bool_value(const std::string& v) const { return v == "true" || v == "false" || bool_values_.contains(v); }
  std::string mark_bool(std::string v) {
    bool_values_.insert(v);
    return v;
  }
  std::string to_int_value(const std::string& v);
  // A unique non-`___` local (the `___` namespace is single-write SSA; a
  // multi-written mux temp must not use it).
  std::string fresh_local(std::string_view stem);
  int         local_cnt_ = 0;

  // In-flight assignment target value for compound assigns (`a += b` lowers
  // the RHS with LValueReference reading this) - CIRCT's lvalue stack, depth 1.
  std::string compound_read_;

  // ── lvalues (slang_lvalue.cpp) ─────────────────────────────────────────────
  void lower_assign(const slang::ast::AssignmentExpression& expr);
  void assign_to(const slang::ast::Expression& lhs, const std::string& rhs);
  void note_write(const slang::ast::Symbol& sym, bool nonblocking, slang::SourceLocation loc);
  const slang::ast::ValueSymbol* resolve_base_symbol(const slang::ast::Expression& base);
  void                           lower_unpacked_write(const slang::ast::Expression& lhs, const std::string& rhs);
  bool                           current_assign_nonblocking_ = false;

  // A packed assignment target resolved to a single contiguous bit-slice of a
  // root variable: nested chains of `.field` / `[idx]` / `[hi:lo]` / conversion
  // on a packed (integral) root collapse to (base, low-bit offset, width).
  struct Packed_lv {
    const slang::ast::ValueSymbol* base = nullptr;
    int64_t                        const_off = 0;  // accumulated constant low-bit offset
    std::string                    dyn_off;        // accumulated dynamic low-bit offset ("" = none)
    int64_t                        width = 0;      // selected slice width in bits
    bool                           is_signed = false;  // signedness of the selected slice
  };
  // Returns false when the path touches an unpacked array or a non-resolvable
  // base (caller then falls back to the unpacked/memory path or a diagnostic).
  bool resolve_packed_lvalue(const slang::ast::Expression& lhs, Packed_lv& out);
  void emit_packed_rmw(const Packed_lv& lv, const std::string& rhs, slang::SourceRange sr);

  // `mem[addr][const-chunk] <= data`: a chunked masked memory write (the XS SRAM
  // models' byte/chunk write-enable idiom). Lowers to a memory write port whose
  // store carries the chunk index, so tolg sets the memory's `wensize` and a
  // per-chunk write-enable (LEC-matching the yosys-slang $memwr WR_EN model).
  // Returns false when lhs is not a constant-aligned bit-slice of a memory
  // element (the caller then falls through to the existing diagnostic).
  bool lower_mem_element_bitslice_write(const slang::ast::Expression& lhs, const std::string& rhs);

  // ── types + conversions (slang_types.cpp) ─────────────────────────────────
  struct Tinfo {
    int  bits      = 0;
    bool is_signed = false;
  };
  static Tinfo tinfo(const slang::ast::Type& t);
  // Like tinfo, but a fixed-size unpacked array of integral elements reports
  // its FLAT packed width (elem_bits * count, unsigned) — the representation an
  // unpacked-array port lowers to. Other types fall through to tinfo.
  static Tinfo flat_or_tinfo(const slang::ast::Type& t);
  void         emit_prim_type_int(const Lnast_nid& parent, int bits, bool is_signed);
  // The single conversion boundary: adjust an integer-semantics value from
  // (from_bits, from_signed) to (to_bits, to_signed). Truncate first, then
  // reinterpret; widening is a no-op in LNAST integer semantics.
  std::string materialize_conversion(const std::string& v, int from_bits, bool from_signed, int to_bits, bool to_signed);
  // Bit-pattern view: a signed value's two's-complement pattern as an
  // unsigned value of `bits` (used by concat / shifts-right / select bases).
  std::string to_pattern(const std::string& v, int bits, bool is_signed);
  // Wrap a mathematically-exact op result to its declared Verilog type
  // (truncate to bits, then sign-reinterpret) - the overflow boundary of
  // arithmetic/shift results.
  std::string fit_wrap(const std::string& v, int bits, bool is_signed);
  std::string mask_text(int bits) const;  // (1<<bits)-1 as a pyrope literal
  // Keep the low `bits` of a value as an UNSIGNED 0..2^bits-1 result. Never
  // uses a single-bit get_mask: Dlop's `x#[i]` contract makes those signed
  // -1/0 booleans, which is not the Verilog bit-extract value.
  std::string trunc_to(const std::string& v, int bits);
  // Extract `bits` starting at constant bit offset `lo` (shift down + trunc).
  std::string extract_field(const std::string& v, int64_t lo, int bits);

  // ── constant evaluation ────────────────────────────────────────────────────
  std::optional<slang::ConstantValue> try_eval(const slang::ast::Expression& expr);
  std::optional<int64_t>              try_eval_int(const slang::ast::Expression& expr);
  static std::string                  const_text(const slang::SVInt& svint);

  // ── naming + reads ─────────────────────────────────────────────────────────
  std::string lname_of(const slang::ast::Symbol& sym);

  // ── provenance + diagnostics (all through the slang_loc seam) ─────────────
  hhds::SourceId mint_loc(slang::SourceRange range);
  hhds::SourceId mint_loc(slang::SourceLocation loc) { return mint_loc(slang::SourceRange(loc, loc)); }
  void           set_pending_loc(slang::SourceRange range);
  void           set_pending_loc(slang::SourceLocation loc) { set_pending_loc(slang::SourceRange(loc, loc)); }
  void           clear_pending_loc();
  // Located `category=unsupported` error: an SV construct the direct reader
  // does not lower. Nothing falls through silently (CIRCT's default-visit
  // policy); expression-level callers return "0" afterwards to keep lowering
  // so one run reports every unsupported site.
  void emit_unsupported(slang::SourceRange range, std::string_view code, std::string message, std::string_view hint = {});
  void emit_unsupported(slang::SourceLocation loc, std::string_view code, std::string message, std::string_view hint = {}) {
    emit_unsupported(slang::SourceRange(loc, loc), code, std::move(message), hint);
  }
  void emit_error(slang::SourceRange range, std::string_view code, std::string_view category, std::string message,
                  std::string_view hint = {});
  void emit_warning(slang::SourceRange range, std::string_view code, std::string_view category, std::string message,
                    std::string_view hint = {});
};
