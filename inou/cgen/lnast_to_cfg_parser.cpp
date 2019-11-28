
#include "lnast_to_cfg_parser.hpp"

std::string Lnast_to_cfg_parser::stringify() {
  fmt::print("\nstart Lnast_to_cfg_parser::stringify\n");

  for (const mmap_lib::Tree_index &it: lnast->depth_preorder(lnast->get_root())) {
    process_node(it);
  }
  flush_statements();

  return buffer;
}

void Lnast_to_cfg_parser::process_node(const mmap_lib::Tree_index& it) {
  const auto& node_data = lnast->get_data(it);

  // for printing out individual node values
  /*
  std::string name(node_data.token.get_text(memblock)); // str_view to string
  fmt::print("current node : prev:{}\tcurr:{}\tit: {}\t", prev_statement_level, curr_statement_level, it.level);
  fmt::print("tree index: pos:{}\tlevel:{}\tnode: {}\ttype: {}\tk:{}\n", it.pos, it.level, name, type, node_data.knum);
  */

  // add while to see pop_statement and to add buffer
  if (it.level < curr_statement_level) {
    pop_statement(it.level, node_data.type);

    while (it.level + 1 < curr_statement_level) {
      pop_statement(it.level, node_data.type);
    }
  }

  if (node_data.type.is_top()) {
    process_top(it.level);
  } else if (node_data.type.is_statements()) {
    // check the buffer to see if this is an if statement
    // and if it is, check if this is an ifel or an else
    if (node_buffer.size() > 0) {
      const auto ntype = node_buffer.front().type;
      if (ntype.is_if()) {
        if (node_buffer.size() > 3 && !node_buffer.back().type.is_statements()) {
          if_buffer.push_back(k_next);
          k_next++;
        }
        if_buffer.push_back(k_next);
      } else if (ntype.is_func_def()) {
        if_buffer.push_back(k_next);
      }
    }
    add_to_buffer(node_data);
    push_statement(it.level);
  } else if (node_data.type.is_cstatements()) {
    if (node_buffer.size() > 0) {
      if (node_buffer.back().type.is_statements()) {
        if_buffer.push_back(k_next);
      }
    }
    add_to_buffer(node_data);
    push_statement(it.level);
  } else if (it.level == curr_statement_level) {
    fmt::print("standard process_buffer\n");
    process_buffer();
    k_stack.push_back(k_next);
    k_next++;
    add_to_buffer(node_data);
  } else {
    add_to_buffer(node_data);
  }

}

void Lnast_to_cfg_parser::process_top(mmap_lib::Tree_level level) {
  level_stack.push_back(level);
  curr_statement_level = level;
  k_next = 1;
}

void Lnast_to_cfg_parser::push_statement(mmap_lib::Tree_level level) {
  fmt::print("push\n");

  level = level + 1;
  level_stack.push_back(curr_statement_level);
  prev_statement_level = curr_statement_level;
  curr_statement_level = level;

  buffer_stack.push_back(node_buffer);
  node_buffer.clear();

  if_buffer_stack.push_back(if_buffer);
  if_buffer.clear();

  k_stack.push_back(k_next);

  fmt::print("after push\n");
}

void Lnast_to_cfg_parser::pop_statement(mmap_lib::Tree_level level, Lnast_ntype type) {
  uint32_t tmp_k = k_next;

  if (curr_statement_level != level && !type.is_cond()) {
    k_next = 0;
  }

  fmt::print("used for processing:\tk_next: {}\n", k_next);

  process_buffer();
  if (curr_statement_level != level) {
    k_next = tmp_k;
  }

  node_buffer = buffer_stack.back();
  buffer_stack.pop_back();

  if_buffer = if_buffer_stack.back();
  if_buffer_stack.pop_back();

  level_stack.pop_back();
  curr_statement_level = prev_statement_level;
  prev_statement_level = level_stack.back();

  k_stack.pop_back();
}

void Lnast_to_cfg_parser::flush_statements() {
  fmt::print("starting to flush statements\n");

  while (buffer_stack.size() > 0) {
    uint32_t tmp_k = k_next;

    process_buffer();

    node_buffer = buffer_stack.back();
    buffer_stack.pop_back();

    if_buffer = if_buffer_stack.back();
    if_buffer_stack.pop_back();

    level_stack.pop_back();
    curr_statement_level = prev_statement_level;
    prev_statement_level = level_stack.back();

    k_stack.pop_back();
  }

  fmt::print("ending flushing statements\n");
}

void Lnast_to_cfg_parser::add_to_buffer(Lnast_node node) {
  node_buffer.push_back(node);
}

