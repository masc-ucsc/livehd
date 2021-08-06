
#include "slang_tree.hpp"

#include <charconv>

#include "mmap_str.hpp"
#include "fmt/format.h"
#include "iassert.hpp"

#include "slang/compilation/Compilation.h"
#include "slang/compilation/Definition.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/CompilationUnitSymbols.h"
#include "slang/symbols/InstanceSymbols.h"
#include "slang/symbols/PortSymbols.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/types/Type.h"

Slang_tree::Slang_tree() {}

void Slang_tree::setup() { parsed_lnasts.clear(); }

mmap_lib::str Slang_tree::create_lnast_tmp() {
  return mmap_lib::str::concat("___", ++tmp_var_cnt);
}

mmap_lib::str Slang_tree::create_lnast_var(mmap_lib::str val) {

  const auto &it = net2attr.find(val);
  if (it == net2attr.end()) {  // OOPS, use before assignment
    auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_attr_get());
    auto tmp_var = create_lnast_tmp();
    lnast->add_child(idx_dot, Lnast_node::create_ref(tmp_var));
    lnast->add_child(idx_dot, Lnast_node::create_ref(val));
    lnast->add_child(idx_dot, Lnast_node::create_const("__last_value"));

    return tmp_var;
  }

  if (it->second == Net_attr::Input) {
    return val.prepend('$');
  } else if (it->second == Net_attr::Output) {
    return val.prepend('%');
  } else if (it->second == Net_attr::Register) {
    return val.prepend('#');
  }

  return val;
}

mmap_lib::str Slang_tree::create_lnast_lhs_var(mmap_lib::str val) {
  const auto &it = net2attr.find(val);
  if (it == net2attr.end()) {
    return val;
  }

  if (it->second == Net_attr::Input) {
    return val.prepend('$');
  } else if (it->second == Net_attr::Output) {
    return val.prepend('%');
  } else if (it->second == Net_attr::Register) {
    return val.prepend('#');
  }

  return val;
}

