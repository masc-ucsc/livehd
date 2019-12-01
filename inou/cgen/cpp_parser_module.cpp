
#include "cpp_parser_module.hpp"

std::string Cpp_parser_module::indent_buffer(int32_t size) {
  return std::string(size * 2, ' ');
}

void Cpp_parser_module::process_inputs() {
  std::string hpp_file = absl::StrCat("class ", filename, " {\n");
  std::string private_str = "";
  std::string public_str = "";
  combinational_str = "";
  sequential_str = absl::StrCat("void ", filename, "::sequential() {\n");

  for (auto var_name : var_manager.variable_map) {
    fmt::print("variable names: {}\n", var_name.first);
    uint32_t var_type = get_variable_type(var_name.first);

    if (var_type == 0) {
    }
    // input
    else if (var_type == 1) {
      if (combinational_str.length()) {
        combinational_str = absl::StrCat(combinational_str, ", uint32_t ", process_variable(var_name.first));
      } else {
        combinational_str = absl::StrCat("combinational (uint32_t ", process_variable(var_name.first));
      }
    }
    // output or register
    else if (var_type == 2 || var_type == 3) {
      if (!private_str.length()) {
        private_str = "private:\n";
        public_str = "public:\n";
      }
      private_str = absl::StrCat(private_str, "  uint32_t ", process_variable(var_name.first), "_next;\n");
      public_str = absl::StrCat(public_str, "  uint32_t ", process_variable(var_name.first), ";\n");

      sequential_str = absl::StrCat(sequential_str, "  ", process_variable(var_name.first), " = ", process_variable(var_name.first), "_next;\n");
    }
  }

  header_str = absl::StrCat(hpp_file, private_str, public_str, "\n  void ", combinational_str, ");\n  void sequential();\n}");
}

std::string Cpp_parser_module::create_header() {
  /*
   * check for sequential
   * check for bits
  */
  return header_str;
}

std::string Cpp_parser_module::create_implementation() {
  std::string buffer = "";

  for (auto node : node_str_buffer) {
    buffer = absl::StrCat(buffer, indent_buffer(node.first), node.second);
  }

  return absl::StrCat("void ", filename, "::", combinational_str, ") {\n", buffer, "}\n", sequential_str, "}\n");
}

std::string Cpp_parser_module::create_file() {
  process_inputs();
  return absl::StrCat("header:\n", create_header(), "\n\nimplementation:\n", create_implementation());
}

void Cpp_parser_module::inc_if_counter() {
  if_counter++;
}

void Cpp_parser_module::dec_if_counter() {
  if_counter--;
}

uint32_t Cpp_parser_module::get_if_counter() {
  return if_counter;
}

void Cpp_parser_module::add_to_buffer_single(std::pair<int32_t, std::string> next) {
  node_str_buffer.push_back(next);
  // statefull_set.insert(std::make_move_iterator(new_vars.begin()), std::make_move_iterator(new_vars.end()));
}

void Cpp_parser_module::add_to_buffer_multiple(std::vector<std::pair<int32_t, std::string>> nodes) {
  node_str_buffer.insert(node_str_buffer.end(), std::make_move_iterator(nodes.begin()), std::make_move_iterator(nodes.end()));
  // statefull_set.insert(std::make_move_iterator(new_vars.begin()), std::make_move_iterator(new_vars.end()));
}

// returns 1 if input
// returns 2 if output
// returns 3 if latch
// otherwise returns 0

uint32_t Cpp_parser_module::get_variable_type(std::string_view var_name) {
  if (var_name.at(0) == '$') {
   return 1;
  } else if (var_name.at(0) == '%') {
    return 2;
  } else if (var_name.at(0) == '#') {
    return 3;
  }

  return 0;
}

std::string Cpp_parser_module::process_variable(std::string_view var_name) {
  if (var_name.at(0) == '$') {
    var_name = absl::StrCat(var_name, "_i");

    if (var_name.at(1) == '\\') {
      return std::string(var_name.substr(2)).c_str();
    } else {
      return std::string(var_name.substr(1)).c_str();
    }
  } else if (var_name.at(0) == '%') {
    var_name = absl::StrCat(var_name, "_o");

    if (var_name.at(1) == '\\') {
      return std::string(var_name.substr(2)).c_str();
    } else {
      return std::string(var_name.substr(1)).c_str();
    }
  } else if (var_name.at(0) == '#') {
    var_name = absl::StrCat(var_name, "_r");

    if (var_name.at(1) == '\\') {
      return std::string(var_name.substr(2)).c_str();
    } else {
      return std::string(var_name.substr(1)).c_str();
    }
  } else {
    return std::string(var_name).c_str();
  }
}

void Cpp_parser_module::node_buffer_stack() {
  sts_buffer_stack.push_back(node_str_buffer);
  node_str_buffer.clear();
}

void Cpp_parser_module::node_buffer_queue() {
  sts_buffer_queue.push_back(node_str_buffer);
  node_str_buffer = sts_buffer_stack.back();
  sts_buffer_stack.pop_back();
}

std::vector<std::pair<int32_t, std::string>> Cpp_parser_module::pop_queue() {
  std::vector<std::pair<int32_t, std::string>> queue_nodes = sts_buffer_queue.front();
  sts_buffer_queue.erase(sts_buffer_queue.begin());
  return queue_nodes;
}

