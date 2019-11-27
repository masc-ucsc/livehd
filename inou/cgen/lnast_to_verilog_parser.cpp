
#include "lnast_to_verilog_parser.hpp"

std::map<std::string, std::string> Lnast_to_verilog_parser::stringify(std::string filepath) {
  root_filename = get_filename(filepath);
  curr_module = new Verilog_parser_module(root_filename);

  inc_indent_buffer();
  inc_indent_buffer();
  for (const mmap_lib::Tree_index &it: lnast->depth_preorder(lnast->get_root())) {
    process_node(it);
  }
  flush_statements();
  dec_indent_buffer();

  file_map.insert(std::pair<std::string, std::string>(curr_module->filename, curr_module->create_file()));
  return file_map;
}

// infustructure
// don't need to change for anything
void Lnast_to_verilog_parser::process_node(const mmap_lib::Tree_index& it) {
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

  if (node_data.type == Lnast_ntype_if) {
    curr_module->inc_if_counter();
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
    fmt::print("finished process_buffer\n");
  } else {
    add_to_buffer(node_data);
  }

  if (node_data.type == Lnast_ntype_func_def) {
    module_stack.push_back(curr_module);
    curr_module = new Verilog_parser_module();
  }
}

// no change
void Lnast_to_verilog_parser::process_top(mmap_lib::Tree_level level) {
  level_stack.push_back(level);
  curr_statement_level = level;
}

// no change
void Lnast_to_verilog_parser::push_statement(mmap_lib::Tree_level level, Lnast_ntype type) {
  fmt::print("push\n");

  level = level + 1;
  level_stack.push_back(curr_statement_level);
  prev_statement_level = curr_statement_level;
  curr_statement_level = level;

  buffer_stack.push_back(node_buffer);
  node_buffer.clear();

  if (Lnast_ntype_statements == type) {
    curr_module->node_buffer_stack();
    inc_indent_buffer();
  }

  fmt::print("after push\n");
}

void Lnast_to_verilog_parser::pop_statement() {
  fmt::print("pop\n");

  process_buffer();

  node_buffer = buffer_stack.back();
  buffer_stack.pop_back();

  if (Lnast_ntype_statements == node_buffer.back().type) {
    curr_module->node_buffer_queue();
    dec_indent_buffer();
  }

  level_stack.pop_back();
  curr_statement_level = prev_statement_level;
  prev_statement_level = level_stack.back();

  fmt::print("after pop\n");
}

void Lnast_to_verilog_parser::flush_statements() {
  fmt::print("starting to flush statements\n");

  while (buffer_stack.size() > 0) {
    process_buffer();

    node_buffer = buffer_stack.back();
    buffer_stack.pop_back();

    if (Lnast_ntype_statements == node_buffer.back().type && buffer_stack.size() > 0) {
      curr_module->node_buffer_queue();
      dec_indent_buffer();
    }

    level_stack.pop_back();
    curr_statement_level = prev_statement_level;
    prev_statement_level = level_stack.back();
  }
  fmt::print("ending flushing statements\n");
}

void Lnast_to_verilog_parser::add_to_buffer(Lnast_node node) {
  node_buffer.push_back(node);
}