void Slang_tree::new_lnast(mmap_lib::str name) {
  lnast = std::make_unique<Lnast>(name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  auto node_stmts = Lnast_node::create_stmts();
  idx_stmts       = lnast->add_child(mmap_lib::Tree_index::root(), node_stmts);

  net2attr.clear();

  tmp_var_cnt = 0;
}

void Slang_tree::process_root(const slang::RootSymbol &root) {
  auto topInstances = root.topInstances;
  for (auto inst : topInstances) {
    fmt::print("slang_tree top:{}\n", inst->name);

    Slang_tree tree;
    I(!has_lnast(inst->name));  // top level should not be already (may sub instances)
    auto ok = tree.process_top_instance(*inst);
    if (!ok) {
      Pass::info("unable to process top module:{}\n", inst->name);
    }
  }
}

std::vector<std::shared_ptr<Lnast>> Slang_tree::pick_lnast() {
  std::vector<std::shared_ptr<Lnast>> v;

  for (auto &l : parsed_lnasts) {
    if (l.second)  // do not push null ptr
      v.emplace_back(l.second);
  }

  parsed_lnasts.clear();

  return v;
}

bool Slang_tree::process_top_instance(const slang::InstanceSymbol &symbol) {
  const auto &def = symbol.getDefinition();

  // Instance bodies are all the same, so if we've visited this one
  // already don't bother doing it again.
  if (parsed_lnasts.contains(def.name)) {
    fmt::print("slang_tree module:{} already parsed\n", def.name);
    return false;
  }

  I(symbol.isModule());  // modules are fine. Interfaces are a TODO

  I(lnast == nullptr);

  parsed_lnasts.emplace(def.name, nullptr);  // insert to avoid recursion (reinsert at the end)

  new_lnast(def.name);

  symbol.resolvePortConnections();
  auto decl_pos = 0u;
  for (const auto &p : symbol.body.getPortList()) {
    if (p->kind == slang::SymbolKind::Port) {
      const auto &port = p->as<slang::PortSymbol>();

      I(port.defaultValue == nullptr);  // give me a case to DEBUG

      mmap_lib::str var_name;
      if (port.direction == slang::ArgumentDirection::In) {
        var_name = mmap_lib::str::concat("$.:", decl_pos, ":", port.name);
        net2attr.emplace(port.name, Net_attr::Input);
      } else {
        var_name = mmap_lib::str::concat("%.:", decl_pos, ":", port.name);
        net2attr.emplace(port.name, Net_attr::Output);
      }

      create_declare_bits_stmts(var_name, port.getType().isSigned(), port.getType().getBitWidth());
      ++decl_pos;

    } else if (p->kind == slang::SymbolKind::InterfacePort) {
      const auto &port = p->as<slang::InterfacePortSymbol>();
      (void)port;

      fmt::print("port:{} FIXME\n", p->name);
    } else {
      I(false);  // What other type?
    }
  }

  for (const auto &member : symbol.body.members()) {
    if (member.kind == slang::SymbolKind::Port) {
      // already done
    } else if (member.kind == slang::SymbolKind::Net) {
      const auto &ns   = member.as<slang::NetSymbol>();
      auto       *expr = ns.getInitializer();
      if (expr) {
        mmap_lib::str lhs_var = create_lnast_var(member.name);

#if 0
        // ALso correct, more checks but is it needed? Not if all the bit ops are correctly tracked
				auto it = net2attr.find(member.name);
				if (it == net2attr.end()) {
					net2attr.emplace(member.name, Net_attr::Local);
					create_declare_bits_stmts(lhs_var, ns.getType().isSigned(), ns.getType().getBitWidth());
				}
				create_assign_stmts(lhs_var, process_expression(*expr));
#else
        create_assign_stmts(lhs_var, process_expression(*expr));
#endif
      }
    } else if (member.kind == slang::SymbolKind::ContinuousAssign) {
      const auto &ca = member.as<slang::ContinuousAssignSymbol>();
      const auto &as = ca.getAssignment();
      bool        ok = process(as.as<slang::AssignmentExpression>());
      if (!ok) {
        lnast = nullptr;
        return false;
      }
    } else if (member.kind == slang::SymbolKind::ProceduralBlock) {
      const auto &pbs = member.as<slang::ProceduralBlockSymbol>();

      if (pbs.procedureKind == slang::ProceduralBlockKind::Always) {
        const auto &stmt = pbs.getBody();

        if (stmt.kind == slang::StatementKind::Timed) {
          Pass::info("always with sensitivity list at {} pos:{}, assuming always_comb",
                     member.location.buffer().getId(),
                     member.location.offset());
          const auto &timed = stmt.as<slang::TimedStatement>();
          if (timed.stmt.kind == slang::StatementKind::Block) {
            const auto &block = timed.stmt.as<slang::BlockStatement>();
            I(block.getStatements().kind == slang::StatementKind::List);
            for (const auto &bstmt : block.getStatements().as<slang::StatementList>().list) {
              if (bstmt->kind == slang::StatementKind::ExpressionStatement) {
                const auto &expr = bstmt->as<slang::ExpressionStatement>().expr;
                bool        ok   = process(expr.as<slang::AssignmentExpression>());
                if (!ok) {
                  lnast = nullptr;
                  return false;
                }
              } else {
                fmt::print("TODO: handle kind {}\n", bstmt->kind);
              }
            }
          }

        } else {
          fmt::print("FIXME: missing sensitivity type\n");
        }
      }
    } else {
      fmt::print("FIXME: missing body type\n");
    }
  }

#ifndef NDEBUG
  lnast->dump();
#endif

  parsed_lnasts.insert_or_assign(def.name, lnast);
  lnast = nullptr;

  return true;
}

bool Slang_tree::process(const slang::AssignmentExpression &expr) {
  const auto &lhs = expr.left();

  mmap_lib::str var_name;
  bool             dest_var_sign;
  int              dest_var_bits;

  mmap_lib::str dest_max_bit;
  mmap_lib::str dest_min_bit;

  if (lhs.kind == slang::ExpressionKind::NamedValue) {
    const auto &var = lhs.as<slang::NamedValueExpression>();
    var_name        = create_lnast_lhs_var(var.symbol.name);
    dest_var_sign   = var.type->isSigned();
    dest_var_bits   = var.type->getBitWidth();
    I(!var.type->isStruct());  // FIXME: structs
  } else if (lhs.kind == slang::ExpressionKind::ElementSelect) {
    const auto &es = lhs.as<slang::ElementSelectExpression>();
    I(es.value().kind == slang::ExpressionKind::NamedValue);

    const auto &var = es.value().as<slang::NamedValueExpression>();
    var_name        = create_lnast_lhs_var(var.symbol.name);

    dest_max_bit = process_expression(es.selector());
    dest_min_bit = dest_max_bit;

    dest_var_sign = false;
    dest_var_bits = 1;
  } else {
    I(lhs.kind == slang::ExpressionKind::RangeSelect);
    const auto &rs = lhs.as<slang::RangeSelectExpression>();
    I(rs.value().kind == slang::ExpressionKind::NamedValue);

    const auto &var = rs.value().as<slang::NamedValueExpression>();
    var_name        = create_lnast_lhs_var(var.symbol.name);
    dest_var_sign   = var.type->isSigned();
    dest_var_bits   = var.type->getBitWidth();
    I(!var.type->isStruct());  // FIXME: structs

    dest_max_bit = process_expression(rs.left());
    dest_min_bit = process_expression(rs.right());
  }

  auto it = net2attr.find(var_name);
  if (it == net2attr.end()) {
    net2attr.emplace(var_name, Net_attr::Local);
    create_declare_bits_stmts(var_name, dest_var_sign, dest_var_bits);
  }

  mmap_lib::str rhs_var;
  if (dest_min_bit.empty() && dest_max_bit.empty()) {
    rhs_var = process_expression(expr.right());
  } else {
    auto expr_var = process_expression(expr.right());

    auto mask_h_var   = create_mask_stmts(dest_max_bit);                        // (1<<H)-1
    auto mask_l_var   = create_bit_not_stmts(create_mask_stmts(dest_min_bit));  // ~((1<<L)-1)
    auto mask_var     = create_bit_and_stmts(mask_h_var, mask_l_var);           // ((1<<H)-1) & (~((1<<L)-1))
    auto mask_not_var = create_bit_not_stmts(mask_var);

    auto adj_rhs_var = create_shl_stmts(expr_var, dest_min_bit);
    auto updated_rhs = create_bit_and_stmts(adj_rhs_var, mask_var);
    auto rest_rhs    = create_bit_and_stmts(var_name, mask_not_var);

    rhs_var = create_bit_or_stmts({updated_rhs, rest_rhs});
  }

  create_assign_stmts(var_name, rhs_var);

  return true;
}

// Return a __tmp for (1<<expr)-1
mmap_lib::str Slang_tree::create_mask_stmts(mmap_lib::str dest_max_bit) {
  if (dest_max_bit.empty())
    return dest_max_bit;

  // some fast precomputed values
  if (dest_max_bit.is_i()) {
    auto value = dest_max_bit.to_i();
    if (value < 63 && value >= 0) {
      uint64_t v = (1ULL << value) - 1;
      return v;
    }
  }

  auto shl_var    = create_shl_stmts("1", dest_max_bit);
  auto mask_h_var = create_minus_stmts(shl_var, "1");

  return mask_h_var;
}

mmap_lib::str Slang_tree::create_bit_not_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_bit_not());
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(var_name[0]))
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));

  return res_var;
}

