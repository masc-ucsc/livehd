#include "lnast_parser.hpp"

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif


void Lnast_parser::elaborate(){
  fmt::print("start elaborate!\n");
  lnast = std::make_unique<Language_neutral_ast>(get_buffer());
  lnast->set_root(Lnast_node(Lnast_ntype_top, Token()));
  if(scan_calc_lineno() == 0) //SH:FIXME: ask Akash to remove the "END" line if you really don't need it
    scan_next();

  build_top_statements(lnast->get_root());
  final_else_sts_type_correction();
}


void Lnast_parser::build_top_statements(const Tree_index& tree_idx_top){
  I(scan_text().at(0) == 'K');
  uint32_t knum = (uint32_t)std::stoi(scan_text().substr(1));  //the token must be a complete alnum
  auto tree_top_sts = lnast->add_child(tree_idx_top, Lnast_node(Lnast_ntype_statements, Token(), knum));
  add_statement(tree_top_sts);
}

void Lnast_parser::add_statement(const Tree_index& tree_top_sts) {
  fmt::print("line:{}, statement:{}\n", line_num, scan_text());

  Scope_id       token_scope = 0;
  Token          cfg_token_beg;
  Token          cfg_token_end;
  Token          loc;
  Lnast_ntype_id type = Lnast_ntype_invalid;
  Token          target_name;
  Tree_index     opr_parent_sts = tree_top_sts; //opr means operator

  knum1 = 0;
  knum2 = 0;
  line_tkcnt = 1;
  while(line_num == scan_calc_lineno()){
    if(scan_is_end()) return ;

    switch (line_tkcnt) {
      case CFG_KNUM1_POS:{
        I(scan_text().at(0) == 'K');
        knum1 = (uint32_t)std::stoi(scan_text().substr(1));
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_KNUM2_POS:{
        if (scan_sview() != "null") {
          I(scan_text().at(0) == 'K');
          knum2 = (uint32_t)std::stoi(scan_text().substr(1));
        } else {
          knum2 = 0;
        }
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_SCOPE_ID_POS:{
        token_scope = process_scope();
        scan_next(); line_tkcnt += 1;
        break;
      }
      case CFG_TOKEN_POS_BEG:{
        cfg_token_beg = scan_get_token();
        scan_next(); line_tkcnt += 1;
        break;
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

        if(!range_stack.empty()){
          auto sts_stack_top   = std::get<0>(range_stack.back());
          auto sts_parent_type = std::get<1>(range_stack.back());
          auto min             = std::get<2>(range_stack.back());
          auto max             = std::get<3>(range_stack.back());
          fmt::print("sts_stack_top:{}\n", ntype_dbg(lnast->get_data(sts_stack_top).type));
          fmt::print("sts_parent_type:{}\n", ntype_dbg(sts_parent_type));
          fmt::print("min:{}\n", min);
          fmt::print("max:{}\n", max);
          fmt::print("knum1:{}\n", knum1);
          if (knum1 >= min && knum1 < max) {
            opr_parent_sts = sts_stack_top;
          } else if (sts_parent_type == Lnast_ntype_func_def && knum1 == max) {
            function_name_correction(type, sts_stack_top);
            range_stack.pop_back();
            break; //won't create opr-subtree as it is just a renaming statement
          } else {
            range_stack.pop_back();
            if(!range_stack.empty()){
              auto new_sts_stack_top   = std::get<0>(range_stack.back());
              opr_parent_sts = new_sts_stack_top;
            }
          }
        }

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
Tree_index Lnast_parser::process_operator_node(const Tree_index& opr_parent_sts, Lnast_ntype_id type){
  if (type == Lnast_ntype_func_def) {
    I(range_stack.empty()); //no function definition in "if-else" or "function definition" block

    auto func_def_root = lnast->add_child(opr_parent_sts, Lnast_node(Lnast_ntype_func_def, Token()));
    auto func_def_sts  = lnast->add_child(func_def_root, Lnast_node(Lnast_ntype_statements, Token()));

    range_stack.emplace_back(std::make_tuple(func_def_sts, Lnast_ntype_func_def, knum1, knum2)); // min < K < max

    return func_def_root;
  } else if (type == Lnast_ntype_if) {
    if (knum2 != 0){
      return lnast->add_child(opr_parent_sts, Lnast_node(Lnast_ntype_if, Token()));
    } else {
      I(!range_stack.empty());
      auto sts_stack_top   = std::get<0>(range_stack.back());
      auto sts_parent_type = std::get<1>(range_stack.back());
      fmt::print("sts_stack_top:{}\n", ntype_dbg(lnast->get_data(sts_stack_top).type));
      fmt::print("sts_parent_type:{}\n", ntype_dbg(sts_parent_type));
      return lnast->get_parent(std::get<0>(range_stack.back()));
    }
  }

  return lnast->add_child(opr_parent_sts, Lnast_node(type, Token()));
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::add_operator_subtree(const Tree_index& tree_idx_opr, const Token& target_name) {
  fmt::print("token is :{}\n", scan_text());

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
void  Lnast_parser::process_func_def_op(const Tree_index& tree_idx_fdef, const Token& target_name){
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
void  Lnast_parser::process_func_call_op(const Tree_index& tree_idx_fcall, const Token& target_name){
  //true function call case: K17  K18  0  98  121  .()  ___g  fun1  ___h   ___i
  //fake function call case: K19  K20  0  123 129  .()  ___j  $a

  lnast->add_child(tree_idx_fcall, Lnast_node(Lnast_ntype_ref, target_name));

  I(token_is_valid_ref());
  lnast->add_child(tree_idx_fcall, Lnast_node(operand_analysis(), scan_get_token()));
  scan_next(); line_tkcnt += 1; //go to ___h

  if(scan_calc_lineno() == line_num + 1){
    //SH:FIXME: only one operand, fake function call for now!!!
    lnast->get_data(tree_idx_fcall).type = Lnast_ntype_pure_assign;
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
void  Lnast_parser::process_if_op(const Tree_index& tree_idx_if, const Token& cond){
  if(knum2 != 0){
    //this is a dummy cond1_sts for cond1 here as in pyrope, cond1_sts is actually executed on upper scope
    lnast->add_child(tree_idx_if, Lnast_node(Lnast_ntype_cstatements, Token()));
    lnast->add_child(tree_idx_if, Lnast_node(Lnast_ntype_cond, cond));
  } else {
    lnast->add_child(tree_idx_if, Lnast_node(Lnast_ntype_cond, cond));
  }
  process_if_else_sts_range(tree_idx_if);
}

void Lnast_parser::process_if_else_sts_range(const Tree_index& tree_idx_if){

  auto opd_1st_ksts = lnast->add_child(tree_idx_if, Lnast_node(Lnast_ntype_statements, Token()));
  auto opd_2nd_ksts = lnast->add_child(tree_idx_if, Lnast_node(Lnast_ntype_cstatements, Token()));//assume csts but might change to exe_sts later
  I(scan_text().at(0) == 'K');
  auto opd_1st = (uint32_t)std::stoi(scan_text().substr(1));
  scan_next(); line_tkcnt += 1; //go to 2nd operand
  I(scan_text().at(0) == 'K');
  auto opd_2nd = (uint32_t)std::stoi(scan_text().substr(1));

  uint32_t if_range_min = opd_1st;
  uint32_t if_range_max = opd_2nd;
  uint32_t else_range_min = opd_2nd;
  uint32_t else_range_max;
  if (knum2 == 0) { //case: else if
    else_range_max = std::get<3>(range_stack.back());
    range_stack.pop_back();//there is an else if, the previous csts range no longer valid, pop!
  } else { // case: full case if-elif-else
    else_range_max = knum2;
  }
  range_stack.emplace_back(std::make_tuple(opd_2nd_ksts, Lnast_ntype_cstatements, else_range_min, else_range_max));
  range_stack.emplace_back(std::make_tuple(opd_1st_ksts, Lnast_ntype_statements,  if_range_min, if_range_max));
}


//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_binary_op(const Tree_index& tree_idx_opr, const Token& target_name) {
  lnast->add_child(tree_idx_opr, Lnast_node(Lnast_ntype_ref, target_name));
  I(token_is_valid_ref());
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
  scan_next(); line_tkcnt += 1; //go to 2nd operand
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
}



//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_assign_like_op(const Tree_index& tree_idx_opr, const Token& target_name) {
  lnast->add_child(tree_idx_opr, Lnast_node(Lnast_ntype_ref, target_name));
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input) || scan_is_token(Token_id_reference));
  lnast->add_child(tree_idx_opr, Lnast_node(operand_analysis(), scan_get_token()));
  fmt::print("add operand:{}\n", scan_text());
}

//scan pos start: first operand token, stop: last operand
void Lnast_parser::process_label_op(const Tree_index& tree_idx_label, const Token& target_name) {
  lnast->add_child(tree_idx_label, Lnast_node(Lnast_ntype_ref, target_name));
  I(scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
  if (scan_is_token(Token_id_alnum) && scan_sview() == "__bits") {
    fmt::print("label op, 1st opd\n", scan_text());
    auto tree_idx_attr_bits = lnast->add_child(tree_idx_label, Lnast_node(Lnast_ntype_attr_bits, scan_get_token()));
    scan_next(); line_tkcnt += 1;
    fmt::print("label op, 2nd opd\n", scan_text());
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_attr_bits, Lnast_node(Lnast_ntype_const, scan_get_token()));
  } else { //case of function argument assignment
    lnast->add_child(tree_idx_label, Lnast_node(operand_analysis(), scan_get_token()));
    scan_next(); line_tkcnt += 1;
    I(token_is_valid_ref());
    lnast->add_child(tree_idx_label, Lnast_node(operand_analysis(), scan_get_token()));
  }
}

Scope_id Lnast_parser::process_scope() {
  return (uint8_t)std::stoi(scan_text());
}

Lnast_ntype_id  Lnast_parser::operand_analysis() {
  if (scan_sview().at(0) == '0' || scan_sview().at(0) == '-')
    return Lnast_ntype_const;
  else
    return Lnast_ntype_ref;
  //else if (scan_sview().at(0) == '$')
  //  return Lnast_ntype_input;
  //else if (scan_sview().at(0) == '%')
  //  return Lnast_ntype_output;
  //else if (scan_sview().at(0) == '@')
  //  return Lnast_ntype_reg;
}

void Lnast_parser::function_name_correction(Lnast_ntype_id type, const Tree_index& sts_idx) {
  I(type == Lnast_ntype_pure_assign);
  I(scan_peep_is_token(Token_id_reference, 1));
  I(scan_peep_sview(1).substr(1,3) == "___");

  auto sts_idx_parent = lnast->get_parent(sts_idx);
  auto target_node = lnast->get_children(sts_idx_parent).at(1);
  lnast->get_data(target_node).token = scan_get_token();
  scan_next(); line_tkcnt += 1; // go to 1st operand
  scan_next(); line_tkcnt += 1; // go to next line
}

void Lnast_parser::final_else_sts_type_correction() {
  for (const auto &it: lnast->depth_preorder()) {
    const auto& node_data = lnast->get_data(it);
    if(node_data.type == Lnast_ntype_if){
      auto last_else_sts = lnast->get_children(it).back();
      auto second_last_exe_sts = lnast->get_children(it).end()[-2];//avoid this expression if we have add_older_sibling()
      I(lnast->get_data(last_else_sts).type == Lnast_ntype_cstatements);
      I(lnast->get_data(second_last_exe_sts).type == Lnast_ntype_statements);
      lnast->get_data(last_else_sts).type = Lnast_ntype_statements;
      //SH:FIXME:bugy on add_younger_sibling, try not to use it for now and keep the "else" along without csts and cond
      //auto tmp = lnast->add_younger_sibling(second_last_exe_sts, Lnast_node(Lnast_ntype_cstatements, Token()));
      //lnast->add_younger_sibling(tmp, Lnast_node(Lnast_ntype_cond, Token()));
    }
  }
}



//SH:todo:add not token, example: !foo
//scan pos will stop at the end of operator token
Lnast_ntype_id Lnast_parser::operator_analysis() {
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
  ntype2str [Lnast_ntype_else]        = "else"       ;
  ntype2str [Lnast_ntype_cond]        = "cond"       ;
  ntype2str [Lnast_ntype_uif]         = "uif"        ;
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
   * for Verilog, the input, output and reg token must be assign as Lnast_ntype_ref type
   * such as
   * $foo    --- input
   * %bar    --- output
   * @potato --- register
   *
   * */
}

std::string Lnast_parser::ntype_dbg(Lnast_ntype_id ntype) {
  return ntype2str[ntype];
}

bool Lnast_parser::token_is_valid_ref(){
  return (scan_is_token(Token_id_alnum) || scan_is_token(Token_id_output) || scan_is_token(Token_id_input));
}


