#include "lnast_parser.hpp"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

void Lnast_parser::elaborate(){
  lnast = std::make_unique<Lnast>(get_buffer());
  lnast->set_root(Lnast_node(Lnast_ntype_top, Token()));
  process_statements_op(lnast->get_root(), 1);
  build_lnast();
}

void Lnast_parser::build_lnast() {
  //fmt::print("line:{}, statement:{}\n", line_num, scan_text());

  uint32_t    cfg_nidx    = 0;
  uint32_t    cfg_nparent = 0;
  uint32_t    cfg_nchild  = 0;
  Token       cfg_token_beg;
  Token       cfg_token_end;
  Token       loc;
  Token       target_name;
  Lnast_ntype type = Lnast_ntype_invalid;
  mmap_lib::Tree_index opr_parent_node; //opr means operator

  line_tkcnt = 1;
  while (line_num == scan_get_token().line) {
    if(unlikely(scan_is_end()))
      return;

    //ask Akash to remove the "END" if you really don't need it
    if(unlikely(line_num == 0)) {
      walk_next_line();
      continue;
    }


    I(line_tkcnt == CFG_IDX_POS);
    fmt::print("pos1 scan_text:{}\n", scan_text());
    cfg_nidx = (uint32_t)std::stoi(scan_text());
    walk_next_token();


    I(line_tkcnt == CFG_PARENT_POS);
    cfg_nparent = (uint32_t)std::stoi(scan_text());
    walk_next_token();
    opr_parent_node = cfg_parent_id2lnast_node[cfg_nparent];


    I(line_tkcnt == CFG_CHILD_POS);
    cfg_nchild = (uint32_t)std::stoi(scan_text());
    walk_next_token();


    I(line_tkcnt == CFG_TOKEN_POS_BEG);
    if (unlikely(scan_text().substr(0,4) == "SEQ0")){
      walk_next_line();
      continue;
    } else if (unlikely(scan_text().substr(0,3) == "SEQ")){
      process_statements_op(opr_parent_node, cfg_nidx);
      walk_next_line();
      continue;
    } else {
      cfg_token_beg = scan_get_token();
      walk_next_token();
    }


    I(line_tkcnt == CFG_TOKEN_POS_END);
    cfg_token_end = scan_get_token();
    walk_next_token();


    I(line_tkcnt == CFG_OP_POS);
    //no regular pattern, might walk_next_token() internally case by case
    type = operator_analysis();
    walk_next_token(); // go to the target_name of operator


    I(token_is_valid_ref());
    target_name = scan_get_token();


    if(type == Lnast_ntype_pure_assign) {
      if (unlikely(function_name_correction(type, target_name))){
        walk_next_token();
        walk_next_line();
        continue;
      }
    }

    auto tree_idx_opr = process_operator_node(opr_parent_node, type, cfg_nidx, target_name);

    //don't need to build subtree at the cfg line of if ___k condition
    if(unlikely(tree_idx_opr.is_invalid())){
      walk_next_line();
      continue;
    }

    walk_next_token(); //go to 1st operand

    I(!tree_idx_opr.is_invalid());
    add_operator_subtree(tree_idx_opr, target_name);
    walk_next_line();
  }
}

