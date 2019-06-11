
#include "lnast.hpp"

//------------- Language_neutral_ast member function start -----
Language_neutral_ast::Language_neutral_ast(std::string_view _buffer, Lnast_ntype_id ntype_top) : buffer(_buffer) {
  I(!buffer.empty());
  set_root(Lnast_node(ntype_top, Token(), 0));
}


//------------- Lnast_parser member function start -------------

void Lnast_parser::elaborate(){
  lnast = std::make_unique<Language_neutral_ast>(get_buffer(), Lnast_ntype_top);
  build_statements(lnast->get_root(), 0);
  subgraph_scope_sync();
}


void Lnast_parser::build_statements(const Tree_index& tree_idx_top, Scope_id cur_scope){
  auto tree_idx_sts = lnast->add_child(tree_idx_top, Lnast_node(Lnast_ntype_statement, scan_get_token(), cur_scope));
  add_statement(tree_idx_sts, cur_scope);
}

Scope_id Lnast_parser::add_statement(const Tree_index& tree_idx_sts, Scope_id cur_scope) {
  fmt::print("line:{}, statement:{}\n", line_num, scan_text());

  Token          node_token;
  Lnast_ntype_id node_type = Lnast_ntype_invalid;
  Scope_id       token_scope = cur_scope;
  Token_entry    cfg_token_beg;
  Token_entry    cfg_token_end;

  int line_tkcnt = 1;
  while(line_num == scan_calc_lineno()){
    if(scan_is_end())
      return token_scope;

    switch (line_tkcnt) {
    case CFG_NODE_NAME_POS:
      node_token = scan_get_token();  //must be a complete alnum
      break;
    case CFG_SCOPE_ID_POS:{
      token_scope = process_scope(tree_idx_sts, cur_scope); //recursive build sub-graph
      if (token_scope < cur_scope) {
        return token_scope; //go back to parent scope
      }
      break;
    }
    case CFG_TOKEN_POS_BEG:
      cfg_token_beg = scan_token(); //must be a complete alnum
      break;
    case CFG_TOKEN_POS_END:
      cfg_token_end = scan_token(); //must be a complete alnum
      break;
    case CFG_OP_POS_BEG: { //no regular pattern, scan_next() internally case by case
      node_type = operator_analysis(line_tkcnt);
      auto tree_idx_op = add_operator_node(tree_idx_sts, node_token, node_type, cur_scope);
      scan_next(); line_tkcnt += 1;
      add_operator_subtree(tree_idx_op, line_tkcnt, cur_scope);
      break;
    }
    default: ;
    }

    scan_next(); line_tkcnt += 1;
  } //end while

  line_num += 1;
  add_statement(tree_idx_sts, cur_scope);
  return cur_scope;
}


//scan pos start from the end of operator token
Tree_index Lnast_parser::add_operator_node(const Tree_index& tree_idx_sts, Token node_token, Lnast_ntype_id node_type, Scope_id cur_scope){
  //K9   K14   0  59  96   ::{  ___e    K11   $a    $b  %o
  //                         ^          ^^^
  if (node_type == Lnast_ntype_pure_assign || node_type == Lnast_ntype_as) {
    if(scan_peep_is_token(Token_id_reference, CFG_OP_FUNC_TMP_REF_RANGE) && scan_peep_sview(CFG_OP_FUNC_TMP_REF_RANGE).substr(1,3) == "___")
      return tree_idx_sts; // don't create a new operator child.
  } else if (node_type == Lnast_ntype_func_def) { //connect to sub-graph-statements
    for (const auto &it:lnast->depth_preorder(lnast->get_root())) {
      auto it_node_name = lnast->get_data(it).node_token.get_text(buffer) ;
      auto it_node_type = lnast->get_data(it).node_type;
      auto it_scope     = lnast->get_data(it).scope;
      if (it_node_name == scan_peep_sview(CFG_OP_FUNC_ROOT_RANGE) && it_node_type == Lnast_ntype_statement)
        return lnast->add_child(it, Lnast_node(Lnast_ntype_func_def, scan_get_token(CFG_SCOPE_OP_TOKEN1ST_RANGE), it_scope));
    }
    I(false); //must found the function definition in the lnast traverse
  }

  return lnast->add_child(tree_idx_sts, Lnast_node(node_type, node_token, cur_scope)); //connect to top-statements
}


