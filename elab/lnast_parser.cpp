
#include "lnast_parser.hpp"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

void Lnast_parser::elaborate(){
  lnast = std::make_unique<Language_neutral_ast<Lnast_node>>(get_buffer());
  lnast->set_root(Lnast_node(Lnast_ntype_top, Token(), 0));
  if(scan_calc_lineno() == 1) //SH:FIXME: ask Akash to remove the "END" line if you really don't need it
    scan_next();

  build_statements(lnast->get_root(), 0);
  subgraph_scope_sync();
}


void Lnast_parser::build_statements(const Tree_index& tree_idx_top, Scope_id cur_scope){
  uint32_t knum = (uint32_t)std::stoi(scan_text().substr(1));  //the token must be a complete alnum
  auto tree_idx_sts = lnast->add_child(tree_idx_top, Lnast_node(Lnast_ntype_statement, Token(), cur_scope, knum));
  add_statement(tree_idx_sts, cur_scope);
}

Scope_id Lnast_parser::add_statement(const Tree_index& tree_idx_sts, Scope_id cur_scope) {
  fmt::print("line:{}, statement:{}\n", line_num, scan_text());

  Token          target;
  Lnast_ntype_id type = Lnast_ntype_invalid;
  Scope_id       token_scope = cur_scope;
  uint32_t       knum = 0;
  Token_entry    cfg_token_beg;
  Token_entry    cfg_token_end;

  int line_tkcnt = 1;
  while(line_num == scan_calc_lineno()){
    if(scan_is_end())
      return token_scope;


    switch (line_tkcnt) {
    case CFG_NODE_NAME_POS:{
      knum = (uint32_t)std::stoi(scan_text().substr(1));
      break;
    }
    case CFG_SCOPE_ID_POS:{
      token_scope = process_scope(tree_idx_sts, cur_scope); //recursive build sub-graph
      if (token_scope < cur_scope)
        return token_scope; //go back to parent scope
      break;
    }
    case CFG_TOKEN_POS_BEG:{
      cfg_token_beg = scan_token();
      break;
    }
    case CFG_TOKEN_POS_END:{
      cfg_token_end = scan_token();
      break;
    }
    case CFG_OP_POS_BEG: { //no regular pattern, scan_next() internally case by case
      type   = operator_analysis(line_tkcnt);

      if (type == Lnast_ntype_func_def) {
        auto new_scope = next_new_scope;
        add_subgraph(tree_idx_sts, new_scope, cur_scope);
        next_new_scope += 1;
        for(int i = 0; i < CFG_OP_POS_BEG-1; i++) // re-parse
          scan_prev();
        break;
      }

      scan_next(); line_tkcnt += 1;

      I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
      target = scan_get_token();

      auto tree_idx_op = add_operator_node(tree_idx_sts, target, type, cur_scope, knum);

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
Tree_index Lnast_parser::add_operator_node(const Tree_index& tree_idx_sts, Token token, Lnast_ntype_id type, Scope_id cur_scope, uint32_t knum){
  //K9   K14   0  59  96   ::{  ___e    K11   $a    $b  %o
  //                         ^          ^^^
  //K14  K15   0  59  96   =    fun1    \___e
  //                                    ^^^^

  if (type == Lnast_ntype_pure_assign || type == Lnast_ntype_as) {
    if(scan_peep_is_token(Token_id_reference, CFG_TARGET_TMP_REF_RANGE) && scan_peep_sview(CFG_TARGET_TMP_REF_RANGE).substr(1,3) == "___"){
      return tree_idx_sts; // don't create a new operator child.
    }

  //} else if (type == Lnast_ntype_func_def) { //connect to sub-graph-statements
  //  for (const auto &it:lnast->depth_preorder(lnast->get_root())) {
  //    auto it_knum   = lnast->get_data(it).knum;
  //    auto it_type   = lnast->get_data(it).type;
  //    auto it_scope  = lnast->get_data(it).scope;

  //    std::string knum_in_func_def(scan_peep_sview(CFG_OP_FUNC_ROOT_RANGE).substr(1));

  //    if (it_knum == std::stoi(knum_in_func_def) && it_type == Lnast_ntype_statement){
  //      return lnast->add_child(it, Lnast_node(Lnast_ntype_func_def, scan_get_token(), it_scope));
  //    }
  //  }
  //  I(false); //must found the function definition in the lnast traverse
  }

  return lnast->add_child(tree_idx_sts, Lnast_node(type, token, cur_scope)); //connect to top-statements
}


//scan pos start: first operand token, stop: last operand
void Lnast_parser::add_operator_subtree(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {
  fmt::print("token is :{}\n", scan_text());
  auto nt = lnast->get_data(tree_idx_op).type;

  if (nt == Lnast_ntype_pure_assign || nt == Lnast_ntype_dp_assign || nt == Lnast_ntype_as || nt == Lnast_ntype_tuple) {//SH:FIXME: handle tuple seperately
    process_assign_like_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_label) {
    process_label_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_func_call) {
    process_func_call_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_func_def) {
    process_func_def_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_if || nt == Lnast_ntype_uif) {
    process_if_op(tree_idx_op, line_tkcnt, cur_scope);
  } else if (nt == Lnast_ntype_statement) {
    process_function_name_replacement(tree_idx_op, line_tkcnt);
  } else {
    process_binary_op(tree_idx_op, line_tkcnt, cur_scope);
  }
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_func_def_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope){
  //K9   K14   0  59  96   ::{  ___e    K11   $a    $b  %o
  I(scan_text().at(0) == 'K');//@ K11, don't create it

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
}


void Lnast_parser::process_function_name_replacement(const Tree_index& tree_idx_op, int& line_tkcnt) {
  if(scan_is_token(Token_id_reference) && scan_sview().substr(1,3) == "___"){
    //won't create new node, just search and replace the correct func name
    for (const auto &it:lnast->depth_preorder()) {
      auto it_name = lnast->get_data(it).token.get_text(buffer) ;
      auto it_type = lnast->get_data(it).type;
      if (it_name == scan_sview().substr(1) && it_type == Lnast_ntype_func_def){
        fmt::print("original node name:{}\n", lnast->get_data(it).token.get_text(buffer));
        lnast->get_data(it).token = scan_get_token(-1);
        fmt::print("new node name:{}\n", lnast->get_data(it).token.get_text(buffer));
        return;
      }
    }
    I(false); //must found the function definition in the lnast traverse
  }
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_assign_like_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input) || scan_is_token(Token_id_reference));
  lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  fmt::print("add operand:{}\n", scan_text());
}


//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_label_op(const Tree_index& tree_idx_op, int& line_tkcnt, Scope_id cur_scope) {

  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  if (scan_is_token(Token_id_alnum) && scan_sview() == "__bits") {
    auto tree_idx_attr_bits = lnast->add_child(tree_idx_op, Lnast_node(Lnast_ntype_attr_bits, scan_get_token(), cur_scope));
    scan_next(); line_tkcnt += 1;
    I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
    lnast->add_child(tree_idx_attr_bits, Lnast_node(Lnast_ntype_const, scan_get_token(), cur_scope));
  } else { //case of function argument assignment
    lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
    scan_next(); line_tkcnt += 1;
    I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
    lnast->add_child(tree_idx_op, Lnast_node(operand_analysis(), scan_get_token(), cur_scope));
  }
}


Scope_id Lnast_parser::process_scope(const Tree_index& tree_idx_sts, Scope_id cur_scope) {
  auto token_scope = (uint8_t)std::stoi(scan_text());
  //fmt::print("token_scope:{}, cur_scope:{}\n", token_scope, cur_scope);
  //if(token_scope > cur_scope) {
  //  for(int i = 0; i < CFG_SCOPE_ID_POS-1; i++) // re-parse
  //    scan_prev();
  //  add_subgraph(tree_idx_sts, token_scope, cur_scope);
  //}
  return token_scope;
}

void Lnast_parser::add_subgraph(const Tree_index& tree_idx_sts, Scope_id new_scope, Scope_id cur_scope) {

  auto knum = (uint32_t)std::stoi(scan_text().substr(1));
  fmt::print("build subgraph:{}\n", new_scope);
  auto tree_idx_subgraph = lnast->add_child(tree_idx_sts, Lnast_node(Lnast_ntype_sub, Token(), new_scope, knum));
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

//SH:Todo:add not token, example: !foo
//scan pos will stop at the end of operator token
Lnast_ntype_id Lnast_parser::operator_analysis(int& line_tkcnt) {
  Lnast_ntype_id type;
  if (scan_is_token(Token_id_op)) { //deal with ()
    type = Lnast_ntype_tuple; // must be a tuple op
    I(scan_peep_is_token(Token_id_cp, 1));
    scan_next();
    line_tkcnt += 1;
  } else if (scan_is_token(Token_id_colon)) {
      if (scan_peep_is_token(Token_id_colon, 1)) { //handle ::{
        type = Lnast_ntype_func_def;
        scan_next();
        I(scan_peep_is_token(Token_id_ob, 1)); //must be a function def op
        scan_next();
        line_tkcnt += 2;
      } else if (scan_peep_is_token(Token_id_eq, 1)) { //handle :=
        type = Lnast_ntype_dp_assign;
        scan_next();
        line_tkcnt += 1;
      } else {
        type = Lnast_ntype_label;
      }
  } else if (scan_is_token(Token_id_dot)) { //handle .()
      if (scan_peep_is_token(Token_id_op, 1)){
        type = Lnast_ntype_func_call; // must be a function call op
        scan_next();
        I(scan_peep_is_token(Token_id_cp, 1));
        scan_next();
        line_tkcnt += 2;
      } else {
        type = Lnast_ntype_dot;
      }
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "as") {
    type = Lnast_ntype_as;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "for") {
    type = Lnast_ntype_for;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "while") {
    type = Lnast_ntype_while;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "if") {
    type = Lnast_ntype_if;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "uif") {
    type = Lnast_ntype_uif;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "I") {
    type = Lnast_ntype_assert;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "and") {
    type = Lnast_ntype_logical_and;
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "or") {
    type = Lnast_ntype_logical_or;
  } else if (scan_is_token(Token_id_eq)) {
    type = Lnast_ntype_pure_assign;
  } else if (scan_is_token(Token_id_and)) {
    type = Lnast_ntype_and;
  } else if (scan_is_token(Token_id_or)) {
    type = Lnast_ntype_or;
  } else if (scan_is_token(Token_id_xor)) {
    type = Lnast_ntype_xor;
  } else if (scan_is_token(Token_id_plus)) {
    type = Lnast_ntype_plus;
  } else if (scan_is_token(Token_id_minus)) {
    type = Lnast_ntype_minus;
  } else if (scan_is_token(Token_id_mult)) {
    type = Lnast_ntype_mult;
  } else if (scan_is_token(Token_id_div)) {
    type = Lnast_ntype_div;
  } else if (scan_is_token(Token_id_eq)) {
    type = Lnast_ntype_eq;
  } else if (scan_is_token(Token_id_le)) {
    type = Lnast_ntype_le;
  } else if (scan_is_token(Token_id_lt)) {
    type = Lnast_ntype_lt;
  } else if (scan_is_token(Token_id_ge)) {
    type = Lnast_ntype_ge;
  } else if (scan_is_token(Token_id_gt)) {
    type = Lnast_ntype_gt;
  } else {
    type = Lnast_ntype_invalid;
  }
  return type;
}

void Lnast_parser::setup_ntype_str_mapping(){
  ntype2str [Lnast_ntype_invalid]     = "invalid"    ;
  ntype2str [Lnast_ntype_statement]   = "statement"  ;
  ntype2str [Lnast_ntype_pure_assign] = "pure_assign";
  ntype2str [Lnast_ntype_dp_assign]   = "dp_assign"  ;
  ntype2str [Lnast_ntype_as]          = "as"         ;
  ntype2str [Lnast_ntype_label]       = "label"      ;
  ntype2str [Lnast_ntype_dot]         = "dot"        ;
  ntype2str [Lnast_ntype_logical_and] = "logical_and";
  ntype2str [Lnast_ntype_logical_or]  = "logical_or" ;
  ntype2str [Lnast_ntype_and]         = "and"        ;
  ntype2str [Lnast_ntype_or]          = "or"         ;
  ntype2str [Lnast_ntype_xor]         = "xor"        ;
  ntype2str [Lnast_ntype_plus]        = "plus"       ;
  ntype2str [Lnast_ntype_minus]       = "minus"      ;
  ntype2str [Lnast_ntype_mult]        = "mult"       ;
  ntype2str [Lnast_ntype_div]         = "div"        ;
  ntype2str [Lnast_ntype_eq]          = "eq"         ;
  ntype2str [Lnast_ntype_lt]          = "lt"         ;
  ntype2str [Lnast_ntype_le]          = "le"         ;
  ntype2str [Lnast_ntype_gt]          = "gt"         ;
  ntype2str [Lnast_ntype_ge]          = "ge"         ;
  ntype2str [Lnast_ntype_tuple]       = "tuple"      ;
  ntype2str [Lnast_ntype_ref]         = "ref"        ;
  ntype2str [Lnast_ntype_const]       = "const"      ;
  ntype2str [Lnast_ntype_input]       = "input"      ;
  ntype2str [Lnast_ntype_output]      = "output"     ;
  ntype2str [Lnast_ntype_reg]         = "reg"        ;
  ntype2str [Lnast_ntype_attr_bits]   = "attr_bits"  ;
  ntype2str [Lnast_ntype_assert]      = "assert"     ;
  ntype2str [Lnast_ntype_if]          = "if"         ;
  ntype2str [Lnast_ntype_else]        = "else"       ;
  ntype2str [Lnast_ntype_cond]        = "cond"       ;
  ntype2str [Lnast_ntype_uif]         = "uif"        ;
  ntype2str [Lnast_ntype_for]         = "for"        ;
  ntype2str [Lnast_ntype_while]       = "while"      ;
  ntype2str [Lnast_ntype_func_call]   = "func_call"  ;
  ntype2str [Lnast_ntype_func_def]    = "func_def"   ;
  ntype2str [Lnast_ntype_sub]         = "sub"        ;
  ntype2str [Lnast_ntype_top]         = "top"        ;
}

std::string Lnast_parser::ntype_dbg(Lnast_ntype_id ntype) {
  return ntype2str[ntype];
}

void Lnast_parser::subgraph_scope_sync() {
  for (const auto &it:lnast->depth_preorder()) {
    auto parent = lnast->get_parent(it);
    if (lnast->get_data(it).scope < lnast->get_data(parent).scope)
      lnast->get_data(it).scope = lnast->get_data(parent).scope;
  }
}


