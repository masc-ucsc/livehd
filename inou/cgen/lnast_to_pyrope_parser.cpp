
#include "lnast_to_pyrope_parser.hpp"

std::string Lnast_to_pyrope_parser::stringify() {
  fmt::print("\nstart Lnast_to_pyrope_parser::stringify\n");

  for (const mmap_lib::Tree_index &it: lnast->depth_preorder(lnast->get_root())) {
    process_node(it);
  }
  flush_statements();

  buffer = absl::StrCat("\n\n", buffer);
  return buffer;
}

void Lnast_to_pyrope_parser::process_node(const mmap_lib::Tree_index& it) {
  const auto& node_data = lnast->get_data(it);

  // add while to see pop_statement and to add buffer
  if (it.level < curr_statement_level) {
    pop_statement();

    while (it.level + 1 < curr_statement_level) {
      pop_statement();
    }
  }

  if (node_data.type.is_top()) {
    process_top(it.level);
  } else if (node_data.type.is_statements()) {
    // check the buffer to see if this is an if statement
    // and if it is, check if this is an ifel or an else
    add_to_buffer(node_data);
    push_statement(it.level, node_data.type);
  } else if (node_data.type.is_cstatements()) {
    add_to_buffer(node_data);
    push_statement(it.level, node_data.type);
  } else if (it.level == curr_statement_level) {
    fmt::print("standard process_buffer\n");
    process_buffer();
    add_to_buffer(node_data);
  } else {
    add_to_buffer(node_data);
  }

}

void Lnast_to_pyrope_parser::process_top(mmap_lib::Tree_level level) {
  level_stack.push_back(level);
  curr_statement_level = level;
}

void Lnast_to_pyrope_parser::push_statement(mmap_lib::Tree_level level, Lnast_ntype type) {
  fmt::print("push\n");

  level = level + 1;
  level_stack.push_back(curr_statement_level);
  prev_statement_level = curr_statement_level;
  curr_statement_level = level;

  buffer_stack.push_back(node_buffer);
  node_buffer.clear();

  if (type.is_statements()) {
    sts_buffer_stack.push_back(buffer);
    buffer = "";
    inc_indent_buffer();
  }

  fmt::print("after push\n");
}

void Lnast_to_pyrope_parser::pop_statement() {
  fmt::print("pop\n");

  process_buffer();

  node_buffer = buffer_stack.back();
  buffer_stack.pop_back();

  if (node_buffer.back().type.is_statements()) {
    fmt::print("pop with a statements\n");
    sts_buffer_queue.push_back(buffer);
    buffer = sts_buffer_stack.back();
    sts_buffer_stack.pop_back();
    dec_indent_buffer();
  }

  level_stack.pop_back();
  curr_statement_level = prev_statement_level;
  prev_statement_level = level_stack.back();
}

void Lnast_to_pyrope_parser::flush_statements() {
  fmt::print("starting to flush statements\n");

  while (buffer_stack.size() > 0) {
    process_buffer();

    node_buffer = buffer_stack.back();
    buffer_stack.pop_back();

    if (node_buffer.back().type.is_statements() && buffer_stack.size() > 0) {
      sts_buffer_queue.push_back(buffer);
      buffer = sts_buffer_stack.back();
      sts_buffer_stack.pop_back();
      dec_indent_buffer();
    }

    level_stack.pop_back();
    curr_statement_level = prev_statement_level;
    prev_statement_level = level_stack.back();
  }

  fmt::print("ending flushing statements\n");
}

void Lnast_to_pyrope_parser::add_to_buffer(Lnast_node node) {
  node_buffer.push_back(node);
}

