
#include "lnast_to_cfg_parser.hpp"

void Lnast_to_cfg_parser::stringify() {
  fmt::print("\nstart Lnast_to_cfg_parser::stringify\n");

  for (const auto &it: lnast->depth_preorder(lnast->get_root())) {
    const auto& node_data = lnast->get_data(it);

    // fmt::print("current levels : p:{} c:{}\n", prev_statement_level, curr_statement_level);
    if (node_data.type == Lnast_ntype_top) {
      process_top(it.level);
    } else if (node_data.type == Lnast_ntype_statements) {
      process_statements(it.level);
    } else if (it.level == (curr_statement_level - 1)) {
      pop_statement();
      add_to_buffer(node_data);
    } else if (it.level == curr_statement_level) {
      process_buffer();
      add_to_buffer(node_data);
    } else {
      add_to_buffer(node_data);
    }


    // for printing out individual node values
    // fmt::print("\ntree index: pos:{} level:{} node: k:{} b:{}\n", it.pos, it.level, node_data.knum, node_data.sbs);
  }
  process_buffer();
}

void Lnast_to_cfg_parser::process_top(Tree_level level) {
  level_stack.push_back(level);
  curr_statement_level = level;
  k_num = 1;
}

void Lnast_to_cfg_parser::process_statements(Tree_level level) {
  level = level + 1;
  level_stack.push_back(level);
  prev_statement_level = curr_statement_level;
  curr_statement_level = level;

  buffer_stack.push_back(node_buffer);
  node_buffer.clear();
}

void Lnast_to_cfg_parser::pop_statement() {
  level_stack.pop_back();
  curr_statement_level = prev_statement_level;
  prev_statement_level = level_stack.back();

  process_buffer();
  node_buffer = buffer_stack.back();
  buffer_stack.pop_back();
}

void Lnast_to_cfg_parser::add_to_buffer(Lnast_node node) {
  node_buffer.push_back(node);
}

void Lnast_to_cfg_parser::process_buffer() {
  if (!node_buffer.size()) return;

  Lnast_ntype_id type = node_buffer.front().type;

  if (type == Lnast_ntype_pure_assign) {
    process_pure_assign();
  } else if (type == Lnast_ntype_as) {
    process_as();
  } else if (type == Lnast_ntype_label) {
    process_label();
  } else if (type == Lnast_ntype_and) {
    process_and();
  } else if (type == Lnast_ntype_xor) {
    process_xor();
  } else if (type == Lnast_ntype_gt) {
    process_gt();
  } else if (type == Lnast_ntype_func_call) {
    process_func_call();
  } else if (type == Lnast_ntype_func_def) {
    process_func_def();
  }

  // fmt::print("buffer size: {} : ", node_buffer.size());
  /*
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
  */

  node_buffer.clear();
}

std::string_view Lnast_to_cfg_parser::get_node_name(Lnast_node node) {
  return node.token.get_text(memblock);
}

std::string Lnast_to_cfg_parser::cat_buffer(std::vector<Lnast_node>::iterator it) {
  std::string tmp = "";
  while (it != node_buffer.end()) {
    tmp += get_node_name(*it);
    if (++it != node_buffer.end()) {
      tmp += " ";
    }
  }
  return tmp;
}

void Lnast_to_cfg_parser::process_pure_assign() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += "= ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_as() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += "as ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_label() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += ": ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_and() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += "& ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_xor() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += "^ ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_gt() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += "> ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_func_call() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += ".() ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::process_func_def() {
  std::string tmp = "";
  std::vector<Lnast_node>::iterator it = node_buffer.begin();
  tmp += "::{ ";
  it++;
  tmp += cat_buffer(it);
  fmt::print("{}\n", tmp);
}

void Lnast_to_cfg_parser::setup_ntype_str_mapping() {
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
  ntype2str[Lnast_ntype_else] = "else";
  ntype2str[Lnast_ntype_cond] = "cond";
  ntype2str[Lnast_ntype_uif] = "uif";
  ntype2str[Lnast_ntype_for] = "for";
  ntype2str[Lnast_ntype_while] = "while";
  ntype2str[Lnast_ntype_func_call] = "func_call";
  ntype2str[Lnast_ntype_func_def] = "func_def";
  ntype2str[Lnast_ntype_top] = "top";
}

std::string Lnast_to_cfg_parser::ntype_dbg(Lnast_ntype_id ntype) {
  return ntype2str[ntype];
}

