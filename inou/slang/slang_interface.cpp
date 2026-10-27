// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "absl/strings/str_cat.h"
#include "slang/ast/types/AllTypes.h"
#include "slang_context.hpp"

using slang::ast::ArgumentDirection;
using slang::ast::SymbolKind;

bool Slang_context::check_passive_interface(const slang::ast::InstanceSymbol& instance) {
  bool ok = true;
  for (const auto& member : instance.body.members()) {
    switch (member.kind) {
      case SymbolKind::Parameter        :
      case SymbolKind::TypeParameter    :
      case SymbolKind::TypeAlias        :
      case SymbolKind::Modport          :
      case SymbolKind::WildcardImport   :
      case SymbolKind::ExplicitImport   :
      case SymbolKind::TransparentMember:
      case SymbolKind::EmptyMember      : break;
      case SymbolKind::Variable         :
      case SymbolKind::Net              : {
        const auto& value = member.as<slang::ast::ValueSymbol>();
        if (value.getType().isIntegral() && !value.getInitializer()) {
          break;
        }
        [[fallthrough]];
      }
      default:
        emit_unsupported(member.location,
                         "unsupported-interface-member",
                         "passive interfaces require uninitialized integral signals; member '" + std::string(member.name)
                             + "' is not supported");
        ok = false;
    }
  }
  return ok;
}

std::vector<Slang_context::Interface_leaf> Slang_context::interface_port_leaves(
    const slang::ast::InterfacePortSymbol& port, slang::ast::InterfacePortSymbol::IfaceConn connection) {
  std::vector<Interface_leaf> leaves;
  const auto [connected, view] = connection;
  if (!connected || (!view && port.modport.empty())) {
    emit_unsupported(port.location,
                     "unsupported-interface-modport",
                     "interface port '" + std::string(port.name) + "' requires an explicitly directed modport");
    return leaves;
  }
  const auto view_name = view ? view->name : port.modport;
  const auto visit     = [&](auto& self, const slang::ast::Symbol& symbol, const std::string& prefix) -> void {
    if (const auto* array = symbol.as_if<slang::ast::InstanceArraySymbol>()) {
      // Slang stores elements in declared order. Formal and actual ranges may
      // differ; positional ordinals preserve the connection's element mapping.
      for (size_t i = 0; i < array->elements.size(); ++i) {
        self(self, *array->elements[i], absl::StrCat(prefix, "e", i, "_"));
      }
      return;
    }
    const auto* instance = symbol.as_if<slang::ast::InstanceSymbol>();
    if (!instance || !instance->isInterface()) {
      emit_unsupported(port.location, "unsupported-interface-connection", "expected a resolved interface instance or array");
      return;
    }
    if (!check_passive_interface(*instance)) {
      return;
    }
    const auto* selected = instance->body.find(view_name);
    const auto* modport  = selected ? selected->as_if<slang::ast::ModportSymbol>() : nullptr;
    if (!modport) {
      emit_unsupported(port.location, "unsupported-interface-modport", "could not resolve the connected modport");
      return;
    }
    for (const auto& member : modport->members()) {
      const auto* mp     = member.as_if<slang::ast::ModportPortSymbol>();
      const auto* signal = mp && mp->internalSymbol ? mp->internalSymbol->as_if<slang::ast::ValueSymbol>() : nullptr;
      if (!signal || mp->explicitConnection
          || (mp->direction != ArgumentDirection::In && mp->direction != ArgumentDirection::Out)) {
        emit_unsupported(member.location,
                         "unsupported-interface-modport",
                         "passive modports support named input/output signals only (no inout, ref, methods or expressions)");
        continue;
      }
      leaves.push_back({signal, absl::StrCat(prefix, mp->name), mp->direction == ArgumentDirection::Out});
    }
  };
  visit(visit, *connected, "");
  return leaves;
}

