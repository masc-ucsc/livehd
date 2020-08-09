
#include "semantic_check.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

#include "fmt/color.h"
#include "fmt/core.h"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "lbench.hpp"
#include "lnast_ntype.hpp"
#include "pass.hpp"
#include "prp_lnast.hpp"

bool Semantic_check::is_primitive_op(const Lnast_ntype node_type) {
  if (node_type.is_logical_op() || node_type.is_unary_op() || node_type.is_nary_op() || node_type.is_assign()
      || node_type.is_dp_assign() || node_type.is_as() || node_type.is_eq() || node_type.is_select() || node_type.is_bit_select()
      || node_type.is_logic_shift_right() || node_type.is_arith_shift_right() || node_type.is_arith_shift_left()
      || node_type.is_rotate_shift_right() || node_type.is_rotate_shift_left() || node_type.is_dynamic_shift_left()
      || node_type.is_dynamic_shift_right() || node_type.is_dot() || node_type.is_tuple() || node_type.is_tuple_concat()) {
    return true;
  } else {
    return false;
  }
}

bool Semantic_check::is_tree_structs(const Lnast_ntype node_type) {
  if (node_type.is_stmts() || node_type.is_cstmts() || node_type.is_if() || node_type.is_cond() || node_type.is_uif()
      || node_type.is_for() || node_type.is_while() || node_type.is_func_call() || node_type.is_func_def()) {
    return true;
  } else {
    return false;
  }
}

bool Semantic_check::in_write_list(std::string_view node_name, std::string_view stmt_name) {
  for (auto node : write_dict) {
    if (node.first == node_name && node.second == stmt_name) {
      return true;
    }
  }
  return false;
}

bool Semantic_check::in_read_list(std::string_view node_name, std::string_view stmt_name) {
  for (auto node : read_dict) {
    if (node.first == node_name && node.second == stmt_name) {
      return true;
    }
  }
  return false;
}

bool Semantic_check::in_inefficient_LNAST(std::string_view node_name) {
  for (auto name : inefficient_LNAST) {
    if (name == node_name) {
      return true;
    }
  }
  return false;
}

bool Semantic_check::in_output_vars(std::string_view node_name) {
  for (auto name : output_vars) {
    if (name == node_name) {
      return true;
    }
  }
  return false;
}

void Semantic_check::add_to_write_list(Lnast *lnast, std::string_view node_name, std::string_view stmt_name) {
  if (!in_write_list(node_name, stmt_name)) {
    if (node_name != "null") {
      write_dict[node_name] = stmt_name;
    }
  } else {
    if (node_name[0] == '_' && node_name[1] == '_' && node_name[2] == '_') {
      error_print_lnast_by_name(lnast, node_name);
      Pass::error("Temporary Variable Error: {} should be only written to once\n", node_name);
    }
  }
}

void Semantic_check::add_to_read_list(std::string_view node_name, std::string_view stmt_name) {
  if (!in_read_list(node_name, stmt_name)) {
    read_dict[node_name] = stmt_name;
  }
}

void Semantic_check::add_to_lhs_list(Lnast_nid node_name) {
  lhs_list.push_back(node_name);
}

void Semantic_check::add_to_rhs_list(std::vector<Lnast_nid> node_name) {
  rhs_list.push_back(node_name);
}

void Semantic_check::add_to_output_vars(std::string_view node_name) {
  if (!in_output_vars(node_name)) {
    output_vars.insert(node_name);
  }
}

void Semantic_check::error_print_lnast_by_name(Lnast *lnast, std::string_view error_name) {
  // Print LNAST and indicate error based Node's Text Name
  Prp_lnast converter;
  bool      printed = false;
  fmt::print("\n");

  for (const auto &it : lnast->depth_preorder(lnast->get_root())) {
    auto        node = lnast->get_data(it);
    std::string indent{"  "};
    for (int i = 0; i < it.level; ++i) indent += "  ";

    fmt::print("{} {} {:>20} : {}", it.level, indent, node.type.to_s(), node.token.get_text());

    if (node.token.get_text() == error_name && !printed) {
      fmt::print(fmt::fg(fmt::color::red), "    <==========\n");
      printed = true;
    } else {
      fmt::print("\n");
    }
  }
  fmt::print("\n");
}

