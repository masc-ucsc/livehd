
#include "verilog_parser_module.hpp"

std::string Verilog_parser_module::indent_buffer(int32_t size) {
  return std::string(size * 2, ' ');
}

std::string Verilog_parser_module::create_header() {
  std::string inputs = "input clk,\ninput reset";
  std::string outputs;
  std::string wires;

  for(auto var_name : statefull_set) {
    fmt::print("variable names: {}\n", var_name);
    uint32_t var_type = get_variable_type(var_name);
    if (var_type == 1) {
      inputs = absl::StrCat(inputs, ",\ninput ", var_name);
    } else if (var_type == 2) {
      outputs = absl::StrCat(outputs, ",\noutput ", var_name);
    } else {
      wires = absl::StrCat(wires, " wire ", var_name, ";\n");
    }
  }

  return absl::StrCat("module ", filename, " (", inputs, outputs, ");\n", wires, "\n");
}

std::string Verilog_parser_module::create_footer() {
  return absl::StrCat("end module\n");
}

std::string Verilog_parser_module::create_always() {
  std::string buffer = absl::StrCat(indent_buffer(1), "always @(*) begin\n");

  for (auto node : node_str_buffer) {
    buffer = absl::StrCat(buffer, indent_buffer(node.first), node.second);
  }

  return absl::StrCat(buffer, indent_buffer(1), "end\n");
}

std::string Verilog_parser_module::create_next() {
  std::string buffer = absl::StrCat(indent_buffer(1), "always @(posedge clk) begin\n");

  for (auto ele : statefull_set) {
    if (get_variable_type(ele) == 2) {
      buffer = absl::StrCat(buffer, indent_buffer(2), ele, " = ", ele, "_next\n");
    }
  }

  return absl::StrCat(buffer, indent_buffer(1), "end\n");
}

std::string Verilog_parser_module::create_file() {
  return absl::StrCat(create_header(), create_always(), create_next(), create_footer());
}

void Verilog_parser_module::add_to_buffer_single(std::pair<int32_t, std::string> next, std::set<std::string_view> new_vars) {
  node_str_buffer.push_back(next);
  statefull_set.insert(std::make_move_iterator(new_vars.begin()), std::make_move_iterator(new_vars.end()));
}

void Verilog_parser_module::add_to_buffer_multiple(std::vector<std::pair<int32_t, std::string>> nodes, std::set<std::string_view> new_vars) {
  node_str_buffer.insert(node_str_buffer.end(), std::make_move_iterator(nodes.begin()), std::make_move_iterator(nodes.end()));
  statefull_set.insert(std::make_move_iterator(new_vars.begin()), std::make_move_iterator(new_vars.end()));
}

// returns 1 if input
// returns 2 if output
// otherwise returns 0

uint32_t Verilog_parser_module::get_variable_type(std::string_view var_name) {
  if (var_name.at(0) == '$') {
   return 1;
  } else if (var_name.at(0) == '%') {
    return 2;
  }

  return 0;
}

void Verilog_parser_module::node_buffer_stack() {
  sts_buffer_stack.push_back(node_str_buffer);
  node_str_buffer.clear();
}

void Verilog_parser_module::node_buffer_queue() {
  sts_buffer_queue.push_back(node_str_buffer);
  node_str_buffer = sts_buffer_stack.back();
  sts_buffer_stack.pop_back();
}

std::vector<std::pair<int32_t, std::string>> Verilog_parser_module::pop_queue() {
  std::vector<std::pair<int32_t, std::string>> queue_nodes = sts_buffer_queue.front();
  sts_buffer_queue.erase(sts_buffer_queue.begin());
  return queue_nodes;
}