void Lnast_to_cfg_parser::process_buffer() {
  if (!node_buffer.size()) return;

  fmt::print("process_buffer k_next: {}\n", k_next);

  const auto type = node_buffer.front().type;

  if (type.is_invalid()) {
    // cause an error
  } else if (type.is_statements()) {
    // not processed from buffer
  } else if (type.is_cstatements()) {
    // not processed from buffer
  } else if (type.is_pure_assign()) {
    process_operator();
  } else if (type.is_dp_assign()) {
    process_operator();
  } else if (type.is_as()) {
    process_operator();
  } else if (type.is_label()) {
    process_operator();
  } else if (type.is_dot()) {
    process_operator();
  } else if (type.is_logical_and()) {
    process_operator();
  } else if (type.is_logical_or()) {
    process_operator();
  } else if (type.is_and()) {
    process_operator();
  } else if (type.is_or()) {
    process_operator();
  } else if (type.is_xor()) {
    process_operator();
  } else if (type.is_plus()) {
    process_operator();
  } else if (type.is_minus()) {
    process_operator();
  } else if (type.is_mult()) {
    process_operator();
  } else if (type.is_div()) {
    process_operator();
  } else if (type.is_same()) {
    process_operator();
  } else if (type.is_lt()) {
    process_operator();
  } else if (type.is_le()) {
    process_operator();
  } else if (type.is_gt()) {
    process_operator();
  } else if (type.is_ge()) {
    process_operator();
  } else if (type.is_tuple()) {
    // not implemented in lnast
  } else if (type.is_ref()) {
    // not processed from buffer
  } else if (type.is_const()) {
    // not processed from buffer
  } else if (type.is_attr()) {
    // not processed from buffer
  } else if (type.is_assert()) {
    // check in on node representation
  } else if (type.is_if()) {
    process_if();
  } else if (type.is_cond()) {
    // not processed from buffer
  } else if (type.is_for()) {
    // not implemented in lnast
  } else if (type.is_while()) {
    // not implemented in lnast
  } else if (type.is_func_call()) {
    process_func_call();
  } else if (type.is_func_def()) {
    process_func_def();
  } else if (type.is_top()) {
    // not processed from buffer
  } else {
    // throw error
  }

  for (auto const& node : node_buffer) {
    auto name{node.token.get_text(memblock)};
    if (name.empty()) {
      fmt::print("{} ", node.type.debug_name());
    } else {
      fmt::print("{} ", name);
    }
  }
  fmt::print("\n");

  std::string k_next_str;
  if (k_next == 0) {
    k_next_str = "null";
  } else {
    k_next_str = absl::StrCat("K", k_next);
  }
  buffer = absl::StrCat(buffer, "K", k_stack.back(), "\t", k_next_str, "\t", node_str_buffer);
  k_stack.pop_back();
  node_str_buffer = "";

  if_buffer.clear();
  node_buffer.clear();
}

std::string_view Lnast_to_cfg_parser::get_node_name(Lnast_node node) {
  return node.token.get_text(memblock);
}

void Lnast_to_cfg_parser::flush_it(std::vector<Lnast_node>::iterator it) {
  while (it != node_buffer.end()) {
    const auto type = (*it).type;
    if (type.is_statements() || type.is_cstatements()) {
      it++;
      continue;
    }

    node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*it));
    if (++it != node_buffer.end()) {
      node_str_buffer = absl::StrCat(node_str_buffer, "\t");
    }
  }
}

void Lnast_to_cfg_parser::process_operator() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*it), "\t");
  it++;
  flush_it(it);
  node_str_buffer = absl::StrCat(node_str_buffer, "\n");
}

void Lnast_to_cfg_parser::process_if() {
  fmt::print("start process_if\n");

  std::vector<Lnast_node>::iterator node_it = node_buffer.begin();
  std::vector<uint32_t>::iterator if_it = if_buffer.begin();

  node_str_buffer = absl::StrCat(node_str_buffer, "if\t");
  node_it++; // if
  node_it++; // csts
  node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*node_it), "\t");
  node_str_buffer = absl::StrCat(node_str_buffer, "K", *if_it, "\t");
  if_it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "K", *if_it, "\t");
  if_it++;
  node_it++; // cond
  node_it++; // sts
  node_str_buffer = absl::StrCat(node_str_buffer, "\n");

  while (node_it != node_buffer.end()) {
    if ((*node_it).type.is_cond()) {
      node_str_buffer = absl::StrCat(node_str_buffer, "K", *if_it, "\t");
      if_it++;
      node_str_buffer = absl::StrCat(node_str_buffer, "null", "\tif\t");
      node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*node_it), "\t");
      node_str_buffer = absl::StrCat(node_str_buffer, "K", *if_it, "\t");
      if_it++;
      node_str_buffer = absl::StrCat(node_str_buffer, "K", *if_it, "\t");
      if_it++;
      node_str_buffer = absl::StrCat(node_str_buffer, "\n");
    }
    node_it++;
  }
}

void Lnast_to_cfg_parser::process_func_call() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  node_str_buffer = absl::StrCat(node_str_buffer, ".()\t");
  it++;
  flush_it(it);
  node_str_buffer = absl::StrCat(node_str_buffer, "\n");
}

void Lnast_to_cfg_parser::process_func_def() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  node_str_buffer = absl::StrCat(node_str_buffer, "::{\t");
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*it), "\t");
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "K", if_buffer.front(), "\t");
  flush_it(it);
  node_str_buffer = absl::StrCat(node_str_buffer, "\n");
}

