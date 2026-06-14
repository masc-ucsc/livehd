//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Module / process / instance lowering (todo/ 2s subtasks A+C). Two-phase
// like CIRCT Structure.cpp: each module body is collected (state-variable
// classification seeded from always-block timing patterns, the yosys-slang
// async_pattern.cc model) before its members lower in source order. Every
// module emits one Lnast in the extracted unit form; instances lower to
// func_call statements the upass/tolg Sub machinery resolves by module name.

#include <cctype>
#include <functional>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Statement.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

using slang::ast::ExpressionKind;
using slang::ast::StatementKind;
using slang::ast::SymbolKind;

namespace {

// Walk an assignment LHS spine down to the written base symbol.
const slang::ast::ValueSymbol* lhs_base_symbol(const slang::ast::Expression& lhs) {
  const auto* e = &lhs;
  while (true) {
    switch (e->kind) {
      case ExpressionKind::NamedValue:
      case ExpressionKind::HierarchicalValue: return &e->as<slang::ast::ValueExpressionBase>().symbol;
      case ExpressionKind::ElementSelect: e = &e->as<slang::ast::ElementSelectExpression>().value(); break;
      case ExpressionKind::RangeSelect: e = &e->as<slang::ast::RangeSelectExpression>().value(); break;
      case ExpressionKind::MemberAccess: e = &e->as<slang::ast::MemberAccessExpression>().value(); break;
      case ExpressionKind::Conversion: e = &e->as<slang::ast::ConversionExpression>().operand(); break;
      case ExpressionKind::Concatenation: return nullptr;  // caller iterates operands itself
      default: return nullptr;
    }
  }
}

// Collect the symbols written by a statement subtree, split by style.
struct Write_collector : public slang::ast::ASTVisitor<Write_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*> blocking;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> nonblocking;

  void handle(const slang::ast::AssignmentExpression& expr) {
    auto& set = expr.isNonBlocking() ? nonblocking : blocking;

    std::function<void(const slang::ast::Expression&)> note = [&](const slang::ast::Expression& lhs) {
      if (lhs.kind == ExpressionKind::Concatenation) {
        for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
          note(*op);
        }
        return;
      }
      if (const auto* sym = lhs_base_symbol(lhs)) {
        set.insert(sym);
      }
    };
    note(expr.left());

    visitDefault(expr);
  }
};

}  // namespace

std::string Slang_context::module_name_of(const slang::ast::InstanceSymbol& symbol) {
  const auto* body = symbol.getCanonicalBody();
  if (body == nullptr) {
    body = &symbol.body;
  }
  auto it = module_names_.find(body);
  if (it != module_names_.end()) {
    return it->second;
  }
  // Distinct parameterizations of one definition need distinct unit names;
  // the first (or only) body keeps the verilog name so --top/LEC line up.
  std::string base{symbol.getDefinition().name};
  std::string name = base;
  int         n    = 0;
  while (module_names_used_.contains(name)) {
    name = absl::StrCat(base, "_p", ++n);
  }
  module_names_used_.insert(name);
  module_names_.emplace(body, name);
  return name;
}

void Slang_context::emit_module_io(const slang::ast::InstanceSymbol& symbol, const Lnast_nid& in_tup,
                                   const Lnast_nid& out_tup) {
  for (const auto& p : symbol.body.getPortList()) {
    if (p->kind == SymbolKind::Port) {
      const auto& port = p->as<slang::ast::PortSymbol>();

      if (port.direction == slang::ast::ArgumentDirection::InOut || port.direction == slang::ast::ArgumentDirection::Ref) {
        emit_unsupported(port.location, "unsupported-inout-port",
                         std::string("port '") + std::string(port.name) + "' is inout/ref, which --reader slang cannot lower");
        continue;
      }

      const auto& type   = port.getType();
      const bool  is_out = port.direction == slang::ast::ArgumentDirection::Out;

      // Unpacked-array port `T arr[N-1:0]` -> flat packed [N*elem_bits-1:0] IO
      // (Verilator/yosys flatten unpacked ports identically, so LEC lines up);
      // element access lowers to a bit-slice (see flat_port_read/write).
      bool     is_flat_array = false;
      int      io_bits       = 0;
      bool     io_signed     = false;
      Mem_info flat_mi;
      if (!type.isIntegral()) {
        const auto& ct = type.getCanonicalType();
        if (ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
          const auto& arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
          const auto& elem = arr.elementType.getCanonicalType();
          if (elem.isIntegral() && !elem.isUnpackedArray()) {
            auto ei            = tinfo(elem);
            flat_mi.lower      = arr.range.lower();
            flat_mi.elem_bits  = ei.bits;
            flat_mi.elem_signed = ei.is_signed;
            flat_mi.size       = arr.range.width();
            io_bits            = ei.bits * static_cast<int>(flat_mi.size);
            io_signed          = false;  // flattened bus is just bits
            is_flat_array      = true;
          }
        }
        if (!is_flat_array) {
          emit_unsupported(port.location, "unsupported-port-type",
                           std::string("port '") + std::string(port.name) + "' has a non-integral type");
          continue;
        }
      } else {
        auto ti   = tinfo(type);
        io_bits   = ti.bits;
        io_signed = ti.is_signed;
      }

      // The inside-the-module symbol the body references.
      const auto* internal = port.internalSymbol;
      std::string var_name{port.name};
      {
        bool plain = !var_name.empty() && !std::isdigit(static_cast<unsigned char>(var_name.front()));
        for (const char c : var_name) {
          plain &= std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }
        if (!plain) {
          var_name = absl::StrCat("`", var_name, "`");  // escaped verilog identifier
        }
      }
      if (internal != nullptr) {
        sym_lname_.emplace(internal, var_name);
        used_names_.insert(var_name);
        declared_.insert(internal);  // io entries ARE the declaration
        if (is_out) {
          output_syms_.insert(internal);
        } else {
          input_syms_.insert(internal);
        }
        if (is_flat_array) {
          flat_port_syms_.insert(internal);
          mem_info_.emplace(internal, flat_mi);
        }
      }

      if (!is_flat_array && type.hasFixedRange() && !type.getFixedRange().isDescending()) {
        emit_warning(slang::SourceRange(port.location, port.location), "big-endian-port", "io",
                     std::string("port '") + std::string(port.name)
                         + "' is big-endian; flipping IO (mind mix/match with other modules)");
      }

      auto& ln    = *builder_.lnast;
      auto  entry = ln.add_child(is_out ? out_tup : in_tup, Lnast_ntype::create_store());
      ln.set_pending_srcid(mint_loc(port.location));
      ln.add_child(entry, Lnast_node::create_ref(var_name));
      ln.add_child(entry, Lnast_node::create_const("nil"));  // no default value
      emit_prim_type_int(entry, io_bits, io_signed);
      if (is_out) {
        // `@[]` landing-cycle opt-out: the form foreign Verilog modules, which
        // carry no timing markings, ingest as.
        auto st = ln.add_child(entry, Lnast_ntype::create_stages());
        ln.add_child(st, Lnast_node::create_const("nil"));
        ln.add_child(st, Lnast_node::create_const("nil"));
      }
      ln.set_pending_srcid(hhds::SourceId_invalid);
    } else if (p->kind == SymbolKind::InterfacePort) {
      emit_unsupported(p->location, "unsupported-interface-port",
                       std::string("interface port '") + std::string(p->name) + "' is not supported by --reader slang",
                       "use --reader yosys-slang for interface ports");
    } else if (p->kind == SymbolKind::MultiPort) {
      emit_unsupported(p->location, "unsupported-multi-port",
                       std::string("multi-port '") + std::string(p->name) + "' is not supported by --reader slang yet");
    } else {
      emit_unsupported(p->location, "unsupported-port-kind",
                       std::string("port '") + std::string(p->name) + "' has an unsupported kind");
    }
  }
}

