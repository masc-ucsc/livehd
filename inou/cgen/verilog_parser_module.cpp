
#include "verilog_parser_module.hpp"

std::string Verilog_parser_module::indent_buffer(int32_t size) {
  return std::string(size * 2, ' ');
}


std::string Verilog_parser_module::create_file() {
  std::string always_str = absl::StrCat(indent_buffer(1), "always_comb begin\n");
  std::string next_str;

  for(auto ele : var_manager.variable_map) {
    if (get_variable_type(ele.first) == 3) {
      absl::StrAppend(&next_str, "\n", indent_buffer(1), "always @(posedge clk) begin\n", indent_buffer(2), process_variable(ele.first), " <= ", process_variable(ele.first), "_next;\n", indent_buffer(1), "end\n");

      has_sequential = true;
    }
  }

  std::string module_start = absl::StrCat("module ", filename, " (");
  std::string start_filler = std::string(module_start.length(), ' ');
  std::string inputs = "";
  if (has_sequential) {
    inputs = absl::StrCat("input clk,\n", start_filler, "input reset");
  }
  std::string outputs = "";
  std::string wires = "";

  for(auto var_name : var_manager.variable_map) {
    fmt::print("variable names: {}\n", var_name.first);
    uint32_t var_type = get_variable_type(var_name.first);
    if (var_type == 0) {
      absl::StrAppend(&wires, "  wire ", process_variable(var_name.first), ";\n");
      absl::StrAppend(&always_str, indent_buffer(2), process_variable(var_name.first), " = 0;\n");
    } else {
      std::string bits_string;

      if (var_name.second->bits > 1) { // default setting
        bits_string = absl::StrCat("[", var_name.second->bits - 1, ":0] ");
      }

      std::string phrase = absl::StrCat(bits_string, process_variable(var_name.first));
      arg_vars.push_back(process_variable(var_name.first));

      if (var_type == 1) {
        if (inputs.length()) {
          absl::StrAppend(&inputs, ",\n", start_filler, "input ", phrase);
        } else {
          inputs = absl::StrCat("input ", phrase);
        }
      } else if (var_type == 2) {
        absl::StrAppend(&outputs, ",\n", start_filler, "output reg ", phrase);
        output_vars.push_back(process_variable(var_name.first));
      } else if (var_type == 3) {
        absl::StrAppend(&outputs, ",\n", start_filler, "output reg ", phrase);
        absl::StrAppend(&wires, "  wire ", process_variable(var_name.first), "_next;\n");
        absl::StrAppend(&always_str, indent_buffer(2), process_variable(var_name.first), "_next = 0;\n");
        output_vars.push_back(process_variable(var_name.first));
      }
    }
  }
  absl::StrAppend(&module_start, inputs, outputs, ");\n", wires, "\n");

  for (auto ele : func_calls) {
    absl::StrAppend(&module_start, "  ", ele, "\n");
  }

  for (auto node : node_str_buffer) {
    absl::StrAppend(&always_str, indent_buffer(node.first), node.second);
  }

  return absl::StrCat(module_start, "\n", always_str, indent_buffer(1), "end\n", next_str, "end module\n");
}

void Verilog_parser_module::inc_if_counter() {
  if_counter++;
}

void Verilog_parser_module::dec_if_counter() {
  if_counter--;
}

uint32_t Verilog_parser_module::get_if_counter() {
  return if_counter;
}

void Verilog_parser_module::add_to_buffer_single(std::pair<int32_t, std::string> next) {
  node_str_buffer.push_back(next);
}

void Verilog_parser_module::add_to_buffer_multiple(std::vector<std::pair<int32_t, std::string>> nodes) {
  node_str_buffer.insert(node_str_buffer.end(), std::make_move_iterator(nodes.begin()), std::make_move_iterator(nodes.end()));
}

// returns 1 if input
// returns 2 if output
// returns 3 if latch
// otherwise returns 0
uint32_t Verilog_parser_module::get_variable_type(std::string_view var_name) {
  if (var_name.at(0) == '$') {
   return 1;
  } else if (var_name.at(0) == '%') {
    return 2;
  } else if (var_name.at(0) == '#') {
    return 3;
  }

  return 0;
}

std::string Verilog_parser_module::process_variable(std::string_view var_name) {
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

