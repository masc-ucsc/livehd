
#include "lnast_to_verilog_parser.hpp"

std::string Lnast_to_verilog_parser::stringify() {
  buffer = absl::StrCat(buffer, "\n\n");

  fmt::print("\nstart Lnast_to_verilog_parser::stringify\n");

  for (const Tree_index &it: lnast->depth_preorder(lnast->get_root())) {
    process_node(it);
  }
  process_buffer();

  return buffer;
}

void Lnast_to_verilog_parser::process_node(const Tree_index& it) {
  const auto& node_data = lnast->get_data(it);

  // for printing out individual node values
  /*
  std::string name(node_data.token.get_text(memblock)); // str_view to string
  std::string type = ntype_dbg(node_data.type);
  fmt::print("current node : prev:{}\tcurr:{}\tit: {}\t", prev_statement_level, curr_statement_level, it.level);
  fmt::print("tree index: pos:{}\tlevel:{}\tnode: {}\ttype: {}\tk:{}\n", it.pos, it.level, name, type, node_data.knum);
  */

  std::string type = ntype_dbg(node_data.type);

  // add while to see pop_statement and to add buffer
  if (it.level < curr_statement_level) {
    pop_statement(it.level, node_data.type);

    while (it.level + 1 < curr_statement_level) {
      pop_statement(it.level, node_data.type);
    }
    type = ntype_dbg(node_data.type);
  }

  if (node_data.type == Lnast_ntype_top) {
    process_top(it.level);
  } else if (node_data.type == Lnast_ntype_statements) {
    // check the buffer to see if this is an if statement
    // and if it is, check if this is an ifel or an else
    if (node_buffer.size() > 0) {
      Lnast_ntype_id type = node_buffer.front().type;
      if (type == Lnast_ntype_if) {
        if (node_buffer.size() > 3 && node_buffer.back().type != Lnast_ntype_statements) {
          if_buffer.push_back(k_next);
          k_next++;
        }
        if_buffer.push_back(k_next);
      } else if (type == Lnast_ntype_func_def) {
        if_buffer.push_back(k_next);
      }
    }
    add_to_buffer(node_data);
    push_statement(it.level);
  } else if (node_data.type == Lnast_ntype_cstatements) {
    if (node_buffer.size() > 0) {
      if (node_buffer.back().type == Lnast_ntype_statements) {
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

void Lnast_to_verilog_parser::process_top(Tree_level level) {
  level_stack.push_back(level);
  curr_statement_level = level;
  k_next = 1;
}

void Lnast_to_verilog_parser::push_statement(Tree_level level) {
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

void Lnast_to_verilog_parser::pop_statement(Tree_level level, Lnast_ntype_id type) {
  uint32_t tmp_k = k_next;

  if (curr_statement_level != level && type != Lnast_ntype_cond) {
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

void Lnast_to_verilog_parser::add_to_buffer(Lnast_node node) {
  node_buffer.push_back(node);
}

void Lnast_to_verilog_parser::process_buffer() {
  if (!node_buffer.size()) return;

  fmt::print("process_buffer k_next: {}\n", k_next);

  Lnast_ntype_id type = node_buffer.front().type;

  if (type == Lnast_ntype_pure_assign) {
    // check if should be in combinational or stateful
    process_operator();
    // process_pure_assign();
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

  /*
  std::string k_next_str;
  if (k_next == 0) {
    k_next_str = "null";
  } else {
    k_next_str = absl::StrCat("K", k_next);
  }
  */

  buffer = absl::StrCat(buffer, node_str_buffer);
  k_stack.pop_back();
  node_str_buffer = "";

  if_buffer.clear();
  node_buffer.clear();
}

std::string_view Lnast_to_verilog_parser::get_node_name(Lnast_node node) {
  return node.token.get_text(memblock);
}

void Lnast_to_verilog_parser::flush_it(std::vector<Lnast_node>::iterator it) {
  while (it != node_buffer.end()) {
    Lnast_ntype_id type = (*it).type;
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

std::string_view Lnast_to_verilog_parser::join_it(std::vector<Lnast_node>::iterator it, std::string del) {
  std::string value = "";
  while (it != node_buffer.end()) {
    Lnast_ntype_id type = (*it).type;
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

std::string_view Lnast_to_verilog_parser::process_number(std::string_view num) {
    std::string::size_type n;
    n = num.find("d");
    return num.substr(n + 1);
}

bool Lnast_to_verilog_parser::is_ref(std::string_view test_string) {
  /*
  std::regex e("___[a-zA-Z]");
  return std::regex_match(test_string, e);
  */
  return test_string.find("___") == 0;
}

/*
void Lnast_to_verilog_parser::process_pure_assign() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "pure_assign:\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  std::map<std::string_view, std::string>::iterator map_it;
  std::string_view ref = get_node_name(*it);
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
    fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
  }
  value = absl::StrCat(value, ref);

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }

  fmt::print("pure_assign value:\tkey: {}\tvalue: {}\n", key, value);
  node_str_buffer = absl::StrCat(node_str_buffer, key, " = ", value, "\n");
}

void Lnast_to_verilog_parser::process_as() {
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

void Lnast_to_verilog_parser::process_label() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_label:\t");
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

  /*
  while (it != node_buffer.end()) {
    Lnast_ntype_id type = (*it).type;
    if (type == Lnast_ntype_statements || type == Lnast_ntype_cstatements) {
      it++;
      continue;
    }
    std::string_view ref = get_node_name(*it);
    map_it = ref_map.find(ref);

    if (map_it != ref_map.end()) {
      ref = it->second;
    }

    value = absl::StrCat(value, ref);
    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, " ", type, " ");
    }
  }
  */

/*
void Lnast_to_verilog_parser::process_and() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_and:\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
    }

    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, ref, " & ");
    } else {
      value = absl::StrCat(value, ref);
    }
  }

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }

  fmt::print("process_and value:\tkey: {}\tvalue: {}\n", key, value);
  node_str_buffer = absl::StrCat(node_str_buffer, key, " is actually ", value, "\n");
}

void Lnast_to_verilog_parser::process_xor() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_xor:\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    }

    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, ref, " ^ ");
    } else {
      value = absl::StrCat(value, ref);
    }
  }

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }

  fmt::print("process_xor value:\tkey: {}\tvalue: {}\n", key, value);
  node_str_buffer = absl::StrCat(node_str_buffer, key, " saving as ", value, "\n");
}

void Lnast_to_verilog_parser::process_plus() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_plus:\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    }

    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, ref, " + ");
    } else {
      value = absl::StrCat(value, ref);
    }
  }

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }

  fmt::print("process_xor value:\tkey: {}\tvalue: {}\n", key, value);
  node_str_buffer = absl::StrCat(node_str_buffer, key, " is actually as ", value, "\n");
}

void Lnast_to_verilog_parser::process_gt() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_gt:\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    }

    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, ref, " > ");
    } else {
      value = absl::StrCat(value, ref);
    }
  }

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }

  fmt::print("process_gt value:\tkey: {}\tvalue: {}\n", key, value);
  node_str_buffer = absl::StrCat(node_str_buffer, key, " real value ", value, "\n");
}
*/

void Lnast_to_verilog_parser::process_operator() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  std::string op_type = ntype_dbg((*it).type);
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "process_operator:\t", op_type, "\t");
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
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
      value = absl::StrCat(value, " ", op_type, " ");
    }
  }

  fmt::print("process_{} value:\tkey: {}\tvalue: {}\n", op_type, key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    node_str_buffer = absl::StrCat(node_str_buffer, key, " stored as ", value, "\n");
  }
}

void Lnast_to_verilog_parser::process_if() {
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
    if ((*node_it).type == Lnast_ntype_cond) {
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

void Lnast_to_verilog_parser::process_func_call() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  node_str_buffer = absl::StrCat(node_str_buffer, ".()\t");
  it++;
  flush_it(it);
  node_str_buffer = absl::StrCat(node_str_buffer, "\n");
}

void Lnast_to_verilog_parser::process_func_def() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  node_str_buffer = absl::StrCat(node_str_buffer, "::{\t");
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, get_node_name(*it), "\t");
  it++;
  node_str_buffer = absl::StrCat(node_str_buffer, "K", if_buffer.front(), "\t");
  flush_it(it);
  node_str_buffer = absl::StrCat(node_str_buffer, "\n");
}

void Lnast_to_verilog_parser::setup_ntype_str_mapping() {
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
  ntype2str[Lnast_ntype_for] = "for";
  ntype2str[Lnast_ntype_while] = "while";
  ntype2str[Lnast_ntype_func_call] = "func_call";
  ntype2str[Lnast_ntype_func_def] = "func_def";
  ntype2str[Lnast_ntype_top] = "top";
}

std::string Lnast_to_verilog_parser::ntype_dbg(Lnast_ntype_id ntype) {
  return ntype2str[ntype];
}