void Semantic_check::error_print_lnast_by_type(Lnast *lnast, std::string_view error_name) {
  // Print LNAST and indicate error based Node's Type
  Prp_lnast converter;
  bool      printed = false;
  fmt::print("\n");

  for (const auto &it : lnast->depth_preorder(lnast->get_root())) {
    auto        node = lnast->get_data(it);
    std::string indent{"  "};
    for (int i = 0; i < it.level; ++i) indent += "  ";

    fmt::print("{} {} {:>20} : {}", it.level, indent, node.type.to_s(), node.token.get_text());

    if (node.type.to_s() == error_name && !printed) {
      fmt::print(fmt::fg(fmt::color::red), "    <==========\n");
      printed = true;
    } else {
      fmt::print("\n");
    }
  }
  fmt::print("\n");
}

void Semantic_check::error_print_lnast_var_warn(Lnast *lnast, std::vector<std::string_view> error_names) {
  // Print LNAST and indicate error based a vector of variable warnings
  Prp_lnast converter;
  bool      printed = false;
  fmt::print("\n");

  for (const auto &it : lnast->depth_preorder(lnast->get_root())) {
    auto        node = lnast->get_data(it);
    std::string indent{"  "};
    for (int i = 0; i < it.level; ++i) indent += "  ";

    fmt::print("{} {} {:>20} : {}", it.level, indent, node.type.to_s(), node.token.get_text());

    if (error_names.size() != 0) {
      for (auto node_name = error_names.begin(); node_name != error_names.end(); *node_name++) {
        if (*node_name == node.token.get_text()) {
          fmt::print(fmt::fg(fmt::color::red), "    <==========\n");
          error_names.erase(node_name);
          printed = true;
          break;
        }
      }
      if (!printed) {
        fmt::print("\n");
      }
      printed = false;
    } else {
      fmt::print("\n");
    }
  }
  fmt::print("\n");
}

void Semantic_check::resolve_read_write_lists(Lnast *lnast) {
  // Fixme: May separate arrays just for func_def to make sure catch non-written output variables
  for (auto node_name : write_dict) {
    // Resolve Write and Read Dicts
    if (node_name.first[0] != '%' && read_dict.contains(node_name.first)) {
      write_dict.erase(node_name.first);
    // Make sure that if output variable is declared, it is written to
    } else if (node_name.first[0] == '%' && in_output_vars(node_name.first)) {
      write_dict.erase(node_name.first);
    } else if (node_name.first[0] == '%') {
      write_dict.erase(node_name.first);
    }
  }
  // Variable Warning
  if (write_dict.size() != 0) {
    std::vector<std::string_view> error_names;
    for (auto node_name : write_dict) {
      error_names.push_back(node_name.first);
    }
    error_print_lnast_var_warn(lnast, error_names);
    auto first_entry = write_dict.begin();
    fmt::print(fmt::fg(fmt::color::blue), "Variable Warning");
    fmt::print(": {}", first_entry->first);
    for (auto node_name : write_dict) {
      if (node_name == *first_entry) {
        continue;
      }
      fmt::print(", {}", node_name.first);
    }
    fmt::print(" were written but never read\n");
  }
}

void Semantic_check::resolve_lhs_rhs_lists(Lnast *lnast) {
  // for (auto node : lhs_list) {
  //   fmt::print("{}, ", lnast->get_name(node));
  // }
  // fmt::print("\n");
  // for (auto node : rhs_list) {
  //   for (auto inner : node) {
  //     fmt::print("{}, ", lnast->get_name(inner));
  //   }
  // }
  int lhs_list_size = (int) lhs_list.size();
  for (int i = 0; i < lhs_list_size; i++) {
    // auto lhs_type = lnast->get_data(lhs_list[i]).type;
    auto lhs_name = lnast->get_name(lhs_list[i]);
    if (lhs_name[0] != '_' && lhs_name[1] != '_' && lhs_name[2] != '_' && rhs_list[i].size() == 1 && lhs_name != "null" && lhs_name[0] != '$') {
      auto rhs_type = lnast->get_data(rhs_list[i][0]).type;
      auto rhs_name = lnast->get_name(rhs_list[i][0]);
      if (rhs_name[0] != '_' && rhs_name[1] != '_' && rhs_name[2] != '_' && rhs_type.is_ref()) {
      // if (rhs_name[0] != '_' && rhs_name[1] != '_' && rhs_name[2] != '_' && rhs_name[0] != '$' && rhs_type.is_ref()) {
        inefficient_LNAST.insert(lhs_name);
      }
    }
  }
  if (inefficient_LNAST.size() != 0) {
    auto first = inefficient_LNAST.begin();
    fmt::print(fmt::fg(fmt::color::blue),"\nInefficient LNAST Warning");
    fmt::print(": {}", *first);
    for (auto name : inefficient_LNAST) {
      if (name == *first) {
        continue;
      }
      std::cout << ", " << name;
    }
    std::cout << " may be unnecessary\n";
  }
}