// Pass 1 of the module conversion: classify processes and decide which
// variables are clocked state (reg_syms_). A variable is state when an
// edge-sensitive process writes it nonblocking. Blocking-written variables in
// edge processes stay process-local temps; one written there but read
// elsewhere has flop semantics this reader does not model yet -> diagnosed.
void Slang_context::collect_state_vars(const slang::ast::InstanceBodySymbol& body) {
  for (const auto& member : body.members()) {
    if (member.kind != SymbolKind::ProceduralBlock) {
      continue;
    }
    const auto& pbs = member.as<slang::ast::ProceduralBlockSymbol>();

    bool is_edge = false;
    if (pbs.procedureKind == slang::ast::ProceduralBlockKind::Always
        || pbs.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
      const auto& stmt = pbs.getBody();
      if (stmt.kind == StatementKind::Timed) {
        const auto& timing = stmt.as<slang::ast::TimedStatement>().timing;
        auto        scan   = [&](const slang::ast::TimingControl& tc) {
          if (tc.kind == slang::ast::TimingControlKind::SignalEvent) {
            auto edge = tc.as<slang::ast::SignalEventControl>().edge;
            is_edge |= edge == slang::ast::EdgeKind::PosEdge || edge == slang::ast::EdgeKind::NegEdge;
          }
        };
        if (timing.kind == slang::ast::TimingControlKind::EventList) {
          for (const auto* ev : timing.as<slang::ast::EventListControl>().events) {
            scan(*ev);
          }
        } else {
          scan(timing);
        }
      }
    }
    if (!is_edge) {
      continue;
    }

    Write_collector wc;
    pbs.getBody().visit(wc);
    for (const auto* sym : wc.nonblocking) {
      reg_syms_.insert(sym);
    }
  }
}

bool Slang_context::lower_module(const slang::ast::InstanceSymbol& symbol) {
  const auto* body = symbol.getCanonicalBody();
  if (body == nullptr) {
    body = &symbol.body;
  }

  if (auto it = lowered_.find(body); it != lowered_.end()) {
    return it->second != nullptr;  // already done (or already failed)
  }

  if (!symbol.isModule()) {
    emit_unsupported(symbol.location, "unsupported-instance-kind",
                     std::string("'") + std::string(symbol.name) + "' is not a module (interfaces/programs unsupported)");
    return false;
  }

  lowered_.emplace(body, nullptr);  // breaks recursion; reinserted at the end

  auto unit_name = module_name_of(symbol);

  // Save in-flight per-module state (a submodule definition lowers
  // recursively from its instantiation site).
  auto saved_builder       = std::move(builder_);
  auto saved_body          = body_;
  auto saved_eval          = std::move(eval_ctx_);
  auto saved_sym_lname     = std::move(sym_lname_);
  auto saved_used          = std::move(used_names_);
  auto saved_inputs        = std::move(input_syms_);
  auto saved_outputs       = std::move(output_syms_);
  auto saved_regs          = std::move(reg_syms_);
  auto saved_mems          = std::move(mem_syms_);
  auto saved_declared      = std::move(declared_);
  auto saved_prefix        = std::move(genblk_prefix_);
  auto saved_failed        = module_failed_;
  auto saved_proc_kind     = proc_kind_;
  auto saved_style         = std::move(proc_assign_style_);
  auto saved_blocking      = std::move(proc_blocking_written_);
  auto saved_bools         = std::move(bool_values_);
  auto saved_mem_info      = std::move(mem_info_);
  auto saved_reg_declared  = std::move(reg_declared_);
  auto saved_local_cnt     = local_cnt_;

  builder_ = Lnast_builder();
  sym_lname_.clear();
  used_names_.clear();
  input_syms_.clear();
  output_syms_.clear();
  reg_syms_.clear();
  mem_syms_.clear();
  declared_.clear();
  genblk_prefix_.clear();
  module_failed_ = false;
  proc_kind_     = Proc_kind::none;
  proc_assign_style_.clear();
  proc_blocking_written_.clear();
  bool_values_.clear();
  mem_info_.clear();
  reg_declared_.clear();
  local_cnt_ = 0;

  body_ = body;
  eval_ctx_.emplace(body->asSymbol(), slang::ast::EvalFlags::CacheResults);

  builder_.lnast = std::make_shared<Lnast>(unit_name);
  builder_.lnast->set_lambda_kind("mod");
  auto root_nid = builder_.lnast->set_root(Lnast_ntype::create_top());
  builder_.lnast->set_srcid(root_nid, mint_loc(symbol.location));
  auto io_nid  = builder_.lnast->add_child(root_nid, Lnast_ntype::create_io());
  auto in_tup  = builder_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  auto out_tup = builder_.lnast->add_child(io_nid, Lnast_ntype::create_tuple_add());
  builder_.idx_stmts = builder_.lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  emit_module_io(symbol, in_tup, out_tup);
  collect_state_vars(*body);
  // Hoist every state reg's declare to module start: drivers emit in
  // dataflow order, so a comb reader sorted before the owning edge process
  // must already see the declare (reg q-reads are order-free only once
  // declared). Output regs declare here too - the q pin IS the output.
  for (const auto* sym : reg_syms_) {
    declare_reg(sym->as<slang::ast::ValueSymbol>());
  }
  lower_members(*body);

  bool ok = !module_failed_;
  if (ok) {
    lowered_[body] = builder_.lnast;
    ordered_lnasts_.push_back(builder_.lnast);
  }

  // restore the enclosing module's state
  builder_               = std::move(saved_builder);
  body_                  = saved_body;
  eval_ctx_              = std::move(saved_eval);
  sym_lname_             = std::move(saved_sym_lname);
  used_names_            = std::move(saved_used);
  input_syms_            = std::move(saved_inputs);
  output_syms_           = std::move(saved_outputs);
  reg_syms_              = std::move(saved_regs);
  mem_syms_              = std::move(saved_mems);
  declared_              = std::move(saved_declared);
  genblk_prefix_         = std::move(saved_prefix);
  module_failed_         = saved_failed;
  proc_kind_             = saved_proc_kind;
  proc_assign_style_     = std::move(saved_style);
  proc_blocking_written_ = std::move(saved_blocking);
  bool_values_           = std::move(saved_bools);
  mem_info_              = std::move(saved_mem_info);
  reg_declared_          = std::move(saved_reg_declared);
  local_cnt_             = saved_local_cnt;

  return ok;
}