//scan pos start: first operand token, stop: last operand
void Lnast_parser::add_operator_subtree(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {
  I(line_tkcnt > CFG_OP_POS_BEG);
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  auto nt = lnast->get_data(tree_idx_op).node_type;

  if (nt == Lnast_ntype_pure_assign || nt == Lnast_ntype_dp_assign || nt == Lnast_ntype_as || nt == Lnast_ntype_tuple) {//SH:FIXME: handle tuple seperately
    process_assign_like_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_lable) {
    process_lable_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_func_call) {
    process_func_call_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_func_def) {
    process_func_def_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_if || nt == Lnast_ntype_uif) {
    process_if_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_statement) {
    process_function_name_replacement(tree_idx_op, line_tkcnt, cur_scope);
  } else {
    process_binary_op(tree_idx_op, line_tkcnt, cur_scope);
  }
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_func_def_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope){
  //K9   K14   0  59  96   ::{  ___e    K11   $a    $b  %o
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  scan_next(); line_tkcnt += 1; //@ K11, don't create it
  I(scan_text().at(0) == 'K');

  scan_next(); line_tkcnt += 1; //@ $a
  auto local_line_num = scan_calc_lineno();
  while (scan_calc_lineno() == local_line_num) {
    I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
    lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
    scan_next(); line_tkcnt += 1; //@ $b -> %o ...
  }
  scan_prev(); //for the final dummy scan_next() in while loop
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_func_call_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope){
  //K17  K18  0  98  121  .()  ___g  fun1  ___h   ___i
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  scan_next(); line_tkcnt += 1; //@ fun1

  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  scan_next(); line_tkcnt += 1; //@ ___h

  auto local_line_num = scan_calc_lineno();
  while (scan_calc_lineno() == local_line_num) {
    I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
    lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
    scan_next(); line_tkcnt += 1; //@ ___i -> ___j ...
  }
  scan_prev(); //for the final dummy scan_next() in while loop
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_if_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope){
  ;
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_binary_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  scan_next(); line_tkcnt += 1;
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  scan_next(); line_tkcnt += 1;
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
}


void Lnast_parser::process_function_name_replacement(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id) {
  if(scan_peep_is_token(Token_id_reference, 1) && scan_peep_sview(1).substr(1,3) == "___"){
    //won't create new node, just search and replace the correct func name
    for (const auto &it:lnast->depth_preorder(lnast->get_root())) {
      auto it_node_name = lnast->get_data(it).node_token.get_text(buffer) ;
      auto it_node_type = lnast->get_data(it).node_type;
      if (it_node_name == scan_next_sview().substr(1) && it_node_type == Lnast_ntype_ref){
        fmt::print("it_node_name:{}\n", it_node_name);
        fmt::print("scan_next_sview():{}\n", scan_next_sview());
        fmt::print("original node name:{}\n", lnast->get_data(it).node_token.get_text(buffer));
        lnast->get_data(it).node_token = scan_get_token();
        fmt::print("node name become:{}\n", lnast->get_data(it).node_token.get_text(buffer));
        scan_next(); line_tkcnt += 1;
        return;
      }
    }
    I(false); //must found the function definition in the lnast traverse
  }
}
//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_assign_like_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {

  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  fmt::print("add operand:{}\n", scan_text());
  scan_next(); line_tkcnt += 1;
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input) || scan_is_token(Token_id_reference));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  fmt::print("add operand:{}\n", scan_text());
}


//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_lable_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  scan_next(); line_tkcnt += 1;

  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  if (scan_is_token(Token_id_alnum) && scan_sview() == "__bits") {
    auto tree_idx_attr_bits = lnast->add_child(tree_idx_op, Lnast_node(Lnast_ntype_attr_bits, scan_get_token(), cur_scope));
    scan_next(); line_tkcnt += 1;
    I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
    lnast->add_child(tree_idx_attr_bits, Lnast_node(Lnast_ntype_const, scan_get_token(), cur_scope));
  } else {
    lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
    scan_next(); line_tkcnt += 1;
    I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
    lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  }
}


Scope_id Lnast_parser::process_scope(const Tree_index& tree_idx_sts, Scope_id cur_scope) {
  auto token_scope = (uint8_t)std::stoi(scan_text());
  fmt::print("token_scope:{}, cur_scope:{}\n", token_scope, cur_scope);
  if(token_scope > cur_scope) {
    for(int i = 0; i < CFG_SCOPE_ID_POS-1; i++) // re-parse
      scan_prev();
    //fmt::print("scan_text is now {}\n", scan_text());
    add_subgraph(tree_idx_sts, token_scope, cur_scope);
  }
  return token_scope;
}

void Lnast_parser::add_subgraph(const Tree_index& tree_idx_sts, Scope_id new_scope, Scope_id cur_scope) {
  auto tree_idx_subgraph = lnast->add_child(tree_idx_sts, Lnast_node(Lnast_ntype_sub, scan_get_token(), new_scope));
  build_statements(tree_idx_subgraph, new_scope);
}

Lnast_ntype_id  Lnast_parser::operand_analysis() {
  if (scan_sview().at(0) == '0' || scan_sview().at(0) == '-')
    return Lnast_ntype_const;
  else if (scan_sview().at(0) == '$')
    return Lnast_ntype_input;
  else if (scan_sview().at(0) == '%')
    return Lnast_ntype_output;
  else if (scan_sview().at(0) == '@')
    return Lnast_ntype_reg;
  else
    return Lnast_ntype_ref;
}

