
#include "lnast_to_verilog_parser.hpp"

void Lnast_to_verilog_parser::generate() {
  curr_module = new Verilog_parser_module(lnast->get_top_module_name());

  for (const mmap_lib::Tree_index &it: lnast->depth_preorder(lnast->get_root())) {
    process_node(it);
  }
  flush_statements();
  curr_module->dec_indent_buffer();

  for(const auto it:file_map) {
    fmt::print("lnast_to_verilog_parser path:{} file:{}\n", path, it.first);
    fmt::print("{}\n",it.second);
    fmt::print("<<EOF\n");
  }
}

// infustructure
// don't need to change for anything
void Lnast_to_verilog_parser::process_node(const mmap_lib::Tree_index& it) {
  const auto& node_data = lnast->get_data(it);
  // add while to see pop_statement and to add buffer
  if (it.level < curr_statement_level) {
    pop_statement();

    while (it.level + 1 < curr_statement_level) {
      pop_statement();
    }
  }

  if (node_data.type.is_if()) {
    curr_module->inc_if_counter();
  }

  if (node_data.type.is_top()) {
    process_top(it.level);
  } else if (node_data.type.is_statements()) {
    add_to_buffer(node_data);
    push_statement(it.level, node_data.type);
  } else if (node_data.type.is_cstatements()) {
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

  if (node_data.type.is_func_def()) {
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
  fmt::print("before push\n");

  level = level + 1;
  level_stack.push_back(curr_statement_level);
  prev_statement_level = curr_statement_level;
  curr_statement_level = level;

  buffer_stack.push_back(node_buffer);
  node_buffer.clear();

  if (type.is_statements()) {
    curr_module->node_buffer_stack();
    curr_module->inc_indent_buffer();
  }

  fmt::print("after push\n");
}

void Lnast_to_verilog_parser::pop_statement() {
  fmt::print("before pop\n");

  process_buffer();

  node_buffer = buffer_stack.back();
  buffer_stack.pop_back();

  if (node_buffer.back().type.is_statements()) {
    curr_module->node_buffer_queue();
    curr_module->dec_indent_buffer();
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

    if (node_buffer.back().type.is_statements() && buffer_stack.size() > 0) {
      curr_module->node_buffer_queue();
      curr_module->dec_indent_buffer();
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

  if (type.is_pure_assign()) {
    // check if should be in combinational or stateful
    process_assign();
  } else if (type.is_dp_assign()) {
    process_assign();
  } else if (type.is_as()) {
    process_as();
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
    // not implemented
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
    auto name{node.token.get_text()};
    if (name.empty()) {
      fmt::print("{} ", node.type.debug_name_verilog());
    } else {
      fmt::print("{} ", name);
    }
  }
  fmt::print("\n");

  node_buffer.clear();
}

std::string_view Lnast_to_verilog_parser::get_node_name(Lnast_node node) {
  return node.token.get_text();
}

bool Lnast_to_verilog_parser::is_number(std::string_view test_string) {
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
  return test_string.find("___") == 0;
}

bool Lnast_to_verilog_parser::is_attr(std::string_view test_string) {
  return test_string.find("__") == 0 && !is_ref(test_string);
}

void Lnast_to_verilog_parser::process_assign() {

  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string_view ref = get_node_name(*it);
  auto map_it = ref_map.find(ref);
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
    fmt::print("map_it: inserting:\tkey:{}\tvalue:{}\n", key, ref);
    ref_map.insert(std::pair<std::string_view, std::string>(key, (std::string)ref));
  } else {
    // checks if it is a function
    std::size_t func_index = ref.find("(");
    std::string_view func_name = ref.substr(0, func_index);
    auto func_it = func_map.find((std::string)func_name);
    if (func_it != func_map.end()) {
      auto output_vars = func_it->second->output_vars.begin();
      std::string phrase = absl::StrCat(func_name, " ", key, ref.substr(func_index), ", ");
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

        absl::StrAppend(&phrase, ".", *output_vars, "(", new_key, ")");

        if (++output_vars != func_it->second->output_vars.end()) {
          absl::StrAppend(&phrase, ", ");
        } else {
          absl::StrAppend(&phrase, ")");
        }
      }

      fmt::print("the phrase of the day is : {}\n", phrase);
      curr_module->func_calls.push_back(phrase);
    } else {
      fmt::print("statefull_set:\tinserting:\tkey:{}\n", key);
      std::string value = curr_module->process_variable(ref);

      std::string phrase = curr_module->process_variable(key);
      if (curr_module->get_variable_type(key) == 3) {
        absl::StrAppend(&phrase, "_next");
      }
      absl::StrAppend(&phrase, " = ", value, ";\n");

      curr_module->var_manager.insert_variable(key);
      curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), phrase));

      fmt::print("pure_assign map:\tkey: {}\tvalue: {}\n", key, value);
    }
  }
}

void Lnast_to_verilog_parser::process_as() {
  auto it = node_buffer.begin();
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string_view ref = get_node_name(*it);
  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  std::string value = (std::string)ref;

  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    if (is_attr(value)) {
      if (curr_module->get_if_counter()) {
        // throw error
      } else {
        std::string phrase = absl::StrCat("(* LNAST: ", key, " as " , value, " *)\n");
        curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), phrase));
        curr_module->var_manager.insert_variable(key);

        Variable_options* attr_var = curr_module->var_manager.get(key);
        attr_var->update_attr(value);
      }
    }
  }

  fmt::print("process_as value:\tkey: {}\tvalue: {}\n", key, value);
}