void Lnast_to_pyrope_parser::process_buffer() {
  if (!node_buffer.size()) return;

  const auto type = node_buffer.front().type;

  if (type.is_invalid()) {
    // add error
  } else if (type.is_pure_assign()) {
    process_assign("as");
  } else if (type.is_dp_assign()) {
    process_assign("=");
  } else if (type.is_as()) {
    process_operator();
    // process_as();
  } else if (type.is_label()) {
    process_label();
  } else if (type.is_dot()) {
    process_label();
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
    // add error that tuple is not supported
  } else if (type.is_ref()) {
    // is added to buffer
  } else if (type.is_const()) {
    // is added to buffer
  } else if (type.is_attr()) {
    // is added to buffer
  } else if (type.is_assert()) {
    // check in for node configuration
  } else if (type.is_if()) {
    process_if();
  } else if (type.is_cond()) {
    // is added to buffer
  } else if (type.is_for()) {
    // not implemented in lnast
  } else if (type.is_while()) {
    // not implemented in lnast
  } else if (type.is_func_call()) {
    process_func_call();
  } else if (type.is_func_def()) {
    process_func_def();
  } else if (type.is_top()) {
    // not added to any buffer
  }

  for (auto const& node : node_buffer) {
    auto name{node.token.get_text(memblock)};
    if (name.empty()) {
      fmt::print("{} ", node.type.debug_name_pyrope());
    } else {
      fmt::print("{} ", name);
    }
  }
  fmt::print("\n");

  buffer = absl::StrCat(buffer, node_str_buffer);
  node_str_buffer = "";

  node_buffer.clear();
}

std::string_view Lnast_to_pyrope_parser::get_node_name(Lnast_node node) {
  return node.token.get_text(memblock);
}

void Lnast_to_pyrope_parser::flush_it(std::vector<Lnast_node>::iterator it) {
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

std::string_view Lnast_to_pyrope_parser::join_it(std::vector<Lnast_node>::iterator it, std::string del) {
  std::string value = "";
  while (it != node_buffer.end()) {
    const auto type = (*it).type;
    if (type.is_statements() || type.is_cstatements()) {
      it++;
      continue;
    }

    value = absl::StrCat(value, get_node_name(*it));
    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, " ", del, " ");
    }
  }
  fmt::print("join_it: {}\n", value);
  return value;
}

bool Lnast_to_pyrope_parser::is_number(std::string_view test_string) {
  if (test_string.find("0d") == 0) {
    return true;
  } else if (test_string.find("0b") == 0) {
    return true;
  } else if (test_string.find("0x") == 0) {
    return true;
  }
  return false;
}

std::string_view Lnast_to_pyrope_parser::process_number(std::string_view num_string) {
  if (num_string.find("0d") == 0) {
    return num_string.substr(2);
  }
  return num_string;
}

bool Lnast_to_pyrope_parser::is_ref(std::string_view test_string) {
  /*
  std::regex e("___[a-zA-Z]");
  return std::regex_match(test_string, e);
  */
  // TODO(joapena): check for better way to compare string
  return test_string.find("___") == 0;
}

void Lnast_to_pyrope_parser::inc_indent_buffer(){
  // indent_buffer() = absl::StrCat(indent_buffer(), "  ");
  indent_buffer_size++;
}

void Lnast_to_pyrope_parser::dec_indent_buffer() {
  // indent_buffer() = indent_buffer().substr(0, indent_buffer().size() - 2);
  indent_buffer_size--;
}

std::string Lnast_to_pyrope_parser::indent_buffer() {
  return std::string(indent_buffer_size * 2, ' ');
}

void Lnast_to_pyrope_parser::process_assign(std::string_view assign_type) {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  std::map<std::string_view, std::string>::iterator map_it;
  std::string_view ref = get_node_name(*it);
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
    fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
  } else if (is_number(ref)) {
    ref = process_number(ref);
  }
  value = absl::StrCat(value, ref);

  fmt::print("pure_assign value:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), key, " ", assign_type, " ", value, "\n");
  }
}

/*
void Lnast_to_pyrope_parser::process_as() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_as:\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  std::map<std::string_view, std::string>::iterator map_it;
  std::string_view ref = get_node_name(*it);
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  value = absl::StrCat(value, ref);

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }

  fmt::print("process_as value:\tkey: {}\tvalue: {}\n", key, value);

  node_str_buffer = absl::StrCat(node_str_buffer, key, " as " , value, "\n");
}
*/