// comp_type_array declare for an unpacked array (a verilog memory). The
// fwd=0 attr rides ahead of the declare: verilog nonblocking memory writes
// never forward to same-cycle reads (Pyrope reg arrays default to
// program-order forwarding).
bool Slang_context::declare_unpacked(const slang::ast::ValueSymbol& sym, bool is_reg) {
  const auto& ct = sym.getType().getCanonicalType();
  if (ct.kind != slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    emit_unsupported(sym.location, "unsupported-array-kind",
                     std::string("array '") + std::string(sym.name) + "' is not a fixed-size unpacked array");
    return false;
  }
  const auto& arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
  const auto& elem = arr.elementType.getCanonicalType();
  if (!elem.isIntegral() || elem.isUnpackedArray()) {
    emit_unsupported(sym.location, "unsupported-mem-element",
                     std::string("memory '") + std::string(sym.name)
                         + "' has a non-integral or multi-dimensional element type");
    return false;
  }
  auto ei = tinfo(elem);

  Mem_info mi;
  mi.lower       = arr.range.lower();
  mi.elem_bits   = ei.bits;
  mi.elem_signed = ei.is_signed;
  mi.size        = arr.range.width();
  mem_info_.emplace(&sym, mi);
  mem_syms_.insert(&sym);

  auto  name = lname_of(sym);
  auto& ln   = *builder_.lnast;
  set_pending_loc(sym.location);
  if (is_reg) {
    auto aidx = builder_.add_child(Lnast_ntype::create_attr_set());
    ln.add_child(aidx, Lnast_node::create_ref(name));
    ln.add_child(aidx, Lnast_node::create_const("fwd"));
    ln.add_child(aidx, Lnast_node::create_const("0"));
  }
  auto didx = builder_.add_child(Lnast_ntype::create_declare());
  ln.add_child(didx, Lnast_node::create_ref(name));
  auto tidx = ln.add_child(didx, Lnast_ntype::create_comp_type_array());
  emit_prim_type_int(tidx, ei.bits, ei.is_signed);
  ln.add_child(tidx, Lnast_node::create_const(absl::StrCat("[", mi.size, "]")));
  ln.add_child(didx, Lnast_node::create_const(is_reg ? "reg" : "mut"));
  if (is_reg) {
    ln.add_child(didx, Lnast_node::create_const("nil"));  // no power-on contents
  }
  clear_pending_loc();
  return true;
}

// State regs declare once at module start, output regs included (ports sit
// in declared_ from the io emission, hence the dedicated reg_declared_ set).
void Slang_context::declare_reg(const slang::ast::ValueSymbol& sym) {
  if (reg_declared_.contains(&sym)) {
    return;
  }
  reg_declared_.insert(&sym);
  declared_.insert(&sym);

  const auto& type = sym.getType();
  if (type.getCanonicalType().isUnpackedArray()) {
    declare_unpacked(sym, /*is_reg=*/true);
    return;
  }
  if (!type.isIntegral()) {
    emit_unsupported(sym.location, "unsupported-var-type",
                     std::string("variable '") + std::string(sym.name) + "' has a non-integral type");
    return;
  }

  auto ti   = tinfo(type);
  auto name = lname_of(sym);
  set_pending_loc(sym.location);
  builder_.create_declare_stmts(name,
                                "reg",
                                ti.is_signed ? std::string(Dlop::get_mask_value(ti.bits - 1)->to_pyrope()) : mask_text(ti.bits),
                                ti.is_signed ? std::string(Dlop::get_neg_mask_value(ti.bits - 1)->to_pyrope()) : "0",
                                "nil");  // no reset by default; async patterns override via attrs
  clear_pending_loc();
}

void Slang_context::declare_value_symbol(const slang::ast::ValueSymbol& sym, bool force_reg) {
  if (force_reg || reg_syms_.contains(&sym)) {
    declare_reg(sym);
    return;
  }
  if (declared_.contains(&sym)) {
    return;
  }
  declared_.insert(&sym);

  const auto& type = sym.getType();
  if (type.getCanonicalType().isUnpackedArray()) {
    declare_unpacked(sym, /*is_reg=*/false);
    return;
  }
  if (!type.isIntegral()) {
    emit_unsupported(sym.location, "unsupported-var-type",
                     std::string("variable '") + std::string(sym.name) + "' has a non-integral type");
    return;
  }

  auto ti   = tinfo(type);
  auto name = lname_of(sym);

  set_pending_loc(sym.location);
  {
    // No declared range on locals: the importer masks every op result to its
    // verilog width already, and a range would conflict with the x poison
    // init (an all-? literal reads as -1 to the bitwidth range check).
    builder_.create_declare_stmts(name, "mut", "", "");
    // poison init: a read of never-assigned bits is x, not silent 0
    if (ti.is_signed) {
      builder_.create_assign_stmts(name, "0sb?");
    } else {
      std::string qmarks(static_cast<size_t>(ti.bits), '?');
      builder_.create_assign_stmts(name, absl::StrCat("0ub", qmarks));
    }
  }
  clear_pending_loc();
}