void Lnast_parser::process_statements_op(const mmap_lib::Tree_index& parent_of_sts, uint32_t self_idx){
  if(lnast->get_data(parent_of_sts).type == Lnast_ntype_top){
    auto tree_top_sts = lnast->add_child(parent_of_sts, Lnast_node(Lnast_ntype_statements, Token()));
    cfg_parent_id2lnast_node[self_idx] = tree_top_sts;
  } else if (lnast->get_data(parent_of_sts).type == Lnast_ntype_if){
    if(!buffer_if_condition_used){
      auto if_csts = lnast->add_child(parent_of_sts, Lnast_node(Lnast_ntype_cstatements, Token()));
      cfg_parent_id2lnast_node[self_idx] = if_csts;
      lnast->add_child(parent_of_sts, Lnast_node(Lnast_ntype_cond, buffer_if_condition));
      buffer_if_condition_used = true;
    } else { // normal statements
      auto if_sts = lnast->add_child(parent_of_sts, Lnast_node(Lnast_ntype_statements, Token()));
      cfg_parent_id2lnast_node[self_idx] = if_sts;
    }
  } else if (lnast->get_data(parent_of_sts).type == Lnast_ntype_func_def) {
    auto tree_func_def_sts = lnast->add_child(parent_of_sts, Lnast_node(Lnast_ntype_statements, Token()));
    cfg_parent_id2lnast_node[self_idx] = tree_func_def_sts;
  }
}


//scan pos start from the end of operator token
mmap_lib::Tree_index Lnast_parser::process_operator_node(const mmap_lib::Tree_index& opr_parent_node, Lnast_ntype type, uint32_t self_idx, const Token& target_name){
  if (type == Lnast_ntype_func_def) {
    auto func_def_root = lnast->add_child(opr_parent_node, Lnast_node(Lnast_ntype_func_def, Token()));
    cfg_parent_id2lnast_node[self_idx] = func_def_root;
    return func_def_root;
  } else if (type == Lnast_ntype_if) {
    auto if_idx = lnast->add_child(opr_parent_node, Lnast_node(Lnast_ntype_if, Token()));
    cfg_parent_id2lnast_node[self_idx] = if_idx;
    buffer_if_condition = target_name;
    buffer_if_condition_used = false;
    return mmap_lib::Tree_index(-1, -1);
  } else if (type == Lnast_ntype_elif) {
    buffer_if_condition = target_name;
    buffer_if_condition_used = false;
    return mmap_lib::Tree_index(-1, -1);
  }

  return lnast->add_child(opr_parent_node, Lnast_node(type, Token()));
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::add_operator_subtree(const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name) {
  //fmt::print("token is :{}\n", scan_text());

  auto nt = lnast->get_data(tree_idx_opr).type;


  if (nt == Lnast_ntype_pure_assign || nt == Lnast_ntype_dp_assign || nt == Lnast_ntype_as || nt == Lnast_ntype_tuple) {//sh:fixme: handle tuple seperately
    process_assign_like_op(tree_idx_opr, target_name);
  } else if (nt == Lnast_ntype_label) {
    process_label_op(tree_idx_opr, target_name);
  } else if (nt == Lnast_ntype_func_call) {
    process_func_call_op(tree_idx_opr, target_name);
  } else if (nt == Lnast_ntype_func_def) {
    process_func_def_op(tree_idx_opr, target_name);
  } else if (nt == Lnast_ntype_if || nt == Lnast_ntype_uif) {
    process_if_op(tree_idx_opr, target_name);
  } else {
    process_binary_op(tree_idx_opr, target_name);
  }
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_func_def_op(const mmap_lib::Tree_index& tree_idx_fdef, const Token& target_name){
  //10  1  8  59  96  ::{  ___e   $a    $b  %o

  buffer_tmp_func_name_idx = lnast->add_child(tree_idx_fdef, Lnast_node(Lnast_ntype_ref, target_name));

  walk_next_token(); //sh:fixme: jump across strange null in func_def cfg
  auto local_line_num = scan_calc_lineno();
  while (scan_calc_lineno() == local_line_num) {
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_fdef, Lnast_node(operand_analysis(), scan_get_token()));
    walk_next_token(); //go to $a -> go to $b -> ...
  }
  scan_prev(); //for the final dummy scan_next() in while loop
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_func_call_op(const mmap_lib::Tree_index& tree_idx_fcall, const Token& target_name){
  //true function call case: K17  K18  0  98  121  .()  ___g  fun1  ___h   ___i
  //fake function call case: K19  K20  0  123 129  .()  ___j  $a

  lnast->add_child(tree_idx_fcall, Lnast_node(Lnast_ntype_ref, target_name));

  I(token_is_valid_ref());
  lnast->add_child(tree_idx_fcall, Lnast_node(operand_analysis(), scan_get_token()));
  walk_next_token(); //go to ___h

  if(scan_calc_lineno() == line_num + 1){
    //SH:FIXME: only one operand, fake function call for now!!!
    lnast->ref_data(tree_idx_fcall)->type = Lnast_ntype_pure_assign;
    scan_prev();
    return;
  }

  auto local_line_num = scan_calc_lineno();
  while (scan_calc_lineno() == local_line_num) {
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_fcall, Lnast_node(operand_analysis(), scan_get_token()));
    walk_next_token(); //go to ___i -> ___j ...
  }
  scan_prev(); //for the final dummy scan_next() in while loop
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_if_op(const mmap_lib::Tree_index& tree_idx_if, const Token& cond){
  //lnast->add_child(parent_of_sts, Lnast_node(Lnast_ntype_cond, buffer_if_condition));
}



//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_binary_op(const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name) {
  lnast->add_child(tree_idx_opr, Lnast_node(Lnast_ntype_ref, target_name));
  I(token_is_valid_ref());
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
  walk_next_token(); //go to 2nd operand
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
}



//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_assign_like_op(const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name) {
  lnast->add_child(tree_idx_opr, Lnast_node(Lnast_ntype_ref, target_name));
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input) || scan_is_token(Token_id_reference));
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_label_op(const mmap_lib::Tree_index& tree_idx_label, const Token& target_name) {
  lnast->add_child(tree_idx_label, Lnast_node(Lnast_ntype_ref, target_name));
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  if (scan_is_token(Token_id_alnum) && scan_sview() == "__bits") {
    auto tree_idx_attr_bits = lnast->add_child(tree_idx_label, Lnast_node(Lnast_ntype_attr_bits, scan_get_token()));
    walk_next_token();
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_attr_bits, Lnast_node(Lnast_ntype_const, scan_get_token()));
  } else { //case of function argument assignment
    lnast->add_child(tree_idx_label, Lnast_node(operand_analysis(), scan_get_token()));
    walk_next_token();
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_label, Lnast_node(operand_analysis(), scan_get_token()));
  }
}