void Lnast_to_verilog_parser::process_buffer() {
  if (!node_buffer.size()) return;

  Lnast_ntype type = node_buffer.front().type;

  if (type == Lnast_ntype_pure_assign) {
    // check if should be in combinational or stateful
    process_assign();
  } else if (type == Lnast_ntype_dp_assign) {
    process_assign();
  } else if (type == Lnast_ntype_as) {
    process_as();
  } else if (type == Lnast_ntype_label) {
    process_label();
  } else if (type == Lnast_ntype_dot) {
    process_label();
  } else if (type == Lnast_ntype_logical_and) {
    process_operator();
  } else if (type == Lnast_ntype_logical_or) {
    process_operator();
  } else if (type == Lnast_ntype_and) {
    process_operator();
  } else if (type == Lnast_ntype_or) {
    process_operator();
  } else if (type == Lnast_ntype_xor) {
    process_operator();
  } else if (type == Lnast_ntype_plus) {
    process_operator();
  } else if (type == Lnast_ntype_minus) {
    process_operator();
  } else if (type == Lnast_ntype_mult) {
    process_operator();
  } else if (type == Lnast_ntype_div) {
    process_operator();
  } else if (type == Lnast_ntype_same) {
    process_operator();
  } else if (type == Lnast_ntype_lt) {
    process_operator();
  } else if (type == Lnast_ntype_le) {
    process_operator();
  } else if (type == Lnast_ntype_gt) {
    process_operator();
  } else if (type == Lnast_ntype_ge) {
    process_operator();
  } else if (type == Lnast_ntype_tuple) {
    // not implemented
  } else if (type == Lnast_ntype_ref) {
    // is added to buffer
  } else if (type == Lnast_ntype_const) {
    // is added to buffer
  } else if (type == Lnast_ntype_attr_bits) {
    // is added to buffer
  } else if (type == Lnast_ntype_assert) {
    // check in for node configuration
  } else if (type == Lnast_ntype_if) {
    process_if();
  } else if (type == Lnast_ntype_cond) {
    // is added to buffer
  } else if (type == Lnast_ntype_for) {
    // not implemented in lnast
  } else if (type == Lnast_ntype_while) {
    // not implemented in lnast
  } else if (type == Lnast_ntype_func_call) {
    process_func_call();
  } else if (type == Lnast_ntype_func_def) {
    process_func_def();
  } else if (type == Lnast_ntype_top) {
    // not added to any buffer
  }

  for (auto const& node : node_buffer) {
    std::string name(node.token.get_text(memblock)); // str_view to string
    std::string type_dbg = ntype_dbg(node.type);
    if (name == "") {
      fmt::print("{}({}) ", type_dbg, node.type);
    } else {
      fmt::print("{} ", name);
    }
  }
  fmt::print("\n");

  node_buffer.clear();
}

std::string_view Lnast_to_verilog_parser::get_node_name(Lnast_node node) {
  return node.token.get_text(memblock);
}

bool Lnast_to_verilog_parser::is_number(std::string_view test_string) {
  // TODO(joapena): check how to use regex
  if (test_string.find("0d") == 0) {
    return true;
  } else if (test_string.find("0b") == 0) {
    return true;
  } else if (test_string.find("0x") == 0) {
    return true;
  }
  return false;
}

std::string_view Lnast_to_verilog_parser::process_number(std::string_view num_string) {
  if (num_string.find("0d") == 0) {
    return num_string.substr(2);
  }
  return num_string;
}

bool Lnast_to_verilog_parser::is_ref(std::string_view test_string) {
  /*
  std::regex e("___[a-zA-Z]");
  return std::regex_match(test_string, e);
  */
  // TODO(joapena): check for better way to compare string
  return test_string.find("___") == 0;
}

bool Lnast_to_verilog_parser::is_attr(std::string_view test_string) {
  return test_string.find("__") == 0 && !is_ref(test_string);
}

void Lnast_to_verilog_parser::inc_indent_buffer(){
  // indent_buffer() = absl::StrCat(indent_buffer(), "  ");
  indent_buffer_size++;
}

void Lnast_to_verilog_parser::dec_indent_buffer() {
  // indent_buffer() = indent_buffer().substr(0, indent_buffer().size() - 2);
  indent_buffer_size--;
}

std::string Lnast_to_verilog_parser::get_filename(std::string filepath) {
  std::vector<std::string> filepath_split = absl::StrSplit(filepath, '/');
  std::pair<std::string, std::string> fname = absl::StrSplit(filepath_split[filepath_split.size() - 1], '.');
  return fname.first;
}