namespace {

// Per-driver def/use collection for the dependency sort. Reads are every
// NamedValue in rvalue position plus partial-write LHS bases (the RMW
// lowering reads them); full scalar-write LHS bases are NOT reads.
struct Dep_collector : public slang::ast::ASTVisitor<Dep_collector, slang::ast::VisitFlags::AllGood> {
  absl::flat_hash_set<const slang::ast::ValueSymbol*> reads;
  absl::flat_hash_set<const slang::ast::ValueSymbol*> writes;

  void note_lhs(const slang::ast::Expression& lhs) {
    switch (lhs.kind) {
      case ExpressionKind::NamedValue:
      case ExpressionKind::HierarchicalValue: writes.insert(&lhs.as<slang::ast::ValueExpressionBase>().symbol); return;
      case ExpressionKind::Conversion: note_lhs(lhs.as<slang::ast::ConversionExpression>().operand()); return;
      case ExpressionKind::Concatenation:
        for (const auto* op : lhs.as<slang::ast::ConcatenationExpression>().operands()) {
          note_lhs(*op);
        }
        return;
      case ExpressionKind::ElementSelect: {
        const auto& es = lhs.as<slang::ast::ElementSelectExpression>();
        es.selector().visit(*this);
        if (const auto* sym = lhs_base_symbol(es.value())) {
          writes.insert(sym);
          reads.insert(sym);  // partial write reads the base (RMW)
        }
        return;
      }
      case ExpressionKind::RangeSelect: {
        const auto& rs = lhs.as<slang::ast::RangeSelectExpression>();
        rs.left().visit(*this);
        rs.right().visit(*this);
        if (const auto* sym = lhs_base_symbol(rs.value())) {
          writes.insert(sym);
          reads.insert(sym);
        }
        return;
      }
      case ExpressionKind::MemberAccess: {
        if (const auto* sym = lhs_base_symbol(lhs)) {
          writes.insert(sym);
          reads.insert(sym);
        }
        return;
      }
      default:
        if (const auto* sym = lhs_base_symbol(lhs)) {
          writes.insert(sym);
          reads.insert(sym);
        }
    }
  }

  void handle(const slang::ast::AssignmentExpression& expr) {
    note_lhs(expr.left());
    expr.right().visit(*this);
    if (expr.timingControl != nullptr) {
      expr.timingControl->visit(*this);
    }
  }

  void handle(const slang::ast::ValueExpressionBase& expr) { reads.insert(&expr.symbol); }
};

}  // namespace