Lnast_ntype  Lnast_parser::operand_analysis() {
  if (scan_sview().at(0) == '0' || scan_sview().at(0) == '-')
    return Lnast_ntype_const;
  else
    return Lnast_ntype_ref;//includes io and reg such as $a, %b, @r
}

bool Lnast_parser::function_name_correction(Lnast_ntype type, const Token& target_name) {
  I(type == Lnast_ntype_pure_assign);
  if (scan_peep_is_token(Token_id_reference, 1) && scan_peep_sview(1).substr(1,3) == "___") {
    lnast->ref_data(buffer_tmp_func_name_idx)->token = target_name;
    return true;
  }
  return false;
}

//SH:todo:add not token, example: !foo
//scan pos will stop at the end of operator token
Lnast_ntype Lnast_parser::operator_analysis() {
  Lnast_ntype type;
  if (scan_is_token(Token_id_op)) { //deal with ()
    type = Lnast_ntype_tuple; // must be a tuple op
    I(scan_peep_is_token(Token_id_cp, 1));
    walk_next_token();
  } else if (scan_is_token(Token_id_colon)) {
      if (scan_peep_is_token(Token_id_colon, 1)) { //handle ::{
        type = Lnast_ntype_func_def;
        walk_next_token();
        I(scan_peep_is_token(Token_id_ob, 1)); //must be a function def op
        walk_next_token();
      } else if (scan_peep_is_token(Token_id_eq, 1)) { //handle :=
        type = Lnast_ntype_dp_assign;
        walk_next_token();
      } else {
        type = Lnast_ntype_label;
      }
  } else if (scan_is_token(Token_id_dot)) { //handle .()
      if (scan_peep_is_token(Token_id_op, 1)){
        type = Lnast_ntype_func_call; // must be a function call op
        walk_next_token();
        I(scan_peep_is_token(Token_id_cp, 1));
        walk_next_token();
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
  } else if (scan_is_token(Token_id_alnum) && scan_text() == "elif"){
    type = Lnast_ntype_elif;
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
  } else if (scan_is_token(Token_id_same)) {
    type = Lnast_ntype_same;
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
  ntype2str [Lnast_ntype_statements]  = "sts"  ;
  ntype2str [Lnast_ntype_cstatements] = "csts"  ;
  ntype2str [Lnast_ntype_pure_assign] = "=";
  ntype2str [Lnast_ntype_dp_assign]   = ":="  ;
  ntype2str [Lnast_ntype_as]          = "as"         ;
  ntype2str [Lnast_ntype_label]       = "label"      ;
  ntype2str [Lnast_ntype_dot]         = "dot"        ;
  ntype2str [Lnast_ntype_logical_and] = "and";
  ntype2str [Lnast_ntype_logical_or]  = "or" ;
  ntype2str [Lnast_ntype_and]         = "&"        ;
  ntype2str [Lnast_ntype_or]          = "|"         ;
  ntype2str [Lnast_ntype_xor]         = "^"        ;
  ntype2str [Lnast_ntype_plus]        = "+"       ;
  ntype2str [Lnast_ntype_minus]       = "-"      ;
  ntype2str [Lnast_ntype_mult]        = "*"       ;
  ntype2str [Lnast_ntype_div]         = "/"        ;
  ntype2str [Lnast_ntype_eq]          = "="         ;
  ntype2str [Lnast_ntype_same]        = "=="         ;
  ntype2str [Lnast_ntype_lt]          = "<"         ;
  ntype2str [Lnast_ntype_le]          = "<="         ;
  ntype2str [Lnast_ntype_gt]          = ">"         ;
  ntype2str [Lnast_ntype_ge]          = ">="         ;
  ntype2str [Lnast_ntype_tuple]       = "()"      ;
  ntype2str [Lnast_ntype_ref]         = "ref"        ;
  ntype2str [Lnast_ntype_const]       = "const"      ;
  ntype2str [Lnast_ntype_attr_bits]   = "attr_bits"  ;
  ntype2str [Lnast_ntype_assert]      = "I"     ;
  ntype2str [Lnast_ntype_if]          = "if"         ;
  ntype2str [Lnast_ntype_cond]        = "cond"       ;
  ntype2str [Lnast_ntype_uif]         = "uif"        ;
  ntype2str [Lnast_ntype_elif]         = "elif"        ;
  ntype2str [Lnast_ntype_phi]         = "phi"        ;
  ntype2str [Lnast_ntype_for]         = "for"        ;
  ntype2str [Lnast_ntype_while]       = "while"      ;
  ntype2str [Lnast_ntype_func_call]   = "func_call"  ;
  ntype2str [Lnast_ntype_func_def]    = "func_def"   ;
  ntype2str [Lnast_ntype_top]         = "top"        ;
  /* *
   * ntype2str [Lnast_ntype_input]       = "input"      ;
   * ntype2str [Lnast_ntype_output]      = "output"     ;
   * ntype2str [Lnast_ntype_reg]         = "reg"        ;
   *
   * Note:
   * for Verilog, the input, output and reg token must be assigned as Lnast_ntype_ref type
   * such as
   * $foo    --- input
   * %bar    --- output
   * #potato --- register
   *
   * */
}

std::string Lnast_parser::ntype_dbg(Lnast_ntype ntype) {
  return ntype2str[ntype];
}

bool Lnast_parser::token_is_valid_ref(){
  return (scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input) || scan_is_token(Token_id_register));
}


