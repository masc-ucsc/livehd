
#include "lnast_to_pyrope_parser.hpp"

std::string Lnast_to_pyrope_parser::stringify() {
  fmt::print("\nstart Lnast_to_pyrope_parser::stringify\n");

  for (const mmap_lib::Tree_index &it: lnast->depth_preorder(lnast->get_root())) {
    process_node(it);
  }
  process_buffer();

  buffer = absl::StrCat("\n\n", buffer);
  return buffer;
}

void Lnast_to_pyrope_parser::process_node(const mmap_lib::Tree_index& it) {
  const auto& node_data = lnast->get_data(it);
  std::string type = ntype_dbg(node_data.type);

  // add while to see pop_statement and to add buffer
  if (it.level < curr_statement_level) {
    pop_statement();

    while (it.level + 1 < curr_statement_level) {
      pop_statement();
    }
    type = ntype_dbg(node_data.type);
  }

  if (node_data.type == Lnast_ntype_top) {
    process_top(it.level);
  } else if (node_data.type == Lnast_ntype_statements) {
    // check the buffer to see if this is an if statement
    // and if it is, check if this is an ifel or an else
    add_to_buffer(node_data);
    push_statement(it.level, node_data.type);
  } else if (node_data.type == Lnast_ntype_cstatements) {
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

  if (Lnast_ntype_statements == type) {
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

  if (Lnast_ntype_statements == node_buffer.back().type) {
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

void Lnast_to_pyrope_parser::add_to_buffer(Lnast_node node) {
  node_buffer.push_back(node);
}

void Lnast_to_pyrope_parser::process_buffer() {
  if (!node_buffer.size()) return;

  Lnast_ntype type = node_buffer.front().type;

  if (type == Lnast_ntype_pure_assign) {
    // check if should be in combinational or stateful
    // process_operator();
    process_pure_assign();
  } else if (type == Lnast_ntype_as) {
    process_operator();
    // process_as();
  } else if (type == Lnast_ntype_label) {
    process_label();
  } else if (type == Lnast_ntype_and) {
    process_operator();
    // process_and();
  } else if (type == Lnast_ntype_xor) {
    process_operator();
    // process_xor();
  } else if (type == Lnast_ntype_plus) {
    process_operator();
    // process_plus();
  } else if (type == Lnast_ntype_gt) {
    process_operator();
    // process_gt();
  } else if (type == Lnast_ntype_if) {
    process_if();
  } else if (type == Lnast_ntype_func_call) {
    process_func_call();
  } else if (type == Lnast_ntype_func_def) {
    process_func_def();
  }

  for (auto const& node : node_buffer) {
    std::string name(node.token.get_text(memblock)); // str_view to string
    std::string type = ntype_dbg(node.type);
    if (name == "") {
      fmt::print("{}({}) ", type, node.type);
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
    Lnast_ntype type = (*it).type;
    if (type == Lnast_ntype_statements || type == Lnast_ntype_cstatements) {
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
    Lnast_ntype type = (*it).type;
    if (type == Lnast_ntype_statements || type == Lnast_ntype_cstatements) {
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

void Lnast_to_pyrope_parser::process_pure_assign() {
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
    node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), key, " = ", value, "\n");
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
  value = absl::StrCat(value, ref, ":", process_number(get_node_name(*it)));

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
  std::string op_type = ntype_dbg((*it).type);
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
      value = absl::StrCat(value, " ", op_type, " ");
    }
  }

  fmt::print("process_{} value:\tkey: {}\tvalue: {}\n", op_type, key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), key, " ", op_type,"  ", value, "\n");
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
    if ((*it).type == Lnast_ntype_cstatements) {
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
  it++; // sts
  node_str_buffer = absl::StrCat(node_str_buffer, indent_buffer(), get_node_name(*it), " = :(");
  it++; // ref
  while (it != node_buffer.end()) {
    node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*it));

    if (++it != node_buffer.end()) {
      node_str_buffer = absl::StrCat(node_str_buffer, ",");
    } else {
      node_str_buffer = absl::StrCat(node_str_buffer, "):{\n");
    }
  }
  node_str_buffer = absl::StrCat(node_str_buffer, sts_buffer_queue.front(), indent_buffer(), "}\n");
  sts_buffer_queue.erase(sts_buffer_queue.begin());
}

void Lnast_to_pyrope_parser::setup_ntype_str_mapping() {
  ntype2str[Lnast_ntype_invalid] = "invalid";
  ntype2str[Lnast_ntype_statements] = "sts";
  ntype2str[Lnast_ntype_cstatements] = "csts";
  ntype2str[Lnast_ntype_pure_assign] = "=";
  ntype2str[Lnast_ntype_dp_assign] = ":=";
  ntype2str[Lnast_ntype_as] = "as";
  ntype2str[Lnast_ntype_label] = "label";
  ntype2str[Lnast_ntype_dot] = "dot";
  ntype2str[Lnast_ntype_logical_and] = "and";
  ntype2str[Lnast_ntype_logical_or] = "or";
  ntype2str[Lnast_ntype_and] = "&";
  ntype2str[Lnast_ntype_or] = "|";
  ntype2str[Lnast_ntype_xor] = "^";
  ntype2str[Lnast_ntype_plus] = "+";
  ntype2str[Lnast_ntype_minus] = "-";
  ntype2str[Lnast_ntype_mult] = "*";
  ntype2str[Lnast_ntype_div] = "/";
  ntype2str[Lnast_ntype_same] = "==";
  ntype2str[Lnast_ntype_lt] = "<";
  ntype2str[Lnast_ntype_le] = "<=";
  ntype2str[Lnast_ntype_gt] = ">";
  ntype2str[Lnast_ntype_ge] = ">=";
  ntype2str[Lnast_ntype_tuple] = "()";
  ntype2str[Lnast_ntype_ref] = "ref";
  ntype2str[Lnast_ntype_const] = "const";
  ntype2str[Lnast_ntype_attr_bits] = "attr_bits";
  ntype2str[Lnast_ntype_assert] = "I";
  ntype2str[Lnast_ntype_if] = "if";
  //ntype2str[Lnast_ntype_else] = "else";
  ntype2str[Lnast_ntype_cond] = "cond";
  ntype2str[Lnast_ntype_uif] = "uif";
  ntype2str[Lnast_ntype_elif] = "elif";
  ntype2str[Lnast_ntype_for] = "for";
  ntype2str[Lnast_ntype_while] = "while";
  ntype2str[Lnast_ntype_func_call] = "func_call";
  ntype2str[Lnast_ntype_func_def] = "func_def";
  ntype2str[Lnast_ntype_top] = "top";
}

std::string Lnast_to_pyrope_parser::ntype_dbg(Lnast_ntype ntype) {
  return ntype2str[ntype];
}