void Slang_context::lower_members(const slang::ast::Scope& scope) {
  // ── pass 1: collect drivers (recursing through generate blocks) ───────────
  struct Driver {
    const slang::ast::Symbol*                           member;
    std::string                                         prefix;  // genblk name prefix at collection point
    absl::flat_hash_set<const slang::ast::ValueSymbol*> reads;
    absl::flat_hash_set<const slang::ast::ValueSymbol*> writes;
  };
  std::vector<Driver> drivers;

  std::function<void(const slang::ast::Scope&)> collect = [&](const slang::ast::Scope& sc) {
    for (const auto& member : sc.members()) {
      switch (member.kind) {
        case SymbolKind::Port:
        case SymbolKind::Parameter:
        case SymbolKind::TypeParameter:
        case SymbolKind::TypeAlias:
        case SymbolKind::TransparentMember:
        case SymbolKind::EmptyMember:
        case SymbolKind::Genvar:
        case SymbolKind::StatementBlock:    // lowered where referenced (slang puts them next to procedures)
        case SymbolKind::Subroutine:        // bodies fold at call sites or are diagnosed there
        case SymbolKind::ElabSystemTask:    // $info/$warning/$error handled by slang itself
        case SymbolKind::WildcardImport:    // `import pkg::*` — slang already resolved the names
        case SymbolKind::ExplicitImport:    // `import pkg::sym` — ditto
        case SymbolKind::Modport:           // interface modport view; not codegen-relevant here
        case SymbolKind::AssertionPort:     // property/sequence formal args
        case SymbolKind::Sequence:          // named sequences (assertion-only, not synthesized)
        case SymbolKind::Property:          // named properties (assertion-only, not synthesized)
          break;

        case SymbolKind::Net: {
          const auto& ns = member.as<slang::ast::NetSymbol>();
          if (const auto* expr = ns.getInitializer()) {
            Driver d{&member, genblk_prefix_, {}, {}};
            Dep_collector dc;
            expr->visit(dc);
            d.reads = std::move(dc.reads);
            d.writes.insert(&ns);
            drivers.push_back(std::move(d));
          }
          break;
        }

        case SymbolKind::Variable: {
          const auto& vs = member.as<slang::ast::VariableSymbol>();
          if (vs.getInitializer() != nullptr) {
            emit_warning(slang::SourceRange(vs.location, vs.location), "var-init-ignored", "unsupported",
                         std::string("initializer of '") + std::string(vs.name) + "' is ignored (initial-block semantics)");
          }
          break;
        }

        case SymbolKind::ContinuousAssign: {
          Driver        d{&member, genblk_prefix_, {}, {}};
          Dep_collector dc;
          member.as<slang::ast::ContinuousAssignSymbol>().getAssignment().visit(dc);
          d.reads  = std::move(dc.reads);
          d.writes = std::move(dc.writes);
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::ProceduralBlock: {
          Driver        d{&member, genblk_prefix_, {}, {}};
          Dep_collector dc;
          member.as<slang::ast::ProceduralBlockSymbol>().getBody().visit(dc);
          d.reads  = std::move(dc.reads);
          d.writes = std::move(dc.writes);
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::Instance: {
          Driver d{&member, genblk_prefix_, {}, {}};
          for (const auto* conn : member.as<slang::ast::InstanceSymbol>().getPortConnections()) {
            const auto* expr = conn->getExpression();
            if (expr == nullptr || conn->port.kind != SymbolKind::Port) {
              continue;
            }
            Dep_collector dc;
            if (conn->port.as<slang::ast::PortSymbol>().direction == slang::ast::ArgumentDirection::Out) {
              const auto* target = expr;
              if (const auto* assign = target->as_if<slang::ast::AssignmentExpression>()) {
                target = &assign->left();
              }
              dc.note_lhs(*target);
            } else {
              expr->visit(dc);
            }
            d.reads.insert(dc.reads.begin(), dc.reads.end());
            d.writes.insert(dc.writes.begin(), dc.writes.end());
          }
          drivers.push_back(std::move(d));
          break;
        }

        case SymbolKind::GenerateBlock: {
          const auto& gen = member.as<slang::ast::GenerateBlockSymbol>();
          if (gen.isUninstantiated) {
            break;
          }
          auto saved_prefix = genblk_prefix_;
          if (!gen.name.empty() || gen.getParentScope()->asSymbol().kind != SymbolKind::GenerateBlockArray) {
            genblk_prefix_ = absl::StrCat(genblk_prefix_, gen.getExternalName(), "_");
          }
          collect(gen);
          genblk_prefix_ = saved_prefix;
          break;
        }

        case SymbolKind::GenerateBlockArray: {
          const auto& arr          = member.as<slang::ast::GenerateBlockArraySymbol>();
          auto        saved_prefix = genblk_prefix_;
          for (const auto* entry : arr.entries) {
            std::string idx_txt
                = entry->arrayIndex != nullptr ? entry->arrayIndex->toString() : std::to_string(entry->constructIndex);
            genblk_prefix_ = absl::StrCat(saved_prefix, arr.getExternalName(), "_", idx_txt, "_");
            collect(*entry);
          }
          genblk_prefix_ = saved_prefix;
          break;
        }

        case SymbolKind::UninstantiatedDef:
          emit_unsupported(member.location, "unknown-module",
                           std::string("instance '") + std::string(member.name)
                               + "' refers to an unknown module (blackboxes are not supported by --reader slang)",
                           "provide the module source, or use --reader yosys-slang with a blackbox library");
          break;

        default:
          emit_unsupported(member.location, "unsupported-member",
                           std::string("module member '") + std::string(member.name) + "' (kind "
                               + std::string(slang::ast::toString(member.kind)) + ") is not supported by --reader slang");
      }
    }
  };
  collect(scope);

  // ── pass 2: dependency edges. A read of a wire depends on every driver
  // writing it; reads of regs (q pins), inputs, and locals are order-free.
  // Co-writers of one wire (partial writers) keep source order instead of an
  // edge (the RMW chain is order-stable and a cycle otherwise).
  absl::flat_hash_map<const slang::ast::ValueSymbol*, std::vector<size_t>> writers_of;
  for (size_t i = 0; i < drivers.size(); ++i) {
    for (const auto* w : drivers[i].writes) {
      writers_of[w].push_back(i);
    }
  }
  std::vector<absl::flat_hash_set<size_t>> deps(drivers.size());
  for (size_t i = 0; i < drivers.size(); ++i) {
    for (const auto* r : drivers[i].reads) {
      if (reg_syms_.contains(r) || input_syms_.contains(r)) {
        continue;
      }
      auto it = writers_of.find(r);
      if (it == writers_of.end()) {
        continue;
      }
      const bool i_writes_r = drivers[i].writes.contains(r);
      for (size_t w : it->second) {
        if (w == i || i_writes_r) {
          continue;
        }
        deps[i].insert(w);
      }
    }
  }

  // ── pass 3: stable Kahn (ready drivers emit in source order) ──────────────
  std::vector<size_t> order;
  std::vector<bool>   emitted(drivers.size(), false);
  bool                progress = true;
  while (progress) {
    progress = false;
    for (size_t i = 0; i < drivers.size(); ++i) {
      if (emitted[i]) {
        continue;
      }
      bool ready = true;
      for (size_t d : deps[i]) {
        if (!emitted[d]) {
          ready = false;
          break;
        }
      }
      if (ready) {
        order.push_back(i);
        emitted[i] = true;
        progress   = true;
      }
    }
  }
  std::vector<size_t> cyclic;
  for (size_t i = 0; i < drivers.size(); ++i) {
    if (!emitted[i]) {
      cyclic.push_back(i);
    }
  }
  if (!cyclic.empty()) {
    emit_warning(slang::SourceRange(drivers[cyclic.front()].member->location, drivers[cyclic.front()].member->location),
                 "comb-loop", "time",
                 "combinational dependency cycle between drivers; falling back to source order with settled reads");
  }

  // ── pass 4: emit ──────────────────────────────────────────────────────────
  auto emit_driver = [&](size_t i, bool cyclic_member) {
    const auto& d            = drivers[i];
    auto        saved_prefix = genblk_prefix_;
    genblk_prefix_           = d.prefix;
    in_comb_cycle_           = cyclic_member;
    switch (d.member->kind) {
      case SymbolKind::Net: {
        const auto& ns = d.member->as<slang::ast::NetSymbol>();
        proc_kind_     = Proc_kind::none;
        set_pending_loc(ns.getInitializer()->sourceRange);
        // A net is an integer lvalue, so a bool initializer (e.g. `wire x =
        // a == b;`) materializes to 0/1 — the same rule lower_assign applies to
        // the continuous-`assign` path.
        auto v = to_int_value(lower_rvalue(*ns.getInitializer()));
        if (!declared_.contains(&ns) && !input_syms_.contains(&ns)) {
          declare_value_symbol(ns, false);
        }
        builder_.create_assign_stmts(lname_of(ns), v);
        clear_pending_loc();
        break;
      }
      case SymbolKind::ContinuousAssign: lower_continuous_assign(d.member->as<slang::ast::ContinuousAssignSymbol>()); break;
      case SymbolKind::ProceduralBlock: lower_process(d.member->as<slang::ast::ProceduralBlockSymbol>()); break;
      case SymbolKind::Instance: lower_instance(d.member->as<slang::ast::InstanceSymbol>()); break;
      default: break;
    }
    in_comb_cycle_ = false;
    genblk_prefix_ = saved_prefix;
  };

  for (size_t i : order) {
    emit_driver(i, false);
  }
  for (size_t i : cyclic) {
    emit_driver(i, true);
  }
}

void Slang_context::lower_continuous_assign(const slang::ast::ContinuousAssignSymbol& ca) {
  proc_kind_ = Proc_kind::none;
  proc_assign_style_.clear();
  proc_blocking_written_.clear();

  if (ca.getDelay() != nullptr) {
    emit_warning(slang::SourceRange(ca.location, ca.location), "delay-ignored", "unsupported", "assignment delay is ignored (synthesis semantics)");
  }

  const auto& as = ca.getAssignment();
  if (as.kind != ExpressionKind::Assignment) {
    emit_unsupported(ca.location, "unsupported-continuous-assign", "unsupported continuous assignment shape");
    return;
  }
  set_pending_loc(as.sourceRange);
  lower_assign(as.as<slang::ast::AssignmentExpression>());
  clear_pending_loc();
}

void Slang_context::lower_process(const slang::ast::ProceduralBlockSymbol& pbs) {
  using slang::ast::ProceduralBlockKind;
  using slang::ast::TimingControlKind;

  proc_assign_style_.clear();
  proc_blocking_written_.clear();
  unroll_budget_ = options_.unroll_limit;

  switch (pbs.procedureKind) {
    case ProceduralBlockKind::Initial:
      // Synthesis ignores initial blocks (memory init is a 2s-D follow-up).
      emit_warning(slang::SourceRange(pbs.location, pbs.location), "initial-ignored", "unsupported", "initial block is ignored (synthesis semantics)");
      return;
    case ProceduralBlockKind::Final: return;
    case ProceduralBlockKind::AlwaysComb: lower_comb_process(pbs.getBody()); return;
    case ProceduralBlockKind::AlwaysLatch:
      emit_unsupported(pbs.location, "unsupported-latch", "always_latch is not supported by --reader slang");
      return;
    case ProceduralBlockKind::Always:
    case ProceduralBlockKind::AlwaysFF: break;
  }

  const auto& stmt = pbs.getBody();
  // A standalone `assert/assume/cover property(...)` is modeled by slang as an
  // implicit Always procedural block whose body is the assertion (possibly
  // wrapped in a Block/List), NOT a Timed event-control. Assertions are not
  // synthesized, so ignore such bodies (mirrors lower_statement in slang_stmt.cpp).
  std::function<bool(const slang::ast::Statement&)> assertion_only = [&](const slang::ast::Statement& s) -> bool {
    switch (s.kind) {
      case StatementKind::Empty:
      case StatementKind::ImmediateAssertion:
      case StatementKind::ConcurrentAssertion: return true;
      case StatementKind::Block: return assertion_only(s.as<slang::ast::BlockStatement>().body);
      case StatementKind::List:
        for (const auto* c : s.as<slang::ast::StatementList>().list) {
          if (!assertion_only(*c)) {
            return false;
          }
        }
        return true;
      default: return false;
    }
  };
  if (stmt.kind != StatementKind::Timed && assertion_only(stmt)) {
    emit_warning(stmt.sourceRange, "assertion-ignored", "unsupported", "assertion-only process ignored (synthesis semantics)");
    return;
  }
  if (stmt.kind != StatementKind::Timed) {
    emit_unsupported(stmt.sourceRange, "unsupported-always",
                     "always block without an event control is not supported by --reader slang");
    return;
  }
  const auto& timed = stmt.as<slang::ast::TimedStatement>();

  std::vector<const slang::ast::SignalEventControl*> edges;
  bool                                               implicit = false;
  bool                                               bad      = false;

  std::function<void(const slang::ast::TimingControl&)> classify = [&](const slang::ast::TimingControl& tc) {
    switch (tc.kind) {
      case TimingControlKind::ImplicitEvent: implicit = true; break;
      case TimingControlKind::SignalEvent: {
        const auto& se = tc.as<slang::ast::SignalEventControl>();
        if (se.iffCondition != nullptr) {
          emit_unsupported(tc.sourceRange, "unsupported-iff", "iff event qualifiers are not supported");
          bad = true;
          return;
        }
        switch (se.edge) {
          case slang::ast::EdgeKind::None: implicit = true; break;  // @(a or b) sensitivity-list style
          case slang::ast::EdgeKind::PosEdge:
          case slang::ast::EdgeKind::NegEdge: edges.push_back(&se); break;
          case slang::ast::EdgeKind::BothEdges:
            emit_unsupported(tc.sourceRange, "unsupported-dual-edge", "dual-edge @(edge x) is not supported");
            bad = true;
            break;
        }
        break;
      }
      case TimingControlKind::EventList:
        for (const auto* ev : tc.as<slang::ast::EventListControl>().events) {
          classify(*ev);
        }
        break;
      default:
        emit_unsupported(tc.sourceRange, "unsupported-timing", "this event control is not supported by --reader slang");
        bad = true;
    }
  };
  classify(timed.timing);

  if (bad) {
    return;
  }
  if (implicit && !edges.empty()) {
    emit_unsupported(timed.timing.sourceRange, "edge-implicit-mixing",
                     "mixing edge and non-edge sensitivity in one always block is not supported");
    return;
  }
  if (implicit || edges.empty()) {
    lower_comb_process(timed.stmt);
    return;
  }

  // Edge-sensitive: extract the async-reset rungs (yosys-slang
  // interpret_async_pattern) until one clock trigger remains. Each extra
  // edge trigger must guard the next if/else rung; its then-arm holds the
  // CONST reset values, which become per-reg initial/reset_pin/sync attrs.
  std::vector<const slang::ast::Statement*> prologue;
  const slang::ast::Statement*              body = &timed.stmt;

  while (edges.size() > 1) {
    if (body->kind == StatementKind::Block) {
      body = &body->as<slang::ast::BlockStatement>().body;
      continue;
    }
    if (body->kind != StatementKind::Conditional) {
      emit_unsupported(body->sourceRange, "unsupported-async-pattern",
                       "expected `if (rst) ... else ...` rungs for the extra edge triggers",
                       "use a synchronous reset, or --reader yosys-slang");
      return;
    }
    const auto& cond_stmt = body->as<slang::ast::ConditionalStatement>();
    if (cond_stmt.conditions.size() != 1 || cond_stmt.conditions[0].pattern != nullptr || cond_stmt.ifFalse == nullptr) {
      emit_unsupported(body->sourceRange, "unsupported-async-pattern",
                       "the async-reset if must have one plain condition and an else arm");
      return;
    }

    // Normalize the condition to (signal symbol, polarity).
    const slang::ast::Expression* cond     = cond_stmt.conditions[0].expr;
    bool                          polarity = true;
    while (true) {
      if (cond->kind == ExpressionKind::UnaryOp) {
        const auto& un = cond->as<slang::ast::UnaryExpression>();
        if (un.op == slang::ast::UnaryOperator::LogicalNot || un.op == slang::ast::UnaryOperator::BitwiseNot) {
          polarity = !polarity;
          cond     = &un.operand();
          continue;
        }
      }
      if (cond->kind == ExpressionKind::Conversion) {
        cond = &cond->as<slang::ast::ConversionExpression>().operand();
        continue;
      }
      if (cond->kind == ExpressionKind::BinaryOp) {
        const auto& bin = cond->as<slang::ast::BinaryExpression>();
        if (bin.op == slang::ast::BinaryOperator::Equality || bin.op == slang::ast::BinaryOperator::Inequality) {
          if (auto cv = try_eval(bin.right()); cv && cv->isInteger()) {
            const bool rhs_true = cv->isTrue();
            if (bin.op == slang::ast::BinaryOperator::Inequality ? rhs_true : !rhs_true) {
              polarity = !polarity;
            }
            cond = &bin.left();
            continue;
          }
        }
      }
      break;
    }
    if (cond->kind != ExpressionKind::NamedValue) {
      emit_unsupported(cond->sourceRange, "unsupported-async-pattern", "the async-reset condition must be a plain signal");
      return;
    }
    const auto* rst_sym = &cond->as<slang::ast::NamedValueExpression>().symbol;

    // Match the condition signal to one of the extra edge triggers.
    size_t match = edges.size();
    for (size_t i = 0; i < edges.size(); ++i) {
      if (edges[i]->expr.kind == ExpressionKind::NamedValue
          && &edges[i]->expr.as<slang::ast::NamedValueExpression>().symbol == rst_sym) {
        match = i;
        break;
      }
    }
    if (match == edges.size()) {
      emit_unsupported(cond->sourceRange, "unsupported-async-pattern",
                       "the async-reset condition does not name one of the edge triggers");
      return;
    }
    const bool edge_pos = edges[match]->edge == slang::ast::EdgeKind::PosEdge;
    if (edge_pos != polarity) {
      emit_warning(cond->sourceRange, "async-reset-polarity", "time",
                   "the async-reset guard polarity does not match its edge trigger");
    }
    if (!input_syms_.contains(rst_sym)) {
      emit_unsupported(cond->sourceRange, "unsupported-async-pattern", "the async reset must be a module input");
      return;
    }

    // The then-arm must be const nonblocking stores to regs: those become
    // the reset values.
    std::function<bool(const slang::ast::Statement&)> harvest = [&](const slang::ast::Statement& s) -> bool {
      switch (s.kind) {
        case StatementKind::Empty: return true;
        case StatementKind::Block: return harvest(s.as<slang::ast::BlockStatement>().body);
        case StatementKind::List: {
          for (const auto* sub : s.as<slang::ast::StatementList>().list) {
            if (!harvest(*sub)) {
              return false;
            }
          }
          return true;
        }
        case StatementKind::Conditional: {
          // Reset arms commonly guard config-dependent regs with a compile-time
          // `if` (e.g. `if (CVA6Cfg.RVZCMT) ...`, `if (FPGA_ALTERA) ...`). Fold
          // the constant condition and harvest only the taken branch.
          const auto& cs = s.as<slang::ast::ConditionalStatement>();
          if (cs.conditions.size() != 1 || cs.conditions[0].pattern != nullptr) {
            return false;
          }
          auto cv = try_eval(*cs.conditions[0].expr);
          if (!cv || !cv->isInteger()) {
            return false;  // a non-constant reset guard has no flop lowering
          }
          if (cv->isTrue()) {
            return harvest(cs.ifTrue);
          }
          return cs.ifFalse == nullptr ? true : harvest(*cs.ifFalse);
        }
        case StatementKind::ExpressionStatement: {
          const auto& e = s.as<slang::ast::ExpressionStatement>().expr;
          if (e.kind != ExpressionKind::Assignment) {
            return false;
          }
          const auto& as  = e.as<slang::ast::AssignmentExpression>();
          const auto* sym = lhs_base_symbol(as.left());
          if (sym == nullptr || as.left().kind != ExpressionKind::NamedValue || !reg_syms_.contains(sym)) {
            return false;
          }
          auto cv = try_eval(as.right());
          if (!cv || !cv->isInteger()) {
            return false;
          }
          declare_reg(*sym);  // ensure declared (hoisting normally did)
          auto  name = lname_of(*sym);
          auto& ln   = *builder_.lnast;
          auto  emit_attr = [&](std::string_view key, const std::string& val, bool val_is_ref) {
            auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
            ln.add_child(idx, Lnast_node::create_ref(name));
            ln.add_child(idx, Lnast_node::create_const(key));
            if (val_is_ref) {
              ln.add_child(idx, Lnast_node::create_ref(val));
            } else {
              ln.add_child(idx, Lnast_node::create_const(val));
            }
          };
          emit_attr("initial", const_text(cv->integer()), false);
          emit_attr("reset_pin", lname_of(*rst_sym), true);
          emit_attr("sync", "false", false);
          if (!edge_pos) {
            emit_attr("negreset", "true", false);
          }
          return true;
        }
        default: return false;
      }
    };
    if (!harvest(cond_stmt.ifTrue)) {
      emit_unsupported(cond_stmt.ifTrue.sourceRange, "unsupported-async-load",
                       "the async-reset arm must contain only constant non-blocking writes to state regs",
                       "non-constant async loads have no flop lowering; use --reader yosys-slang");
      return;
    }

    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(match));
    body = cond_stmt.ifFalse;
  }

  lower_ff_process(*edges[0], *body, prologue);
}

void Slang_context::lower_comb_process(const slang::ast::Statement& body) {
  proc_kind_ = Proc_kind::comb;
  lower_statement(body);
  proc_kind_ = Proc_kind::none;
}

void Slang_context::lower_ff_process(const slang::ast::SignalEventControl& clock, const slang::ast::Statement& body,
                                     std::vector<const slang::ast::Statement*>& prologue) {
  proc_kind_ = Proc_kind::seq;

  // Identify the clock signal; tolg reuses a declared 1-bit `clk`/`clock`
  // input implicitly. Other names/edges ride per-reg attrs after the body
  // (clock_pin / posclk), keyed on the regs this process writes.
  const slang::ast::ValueSymbol* clk_sym = nullptr;
  if (clock.expr.kind == ExpressionKind::NamedValue) {
    clk_sym = &clock.expr.as<slang::ast::NamedValueExpression>().symbol;
  }
  if (clk_sym == nullptr) {
    emit_unsupported(clock.sourceRange, "unsupported-clock", "the clock must be a plain signal");
    proc_kind_ = Proc_kind::none;
    return;
  }
  const bool negedge      = clock.edge == slang::ast::EdgeKind::NegEdge;
  const bool implicit_clk = !negedge && (clk_sym->name == "clk" || clk_sym->name == "clock");

  Write_collector wc;
  body.visit(wc);

  // A blocking-written variable of an edge process that other code reads has
  // flop semantics this reader does not model yet.
  for (const auto* sym : wc.blocking) {
    if (output_syms_.contains(sym)) {
      emit_unsupported(sym->location, "blocking-ff-output",
                       std::string("output '") + std::string(sym->name)
                           + "' is blocking-assigned in an edge-sensitive process; --reader slang only supports `<=` for state",
                       "use a non-blocking assignment");
    }
  }

  for (const auto* stmt : prologue) {
    lower_statement(*stmt);
  }
  lower_statement(body);

  if (!implicit_clk) {
    auto& ln = *builder_.lnast;
    for (const auto* sym : wc.nonblocking) {
      if (!reg_syms_.contains(sym)) {
        continue;
      }
      auto name = lname_of(*sym);
      if (!(clk_sym->name == "clk" || clk_sym->name == "clock")) {
        auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(idx, Lnast_node::create_ref(name));
        ln.add_child(idx, Lnast_node::create_const("clock_pin"));
        ln.add_child(idx, Lnast_node::create_ref(lname_of(*clk_sym)));
      }
      if (negedge) {
        auto idx = builder_.add_child(Lnast_ntype::create_attr_set());
        ln.add_child(idx, Lnast_node::create_ref(name));
        ln.add_child(idx, Lnast_node::create_const("posclk"));
        ln.add_child(idx, Lnast_node::create_const("false"));
      }
    }
  }

  proc_kind_ = Proc_kind::none;
}

Slang_context::Tinfo Slang_context::flat_or_tinfo(const slang::ast::Type& t) {
  const auto& ct = t.getCanonicalType();
  if (!ct.isIntegral() && ct.kind == slang::ast::SymbolKind::FixedSizeUnpackedArrayType) {
    const auto& arr  = ct.as<slang::ast::FixedSizeUnpackedArrayType>();
    const auto& elem = arr.elementType.getCanonicalType();
    if (elem.isIntegral() && !elem.isUnpackedArray()) {
      Tinfo r;
      r.bits      = tinfo(elem).bits * static_cast<int>(arr.range.width());
      r.is_signed = false;
      return r;
    }
  }
  return tinfo(t);
}

void Slang_context::lower_instance(const slang::ast::InstanceSymbol& inst) {
  if (!lower_module(inst)) {
    return;  // diagnosed inside
  }
  auto callee = module_name_of(inst);

  proc_kind_ = Proc_kind::none;

  // Lower input connections first, then emit the func_call with named args,
  // then bind outputs. tolg resolves the callee by module name in the
  // registry and emits an Ntype_op::Sub.
  struct Out_conn {
    const slang::ast::PortSymbol* port;
    const slang::ast::Expression* expr;
  };
  std::vector<std::pair<std::string, std::string>> in_args;  // (port, value)
  std::vector<Out_conn>                            outs;
  size_t                                           n_outputs_total = 0;

  for (const auto* conn : inst.getPortConnections()) {
    if (conn->port.kind != SymbolKind::Port) {
      emit_unsupported(inst.location, "unsupported-port-conn",
                       std::string("instance '") + std::string(inst.name) + "' connects an unsupported port kind");
      return;
    }
    const auto& port   = conn->port.as<slang::ast::PortSymbol>();
    const auto* expr   = conn->getExpression();
    const bool  is_out = port.direction == slang::ast::ArgumentDirection::Out;
    if (port.direction == slang::ast::ArgumentDirection::InOut || port.direction == slang::ast::ArgumentDirection::Ref) {
      emit_unsupported(inst.location, "unsupported-inout-port",
                       std::string("instance '") + std::string(inst.name) + "' connects inout port '" + std::string(port.name)
                           + "'");
      return;
    }
    if (is_out) {
      ++n_outputs_total;
    }

    if (expr == nullptr) {  // unconnected
      if (!is_out) {
        auto ti = flat_or_tinfo(port.getType());
        // unconnected input reads x
        std::string qmarks(static_cast<size_t>(ti.bits), '?');
        in_args.emplace_back(std::string(port.name), absl::StrCat("0ub", qmarks));
      }
      continue;
    }

    if (is_out) {
      // slang wraps output connections as `<expr> = EmptyArgument`.
      if (const auto* assign = expr->as_if<slang::ast::AssignmentExpression>()) {
        expr = &assign->left();
      }
      outs.push_back({&port, expr});
    } else {
      set_pending_loc(expr->sourceRange);
      auto v  = to_int_value(lower_rvalue(*expr));
      auto pi = flat_or_tinfo(port.getType());
      auto ei = flat_or_tinfo(*expr->type);
      v       = materialize_conversion(v, ei.bits, ei.is_signed, pi.bits, pi.is_signed);
      clear_pending_loc();
      in_args.emplace_back(std::string(port.name), v);
    }
  }

  auto& ln = *builder_.lnast;
  set_pending_loc(inst.location);
  auto fcall_idx = builder_.add_child(Lnast_ntype::create_func_call());
  auto result    = builder_.create_lnast_tmp();
  ln.add_child(fcall_idx, Lnast_node::create_ref(result));
  ln.add_child(fcall_idx, Lnast_node::create_ref(callee));
  for (const auto& [pname, v] : in_args) {
    auto arg = ln.add_child(fcall_idx, Lnast_ntype::create_store());
    ln.add_child(arg, Lnast_node::create_ref(pname));
    builder_.add_value_child_pub(arg, v);
  }

  // Bind outputs: a single-output callee's result is the value itself;
  // multi-output callees yield a tuple read by field name.
  const bool single_out = n_outputs_total == 1;
  for (const auto& oc : outs) {
    std::string v;
    if (single_out) {
      v = result;
    } else {
      auto tg = builder_.add_child(Lnast_ntype::create_tuple_get());
      auto t  = builder_.create_lnast_tmp();
      ln.add_child(tg, Lnast_node::create_ref(t));
      ln.add_child(tg, Lnast_node::create_ref(result));
      ln.add_child(tg, Lnast_node::create_const(oc.port->name));
      v = t;
    }
    auto pi = flat_or_tinfo(oc.port->getType());
    auto ei = flat_or_tinfo(*oc.expr->type);
    assign_to(*oc.expr, materialize_conversion(v, pi.bits, pi.is_signed, ei.bits, ei.is_signed));
  }
  clear_pending_loc();
}