void Semantic_check::check_primitive_ops(Lnast *lnast, const Lnast_nid &lnidx_opr, const Lnast_ntype node_type,
                                         std::string_view stmt_name) {
  if (!lnast->has_single_child(lnidx_opr)) {

    // Vector for add_to_rhs_list()
    std::vector<Lnast_nid> rhs_args;

    // Unary Operations
    if (node_type.is_assign() || node_type.is_dp_assign() || node_type.is_not() || node_type.is_logical_not()
        || node_type.is_as()) {
      auto lhs      = lnast->get_first_child(lnidx_opr);
      auto lhs_type = lnast->get_data(lhs).type;
      auto rhs      = lnast->get_sibling_next(lhs);
      auto rhs_type = lnast->get_data(rhs).type;

      if (!lhs_type.is_ref()) {
        error_print_lnast_by_name(lnast, lnast->get_name(lhs));
        Pass::error("Unary Operation Error: LHS Node must be Node type 'ref'\n");
      }
      if (!rhs_type.is_ref() && !rhs_type.is_const()) {
        error_print_lnast_by_name(lnast, lnast->get_name(rhs));
        Pass::error("Unary Operation Error: RHS Node must be Node type 'ref' or 'const'\n");
      }
      // Store type 'ref' variables
      add_to_write_list(lnast, lnast->get_name(lhs), stmt_name);
      if (rhs_type.is_ref()) {
        add_to_read_list(lnast->get_name(rhs), stmt_name);
      }
      add_to_lhs_list(lhs);
      rhs_args.push_back(rhs);
      add_to_rhs_list(rhs_args);
      // N-ary Operations (need to add node_type.is_select())
    } else if (node_type.is_logical_and() || node_type.is_logical_or() || node_type.is_nary_op() || node_type.is_eq()
               || node_type.is_bit_select() || node_type.is_logic_shift_right() || node_type.is_arith_shift_right()
               || node_type.is_arith_shift_left() || node_type.is_rotate_shift_right() || node_type.is_rotate_shift_left()
               || node_type.is_dynamic_shift_right() || node_type.is_dynamic_shift_left()) {
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (lnidx_opr_child == lnast->get_first_child(lnidx_opr)) {
          if (!node_type_child.is_ref()) {
            error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr_child));
            Pass::error("N-ary Operation Error: LHS Node must be Node type 'ref'\n");
          }
          // Store type 'ref' variables
          add_to_write_list(lnast, lnast->get_name(lnidx_opr_child), stmt_name);
          add_to_lhs_list(lnidx_opr_child);
          continue;
        } else if (!node_type_child.is_ref() && !node_type_child.is_const()) {
          error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr_child));
          Pass::error("N-ary Operation Error!: RHS Node(s) must be Node type 'ref' or 'const'\n");
        }
        // Store type 'ref' variables
        if (node_type_child.is_ref()) {
          rhs_args.push_back(lnidx_opr_child);
          add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
        }
      }
      add_to_rhs_list(rhs_args);
    } else if (node_type.is_tuple()) {
      int num_of_ref    = 0;
      int num_of_assign = 0;
      int num_of_const  = 0;
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (lnast->get_first_child(lnidx_opr) == lnidx_opr_child) {
          num_of_ref += 1;
          // Store type 'ref' variables
          add_to_write_list(lnast, lnast->get_name(lnidx_opr_child), stmt_name);
          continue;
        }
        if (node_type_child.is_ref()) {
          num_of_ref += 1;
          // Store type 'ref' variables
          add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
        } else if (node_type_child.is_const()) {
          num_of_const += 1;
          add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
        } else if (node_type_child.is_assign()) {
          num_of_assign += 1;
          check_primitive_ops(lnast, lnidx_opr_child, node_type_child, stmt_name);
        } else {
          // Invalid Node Type
          error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
          Pass::error("Tuple Operation Error: Child Node(s) must be Node type 'ref' or 'assign'\n");
        }
      }
      // Missing Nodes
      if (num_of_ref == 0) {
        error_print_lnast_by_type(lnast, node_type.to_s());
        Pass::error("Tuple Operation Error: Missing Reference Node\n");
      }
      // } else if (num_of_assign == 0 && num_of_const == 0) {
      //   error_print_lnast_by_type(lnast, node_type.to_s());
      //   Pass::error("Tuple Operation Error: Missing Assign or Const Node(s)\n");
      // }
    } else if (node_type.is_tuple_concat()) {
      int num_of_ref = 0;
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (lnast->get_first_child(lnidx_opr) == lnidx_opr_child) {
          num_of_ref += 1;
          add_to_write_list(lnast, lnast->get_name(lnidx_opr_child), stmt_name);
          continue;
        }
        if (node_type_child.is_ref()) {
          num_of_ref += 1;
          // Store type 'ref' variables
          add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
        } else {
          // Invalid Node Type
          error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
          Pass::error("Tuple Concatenation Operation Error: Child Node(s) must be Node type 'ref'\n");
        }
      }
      // Missing Nodes
      if (num_of_ref != 3) {
        error_print_lnast_by_type(lnast, node_type.to_s());
        Pass::error("Tuple Concatenation Operation Error: Missing Reference Node\n");
      }
    } else if (node_type.is_select() || node_type.is_dot()) {
      int num_of_ref = 0;
      for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
        const auto node_type_child = lnast->get_data(lnidx_opr_child).type;

        if (node_type_child.is_ref()) {
          num_of_ref += 1;
          // Store type 'ref' variables
          add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
        } else {
          // Invalid Node Type
          error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
          Pass::error("Select / Dot Operation Error: Child Node(s) must be Node type 'ref'\n");
        }
      }
      // Missing Nodes
      if (num_of_ref != 3) {
        error_print_lnast_by_type(lnast, node_type.to_s());
        Pass::error("Select Operation Error: Missing Reference Node(s)\n");
      }
    } else {
      error_print_lnast_by_type(lnast, node_type.to_s());
      Pass::error("Primitive Operation Error: Not a Valid Node Type\n");
    }
  } else {
    error_print_lnast_by_type(lnast, node_type.to_s());
    Pass::error("Primitive Operation Error: Requires at least 2 LNAST Nodes (lhs, rhs)\n");
  }
}