//scan pos will stop at the end of operator token
Lnast_ntype_id Lnast_parser::operator_analysis(int& line_tkcnt) {
  Lnast_ntype_id node_type;
  if (scan_is_token(Token_id_op)) { //deal with ()
    node_type = Lnast_ntype_tuple; // must be a tuple op
    I(scan_peep_is_token(Token_id_cp, 1));
    scan_next();
    line_tkcnt += 1;
  } else if (scan_is_token(Token_id_colon)) {
      if (scan_peep_is_token(Token_id_colon, 1)) { //handle ::{
        node_type = Lnast_ntype_func_def;
        scan_next();
        I(scan_peep_is_token(Token_id_ob, 1)); //must be a function def op
        scan_next();
        line_tkcnt += 2;
      } else if (scan_peep_is_token(Token_id_eq, 1)) { //handle :=
        node_type = Lnast_ntype_dp_assign;
        scan_next();
        line_tkcnt += 1;
      } else {
        node_type = Lnast_ntype_lable;
      }
  } else if (scan_is_token(Token_id_dot)) { //handle .()
      if (scan_peep_is_token(Token_id_op, 1)){
        node_type = Lnast_ntype_func_call; // must be a function call op
        scan_next();
        I(scan_peep_is_token(Token_id_cp, 1));
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
  return node_type;
}

std::string Lnast_parser::ntype_dbg(Lnast_ntype_id ntype) {
  std::string ntype_str;
  switch (ntype) {
    case Lnast_ntype_invalid:           ntype_str = "invalid"      ; break;
    case Lnast_ntype_statement:         ntype_str = "statement"    ; break;
    case Lnast_ntype_pure_assign:       ntype_str = "pure_assign"  ; break;
    case Lnast_ntype_dp_assign:         ntype_str = "dp_assign"    ; break;
    case Lnast_ntype_as:                ntype_str = "as"           ; break;
    case Lnast_ntype_lable:             ntype_str = "lable"        ; break;
    case Lnast_ntype_dot:               ntype_str = "dot"          ; break;
    case Lnast_ntype_logical_and:       ntype_str = "logical_and"  ; break;
    case Lnast_ntype_logical_or:        ntype_str = "logical_or"   ; break;
    case Lnast_ntype_and:               ntype_str = "and"          ; break;
    case Lnast_ntype_or:                ntype_str = "or"           ; break;
    case Lnast_ntype_xor:               ntype_str = "xor"          ; break;
    case Lnast_ntype_plus:              ntype_str = "plus"         ; break;
    case Lnast_ntype_minus:             ntype_str = "minus"        ; break;
    case Lnast_ntype_mult:              ntype_str = "mult"         ; break;
    case Lnast_ntype_div:               ntype_str = "div"          ; break;
    case Lnast_ntype_eq:                ntype_str = "eq"           ; break;
    case Lnast_ntype_lt:                ntype_str = "lt"           ; break;
    case Lnast_ntype_le:                ntype_str = "le"           ; break;
    case Lnast_ntype_gt:                ntype_str = "gt"           ; break;
    case Lnast_ntype_ge:                ntype_str = "ge"           ; break;
    case Lnast_ntype_tuple:             ntype_str = "tuple"        ; break;
    case Lnast_ntype_ref:               ntype_str = "ref"          ; break;
    case Lnast_ntype_const:             ntype_str = "const"        ; break;
    case Lnast_ntype_input:             ntype_str = "input"        ; break;
    case Lnast_ntype_output:            ntype_str = "output"       ; break;
    case Lnast_ntype_reg:               ntype_str = "reg"          ; break;
    case Lnast_ntype_attr_bits:         ntype_str = "attr_bits"    ; break;
    case Lnast_ntype_assert:            ntype_str = "assert"       ; break;
    case Lnast_ntype_if:                ntype_str = "if"           ; break;
    case Lnast_ntype_uif:               ntype_str = "uif"          ; break;
    case Lnast_ntype_for:               ntype_str = "for"          ; break;
    case Lnast_ntype_while:             ntype_str = "while"        ; break;
    case Lnast_ntype_func_call:         ntype_str = "func_call"    ; break;
    case Lnast_ntype_func_def:          ntype_str = "func_def"     ; break;
    case Lnast_ntype_sub:               ntype_str = "sub"          ; break;
    case Lnast_ntype_top:               ntype_str = "top"          ; break;
    default:;
  }
  return ntype_str;
}

void Lnast_parser::subgraph_scope_sync() {
  for (const auto &it:lnast->depth_preorder(lnast->get_root())) {
    auto parent = lnast->get_parent(it);
    if (lnast->get_data(it).scope < lnast->get_data(parent).scope)
      lnast->get_data(it).scope = lnast->get_data(parent).scope;
  }
}