void Lnast_to_verilog_parser::process_label() {
  auto it = node_buffer.begin();
  const auto access_type = it->type;
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string_view ref = get_node_name(*it);
  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
  }
  it++;
  std::string value = absl::StrCat(ref, access_type.debug_name_verilog(), process_number(get_node_name(*it)));

  fmt::print("process_label map:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    fmt::print("inserting:\tkey:{}\tvalue:{}\n", key, value);
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    // should never enter here
  }
}

void Lnast_to_verilog_parser::process_operator() {
  auto it = node_buffer.begin();
  const auto op_type = it->type;
  it++;
  std::string_view key = get_node_name(*it);
  it++;

  std::string value = "";
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    auto map_it = ref_map.find(ref);
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

    absl::StrAppend(&value, ref);
    if (++it != node_buffer.end()) {
      absl::StrAppend(&value, " ", op_type.debug_name_verilog(), " ");
    }
  }

  fmt::print("process_{} map:\tkey: {}\tvalue: {}\n", op_type.debug_name_verilog(), key, value);
  if (is_ref(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    std::string phrase = absl::StrCat(key, " ", op_type.debug_name_verilog(),"  ", value, "\n");
    curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), phrase));
  }
}

void Lnast_to_verilog_parser::process_if() {
  fmt::print("start process_if\n");

  std::vector<std::pair<int32_t, std::string>> new_nodes;

  auto it = node_buffer.begin();
  it++; // if
  it++; // csts
  std::string_view ref = get_node_name(*it);
  auto map_it = ref_map.find(ref);
  if (map_it != ref_map.end()) {
    ref = map_it->second;
    fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
  }
  new_nodes.push_back(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), absl::StrCat("if (", ref, ") begin\n")));
  it++; // cond
  auto queue_nodes = curr_module->pop_queue();
  new_nodes.insert(new_nodes.end(), std::make_move_iterator(queue_nodes.begin()), std::make_move_iterator(queue_nodes.end()));
  new_nodes.push_back(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), "end"));
  it++; // sts

  while (it != node_buffer.end()) {
    // this is the elif case
    if ((*it).type.is_cstatements()) {
      it++; // csts
      ref = get_node_name(*it);
      map_it = ref_map.find(ref);
      if (map_it != ref_map.end()) {
        ref = map_it->second;
        fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
      }
      new_nodes.push_back(std::pair<int32_t, std::string>(0, absl::StrCat(" else if (", ref, ") begin\n")));
      it++; // cond
      queue_nodes = curr_module->pop_queue();
      new_nodes.insert(new_nodes.end(), std::make_move_iterator(queue_nodes.begin()), std::make_move_iterator(queue_nodes.end()));
      new_nodes.push_back(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), "end"));
      it++; // sts
    }
    // this is the else case
    else {
      new_nodes.push_back(std::pair<int32_t, std::string>(0, " else begin\n"));
      queue_nodes = curr_module->pop_queue();
      new_nodes.insert(new_nodes.end(), std::make_move_iterator(queue_nodes.begin()), std::make_move_iterator(queue_nodes.end()));
      new_nodes.push_back(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), "end"));
      it++; // sts
    }
  }
  new_nodes.push_back(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), "\n"));

  curr_module->add_to_buffer_multiple(new_nodes);
  curr_module->dec_if_counter();

  fmt::print("end process_if\n");
}

void Lnast_to_verilog_parser::process_func_call() {
  fmt::print("start process_func_call\n");

  auto it = node_buffer.begin();
  it++; // func_call
  std::string_view key = get_node_name(*it);
  it++; // sts

  std::string func_name = absl::StrCat(root_filename, "_", get_node_name(*it));
  std::map<std::string, Verilog_parser_module*>::iterator func_module = func_map.find(func_name);
  fmt::print("found module : {} : size {}\n", func_module->first, func_module->second->arg_vars.size());

  std::string value = absl::StrCat(func_name, "(");
  if (func_module->second->has_sequential) {
    value = absl::StrCat(value, ".clk(clk), .reset(reset)");
  }

  it++; // ref
  while (it != node_buffer.end()) {
    std::string_view ref = get_node_name(*it);
    auto map_it = ref_map.find(ref);
    if (map_it != ref_map.end()) {
      ref = map_it->second;
      fmt::print("map_it find: {} | {}\n", map_it->first, map_it->second);
    }

    std::vector<std::string> split_str = absl::StrSplit(ref, "=");
    absl::StrAppend(&value, ".", split_str[0],"(", split_str[1], ")");
    if (++it != node_buffer.end()) {
      absl::StrAppend(&value, ", ");
    }
  }

  fmt::print("process_func_call: map:\tkey: {}\tvalue: {}\n", key, value);
  if (is_ref(key)) {
    ref_map.insert(std::pair<std::string_view, std::string>(key, value));
  } else {
    curr_module->add_to_buffer_single(std::pair<int32_t, std::string>(curr_module->get_indent_buffer(), value));
  }

  fmt::print("end process_func_call\n");
}

void Lnast_to_verilog_parser::process_func_def() {
  fmt::print("start process_func_def\n");

  auto it = node_buffer.begin();
  it++; // func_def
  std::string func_name = absl::StrCat(root_filename, "_", get_node_name(*it));
  curr_module->filename = func_name;
  fmt::print("func def : {}\n", curr_module->filename);
  // function name
  it++; // ref
  // the variables
  while (!it->type.is_statements()) {
    curr_module->var_manager.insert_variable(get_node_name(*it));
    it++;
  }
  it++; // sts

  curr_module->add_to_buffer_multiple(curr_module->pop_queue());

  file_map.insert(std::pair<std::string, std::string>(func_name, curr_module->create_file()));
  func_map.insert(std::pair<std::string, Verilog_parser_module*>(curr_module->filename, curr_module));

  curr_module = module_stack.back();
  module_stack.pop_back();

  fmt::print("end process_func_def\n");
}