void Semantic_check::check_tree_struct_ops(Lnast *lnast, const Lnast_nid &lnidx_opr, const Lnast_ntype node_type,
                                           std::string_view stmt_name) {
  if (node_type.is_if()) {
    check_if_op(lnast, lnidx_opr, stmt_name);
  } else if (node_type.is_for()) {
    check_for_op(lnast, lnidx_opr, stmt_name);
  } else if (node_type.is_while()) {
    check_while_op(lnast, lnidx_opr, stmt_name);
  } else if (node_type.is_func_call()) {
    check_func_call(lnast, lnidx_opr, stmt_name);
  } else if (node_type.is_func_def()) {
    check_func_def(lnast, lnidx_opr, stmt_name);
  } else {
    // Invalid Node Type
    error_print_lnast_by_type(lnast, node_type.to_s());
    Pass::error("Tree Structure Operation Error: Not a Valid Node Type\n");
  }
}

void Semantic_check::check_if_op(Lnast *lnast, const Lnast_nid &lnidx_opr, std::string_view stmt_name) {
  int cstmts_count = 0;
  int cond_count   = 0;
  int stmts_count  = 0;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (ntype_child.is_cstmts() || ntype_child.is_stmts()) {
      std::string_view new_stmt_name = stmt_name;
      if (ntype_child.is_cstmts()) {
        cstmts_count += 1;
      } else {
        stmts_count += 1;
        new_stmt_name = lnast->get_name(lnidx_opr_child);
      }
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;

        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child, new_stmt_name);
        } else if (is_tree_structs(ntype_child_child)) {
          check_tree_struct_ops(lnast, lnidx_opr_child_child, ntype_child_child, new_stmt_name);
        }
      }
    } else if (ntype_child.is_cond()) {
      cond_count += 1;
      add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
    } else {
      // Invalid Node Type
      error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
      Pass::error("If Operation Error: Child Node(s) must be Node type 'cstmts', 'stmts', or 'condition'\n");
    }
  }
  // Missing Nodes
  // auto ntype = lnast->get_data(lnidx_opr).type;
  // if (cstmts_count < cond_count) {
  //   error_print_lnast_by_type(lnast, ntype.to_s());
  //   Pass::error("If Operation Error: Missing Conditional Statments and/or Condition Node\n");
  // } else if (cstmts_count > cond_count) {
  //   error_print_lnast_by_type(lnast, ntype.to_s());
  //   Pass::error("If Operation Error: Missing Condition Node\n");
  // } else if (stmts_count < cstmts_count) {
  //   error_print_lnast_by_type(lnast, ntype.to_s());
  //   Pass::error("If Operation Error: Missing Statements Node\n");
  // }
}

