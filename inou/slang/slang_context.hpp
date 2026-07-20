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
#include <map>
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
    // The USER passed --ignore-unknown-modules (inou_slang always injects it
    // at the slang level so the reader owns the policy): lower unknown-module
    // instances as blackbox sub-instances instead of a clean error.
    bool blackbox_unknown = false;
    // Keep a package parameter reference as a symbolic `pkg.PARAM` (emitting a
    // `pub comptime const` package unit) instead of folding it to its literal —
    // for readable, provenance-preserving Pyrope emission. OFF by default: it
    // requires the constprop preserve-mode + prp_writer package-import synthesis
    // (WIP), so a normal compile keeps folding as before.
    bool preserve_param_provenance = false;
    // Emit a qualifying packed-struct PORT (struct_port_bundle_ok) as a Pyrope
    // tuple/bundle port — a tuple-typed io entry plus tuple_get/field-store
    // body accesses — instead of one flat bus. Defaulted ON by the CLI exactly
    // when preserve_param_provenance defaults ON (pyrope-emitting, no-graphs
    // compile); a graphs flow keeps flat ports (that flat lgraph is the LEC
    // reference).
    bool struct_port_bundles = false;
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
  absl::flat_hash_map<const slang::ast::Symbol*, std::pair<int, bool>> output_info_;  // {flat bits, is_signed} per output, for the body-top X-default poison-init of non-reg outputs
  absl::flat_hash_set<const slang::ast::Symbol*>              reg_syms_;   // clocked state vars
  absl::flat_hash_set<const slang::ast::Symbol*>              wire_syms_;  // 2c-wire — comb-cycle nets: declared `wire` so reads are position-independent
  // A `wire` net that is MULTIPLY written (a case/priority-if or bit-slice
  // pattern) cannot be a single-driver wire directly. It is SPLIT: the writes
  // go to a `mut <name>` accumulator (program-order last-wins), reads through
  // the `wire` see the resolved value, and one bridge `wire = mut` is the wire's
  // sole driver. Maps the net symbol → its mut-accumulator lname. During pass-4
  // emission, a driver that WRITES the net has sym_lname_ swapped to the tmp (so
  // its writes AND its own RMW reads hit the mut); other drivers read the wire.
  absl::flat_hash_map<const slang::ast::Symbol*, std::string> wire_split_tmp_;
  // Subset of wire_split_tmp_ keys that are FLATTENED-AGGREGATE splits: a
  // wire-classified local whose representation is a single flattened MUT bus
  // (a comb struct/const-indexed array packed by declare_unpacked's flatten
  // branch, pre-declared BEFORE the wire classification runs). The EXISTING
  // mut is the accumulator; readers see a fresh `<name>__wnet` wire bridged
  // once at the end (the inverse naming of the scalar split — the mut cannot
  // be re-declared). Without this, a merge/mux driver sorted before the
  // writers reads the bus's INITIAL value instead of the resolved net
  // (minion_dcache_miss_handler_unit's `writeback_req_o |= mh_wb_req[i]`
  // read mh_wb_req before the child instances wrote it — LEC-refuted).
  absl::flat_hash_set<const slang::ast::Symbol*> wire_split_flat_;
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
  void        collect_state_vars(const slang::ast::Scope& body);
  // Module bodies emit DRIVERS (continuous assigns, processes, instances) in
  // dataflow dependency order, not source order: LNAST/tolg resolve reads
  // sequentially, while verilog wires are order-free nets. Combinational
  // cycles fall back to source order + settled reads (LNAST-tier only).
  void lower_members(const slang::ast::Scope& scope);
  void        lower_process(const slang::ast::ProceduralBlockSymbol& pbs);
  void        lower_comb_process(const slang::ast::Statement& body);
  void        lower_ff_process(const slang::ast::SignalEventControl& clock, const slang::ast::Statement& body,
                               std::vector<const slang::ast::Statement*>& prologue);
  void        lower_instance(const slang::ast::InstanceSymbol& inst);
  // Blackbox instance (slang UninstantiatedDef, i.e. --ignore-unknown-modules):
  // no definition, so port directions come from the collect-pass inference
  // (`conn_is_out`, aligned with getPortConnections()). Lowered as a func_call
  // to the definition name; the callee is recorded as an external module on
  // the unit's Lnast so the pyrope emission writes its `import` + call.
  void        lower_unknown_instance(const slang::ast::UninstantiatedDefSymbol& inst, const std::vector<bool>& conn_is_out);
  // Unknown-module definition names already diagnosed (one warning per name,
  // not per instance — XS-scale designs instantiate one SRAM macro x100s).
  absl::flat_hash_set<std::string> unknown_warned_;
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
    // A memory whose element is a packed STRUCT lowers to a TUPLE-typed memory
    // (`reg mem:[N]T`); upass.detuple splits it into per-field scalar memories
    // (`mem.field:[N]w`). The reader decomposes every element access into
    // per-field tuple ops (4-child field `store`, 2-node `tuple_get` read
    // chains) so detuple — which cannot bridge a packed port value — only ever
    // sees field-level ops. `fields` carries each field's name, packed bit
    // offset (slang FieldSymbol.bitOffset, LEC-critical), width and signedness.
    struct Field {
      std::string name;
      int64_t     off       = 0;
      int         bits      = 1;
      bool        is_signed = false;
    };
    bool               is_tuple = false;
    std::string        type_name;  // emitted typedef name (= comp_type_array element ref)
    std::vector<Field> fields;
    // A MULTI-dimensional unpacked array (`T m [A][B]`) linearizes to a 1-D
    // memory of size A*B (row-major, innermost dim contiguous); `dims` keeps
    // each declared dim's lower bound and width, outermost first, so a full
    // `m[i][j]` selector chain folds to one linear index. The memory declare
    // paths fill it for 1-D arrays too (one entry); a flat-PORT Mem_info leaves
    // it empty (single dim implied — ports don't take the linearized path).
    struct Dim {
      int64_t lower = 0;
      int64_t width = 1;
    };
    std::vector<Dim> dims;
    size_t           rank() const { return dims.empty() ? 1 : dims.size(); }
  };
  absl::flat_hash_map<const slang::ast::Symbol*, Mem_info> mem_info_;
  // Per-module stable name for each struct element type (keyed by the canonical
  // type pointer so two memories of the same struct share one typedef) and the
  // set of typedef names already emitted into this module's stmts.
  absl::flat_hash_map<const slang::ast::Type*, std::string> tuple_type_names_;
  absl::flat_hash_set<std::string>                          emitted_tuple_types_;
  std::string tuple_type_name(const slang::ast::Type& elem);
  // Emit a `type T=(...)` region (once per type per module) in the no-default
  // form upass.detuple resolves: declare(T,prim_type_none,'type') + per-field
  // type_spec + tuple_add(Ttemp, field…) + store(T,Ttemp).
  void emit_tuple_typedef(const Mem_info& mi);
  // Per-field tuple-memory primitives (PRE-detuple shapes).
  void        emit_field_store(const std::string& mem_name, const std::string& idx, const std::string& field_name,
                               const std::string& val);
  std::string emit_field_read_chain(const std::string& mem_name, const std::string& idx, const std::string& field_name);
  const Mem_info::Field* find_tuple_field(const Mem_info& mi, std::string_view name) const;
  // Power-on memory contents harvested from `initial begin mem[k]=v; … end`
  // blocks (a pre-pass in lower_module, BEFORE the declares emit). Keyed by the
  // array symbol; the inner map is index→value. declare_unpacked emits these as
  // the reg array's declare initializer (scalar broadcast if uniform, else a
  // tuple literal), matching the hand-written `reg t:[N]T = (…)` goldens.
  absl::flat_hash_map<const slang::ast::Symbol*, std::map<int64_t, int64_t>> mem_init_vals_;
  // Walk an `initial` block body collecting constant `mem[const] = const`
  // element writes into mem_init_vals_.
  void collect_mem_inits(const slang::ast::Statement& stmt);
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
  // PACKED 2-D reg arrays (`reg [N-1:0][W-1:0]`, W>1) that are RUNTIME-indexed
  // somewhere — a firtool-style register file. These memory-ize (one `__memory`
  // node) instead of flattening to a single N*W-bit flop, so they LEC against an
  // equivalent Pyrope memory. Populated by the runtime-index pre-pass; the
  // declare + element read/write consult it to route through the memory path.
  absl::flat_hash_set<const slang::ast::Symbol*> packed_mem_regs_;
  // True iff `sym`'s canonical type is a packed 2-D array of an integral element
  // wider than 1 bit (a true `reg [N-1:0][W-1:0]`, W>1 — NOT a 1-D packed vector
  // whose element is a single bit). Fills N (size), W (elem_bits), the element
  // signedness and the outer range lower bound when it returns true.
  static bool is_packed_2d_array(const slang::ast::Type& type, int64_t& size, int& elem_bits, bool& elem_signed,
                                 int64_t& lower);
  // Flat bit-slice read/write of an unpacked-array port element (reuses the
  // packed set_mask / shift+mask machinery).
  std::string flat_port_read(const slang::ast::ElementSelectExpression& es, const Mem_info& mi);
  void        flat_port_write(const slang::ast::ElementSelectExpression& es, const Mem_info& mi, const std::string& rhs);

  // ── scalar packed-struct vars as per-field BUNDLES ─────────────────────────
  // A scalar (non-array, non-reg, non-port) packed-struct variable lowers to one
  // independent LEAF net per field (`io.operation`, `io.inputx`, …) instead of a
  // single flat packed bus + bit-slices. Field reads/writes become plain scalar
  // reads/writes of the dotted leaf — so a struct that mixes input-fed fields and
  // computed fields no longer collapses into ONE LGraph node whose driver reads
  // itself (the false combinational self-loop that made `io.operation[4]` a
  // bit-slice of a self-referential `io` bus and broke LEC encoding). The dotted
  // leaf survives to cgen as the escaped id `\io.operation`. Mirrors the
  // post-SSA tuple-PORT flatten shape (plain scalar stores to dotted leaves).
  struct Struct_info {
    struct Field {
      std::string name;
      int64_t     off       = 0;  // packed bit offset (for whole-struct reconstruct)
      int         bits      = 1;
      bool        is_signed = false;
    };
    std::vector<Field> fields;
    bool               is_wire = false;  // declared `wire` (position-independent leaf reads)
    // Emitted as a REAL tuple (`wire io:(...)`, field reads/writes via
    // tuple_get/field-store, upass.detuple splits it). Only a cyclic (wire) struct
    // whose fields are ALL scalar — detuple cannot split a NESTED struct field, and
    // a non-cyclic struct keeps its flat-leaf form. Otherwise the per-field flat
    // leaf nets are used (still a bundle in the LGraph, just not a tuple in LNAST).
    bool               is_tuple = false;
  };
  absl::flat_hash_map<const slang::ast::Symbol*, Struct_info> struct_var_info_;
  // Scalar packed-struct vars assigned a whole `'{...}` pattern somewhere (i.e.
  // driven per-field, like the ALU `io`). Only such a struct is emitted as a real
  // tuple — one whose drivers are NOT per-field (an instance-output net, a whole
  // expression copy) cannot be detuple-split, so it keeps the flat-leaf form.
  // Populated by a pre-scan in lower_module; reset per module.
  absl::flat_hash_set<const slang::ast::Symbol*> struct_pattern_assigned_;
  void collect_struct_pattern_assigns(const slang::ast::Scope& scope);
  // Base symbols read via member-access (`x.field`) anywhere in the module.
  // A net-initializer struct (`wire struct{...} x = '{...}`) is split into
  // per-field leaves only when x is field-read (else the whole-net assign is
  // kept — splitting a whole-read-only nested struct breaks its reassembly).
  // Populated by a body-wide pre-scan in lower_module; reset per module.
  absl::flat_hash_set<const slang::ast::ValueSymbol*> struct_field_read_;
  // Packed-struct vars accessed BELOW top level (`c0.field[i]`, `c0.field.sub`) or
  // WHOLE-COPIED (`a = b` as bare names): a nested/array-field struct in either case
  // is mis-lowered by the per-field bundle path, so it must stay a flat bus. Scoped
  // to those vars (not every nested/array struct) to keep struct-heavy designs fast.
  absl::flat_hash_set<const slang::ast::ValueSymbol*> struct_deep_accessed_;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> struct_whole_copied_;
  // Packed-struct vars deep-WRITTEN (`io.sub.x = v`): a read-modify-write on a
  // nested field roots at the whole-struct net (resolve_packed_lvalue), which a
  // per-field bundle lacks, so a deep-written struct with a nested field stays a
  // flat bus. Deep READS of a nested-struct field route through the leaf and are
  // safe to bundle (small_todo_working.md Type B).
  absl::flat_hash_set<const slang::ast::ValueSymbol*> struct_deep_written_;
  // Plain packed-array LOCALS driven by a single whole per-element assignment (a
  // `'{...}` pattern or a per-element `{...}` concat) and read by element select,
  // with no element writes (the Type C shift-network shape — an element reading a
  // sibling of the same array reads the stale whole-array bus / a false comb
  // cycle). Split into per-element leaf nets (declare_array_leaves) — the array
  // analogue of the struct bundle — so each element read routes to its own net.
  // Reset per module.
  absl::flat_hash_set<const slang::ast::ValueSymbol*> struct_array_bundle_;
  bool is_packed_array_bundle_var(const slang::ast::ValueSymbol& sym) const;
  void declare_array_leaves(const slang::ast::ValueSymbol& sym);
  // True for a scalar packed-struct VARIABLE we lower per-field (excludes ports,
  // clocked regs, and arrays — those keep their existing lowering).
  bool is_scalar_struct_var(const slang::ast::ValueSymbol& sym) const;
  // Whole-copied struct whose single whole-net driver is a SELF-REFERENCING
  // '{...}' pattern with one element per top field (CIRCT's `_out_output`
  // idiom): the flat bus would be a false combinational loop, so it stays a
  // bundle (per-field leaves) instead. Pure over the AST — cached by symbol.
  bool whole_copied_selfref_pattern(const slang::ast::ValueSymbol& sym) const;
  mutable absl::flat_hash_map<const slang::ast::ValueSymbol*, bool> selfref_pattern_cache_;
  const Struct_info::Field* find_struct_field(const Struct_info& si, std::string_view name) const;
  // Declare the per-field leaf nets (called from declare_value_symbol).
  void declare_struct_leaves(const slang::ast::ValueSymbol& sym);
  // Raw (non-splitting) scalar store / read of a dotted leaf net. `create_*_stmts`
  // split a dotted name on '.' into tuple_get/tuple_set ops (the LNAST bundle-path
  // split); these build the store node directly so the dotted name stays ONE flat
  // ref that tolg resolves as a wire net (the same shape the SSA port-flatten and
  // the tuple-memory path emit).
  void        emit_leaf_store(const std::string& leaf, const std::string& value);
  std::string read_leaf(const std::string& leaf);
  // Wire-tuple (cyclic) struct field write / read via tuple_get + field-store ops
  // (detuple splits them to leaf nets). A non-cyclic (mut) struct uses the flat
  // emit_leaf_store / read_leaf forms instead.
  void        emit_struct_field_set(const std::string& base, const std::string& field, const std::string& value);
  std::string read_struct_field_get(const std::string& base, const std::string& field);
  // `io = '{...}` / `io = other_struct` whole-struct write → one leaf write per
  // field (each leaf gets its OWN driver value, NOT a re-slice of the
  // concatenated whole — that would reintroduce the field↔field self-loop).
  // Returns false for an unhandled RHS shape (caller falls back to the bus path).
  bool assign_struct_whole(const slang::ast::ValueSymbol& sym, const slang::ast::Expression& rhs);
  // Same split for an ALREADY-LOWERED whole value (an instance-output binding,
  // a concat part, …): slice `value` onto the leaves. Returns false when `sym`
  // has no leaves (not a bundle var) — caller stores flat.
  bool assign_struct_whole_value(const slang::ast::ValueSymbol& sym, const std::string& value, slang::SourceLocation loc);
  // Reconstruct the packed value of a whole-struct read from its leaves.
  std::string read_struct_whole(const slang::ast::ValueSymbol& sym);

  // ── packed-struct PORTS as tuple bundles (M7) ─────────────────────────────
  // A qualifying packed-struct port is emitted as a TUPLE-typed io entry
  // (store(port, nil, tuple_add(per-field store)) — the exact prp2lnast
  // tuple-port shape upass.ssa flattens into dotted leaf ports). Body accesses
  // use the "hand-flattened twin" LEAF form (2-child store/read + set_mask on
  // the dotted leaf `port.field` — what SSA's port flatten rewrites tuple ops
  // INTO), so they ride the normal scalar SSA: the tuple-op route versions
  // top-level field stores but leaves if-arm stores on the base name and
  // binds reads to the FIRST version, which breaks the always_comb idiom
  // (poison + default + conditional overrides + field RMW). A whole-port read
  // reassembles (shift/or of the leaves in packed order), a whole-port write
  // splits per field. Keyed by the port's internalSymbol; fields in SV
  // declaration order (first-declared = MSB — the LEC leaf↔flat-bus
  // correspondence). A REG-driven bundle output port is re-routed at
  // declare_reg time to a flat shadow reg bridged per-field onto the tuple
  // leaves (the entry is erased here so body accesses go flat).
  absl::flat_hash_map<const slang::ast::Symbol*, Struct_info> bundle_port_info_;
  // TYPE-ONLY qualification, shared by the child def and every parent
  // instantiation site (that determinism keeps the two consistent): the
  // canonical port type is a packed STRUCT (not union/enum, not an array of
  // structs), and every field is an integral scalar/packed vector/enum — no
  // struct/union/multi-dim-array-typed fields (those keep today's flat port).
  static bool struct_port_bundle_ok(const slang::ast::Type& t);
  // Field list (SV declaration order, first = MSB) of a qualifying struct
  // port type — the shared shape between emit_module_io and lower_instance.
  static std::vector<Struct_info::Field> struct_port_fields(const slang::ast::Type& t);
  // Full qualification of a port at def AND call sites (option + plain name +
  // type rule). Deterministic: consults no body uses.
  bool bundle_port_qualifies(const slang::ast::PortSymbol& port) const;
  const Struct_info* bundle_port_of(const slang::ast::Symbol& sym) const;
  // COMB bundle OUTPUT ports drive a local per-field SHADOW accumulator
  // (`<port>__bpo.<field>` mut leaves, poison-initialized) and the port leaf
  // gets exactly ONE top-level store — the end-of-module bridge
  // `port.field = <shadow>.field`. Rationale: upass.ssa's port-tuple flatten
  // (on the RECOMPILE of the emitted Pyrope) SSA-versions top-level stores to
  // an output tuple leaf but leaves if-arm stores on the base name; with >=2
  // top-level stores (poison + the SV `'0` default) the final version commit
  // clobbers every arm write (minion_dcache_writeback_unit's l2_req_data_o
  // refuted as constant 0). A local shadow rides the normal, proven scalar
  // SSA; the single-store bridge is safe in both directions. Keyed by the
  // port's internalSymbol; body reads AND writes of the port redirect here.
  absl::flat_hash_map<const slang::ast::Symbol*, std::string> bundle_out_shadow_;
  // Body-access base name of a bundle port: the shadow for a comb output,
  // the port name itself for inputs (and reg-bridged outputs, which are
  // erased from bundle_port_info_ before any body access).
  std::string bundle_port_body_base(const slang::ast::Symbol& sym);
  // Whole-port value from per-field tuple_gets (mirror read_struct_whole).
  std::string read_bundle_port_whole(const slang::ast::ValueSymbol& sym);
  // Whole-port write: slice an already-lowered flat value onto the fields
  // (mirror assign_struct_whole_value). false when sym is not a bundle port.
  bool assign_bundle_port_whole_value(const slang::ast::ValueSymbol& sym, const std::string& value,
                                      slang::SourceLocation loc);

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
  // A reference to a PACKAGE parameter/localparam (`vpu_defs_pkg::PARAM`)
  // resolves, instead of folding to its literal value, to a symbolic dotted ref
  // `pkg.PARAM` — preserving the source's named constant in the emitted Pyrope
  // (upass.constprop keeps it symbolic in the pyrope-emit flow, folds it for
  // the netlist). Returns the `pkg.PARAM` ref and records the (pkg, param,
  // value) for the package-unit emission + the per-module package import;
  // nullopt for anything that is not an integral package-parameter reference.
  std::optional<std::string> package_param_ref(const slang::ast::Expression& expr);
  // Symbol-level core of package_param_ref: an integral package Parameter OR
  // package enum member becomes `pkg.NAME` (recorded for the package unit).
  // Also the defensive hook for read_symbol's Parameter/EnumValue fallbacks.
  std::optional<std::string> package_symbol_ref(const slang::ast::Symbol& sym);
  // The package a symbol is (transitively) declared in, or nullptr.
  static const slang::ast::PackageSymbol* owning_package(const slang::ast::Symbol& sym);
  // The compile-time value of a Parameter / enum member symbol, else nullptr.
  static const slang::ConstantValue* package_const_value(const slang::ast::Symbol& sym);
  // Renders a package-const initializer as READABLE pyrope text (`TXFMA_B19 -
  // TXFMA_B24`, `pkg2.X + 1`) while re-evaluating it in plain int64 arithmetic
  // bottom-up; callers compare the returned value against slang's folded value
  // so any width-wrap/semantics divergence falls back to the literal. Same-
  // package refs render bare; cross-package refs dot-qualify and land in
  // imports_out; every referenced (package, param) lands in refs_out (for the
  // package-unit closure). Only value-faithful ops render (+ - * << >>, unary
  // -, value-preserving conversions); anything else returns nullopt.
  std::optional<std::pair<std::string, int64_t>> render_const_expr(
      const slang::ast::Expression& e, const slang::ast::PackageSymbol* home, std::set<std::string>& imports_out,
      std::vector<std::pair<const slang::ast::PackageSymbol*, std::string>>& refs_out);
  // Any leaf of `expr` is a package Parameter / package enum member.
  static bool contains_package_param(const slang::ast::Expression& expr);
  // The structural lowering of `expr` can preserve pkg.PARAM leaves: every
  // sub-lowering it dispatches to is supported (no unsupported-op hard error a
  // tier-1 fold would otherwise have absorbed). Kinds outside this set keep the
  // tier-1 fold even when they contain a package param ($clog2/$bits system
  // calls, non-constant-capable ops like `**`, assignment patterns, …).
  static bool structural_preserve_ok(const slang::ast::Expression& expr);
  // pkg name → (param name → pyrope value text). Accumulated across ALL modules;
  // one `pub comptime const` package unit is emitted per pkg at the end.
  std::map<std::string, std::map<std::string, std::string>> referenced_pkg_params_;
  // pkg name → its PackageSymbol (for source-order member iteration + the
  // defining-expression closure at emit_package_units time).
  std::map<std::string, const slang::ast::PackageSymbol*> referenced_pkg_syms_;
  // pkg name → (alias name → {face "MAX|MIN", print text "uN"}): scalar type
  // aliases minted from param-named port dims (`[VPU_FCMD_SZ-1:0]` →
  // `pub type VPU_FCMD_SZ_T = u7`), exported by the package unit.
  std::map<std::string, std::map<std::string, std::pair<std::string, std::string>>> referenced_pkg_types_;
  // Provenance: a port whose SV packed dim is `[P-1:0]` with P a package param
  // of value == the port width mints/returns the imported alias text
  // (`pkg.P_T`); nullopt when the dim carries no (recoverable) param name.
  std::optional<std::string> port_dim_alias(const slang::ast::PortSymbol& port, int bits, bool is_signed);
  // Provenance: MODULE-LOCAL params (`localparam CNT_MAX = …` at module-body
  // scope) become body-level `comptime const` declarations, and their refs stay
  // symbolic (package_symbol_ref consults this map). Per-module state.
  absl::flat_hash_map<const slang::ast::Symbol*, std::string> local_param_lname_;
  void emit_local_param_consts(const slang::ast::Scope& body);
  void emit_package_units();  // one namespace .prp per referenced package
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
  void assign_to_pattern(const slang::ast::Expression& lhs, std::span<const slang::ast::Expression* const> elems,
                         const std::string& rhs);
  void note_write(const slang::ast::Symbol& sym, bool nonblocking, slang::SourceLocation loc);
  const slang::ast::ValueSymbol* resolve_base_symbol(const slang::ast::Expression& base);
  void                           lower_unpacked_write(const slang::ast::Expression& lhs, const std::string& rhs);
  // Walk a (possibly nested) element-select chain on an unpacked array down to
  // the expression below the last unpacked dim, collecting the selectors
  // OUTERMOST dim first (`m[i][j]` -> {i, j}). Also accepts the memory-ized
  // packed 2-D reg base (single select on a packed base).
  static const slang::ast::Expression* peel_unpacked_chain(const slang::ast::Expression&                    expr,
                                                           std::vector<const slang::ast::Expression*>&      sels);
  // Linear 0-based element index for a FULL-depth selector chain (row-major,
  // innermost dim contiguous), folding constant selectors. sels.size() must
  // equal mi.rank().
  std::string build_unpacked_index(const Mem_info& mi, const std::vector<const slang::ast::Expression*>& sels);
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
  // Partial (bit-slice) write whose resolved root is a BUNDLE port: const
  // offsets split per overlapped field (full cover = plain field store,
  // partial = field-local splice); a dynamic offset reassembles the whole
  // port, splices at the runtime position, and writes every field back.
  void emit_bundle_port_rmw(const Packed_lv& lv, const std::string& rhs, slang::SourceRange sr);

  // `mem[addr][const-chunk] <= data`: a chunked masked memory write (the XS SRAM
  // models' byte/chunk write-enable idiom). Lowers to a memory write port whose
  // store carries the chunk index, so tolg sets the memory's `wensize` and a
  // per-chunk write-enable (LEC-matching the yosys-slang $memwr WR_EN model).
  // Returns false when lhs is not a constant-aligned bit-slice of a memory
  // element (the caller then falls through to the existing diagnostic).
  bool lower_mem_element_bitslice_write(const slang::ast::Expression& lhs, const std::string& rhs);

  // `mem[addr][dyn-bit/slice] <= data` where the in-word position is NOT
  // constant (so the const-chunk wensize path does not apply): read-modify-write
  // the addressed memory word (read old contents, splice the new bits in at the
  // dynamic offset, write the whole word back). Returns false when lhs is not a
  // bit/slice select of a (non-tuple) memory element.
  bool lower_mem_element_dynamic_write(const slang::ast::Expression& lhs, const std::string& rhs);

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
  // Pyrope literals for the max/min value of a `bits`-wide integer of the given
  // sign. Works around Dlop::get_{,neg_}mask_value's narrow-arg wart (both
  // return 1 for arg <= 1): a 1-bit signed is {0,-1} and a 2-bit signed is
  // {1,-2}, not the (1,1) the naive get_*_mask_value(bits-1) calls produce.
  std::string  int_max_str(int bits, bool is_signed) const;
  std::string  int_min_str(int bits, bool is_signed) const;
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
  // slang's effective (value-)width of a subexpression: the minimum bits needed
  // to represent its value (sign-aware, transparent through implicit context
  // conversions). nullopt when slang can't bound it (treat as "may overflow").
  // Used to skip fit_wrap when an arithmetic result provably fits its type.
  std::optional<int> value_width(const slang::ast::Expression& e) const;
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
  // Like try_eval, but folds references to constant nets/vars by chasing their
  // single constant driver (a `wire x = <const>` initializer or an
  // `assign x = <const>`), recursively. firtool factors async-reset *values*
  // into named constant wires (e.g. `enqPtr <= enqPtr_ptr;` where
  // `enqPtr_ptr = '{...:0}`), which plain expr.eval() cannot fold.
  std::optional<slang::ConstantValue> try_eval_const_net(const slang::ast::Expression& expr, int depth = 0);
  // True iff sym is a packed struct whose every field is scalar (no nested struct
  // field) — the case where a per-field leaf split round-trips cleanly even for a
  // whole-struct read.
  bool struct_is_all_scalar(const slang::ast::ValueSymbol& sym) const;
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
