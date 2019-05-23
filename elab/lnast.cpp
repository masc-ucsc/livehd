
#include "lnast.hpp"

//------------- Language_neutral_ast member function start -----
Language_neutral_ast::Language_neutral_ast(std::string_view _buffer, Lnast_ntype_id ntype_top) : buffer(_buffer) {
  I(!buffer.empty());
  set_root(Lnast_node(ntype_top, 0, 0));
}


//------------- Lnast_parser member function start -------------

void Lnast_parser::elaborate(){
  lnast = std::make_unique<Language_neutral_ast>(get_buffer(), Lnast_ntype_top);
  build_statements(0);
}


void Lnast_parser::build_statements(Scope_id scope){
  auto tree_idx_sts  = lnast->add_child(lnast->get_root(), Lnast_node(Lnast_ntype_statement, scan_token(), scope));
  add_statement(tree_idx_sts, scope);
}

void Lnast_parser::add_statement(const Tree_index &tree_idx_sts, Scope_id cur_scope) {
  fmt::print("line:{}, statement:{}\n", line_num, scan_text());

  Token_entry    node_name;
  Lnast_ntype_id node_type = Lnast_ntype_invalid;
  Token_entry    cfg_token_beg;
  Token_entry    cfg_token_end;
  Scope_id       token_scope = cur_scope;

  int line_tkcnt = 1;
  while(line_num == scan_calc_lineno()){
    if(scan_is_end()) return;

    switch (line_tkcnt) {
      case CFG_NODE_NAME_POS:
        node_name = scan_token();  //must be a complete alnum
        break;
      case CFG_SCOPE_ID_POS:
        token_scope = process_scope(cur_scope); //recursive build sub-graph
        break;
      case CFG_TOKEN_POS_BEG:
        cfg_token_beg = scan_token(); //must be a complete alnum
        break;
      case CFG_TOKEN_POS_END:
        cfg_token_end = scan_token(); //must be a complete alnum
        break;
      case CFG_OP_POS_BEG: { //no regular pattern, start to do scan_next() internally case by case
        operator_analysis(node_type, line_tkcnt);
        auto tree_idx_op = lnast->add_child(tree_idx_sts, Lnast_node(node_type, node_name, cur_scope));
        scan_next();
        line_tkcnt += 1;
        add_operator_subtree(tree_idx_op, line_tkcnt, cur_scope);
        break;
      }
      default: ;
    }

    fmt::print("token:{}\n", scan_text());
    scan_next();
    line_tkcnt += 1;
  }

  line_num += 1;
  add_statement(tree_idx_sts, cur_scope);
}

//scan pos will stop at the last token
void Lnast_parser::add_operator_subtree(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {
  I(line_tkcnt > CFG_OP_POS_BEG);
  I(scan_is_token(Token_id_alnum)); //must be an alnum for first operand
  auto nt = lnast->get_data(tree_idx_op).node_type;

  if (nt == Lnast_ntype_pure_assign || nt == Lnast_ntype_dp_assign || nt == Lnast_ntype_as || nt == Lnast_ntype_tuple) {
    process_assign_like_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_lable) {
    process_lable_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_func_call) {
    process_func_call_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_func_def) {
    process_func_def_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_if || nt == Lnast_ntype_uif) {
    process_if_op(tree_idx_op, line_tkcnt, cur_scope);
  } else {
    process_binary_like_op(tree_idx_op, line_tkcnt, cur_scope)
  }
}



Scope_id Lnast_parser::process_scope(Scope_id cur_scope) {
  auto token_scope= (uint8_t)std::stoi(scan_text());
  if (token_scope == cur_scope) {
    return token_scope;
  } else if (token_scope < cur_scope) {
    for(int i = 0; i < CFG_SCOPE_ID_POS; i++) scan_prev();
    //going back to parent scope
    return token_scope;
  } else {//(token_scope > cur_scope)
    for(int i = 0; i < CFG_SCOPE_ID_POS; i++) scan_prev();
    add_subgraph(); //SH:FIXME: deal with subgraph later
    return token_scope;
  }
}

void Lnast_parser::add_subgraph() {
  ;
}

//scan pos will stop at the end of operator token
void Lnast_parser::operator_analysis(Lnast_ntype_id & node_type, int& line_tkcnt) {
  if (scan_is_token(Token_id_op)) { //deal with ()
    node_type = Lnast_ntype_tuple; // must be a tuple op
    I(scan_next_token_is(Token_id_cp));
    scan_next();
    line_tkcnt += 1;
  } else if (scan_is_token(Token_id_colon)) {
      if (scan_next_token_is(Token_id_colon)) { //handle ::{
        node_type = Lnast_ntype_func_def;
        scan_next();
        I(scan_next_token_is(Token_id_ob)); //must be a function def op
        scan_next();
        line_tkcnt += 2;
      } else if (scan_next_token_is(Token_id_eq)) { //handle :=
        node_type = Lnast_ntype_dp_assign;
        scan_next();
        line_tkcnt += 1;
      } else {
        node_type = Lnast_ntype_lable;
      }
  } else if (scan_is_token(Token_id_dot)) { //handle .()
      if (scan_next_token_is(Token_id_op)){
        node_type = Lnast_ntype_func_call; // must be a function call op
        scan_next();
        I(scan_next_token_is(Token_id_cp));
        scan_next();
        line_tkcnt += 2;
      } else {
        node_type = Lnast_ntype_dot;
      }
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "as") {
    node_type = Lnast_ntype_as;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "for") {
    node_type = Lnast_ntype_for;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "while") {
    node_type = Lnast_ntype_while;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "if") {
    node_type = Lnast_ntype_if;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "uif") {
    node_type = Lnast_ntype_uif;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "I") {
    node_type = Lnast_ntype_assert;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "and") {
    node_type = Lnast_ntype_logical_and;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "or") {
    node_type = Lnast_ntype_logical_or;
  } else if (scan_is_token(Token_id_eq)) {
    node_type = Lnast_ntype_pure_assign;
  } else if (scan_is_token(Token_id_and)) {
    node_type = Lnast_ntype_and;
  } else if (scan_is_token(Token_id_or)) {
    node_type = Lnast_ntype_or;
  } else if (scan_is_token(Token_id_xor)) {
    node_type = Lnast_ntype_xor;
  } else if (scan_is_token(Token_id_plus)) {
    node_type = Lnast_ntype_plus;
  } else if (scan_is_token(Token_id_minus)) {
    node_type = Lnast_ntype_minus;
  } else if (scan_is_token(Token_id_mult)) {
    node_type = Lnast_ntype_mult;
  } else if (scan_is_token(Token_id_div)) {
    node_type = Lnast_ntype_div;
  } else if (scan_is_token(Token_id_eq)) {
    node_type = Lnast_ntype_eq;
  } else if (scan_is_token(Token_id_le)) {
    node_type = Lnast_ntype_le;
  } else if (scan_is_token(Token_id_lt)) {
    node_type = Lnast_ntype_lt;
  } else if (scan_is_token(Token_id_ge)) {
    node_type = Lnast_ntype_ge;
  } else if (scan_is_token(Token_id_gt)) {
    node_type = Lnast_ntype_gt;
  } else {
    node_type = Lnast_ntype_invalid;
  }
}