void Slang_context::prepare_interfaces(const slang::ast::Scope& scope) {
  for (const auto& member : scope.members()) {
    if (const auto* instance = member.as_if<slang::ast::InstanceSymbol>(); instance && instance->isInterface()) {
      if (!check_passive_interface(*instance)) {
        continue;
      }
      for (const auto& field : instance->body.members()) {
        if (field.kind != SymbolKind::Variable && field.kind != SymbolKind::Net) {
          continue;
        }
        const auto& signal = field.as<slang::ast::ValueSymbol>();
        interface_signals_.insert(&signal);
        // Scope-qualified identity also distinguishes generated and arrayed
        // instances. Port aliases already have a directional tuple name.
        if (!sym_lname_.contains(&signal)) {
          sym_lname_[&signal] = ref_name_of_raw(unique_suffixed(field.getHierarchicalPath(), "__if"));
        }
      }
    } else if (const auto* array = member.as_if<slang::ast::InstanceArraySymbol>()) {
      prepare_interfaces(*array);
    } else if (const auto* block = member.as_if<slang::ast::GenerateBlockSymbol>(); block && !block->isUninstantiated) {
      prepare_interfaces(*block);
    } else if (const auto* blocks = member.as_if<slang::ast::GenerateBlockArraySymbol>()) {
      for (const auto* entry : blocks->entries) {
        if (!entry->isUninstantiated) {
          prepare_interfaces(*entry);
        }
      }
    }
  }
}

void Slang_context::emit_interface_io(const slang::ast::InterfacePortSymbol& port, const Lnast_nid& inputs,
                                      const Lnast_nid& outputs) {
  const auto leaves = interface_port_leaves(port, port.getConnection());
  auto&      ln     = *builder_.lnast;
  for (const bool output : {false, true}) {
    if (std::none_of(leaves.begin(), leaves.end(), [&](const auto& leaf) { return leaf.output == output; })) {
      continue;
    }
    // Reserve the direction bundle so a user scalar port cannot collide with
    // the expansion. Its fields are ordinary typed tuple members.
    const std::string bundle = std::string(port.name) + (output ? "__out" : "__in");
    if (body_->findPort(bundle)) {
      emit_unsupported(port.location, "interface-port-name-collision", "interface expansion conflicts with port '" + bundle + "'");
      continue;
    }
    auto entry = ln.add_child(output ? outputs : inputs, Lnast_ntype::create_store());
    ln.add_child(entry, Lnast_node::create_ref(bundle));
    ln.add_child(entry, Lnast_node::create_const("nil"));
    auto tuple = ln.add_child(entry, Lnast_ntype::create_tuple_add());
    for (const auto& leaf : leaves) {
      if (leaf.output != output) {
        continue;
      }
      const auto* signal = leaf.signal;
      interface_signals_.insert(signal);
      const auto ti    = tinfo(signal->getType());
      auto       field = ln.add_child(tuple, Lnast_ntype::create_store());
      ln.add_child(field, Lnast_node::create_ref(leaf.field));
      ln.add_child(field, Lnast_node::create_const("nil"));
      emit_prim_type_int(field, ti.bits, ti.is_signed);
      const auto path = absl::StrCat(bundle, ".", leaf.field);
      if (output) {
        sym_lname_[signal] = ref_name_of_raw(unique_suffixed(absl::StrCat(port.name, "_", leaf.field), "__if"));
        interface_outputs_.emplace_back(signal, path);
      } else {
        sym_lname_[signal] = path;
        declared_.insert(signal);
        input_syms_.insert(signal);
      }
    }
    if (output) {
      auto stages = ln.add_child(entry, Lnast_ntype::create_stages());
      ln.add_child(stages, Lnast_node::create_const("nil"));
      ln.add_child(stages, Lnast_node::create_const("nil"));
    }
  }
}

void Slang_context::bind_interface_outputs() {
  for (const auto& [signal, path] : interface_outputs_) {
    emit_leaf_store(path, read_symbol(*signal, slang::SourceRange(signal->location, signal->location)));
  }
}