mmap_lib::str Slang_tree::create_logical_not_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_logical_not());
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(var_name[0]))
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));

  return res_var;
}

mmap_lib::str Slang_tree::create_reduce_or_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;
  auto res_var = create_lnast_tmp();
  auto or_idx  = lnast->add_child(idx_stmts, Lnast_node::create_reduce_or());
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(var_name[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(var_name));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(var_name));

  return res_var;
}

mmap_lib::str Slang_tree::create_reduce_xor_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;
  auto res_var = create_lnast_tmp();
  auto xor_idx = lnast->add_child(idx_stmts, Lnast_node::create_reduce_xor());
  lnast->add_child(xor_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(var_name[0]))
    lnast->add_child(xor_idx, Lnast_node::create_const(var_name));
  else
    lnast->add_child(xor_idx, Lnast_node::create_ref(var_name));

  return res_var;
}

mmap_lib::str Slang_tree::create_sra_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_sra());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_pick_bit_stmts(mmap_lib::str a_var, mmap_lib::str pos) {
  auto v = create_sra_stmts(a_var, pos);

  return create_bit_and_stmts(v, 1);
}

mmap_lib::str Slang_tree::create_sext_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_sext());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_bit_and_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto and_idx = lnast->add_child(idx_stmts, Lnast_node::create_bit_and());
  lnast->add_child(and_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(and_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(and_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_bit_or_stmts(const std::vector<mmap_lib::str> &var) {
  mmap_lib::str res_var;
  Lnast_nid        lid;

  for (auto v : var) {
    if (v.empty())
      continue;

    if (res_var.empty()) {
      res_var = create_lnast_tmp();
      lid     = lnast->add_child(idx_stmts, Lnast_node::create_bit_or());
      lnast->add_child(lid, Lnast_node::create_ref(res_var));
    }

    if (std::isdigit(v[0]))
      lnast->add_child(lid, Lnast_node::create_const(v));
    else
      lnast->add_child(lid, Lnast_node::create_ref(v));
  }

  return res_var;
}

mmap_lib::str Slang_tree::create_bit_xor_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto or_idx  = lnast->add_child(idx_stmts, Lnast_node::create_bit_xor());
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(or_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_shl_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return a_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto shl_idx = lnast->add_child(idx_stmts, Lnast_node::create_shl());
  lnast->add_child(shl_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(shl_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(shl_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

void Slang_tree::create_dp_assign_stmts(mmap_lib::str lhs_var, mmap_lib::str rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_dp_assign());
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (std::isdigit(rhs_var[0]))
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
}

void Slang_tree::create_assign_stmts(mmap_lib::str lhs_var, mmap_lib::str rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign());
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (std::isdigit(rhs_var[0]))
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
}

void Slang_tree::create_declare_bits_stmts(mmap_lib::str a_var, bool is_signed, int bits) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_add());
  lnast->add_child(idx_dot, Lnast_node::create_ref(a_var));
  if (is_signed) {
    lnast->add_child(idx_dot, Lnast_node::create_const("__sbits"));
  } else {
    lnast->add_child(idx_dot, Lnast_node::create_const("__ubits"));
  }
  lnast->add_child(idx_dot, Lnast_node::create_const(bits));
}

mmap_lib::str Slang_tree::create_minus_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto sub_idx = lnast->add_child(idx_stmts, Lnast_node::create_minus());
  lnast->add_child(sub_idx, Lnast_node::create_ref(res_var));
  if (a_var.empty()) {
    lnast->add_child(sub_idx, Lnast_node::create_const("0"));
  } else {
    if (std::isdigit(a_var[0]))
      lnast->add_child(sub_idx, Lnast_node::create_const(a_var));
    else
      lnast->add_child(sub_idx, Lnast_node::create_ref(a_var));
  }
  if (std::isdigit(b_var[0]))
    lnast->add_child(sub_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(sub_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_plus_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto add_idx = lnast->add_child(idx_stmts, Lnast_node::create_plus());
  lnast->add_child(add_idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(add_idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(add_idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_mult_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty() || a_var == "1")
    return b_var;
  if (b_var.empty() || b_var == "1")
    return a_var;

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_mult());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_div_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (b_var.empty() || b_var == "1")
    return a_var;

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_div());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));

  if (a_var.empty()) {
    lnast->add_child(idx, Lnast_node::create_const("1"));
  } else {
    if (std::isdigit(a_var[0]))
      lnast->add_child(idx, Lnast_node::create_const(a_var));
    else
      lnast->add_child(idx, Lnast_node::create_ref(a_var));
  }

  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::create_mod_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(a_var.size() && b_var.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_mod());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (std::isdigit(a_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  if (std::isdigit(b_var[0]))
    lnast->add_child(idx, Lnast_node::create_const(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_ref(b_var));

  return res_var;
}

mmap_lib::str Slang_tree::process_expression(const slang::Expression &expr) {
  if (expr.kind == slang::ExpressionKind::NamedValue) {
    const auto &nv = expr.as<slang::NamedValueExpression>();
    return create_lnast_var(nv.symbol.name);
  }

  if (expr.kind == slang::ExpressionKind::IntegerLiteral) {
    const auto                       &il    = expr.as<slang::IntegerLiteral>();
    auto                              svint = il.getValue();
    slang::SmallVectorSized<char, 32> buffer;
    if (!svint.hasUnknown() && svint.getMinRepresentedBits() < 8) {
      svint.writeTo(buffer, slang::LiteralBase::Decimal, false);
      return mmap_lib::str(buffer.data(), buffer.size());
    }

    svint.writeTo(buffer, slang::LiteralBase::Hex, false);
    return mmap_lib::str::concat("0x", mmap_lib::str(buffer.data(), buffer.size()));
  }

  if (expr.kind == slang::ExpressionKind::BinaryOp) {
    const auto &op  = expr.as<slang::BinaryExpression>();
    auto        lhs = process_expression(op.left());
    auto        rhs = process_expression(op.right());

    mmap_lib::str var;
    switch (op.op) {
      case slang::BinaryOperator::Add: var = create_plus_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Subtract: var = create_minus_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Multiply: var = create_mult_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Divide: var = create_div_stmts(lhs, rhs); break;
      case slang::BinaryOperator::Mod: var = create_mod_stmts(lhs, rhs); break;
      case slang::BinaryOperator::BinaryAnd: var = create_bit_and_stmts(lhs, rhs); break;
      case slang::BinaryOperator::BinaryOr: var = create_bit_or_stmts({lhs, rhs}); break;
      case slang::BinaryOperator::BinaryXor: var = create_bit_xor_stmts(lhs, rhs); break;
      case slang::BinaryOperator::BinaryXnor: var = create_bit_not_stmts(create_bit_xor_stmts(lhs, rhs)); break;
      default: {
        fmt::print("FIXME unimplemented binary operator\n");
        var = "fix_binary_op";
      }
    }

    if (op.type->isSigned()) {
      return create_sext_stmts(var, op.type->getBitWidth());
    } else {
      auto mask = create_mask_stmts(op.type->getBitWidth());
      return create_bit_and_stmts(var, mask);
    }
  }

  if (expr.kind == slang::ExpressionKind::UnaryOp) {
    const auto &op = expr.as<slang::UnaryExpression>();
    if (op.op == slang::UnaryOperator::BitwiseAnd)
      return process_reduce_and(op);
    if (op.op == slang::UnaryOperator::BitwiseNand)
      return create_bit_not_stmts(process_reduce_and(op));

    auto lhs = process_expression(op.operand());
    switch (op.op) {
      case slang::UnaryOperator::BitwiseNot: return create_bit_not_stmts(lhs);
      case slang::UnaryOperator::LogicalNot: return create_logical_not_stmts(lhs);
      case slang::UnaryOperator::Plus: return lhs;
      case slang::UnaryOperator::Minus: return create_minus_stmts(0, lhs);
      case slang::UnaryOperator::BitwiseOr: return create_reduce_or_stmts(lhs);
      case slang::UnaryOperator::BitwiseXor: return create_reduce_xor_stmts(lhs);
      // do I use bit not or logical not?
      // Also is it ok for it to be two connected references if we have no lnast node?
      case slang::UnaryOperator::BitwiseNor: return create_bit_not_stmts(create_reduce_or_stmts(lhs));
      case slang::UnaryOperator::BitwiseXnor:
        return create_bit_not_stmts(create_reduce_xor_stmts(lhs));
        // case UnaryOperator::Preincrement:
        // case UnaryOperator::Predecrement:
        // case UnaryOperator::Postincrement:
        // case UnaryOperator::Postdecrement:
      default: {
        fmt::print("FIXME unimplemented unary operator\n");
      }
    }
  }

  if (expr.kind == slang::ExpressionKind::Conversion) {
    const auto        &conv    = expr.as<slang::ConversionExpression>();
    const slang::Type *to_type = conv.type;

    auto res = process_expression(conv.operand());  // NOTHING TO DO? (the dp_assign handles it?)

    const slang::Type *from_type = conv.operand().type;

    if (to_type->isSigned() == from_type->isSigned() && to_type->getBitWidth() >= from_type->getBitWidth())
      return res;  // no need to add mask if expanding

    auto min_bits = std::min(to_type->getBitWidth(), from_type->getBitWidth());

    if (to_type->isSigned())
      return create_sext_stmts(res, min_bits);

    I(!to_type->isSigned());
    // and(and(X,a),b) -> and(X,min(a,b))
    auto mask = create_mask_stmts(to_type->getBitWidth());
    return create_bit_and_stmts(res, mask);
#if 0
    if (to_type->isSigned() && !from_type->isSigned()) {
      if (to_type->getBitWidth()<=from_type->getBitWidth()) {
        // sext(and(X,a),b) && a>b -> sext(X,b)
        return create_sext_stmts(res, create_lnast(min_bits));
      }else{
        // sext(and(X,a),b) && a<b -> and(X,a)
        auto mask = create_mask_stmts(create_lnast(min_bits));
        return create_bit_and_stmts(res, mask);
      }
    }

    I(!to_type->isSigned() && from_type->isSigned());

    auto tmp = create_sext_stmts(res, create_lnast(from_type->getBitWidth()));
    auto mask = create_mask_stmts(create_lnast(to_type->getBitWidth()));
    return create_bit_and_stmts(tmp, mask);
#endif
  }

  if (expr.kind == slang::ExpressionKind::Concatenation) {
    const auto &concat = expr.as<slang::ConcatenationExpression>();

    auto offset = 0u;

    std::vector<mmap_lib::str> adjusted_fields;

    for (const auto &e : concat.operands()) {
      auto bits = e->type->getBitWidth();

      auto res_var = process_expression(*e);

      if (offset) {
        res_var = create_shl_stmts(res_var, offset);
      }
      adjusted_fields.emplace_back(res_var);

      offset += bits;
    }

    return create_bit_or_stmts(adjusted_fields);
  }

  fmt::print("FIXME still unimplemented Expression kind\n");

  return "FIXME_op";
}

mmap_lib::str Slang_tree::process_reduce_and(const slang::UnaryExpression &uexpr) {
  // reduce and does not have a direct mapping in Lgraph
  // And(Not(Ror(Not(inp))), inp.MSB)

  const auto &op      = uexpr.operand();
  auto        msb_pos = op.type->getBitWidth() - 1;

  auto inp = process_expression(op);

  auto tmp = create_bit_not_stmts(create_reduce_or_stmts(create_bit_not_stmts(inp)));
  return create_bit_and_stmts(tmp, create_sra_stmts(inp, msb_pos));  // No need pick (reduce is 1 bit)
}