void Lnast_to_verilog_parser::process_assign() {
  std::string value = "";

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  std::string assign_type = ntype_dbg((*it).type);
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string_view ref = get_node_name(*it);
  std::map<std::string_view, std::string>::iterator map_it;
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
    // connect the two stateful stuff
    fmt::print("map_it: find: {} | {}\n", map_it->first, map_it->second);
  } else if (!is_number(ref)) {
    curr_module->var_manager.insert_variable(ref);
  } else if (is_number(ref)) {
    ref = process_number(ref);
    curr_module->var_manager.insert_variable(ref);
  }

  if (is_ref(key)) {
    value = absl::StrCat(value, ref);
    fmt::print("map_it: inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    std::vector<std::string> split_str = absl::StrSplit(ref, "(");
    // fmt::print("split_str : {}\n", split);
    std::map<std::string, Verilog_parser_module*>::iterator func_it;
    func_it = func_map.find(split_str[0]);
    if (func_it != func_map.end()) {
      std::string ref_tmp = absl::StrCat(ref);
      std::vector<std::string>::iterator output_vars = func_it->second->output_vars.begin();
      fmt::print("output_vars : size : {}\n", func_it->second->output_vars.size());
      std::string phrase = absl::StrCat(ref_tmp, ", ");
      // std::string phrase = absl::StrCat(ref_tmp, ", .", *output_vars, "(", key, "))");
      while (output_vars != func_it->second->output_vars.end()) {
        std::string tail = (*output_vars).substr((*output_vars).length() - 2, 2);

        std::string new_key;
        if (tail == "_o" || tail == "_r") {
          new_key = absl::StrCat(key, "_", (*output_vars).substr(0, (*output_vars).length() - 2));
        } else {
          new_key = absl::StrCat(key, "_", *output_vars);
        }
        fmt::print("new_key is : {}\n", new_key);
        curr_module->var_manager.insert_variable(new_key);

        phrase = absl::StrCat(phrase, ".", *output_vars, "(", new_key, ")");

        if (++output_vars != func_it->second->output_vars.end()) {
          phrase = absl::StrCat(phrase, ", ");
        } else {
          phrase = absl::StrCat(phrase, ")");
        }
      }

      fmt::print("the phrase of the day is : {}\n", phrase);

      curr_module->func_calls.push_back(phrase);
    } else {
      fmt::print("statefull_set:\tinserting:\tkey:{}\n", key);
      value = curr_module->process_variable(ref);

      std::string phrase = curr_module->process_variable(key);
      if (curr_module->get_variable_type(key) == 3) {
        phrase = absl::StrCat(phrase, "_next");
      }
      phrase = absl::StrCat(phrase, " ", assign_type, " ", value, ";\n");

      curr_module->var_manager.insert_variable(key);
      curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(indent_buffer_size, phrase));
    }
  }

  fmt::print("pure_assign value:\tkey: {}\tvalue: {}\n", key, value);
}

void Lnast_to_verilog_parser::process_as() {
  std::string value = "";

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::map<std::string_view, std::string>::iterator map_it;
  std::string_view ref = get_node_name(*it);
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  value = absl::StrCat(value, ref);

  // change the variable properties

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    std::string phrase = absl::StrCat("(* LNAST: ", key, " as " , value, " *)\n");
    curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(indent_buffer_size, phrase));
    curr_module->var_manager.insert_variable(key);

    if (is_attr(value) && !curr_module->get_if_counter()) {
      Variable_options* attr_var = curr_module->var_manager.get(key);
      attr_var->update_attr(value);
    }
  }

  fmt::print("process_as value:\tkey: {}\tvalue: {}\n", key, value);
}

void Lnast_to_verilog_parser::process_label() {
  std::string value = "";

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  std::string_view access_type = ntype_dbg((*it).type);
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string_view ref = get_node_name(*it);
  std::map<std::string_view, std::string>::iterator map_it;
  map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  it++;
  value = absl::StrCat(value, ref, access_type, process_number(get_node_name(*it)));

  fmt::print("process_label value:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  }
  /*
  std::smatch m;
  std::regex_search(value, m, std::regex("b|o|d|h"));
  value = process_number(value);
  */
}

void Lnast_to_verilog_parser::process_operator() {
  std::string value = "";

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  std::string op_type = ntype_dbg((*it).type);
  it++;
  std::string_view key = get_node_name(*it);
  it++;

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

      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    } else if (ref.size() > 2 && !is_number(ref)) {
      curr_module->var_manager.insert_variable(ref);
      ref = curr_module->process_variable(ref);
    } else if (is_number(ref)) {
      ref = process_number(ref);
    } else {
      ref = curr_module->process_variable(ref);
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
    std::string phrase = absl::StrCat(key, " ", op_type,"  ", value, "\n");
    curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(indent_buffer_size, phrase));
  }
}