void Semantic_check::check_for_op(Lnast *lnast, const Lnast_nid &lnidx_opr, std::string_view stmt_name) {
  bool stmts      = false;
  int  num_of_ref = 0;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (ntype_child.is_stmts()) {
      stmts           = true;
      // Iterate through statements
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;
        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child, lnast->get_name(lnidx_opr_child));
        } else if (is_tree_structs(ntype_child_child)) {
          check_tree_struct_ops(lnast, lnidx_opr_child_child, ntype_child_child, lnast->get_name(lnidx_opr_child));
        }
      }
    } else if (ntype_child.is_ref()) {
      num_of_ref += 1;
      // Store type 'ref' variables
      add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
    } else {
      // Invalid Node Type
      error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
      Pass::error("For Operation Error: Child Node(s) must be Node type 'stmts', 'it_name', or 'tup'\n");
    }
  }
  // Missing Nodes
  if (num_of_ref < 2) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("For Operation Error: Missing Reference Node(s)\n");
  } else if (!stmts) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("For Operation Error: Missing Statements Node\n");
  }
}

void Semantic_check::check_while_op(Lnast *lnast, const Lnast_nid &lnidx_opr, std::string_view stmt_name) {
  bool cond = false;
  bool stmt = false;
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    if (ntype_child.is_cond()) {
      cond = true;
      add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
    } else if (ntype_child.is_stmts()) {
      stmt            = true;
      // Iterate through statements
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;
        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child, lnast->get_name(lnidx_opr_child));
        } else if (is_tree_structs(ntype_child_child)) {
          check_tree_struct_ops(lnast, lnidx_opr_child_child, ntype_child_child, lnast->get_name(lnidx_opr_child));
        }
      }
    } else {
      // Invalid Node Type
      error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
      Pass::error("While Operation Error: Child Node(s) must be Node type 'condition' or 'stmts'\n");
    }
  }
  // Missing Nodes
  if (!cond) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("While Operation Error: Missing Condition Node\n");
  } else if (!stmt) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("While Operation Error: Missing Statement Node\n");
  }
}

void Semantic_check::check_func_def(Lnast *lnast, const Lnast_nid &lnidx_opr, std::string_view stmt_name) {
  int  num_of_refs = 0;
  bool cond        = false;
  bool stmts       = false;
  // Iterate through children of func def node
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    // First child is the func_name
    if (lnidx_opr_child == lnast->get_first_child(lnidx_opr)) {
      num_of_refs += 1;
      // Store type 'ref' variables
      add_to_write_list(lnast, lnast->get_name(lnidx_opr_child), stmt_name);
      continue;
    }
    if (ntype_child.is_cstmts() | ntype_child.is_stmts()) {
      std::string_view new_stmt_name = stmt_name;
      if (ntype_child.is_stmts()) {
        stmts         = true;
        new_stmt_name = lnast->get_name(lnidx_opr_child);
      }
      for (const auto &lnidx_opr_child_child : lnast->children(lnidx_opr_child)) {
        const auto ntype_child_child = lnast->get_data(lnidx_opr_child_child).type;
        if (is_primitive_op(ntype_child_child)) {
          check_primitive_ops(lnast, lnidx_opr_child_child, ntype_child_child, new_stmt_name);
        } else if (is_tree_structs(ntype_child_child)) {
          check_tree_struct_ops(lnast, lnidx_opr_child_child, ntype_child_child, new_stmt_name);
        }
      }
    } else if (ntype_child.is_cond()) {
      cond = true;
      add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
      // Inputs and Outputs
    } else if (ntype_child.is_ref()) {
      std::string_view ref_name = lnast->get_name(lnidx_opr_child);
      if (ref_name[0] == '%') {
        add_to_output_vars(ref_name);
      }
      add_to_read_list(ref_name, stmt_name);
      num_of_refs += 1;
    } else {
      // Invalid Node Type
      error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
      Pass::error("Func Def Operation Error: Child Node(s) must be Node type 'ref', 'cstmts', or 'stmts'\n");
    }
  }
  // Missing Nodes
  if (num_of_refs < 1) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("Func Def Operation Error: Missing Reference Node\n");
  } else if (!cond) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("Func Def Operation Error: Missing Condition Node\n");
  } else if (!stmts) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("Func Def Operation Error: Missing Statement Node\n");
  }
  // From resolve_read_write_lists()
  for (auto node_name : write_dict) {
    // Resolve Write and Read Dicts
    if (node_name.first[0] == '%' && in_output_vars(node_name.first)) {
      for (auto output_var = output_vars.begin(); output_var != output_vars.end(); *output_var++) {
        if (*output_var == node_name.first) {
          output_vars.erase(output_var);
          break;
        }
      }
    }
  }
  // Output Variable Warning --> only applies to Node type 'func def'
  if (output_vars.size() != 0) {
    std::vector<std::string_view> error_outputs;
    for (auto node_name : output_vars) {
      error_outputs.push_back(node_name);
    }
    error_print_lnast_var_warn(lnast, error_outputs);
    auto first_entry = output_vars.begin();
    fmt::print(fmt::fg(fmt::color::blue), "Output Variable Warning");
    fmt::print(": {}", *first_entry);
    for (auto node_name : output_vars) {
      if (node_name == *first_entry) {
        continue;
      }
      fmt::print(", {}", node_name);
    }
    fmt::print(" should be written since declared in the function definition\n");
  }
}

