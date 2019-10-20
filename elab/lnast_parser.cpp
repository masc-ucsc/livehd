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
  buffer_next_sts_parent = lnast->get_root();
  build_statements_op(buffer_next_sts_parent);

}

void Lnast_parser::build_statements_op(const mmap_lib::Tree_index& tree_idx_sts_parent){
  if(lnast->get_data(tree_idx_sts_parent).type == Lnast_ntype_top){
    auto tree_top_sts = lnast->add_child(tree_idx_sts_parent, Lnast_node(Lnast_ntype_statements, Token()));
    add_statement(tree_top_sts);
  } else {
    return;
  }
}

void Lnast_parser::add_statement(const mmap_lib::Tree_index& tree_top_sts) {
  //fmt::print("line:{}, statement:{}\n", line_num, scan_text());

  uint32_t    cfg_nidx;
  uint32_t    cfg_nparent;
  uint32_t    cfg_nchild;
  Token       cfg_token_beg;
  Token       cfg_token_end;
  Token       loc;
  Token       target_name;
  Lnast_ntype type = Lnast_ntype_invalid;
  mmap_lib::Tree_index     opr_parent_sts = tree_top_sts; //opr means operator

  line_tkcnt = 1;
  while (line_num == scan_get_token().line) {
    if(scan_is_end())
      return;

    //sh:fixme: ask Akash to remove the "END" line if you really don't need it
    if(unlikely(line_num == 0)) {
      scan_next();
      continue;
    }

    switch (line_tkcnt) {
      case CFG_IDX_POS:{
        cfg_nidx = (uint32_t)std::stoi(scan_text());
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_PARENT_POS:{
        cfg_nparent = (uint32_t)std::stoi(scan_text());
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_CHILD_POS:{
        cfg_nchild = (uint32_t)std::stoi(scan_text());
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_TOKEN_POS_BEG:{
        if (unlikely(scan_text().substr(0,4) == "SEQ0")){
          scan_next(); line_tkcnt += 1;
          break;
        } else if (unlikely(scan_text().substr(0,3) == "SEQ")){
          scan_next(); line_tkcnt += 1;
          build_statements_op(buffer_next_sts_parent);
          break;
        } else {
          cfg_token_beg = scan_get_token();
          scan_next(); line_tkcnt += 1;
          break;
        }
      }
      case CFG_TOKEN_POS_END:{
        cfg_token_end = scan_get_token();
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_OP_POS_BEG: { //no regular pattern, scan_next() internally case by case

        type = operator_analysis();

        scan_next(); line_tkcnt += 1; // go to opr target_name
        I(token_is_valid_ref());
        target_name = scan_get_token();
        auto tree_idx_opr = process_operator_node(opr_parent_sts, type);
        scan_next(); line_tkcnt += 1; //go to 1st operand

        add_operator_subtree(tree_idx_opr, target_name);
        scan_next(); line_tkcnt += 1; //go to next line
        break;
      }
      default: ;
    } //end switch
  } //end while

  line_num += 1;
  add_statement(tree_top_sts);
}


//scan pos start from the end of operator token
mmap_lib::Tree_index Lnast_parser::process_operator_node(const mmap_lib::Tree_index& opr_parent_sts, Lnast_ntype type){
  if (type == Lnast_ntype_func_def) {

    auto func_def_root = lnast->add_child(opr_parent_sts, Lnast_node(Lnast_ntype_func_def, Token()));
    auto func_def_sts  = lnast->add_child(func_def_root, Lnast_node(Lnast_ntype_statements, Token()));

    return func_def_root;
  } else if (type == Lnast_ntype_if) {
    //sh:todo
  }

  return lnast->add_child(opr_parent_sts, Lnast_node(type, Token()));
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::add_operator_subtree(const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name) {
  //fmt::print("token is :{}\n", scan_text());

  auto nt = lnast->get_data(tree_idx_opr).type;
  if (nt == Lnast_ntype_pure_assign || nt == Lnast_ntype_dp_assign || nt == Lnast_ntype_as || nt == Lnast_ntype_tuple) {//SH:FIXME: handle tuple seperately
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
  //K9   K14   0  59  96   ::{  ___e    K11   $a    $b  %o
  I(scan_text().at(0) == 'K');//@ K11, don't create it

  lnast->add_child(tree_idx_fdef, Lnast_node(Lnast_ntype_ref, target_name));

  scan_next(); line_tkcnt += 1; //go to $a
  auto local_line_num = scan_calc_lineno();
  while (scan_calc_lineno() == local_line_num) {
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_fdef, Lnast_node(operand_analysis(), scan_get_token()));
    scan_next(); line_tkcnt += 1; //go to $b -> go to %o -> ...
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
  scan_next(); line_tkcnt += 1; //go to ___h

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
    scan_next(); line_tkcnt += 1; //go to ___i -> ___j ...
  }
  scan_prev(); //for the final dummy scan_next() in while loop
}


//scan pos start: first operand token, stop: last operand
void  Lnast_parser::process_if_op(const mmap_lib::Tree_index& tree_idx_if, const Token& cond){
}



//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_binary_op(const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name) {
  lnast->add_child(tree_idx_opr, Lnast_node(Lnast_ntype_ref, target_name));
  I(token_is_valid_ref());
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
  scan_next(); line_tkcnt += 1; //go to 2nd operand
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
}



//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_assign_like_op(const mmap_lib::Tree_index& tree_idx_opr, const Token& target_name) {
  lnast->add_child(tree_idx_opr, Lnast_node(Lnast_ntype_ref, target_name));
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input) || scan_is_token(Token_id_reference));
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
  //fmt::print("add operand:{}\n", scan_text());
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_label_op(const mmap_lib::Tree_index& tree_idx_label, const Token& target_name) {
  lnast->add_child(tree_idx_label, Lnast_node(Lnast_ntype_ref, target_name));
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  if (scan_is_token(Token_id_alnum) && scan_sview() == "__bits") {
    //fmt::print("label op, 1st opd\n", scan_text());
    auto tree_idx_attr_bits = lnast->add_child(tree_idx_label, Lnast_node(Lnast_ntype_attr_bits, scan_get_token()));
    scan_next(); line_tkcnt += 1;
    //fmt::print("label op, 2nd opd\n", scan_text());
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_attr_bits, Lnast_node(Lnast_ntype_const, scan_get_token()));
  } else { //case of function argument assignment
    lnast->add_child(tree_idx_label, Lnast_node(operand_analysis(), scan_get_token()));
    scan_next(); line_tkcnt += 1;
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

void Lnast_parser::function_name_correction(Lnast_ntype type, const mmap_lib::Tree_index& sts_idx) {
  I(type == Lnast_ntype_pure_assign);
  I(scan_peep_is_token(Token_id_reference, 1));
  I(scan_peep_sview(1).substr(1,3) == "___");

  auto sts_idx_parent = lnast->get_parent(sts_idx);
  auto first_child    = lnast->get_first_child(sts_idx_parent);
  auto second_child   = lnast->get_sibling_next(first_child);

  I(!second_child.is_invalid());

  lnast->ref_data(second_child)->token = scan_get_token();
  scan_next(); line_tkcnt += 1; // go to 1st operand
  scan_next(); line_tkcnt += 1; // go to next line
}

//SH:todo:add not token, example: !foo
//scan pos will stop at the end of operator token
Lnast_ntype Lnast_parser::operator_analysis() {
  Lnast_ntype type;
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
   * @potato --- register
   *
   * */
}

std::string Lnast_parser::ntype_dbg(Lnast_ntype ntype) {
  return ntype2str[ntype];
}

bool Lnast_parser::token_is_valid_ref(){
  return (scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
}