void Lnast_to_verilog_parser::process_if() {
  fmt::print("start process_if\n");
  std::vector<std::pair<int32_t, std::string>> new_nodes;

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
  new_nodes.push_back(std::pair<int32_t, std::string>(indent_buffer_size, absl::StrCat("if(", ref, ") {\n")));
  it++; // cond
  std::vector<std::pair<int32_t, std::string>> queue_nodes = curr_module->pop_queue();
  new_nodes.insert(new_nodes.end(), std::make_move_iterator(queue_nodes.begin()), std::make_move_iterator(queue_nodes.end()));
  new_nodes.push_back(std::pair<int32_t, std::string>(indent_buffer_size, "}"));
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
      new_nodes.push_back(std::pair<int32_t, std::string>(0, absl::StrCat(" elif (", ref, ") {\n")));
      it++; // cond
      queue_nodes = curr_module->pop_queue();
      new_nodes.insert(new_nodes.end(), std::make_move_iterator(queue_nodes.begin()), std::make_move_iterator(queue_nodes.end()));
      new_nodes.push_back(std::pair<int32_t, std::string>(indent_buffer_size, "}"));
      it++; // sts
    }

    // this is the else
    else {
      new_nodes.push_back(std::pair<int32_t, std::string>(0, " else {\n"));
      queue_nodes = curr_module->pop_queue();
      new_nodes.insert(new_nodes.end(), std::make_move_iterator(queue_nodes.begin()), std::make_move_iterator(queue_nodes.end()));
      new_nodes.push_back(std::pair<int32_t, std::string>(indent_buffer_size, "}"));
      it++; // sts
    }
  }
  new_nodes.push_back(std::pair<int32_t, std::string>(indent_buffer_size, "\n"));

  curr_module->add_to_buffer_multiple(new_nodes);
  curr_module->dec_if_counter();
}

void Lnast_to_verilog_parser::process_func_call() {
  std::string value = "";

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++; // func_call
  std::string_view key = get_node_name(*it);
  it++; // sts

  std::string func_name = absl::StrCat(root_filename, "_", get_node_name(*it));
  std::map<std::string, Verilog_parser_module*>::iterator func_module = func_map.find(func_name);
  fmt::print("found module : {} : size {}\n", func_module->first, func_module->second->arg_vars.size());

  value = absl::StrCat(value, func_name, "(");
  if (func_module->second->has_sequential) {
    value = absl::StrCat(value, ".clk(clk), .reset(reset)");
  }

  it++; // ref
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    std::map<std::string_view, std::string>::iterator map_it;
    map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    }

    std::vector<std::string> split_str = absl::StrSplit(ref, "=");
    value = absl::StrCat(value, ".", split_str[0],"(", split_str[1], ")");
    if (++it != node_buffer.end()) {
      value = absl::StrCat(value, ", ");
    }
  }

  fmt::print("process_func_call: value:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    // std::string phrase = absl::StrCat(key, " ", op_type,"  ", value, "\n");
    curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(indent_buffer_size, value));
  }
}

void Lnast_to_verilog_parser::process_func_def() {
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++; // func_def
  std::string func_name = absl::StrCat(root_filename, "_", get_node_name(*it));
  curr_module->filename = func_name;
  fmt::print("func def : {}\n", get_node_name(*it));
  // function name
  it++; // ref
  // the variables
  while ((*it).type != Lnast_ntype_statements) {
    curr_module->var_manager.insert_variable(get_node_name(*it));
    it++;
  }
  it++; // sts

  curr_module->add_to_buffer_multiple(curr_module->pop_queue());

  file_map.insert(std::pair<std::string, std::string>(func_name, curr_module->create_file()));

  fmt::print("new module:\n{}", curr_module->create_file());

  func_map.insert(std::pair<std::string, Verilog_parser_module*>(curr_module->filename, curr_module));
  curr_module = module_stack.back();
  module_stack.pop_back();
}

void Lnast_to_verilog_parser::setup_ntype_str_mapping() {
  ntype2str[Lnast_ntype_invalid] = "invalid";
  ntype2str[Lnast_ntype_statements] = "sts";
  ntype2str[Lnast_ntype_cstatements] = "csts";
  ntype2str[Lnast_ntype_pure_assign] = "=";
  ntype2str[Lnast_ntype_dp_assign] = ":=";
  ntype2str[Lnast_ntype_as] = "as";
  ntype2str[Lnast_ntype_label] = "=";
  ntype2str[Lnast_ntype_dot] = "_";
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

std::string Lnast_to_verilog_parser::ntype_dbg(Lnast_ntype ntype) {
  return ntype2str[ntype];
}