void Semantic_check::check_func_call(Lnast *lnast, const Lnast_nid &lnidx_opr, std::string_view stmt_name) {
  int num_of_refs = 0;
  // Iterate through children of func call node
  for (const auto &lnidx_opr_child : lnast->children(lnidx_opr)) {
    const auto ntype_child = lnast->get_data(lnidx_opr_child).type;

    // First child hold the name of the func call
    if (lnidx_opr_child == lnast->get_first_child(lnidx_opr)) {
      num_of_refs += 1;
      add_to_write_list(lnast, lnast->get_name(lnidx_opr_child), stmt_name);
      continue;
    }
    // Nodes are func_name and arguments
    if (ntype_child.is_ref()) {
      num_of_refs += 1;
      add_to_read_list(lnast->get_name(lnidx_opr_child), stmt_name);
    } else if (!ntype_child.is_ref()) {
      error_print_lnast_by_name(lnast, lnast->get_name(lnidx_opr));
      Pass::error("Func Call Operation Error: Child Node(s) must be Node type 'ref'\n");
    }
  }
  // Missing Nodes
  if (num_of_refs != 3) {
    error_print_lnast_by_type(lnast, lnast->get_data(lnidx_opr).type.to_s());
    Pass::error("Func Call Operation Error: Missing Reference Node(s)\n");
  }
}

// NOTE: Test does only consider tuple and tuple concat operations
void Semantic_check::do_check(Lnast *lnast) {
  // Get Lnast Root
  const auto top = lnast->get_root();
  // Get Lnast top statements
  const auto stmts = lnast->get_first_child(top);
  // Iterate through Lnast top statements
  for (const auto &stmt : lnast->children(stmts)) {
    const auto ntype     = lnast->get_data(stmt).type;
    const auto stmt_name = lnast->get_name(stmts);

    if (is_primitive_op(ntype)) {
      check_primitive_ops(lnast, stmt, ntype, stmt_name);
    } else if (is_tree_structs(ntype)) {
      check_tree_struct_ops(lnast, stmt, ntype, stmt_name);
    }
  }
  // fmt::print("Write Dict\n");
  // for (auto name : write_dict) {
  //   fmt::print("{} : {}\n", name.first, name.second);
  // }
  // fmt::print("\n");
  // fmt::print("Read Dict\n");
  // for (auto name : read_dict) {
  //   fmt::print("{} : {}\n", name.first, name.second);
  // }
  // fmt::print("\n");
  // fmt::print("Output Vars\n");
  // for (auto name : output_vars) {
  //   fmt::print("{}\n", name);
  // }
  // Find Errors!
  resolve_lhs_rhs_lists(lnast);
  resolve_read_write_lists(lnast);
  fmt::print("\n");
}