void Lnast_to_pyrope_parser::process_label() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  const auto access_type = it->type;
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  std::string_view ref = get_node_name(*it);
  std::map<std::string_view, std::string>::iterator map_it;
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  it++;
  value = absl::StrCat(value, ref, access_type.debug_name_pyrope(), process_number(get_node_name(*it)));

  fmt::print("process_label value:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    node_str_buffer = absl::StrCat(node_str_buffer, key, " saved as ", value, "\n");
  }
  // TODO(joapena): check how labels are formated
  /*
  if (get_node_name(*it) == "__bits") {
    it++;

    std::string_view value = get_node_name(*it);
    // std::smatch m;
    // std::regex_search(value, m, std::regex("b|o|d|h"));
    value = process_number(value);

    node_str_buffer = absl::StrCat(node_str_buffer, "process_label:\t", key, " saved as ", value, "\n");
  }
  */
}

void Lnast_to_pyrope_parser::process_operator() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  const auto op_type = it->type;
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      if (std::count(map_it->second.begin(), map_it->second.end(), ' ')) {
        ref = absl::StrCat("(", map_it->second, ")");
      } else {
        ref = map_it->second;
      }
      fmt::print("map_it find: {} | {}\n", map_it->first, ref);
    } else if (is_number(ref)) {
      ref = process_number(ref);
    }
    // check if a number

    value = absl::StrCat(value, ref);
    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, " ", op_type.debug_name_pyrope(), " ");
    }
  }

  fmt::print("process_{} value:\tkey: {}\tvalue: {}\n", op_type.debug_name_pyrope(), key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), key, " ", op_type.debug_name_pyrope(),"  ", value, "\n");
  }
}

void Lnast_to_pyrope_parser::process_if() {
  fmt::print("start process_if\n");

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++; // if
  it++; // csts
  std::string_view ref = get_node_name(*it);
  std::map<std::string_view, std::string>::iterator map_it;
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
    fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
  }
  node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), "if(", ref, ") {\n");
  it++; // cond
  node_str_buffer = absl::StrCat(node_str_buffer, sts_buffer_queue.front(), indent_buffer(), "}");
  sts_buffer_queue.erase(sts_buffer_queue.begin());
  it++; // sts

  while (it != node_buffer.end()) {
    // this is the elif
    if ((*it).type.is_cstatements()) {
      it++; // csts
      ref = get_node_name(*it);
      map_it = ref_map.find(ref);
      if (map_it != ref_map.end()) {
        ref = map_it->second;
        fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
      }
      node_str_buffer = absl::StrCat(node_str_buffer, " elif (", ref, ") {\n");
      it++; // cond
      node_str_buffer = absl::StrCat(node_str_buffer, sts_buffer_queue.front(), indent_buffer(), "}");
      sts_buffer_queue.erase(sts_buffer_queue.begin());
      it++; // sts
    }

    // this is the else
    else {
      node_str_buffer = absl::StrCat(node_str_buffer, " else {\n", sts_buffer_queue.front(), indent_buffer(), "}\n");
      sts_buffer_queue.erase(sts_buffer_queue.begin());
      it++; // sts
    }
  }
}

void Lnast_to_pyrope_parser::process_func_call() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++; // func_def
  std::string_view key = get_node_name(*it);
  it++; // sts

  std::string value = "";
  value = absl::StrCat(value, get_node_name(*it), "(");
  it++; // ref
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    }

    value = absl::StrCat(value, ref);
    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, ", ");
    } else {
      value = absl::StrCat(value, ")");
    }
  }

  fmt::print("process_func_call: value:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    node_str_buffer = absl::StrCat(node_str_buffer, value, "\n");
  }
}

void Lnast_to_pyrope_parser::process_func_def() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++; // func_def
  node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), get_node_name(*it), " = :(");
  it++; // ref
  while (!(*it).type.is_statements()) {
    node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*it));

    if ((*++it).type.is_statements()) {
      node_str_buffer = absl::StrCat(node_str_buffer, "):{\n");
    } else {
      node_str_buffer = absl::StrCat(node_str_buffer, ",");
    }
  }
  node_str_buffer = absl::StrCat(node_str_buffer, sts_buffer_queue.front(), indent_buffer(), "}\n");
  sts_buffer_queue.erase(sts_buffer_queue.begin());
}

