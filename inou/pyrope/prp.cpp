#include <ctype.h>
#include <algorithm>
#include <iostream>

#include "fmt/format.h"
#include "prp.hpp"

void Prp::eat_comments(){
	while (scan_is_token(Token_id_comment) && !scan_is_end()) scan_next();
}

bool Prp::rule_top(){
  fmt::print("Hello from rule_top.\n");
  return rule_code_blocks();
}

bool Prp::rule_code_blocks(){
  eat_comments();
  if (!rule_code_block_int()){
    return false;
  }
  while(rule_code_block_int());
  
  return true;
}

bool Prp::rule_code_block_int(){
  eat_comments();
  ast->down();
  if (rule_if_statement()){
    ast->up(Prp_rule_if_statement);
    return true;
  }
  else if (rule_for_statement()){
    ast->up(Prp_rule_for_statement);
    return true;
  }
  else if (rule_while_statement()){
    ast->up(Prp_rule_while_statement);
    return true;
  }
  else if (rule_try_statement()){
    ast->up(Prp_rule_try_statement);
    return true;
  }
  else if (rule_punch_format()){
    ast->up(Prp_rule_punch_format);
    return true;
  }
  else if (rule_assignment_expression()){
    ast->up(Prp_rule_assignment_expression);
    return true;
  }
  else if (rule_function_pipe()){
    ast->up(Prp_rule_function_pipe);
    return true;
  }
  else if (rule_fcall_implicit()){
    ast->up(Prp_rule_fcall_implicit);
    return true;
  }
  else if (rule_fcall_explicit()){
    ast->up(Prp_rule_fcall_explicit);
    return true;
  }
  fmt::print("Couldn't find a valid code block start.\n");
  ast->up(Prp_invalid);
  return false;
}

bool Prp::rule_if_statement(){
  fmt::print("Hello from rule_if_statement.\n");
  int tokens_consumed = 0;
  
  if (scan_is_token(Pyrope_id_unique)){
    debug_consume(); // consume the unique token
    tokens_consumed++;
    if (scan_is_token(Pyrope_id_if)){
      debug_consume(); // consume the if token
      tokens_consumed++;
    }
    else{
      go_back(tokens_consumed);
      return false;
    }
  }
  else if (scan_is_token(Pyrope_id_if)){
    fmt::print("rule_if_statement: Found an if.\n");
    debug_consume(); // consume the if token
    tokens_consumed++;
  }
  else{
    go_back(tokens_consumed);
    return false;
  }
  fmt::print("rule_if_statement: Checking for a logical expression.\n");
  if(rule_logical_expression()){
    fmt::print("rule_if_statement: found a logical expression. Current token:");
    dump_token();
    // optional
    if(scan_is_token(Token_id_colon)){
      debug_consume(); // consume the colon
      tokens_consumed++;
      if(scan_is_token(Token_id_colon)){
        debug_consume(); // consume the colon
        tokens_consumed++;
      }
      else{
        go_back(tokens_consumed);
        fmt::print("rule_if_statment: Doesn't fit rule_if_statement.\n");
        return false;
      }
    }
    if(scan_is_token(Token_id_ob)){
      debug_consume(); // consume the open brace
      tokens_consumed++;
      fmt::print("rule_if_statement: found an open bracket.\n");
      // optional
      rule_code_blocks(); // PROBLEM: occurs many times; doesn't account for code block syntax being incorrect
      fmt::print("rule_if_statement: we just looked for a code block.\n");
      // optional
      if(scan_is_token(Token_id_cb)){
        debug_consume(); // consume the close brace
        tokens_consumed++;
        rule_else_statement(); // PROBLEM: same as above
        fmt::print("rule_if_statment: Fits rule_if_statement.\n");
        return true;
      }
    }
  }
  fmt::print("rule_if_statment: Doesn't fit rule_if_statement.\n");
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_for_statement(){
  fmt::print("Hello from rule_for_statement.\n");
  int tokens_consumed = 0;
  if(scan_is_token(Pyrope_id_for)){
    debug_consume();
    tokens_consumed++;
    if(rule_for_index()){
      // optional
      if(scan_is_token(Token_id_colon)){
        debug_consume(); // consume the colon
        tokens_consumed++;
        if(scan_is_token(Token_id_colon)){
          debug_consume(); // consume the colon
          tokens_consumed++;
        }
        else{
          go_back(tokens_consumed);
          fmt::print("rule_for_statment: Doesn't fit rule_for_statement.\n");
          return false;
        }
      }
      if(scan_is_token(Token_id_ob)){
        debug_consume(); // consume the open brace
        tokens_consumed++;
        // optional
        rule_code_blocks();
        if(scan_is_token(Token_id_cb)){
          debug_consume(); // consume the open brace
          tokens_consumed++;
          return true;
        }
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_for_index(){
  int tokens_consumed = 0;
  
  if(rule_rhs_expression_property()){
    // zero or more
    bool next = true;
    while(next){
      next = rule_rhs_expression_property();
    }
    next = true;
    while(next){
      next = false;
      if(scan_is_token(Token_id_comma)){
        debug_consume();
        tokens_consumed++;
        if(rule_rhs_expression_property()){
          next = true;
        }
        else{
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    return true;
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_else_statement(){
  fmt::print("Hello from rule_else_statement.\n");
  int tokens_consumed = 0;
  
  // option one
  if(scan_is_token(Pyrope_id_elif)){
    fmt::print("rule_else_statement: found an elif.\n");
    debug_consume(); // consume the elif
    tokens_consumed++;
    if(rule_logical_expression()){
      // optional
      if(scan_is_token(Token_id_colon)){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_colon)){
          debug_consume();
          tokens_consumed++;
        }
        else{
          go_back(tokens_consumed);
          return false;
        }
      }
      if(scan_is_token(Token_id_ob)){
        // optional
        rule_code_blocks();
        if(scan_is_token(Token_id_cb)){
          // optional
          rule_else_statement();
          return true;
        }
      }
    }
  }
    
  // option two
  go_back(tokens_consumed);
  tokens_consumed = 0;
  if(scan_is_token(Pyrope_id_else)){
    fmt::print("rule_else_statement: found an else.\n");
    debug_consume(); // consume the else
    tokens_consumed++;
    // optional
    if(scan_is_token(Token_id_colon)){
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_colon)){
        debug_consume();
        tokens_consumed++;
      }
      else{
        go_back(tokens_consumed);
        return false;
      }
    }
    if(scan_is_token(Token_id_ob)){
      debug_consume(); // consume the open brace
      tokens_consumed++;
      // optional
      rule_code_blocks();
      if(scan_is_token(Token_id_cb)){
        debug_consume(); // consume the open brace
        tokens_consumed++;
        return true;
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_while_statement(){
  int tokens_consumed = 0;
  
  if(scan_is_token(Pyrope_id_while)){
    debug_consume(); // consume the while
    tokens_consumed++;
    if(rule_logical_expression()){
      // optional
      if(scan_is_token(Token_id_colon)){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_colon)){
          debug_consume();
          tokens_consumed++;
        }
        else{
          go_back(tokens_consumed);
          return false;
        }
      }
      if(scan_is_token(Token_id_ob)){
        debug_consume(); // consume the open bracket
        tokens_consumed++;
        // optional
        rule_code_blocks();
        if(scan_is_token(Token_id_cb)){
          debug_consume(); // consume the open bracket
          return true;
        }
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_try_statement(){
  int tokens_consumed = 0;
  
  if(scan_is_token(Pyrope_id_try)){
    debug_consume(); // consume the try
    tokens_consumed++;
    if(rule_logical_expression()){
      // optional
      if(scan_is_token(Token_id_colon)){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_colon)){
          debug_consume();
          tokens_consumed++;
        }
        else{
          go_back(tokens_consumed);
          return false;
        }
      }
      if(scan_is_token(Token_id_ob)){
        debug_consume(); // consume the open bracket
        tokens_consumed++;
        // optional
        rule_code_blocks();
        if(scan_is_token(Token_id_cb)){
          debug_consume(); // consume the open bracket
          tokens_consumed++;
          // optional
          rule_scope_else();
          return true;
        }
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_scope_else(){
  int tokens_consumed = 0;
  if(scan_is_token(Pyrope_id_else)){
    debug_consume();
    tokens_consumed++;
    if(scan_is_token(Token_id_ob)){
      debug_consume();
      tokens_consumed++;
      // optional
      rule_scope_body();
      if(scan_is_token(Token_id_cb)){
        debug_consume();
        tokens_consumed++;
        return true;
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_scope_body(){
  // option one
  if(rule_code_blocks()){
    // optional
    rule_logical_expression();
    return true;
  }
  
  // option two
  if(rule_logical_expression()){
    return true;
  }
  
  return false;
}

bool Prp::rule_scope(){
  int tokens_consumed = 0;
  
  if(scan_is_token(Token_id_colon)){
    // optional
    debug_consume();
    tokens_consumed++;
    rule_scope_condition();
    if(scan_is_token(Token_id_colon)){
      debug_consume(); // consume the colon
      return true;
    }
  }
  
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_scope_condition(){
  int tokens_consumed = 0;
  
  // option one
  if(rule_scope_argument()){
    if(scan_is_token(Pyrope_id_when)){
      debug_consume();
      tokens_consumed++;
      if(rule_logical_expression()){
        return true;
      }
    }
  }
  
  // option 2
  go_back(tokens_consumed);
  tokens_consumed = 0;
  // optional
  rule_scope_argument();
  if(scan_is_token(Pyrope_id_when)){
    debug_consume();
    tokens_consumed++;
    if(rule_logical_expression()){
      return true;
    }
  }
  
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_scope_argument(){
  int tokens_consumed = 0;
  
  // option one
  if(scan_is_token(Token_id_op)){
    debug_consume(); // consume the open parenthesis
    tokens_consumed++;
    if(rule_identifier()){
      // zero or more
      while(rule_identifier());
      if(scan_is_token(Token_id_cp)){
        debug_consume();
        return true;
      }
    }
  }
  
  go_back(tokens_consumed);
  tokens_consumed = 0;
  
  // option 2
  if(scan_is_token(Token_id_op)){
    debug_consume();
    tokens_consumed++;
    if(rule_identifier()){
      bool next = true;
      while(next){
        next = scan_is_token(Token_id_comma);
        if(next){
          debug_consume();
          tokens_consumed++;
          if(!rule_identifier()){
            go_back(tokens_consumed);
            return false;
          }
        }
      }
      if(scan_is_token(Token_id_cp)){
        debug_consume();
        return true;
      }
    }
  }
  
  go_back(tokens_consumed);
  return true;
}

bool Prp::rule_scope_declaration(){
  int tokens_consumed = 0;
  
  if(rule_scope()){
    if(scan_is_token(Token_id_ob)){
      debug_consume();
      tokens_consumed++;
      // optional
      rule_scope_body();
      if(scan_is_token(Token_id_cb)){
        debug_consume();
        tokens_consumed++;
        // optional
        rule_scope_else();
        return true;
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_punch_format(){
  int tokens_consumed = 0;
  
  if(scan_is_token(Pyrope_id_punch)){
    debug_consume();
    tokens_consumed++;
    if(rule_identifier()){
      if(scan_is_token(Token_id_at) || scan_is_token(Token_id_percent)){
        debug_consume();
        tokens_consumed++;
        if(rule_punch_rhs()){
          return true;
        }
      }
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_punch_rhs(){
  int tokens_consumed = 0;
  bool next;
  
  // option 1
  if(scan_is_token(Token_id_div)){
    debug_consume(); // consume the div
    tokens_consumed++;
    // optional
    if(rule_identifier()){
      // zero or more of the following:
      next = true;
      while(next){
        next = scan_is_token(Token_id_dot);
        if(next){
          debug_consume(); // consume the dot
          tokens_consumed++;
          if(!rule_identifier()){
            go_back(tokens_consumed);
            return false;
          }
        }
      }
    }
    if(scan_is_token(Token_id_div)){
      debug_consume(); // consume the div
      tokens_consumed++;
      if(scan_is_token(Token_id_dot)){
        debug_consume(); // consume the div
        tokens_consumed++;
        if(rule_identifier()){
          next = true;
          // zero or more of the following
          while(next){
            if(scan_is_token(Token_id_dot)){
              debug_consume(); // consume the div
              tokens_consumed++;
              if(!rule_identifier()){
                go_back(tokens_consumed);
                return false;
              }
            }
          }
        }
      }
    }
  }
  
  go_back(tokens_consumed);
  tokens_consumed = 0;
  
  // option 2
  if(scan_is_token(Token_id_div)){
    debug_consume(); // consume the div
    tokens_consumed++;
    // optional
    if(rule_identifier()){
      // zero or more of the following:
      next = true;
      while(next){
        next = scan_is_token(Token_id_dot);
        if(next){
          debug_consume(); // consume the dot
          tokens_consumed++;
          if(!rule_identifier()){
            go_back(tokens_consumed);
            return false;
          }
        }
      }
    }
    if(scan_is_token(Token_id_div)){
      debug_consume(); // consume the div
      tokens_consumed++;
      next = true;
      // zero or more of the following
      while(next){
        if(scan_is_token(Token_id_dot)){
          debug_consume(); // consume the div
          tokens_consumed++;
          if(!rule_identifier()){
            go_back(tokens_consumed);
            return false;
          }
        }
      }
    }
  }
  
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_function_pipe(){
  fmt::print("Hello from rule_function_pipe.\n");
  int tokens_consumed = 0;
  
  if(rule_fcall_implicit() || rule_logical_expression()){
    if(scan_is_token(Token_id_pipe)){
      debug_consume();
      tokens_consumed++;
      if(rule_fcall_implicit() || rule_fcall_explicit()){
        return true;
      }
    }
  }
  go_back(tokens_consumed);
  fmt::print("Doesn't fit rule_function_pipe.\n");
  return false;
}

bool Prp::rule_fcall_explicit(){
  int tokens_consumed = 0;
  
  if(!rule_constant()){
    if(rule_tuple_dot_notation()){
      if(scan_is_token(Token_id_dot)){
        debug_consume(); // consume the dot
        tokens_consumed++;
        if(rule_fcall_arg_notation()){
          // optional
          rule_scope_declaration();
          if(scan_is_token(Token_id_dot)){
            debug_consume();
            tokens_consumed++;
            if(rule_fcall_explicit() || rule_tuple_dot_notation()){
              return true;
            }
          }
        }
      }
    }
  }
  
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_fcall_arg_notation(){
  int tokens_consumed = 0;
  bool next;
  
  // option 1
  if(scan_is_token(Token_id_op)){
    debug_consume();
    tokens_consumed++;
    if(rule_rhs_expression_property() || rule_lhs_var_name()){
      // zero or more of the following
      while(rule_rhs_expression_property() || rule_lhs_var_name());
      if(scan_is_token(Token_id_cp)){
        debug_consume();
        return true;
      }
    }
  }
  
  tokens_consumed = 0;
  go_back(tokens_consumed);
  
  //option 2
  if(scan_is_token(Token_id_op)){
    debug_consume();
    tokens_consumed++;
    if(rule_rhs_expression_property() || rule_logical_expression()){
      // zero or more of the following
      next = true;
      while(next){
        next = scan_is_token(Token_id_comma);
        if(next){
          debug_consume();
          tokens_consumed++;
          if(!(rule_rhs_expression_property() || rule_logical_expression())){
            go_back(tokens_consumed);
            return false;
          }
        }
      }
      if(scan_is_token(Token_id_cp)){
        debug_consume();
        return true;
      }
    }
  }
  
  tokens_consumed = 0;
  go_back(tokens_consumed);
  
  //option 3
  if(scan_is_token(Token_id_op)){
    debug_consume();
    tokens_consumed++;
    if(scan_is_token(Token_id_cp)){
      debug_consume();
      return true;
    }
  }
  
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_fcall_implicit(){
  return false;
}

bool Prp::rule_assignment_expression(){
	fmt::print("Hello from rule_assignment_expression.\n");
	if(rule_constant()){
		return false;
	}
	
	bool next = rule_lhs_expression() || rule_overload_notation();
	
	if(next){
		next = rule_assignment_operator();
		if(next){
			next = rule_logical_expression() || rule_function_pipe() || rule_fcall_implicit();
      fmt::print("Fits rule_assignment_expression.\n");
			return true;
		}
	}
	else{
    //fmt::print("Parse error at line {}: Expected left hand side expression.\n", scan_calc_lineno());
  }
	return false;
}

/* FIXME: add range notation rule */
bool Prp::rule_lhs_expression(){
  fmt::print("Hello from rule_lhs_expression.\n");
  ast->down();
  bool ok = rule_tuple_notation() || rule_range_notation();
  ast->up(Prp_rule_lhs_expression);
  return ok;
}

/* FIXME: incomplete */
bool Prp:: rule_rhs_expression_property(){
  fmt::print("Hello from rule_rhs_expression_property.\n");
  if(scan_is_token(Token_id_label)){
    debug_consume(); // consume the label
    rule_tuple_notation(); // optional
    return true;
  }
	return false;
}

bool Prp::rule_tuple_notation(){
  fmt::print("Hello from rule_tuple_notation\n");
  int tokens_consumed = 0;
  
  // option 1
  if(!scan_is_token(Token_id_op)){
    fmt::print("rule_tuple_notation: trying option 1.\n");
    if(!rule_bit_selection_notation()){ return false; }
  }
  // options 2 and 3
  else{
    debug_consume();
    tokens_consumed++;
    fmt::print("rule_tuple_notation: trying options 2 and 3.\n");
    if(rule_rhs_expression_property() || rule_logical_expression()){
      bool next = true;
      
      // zero or more of the following
      while(next){
        if(scan_is_token(Token_id_comma)){
          debug_consume();
          tokens_consumed++;
          if(!(rule_rhs_expression_property() || rule_logical_expression())){
            next = false; // NOTE: not a parse error; it's allowed to just have a comma.
          }
        }
        else{ next = false; }
      }
      if(!scan_is_token(Token_id_cp)){ return false; }
      else{
        debug_consume();
        tokens_consumed++;
        rule_tuple_by_notation() || rule_bit_selection_bracket(); // optional
      }
    }
    else{
      if(!scan_is_token(Token_id_cp)){ return false; }
    }
  }
  
  // consume_block(Prp_rule_tuple_notation, lookahead_count);
  return true;
}

bool Prp::rule_range_notation(){
  return false;
}

/*
bool Prp::rule_tuple_notation(){
  fmt::print("Hello from rule_tuple_notation.\n");
	int tokens_consumed = 0;
  int levels_down = 0;
  
	// first option
	bool next = scan_is_token(Token_id_op); // all the first three require this
  if(next){
    ast->add(Prp_rule_tuple_notation, scan_token());
		debug_consume(); // consume the LPAR
    tokens_consumed++;
  }
	
	if(next){
		// bit_selection_notation+
		do{
			next = rule_bit_selection_notation();
		} while(rule_bit_selection_notation());
		if(next){
			next = scan_is_token(Token_id_cp);
			if(next){
				debug_consume(); // consume the RPAR
				tokens_consumed++;
				next = rule_tuple_by_notation() || rule_bit_selection_bracket();
				return true;
			}
		}
	}
	
	if(tokens_consumed > 0){
    go_back(tokens_consumed-1);
    
    // second option
    tokens_consumed = 1;
  }
	
	if(next){
		next = rule_rhs_expression_property() || rule_logical_expression();
		if(next){
			// can be any number of the following
			while(next){
				next = scan_is_token(Token_id_comma);
				if(next){
					debug_consume(); // consume the comma
					tokens_consumed++;
					if(!(rule_rhs_expression_property() || rule_logical_expression())){
            go_back(tokens_consumed);
            return false;
          }
				}
			}
			
			next = scan_is_token(Token_id_cp);
			if(next){
				debug_consume();
				tokens_consumed++;
			}
			next = rule_tuple_by_notation() || rule_bit_selection_bracket(); // optional
			return true;
		}
  }
		
	if(tokens_consumed > 0){
    go_back(tokens_consumed-1);
    
    // third option
    tokens_consumed = 1;
  }
  
  if(next){
    next = scan_is_token(Token_id_cp);
    return next;
  }
	
	// fourth option
  bool ok = rule_bit_selection_notation();
  if(ok){
    go_back(tokens_consumed);
    return true;
  }
  return false;
}
*/

bool Prp::rule_bit_selection_notation(){
  fmt::print("Hello from rule_bit_selection_notation.\n");
	bool next = rule_tuple_dot_notation();
	if (next){
		next = rule_bit_selection_bracket();
    if(next) fmt::print("Fits rule_bit_selection_notation.\n");
    return next;
	}
	return false;
}

bool Prp::rule_tuple_dot_notation(){
  fmt::print("Hello from rule_tuple_dot_notation.\n");
	bool next = rule_tuple_array_notation();
	if(next){
		next = rule_tuple_dot_dot();
		return next;
	}
	return false;
}

bool Prp::rule_tuple_dot_dot(){
  fmt::print("Hello from rule_tuple_dot_dot.\n");
  bool next = true;
  int tokens_consumed = 0;
  
  while(next){
    next = scan_is_token(Token_id_dot);
    if(next){
      debug_consume(); // consume the dot
      tokens_consumed++;
      if(!rule_tuple_array_notation()) {return false;}
    }
  }
  fmt::print("Fits rule_tuple_dot_dot.\n");
  return true;
}

bool Prp::rule_tuple_array_notation(){
  fmt::print("Hello from rule_tuple_array_notation.\n");
	bool next = rule_lhs_var_name();
	if(next){
		next = rule_tuple_array_bracket();
    if(next) fmt::print("Fits rule_tuple_array_notation.\n");
		return next;
	}
	return false;
}

bool Prp::rule_lhs_var_name(){
  fmt::print("Hello from rule_lhs_var_name.\n");
  ast->down();
	if(rule_identifier() || rule_constant()){
    ast->up(Prp_rule_lhs_var_name);
    return true;
  }
  ast->up(Prp_rule_lhs_var_name);
  return false;
}

bool Prp::rule_tuple_array_bracket(){
  fmt::print("Hello from rule_tuple_array_bracket.\n");
  bool next = true;
  int tokens_consumed = 0;
  
  /* zero or more */
  while(next){
    next = scan_is_token(Token_id_obr);
    if(next){
      debug_consume(); // consume the LBRK
      tokens_consumed++;
      next = rule_logical_expression() || rule_tuple_notation_no_bracket();
      if(next){
        next = scan_is_token(Token_id_cbr);
        if(next){
          debug_consume(); // consume the RBRK
          tokens_consumed++;
        }
        else{
          go_back(tokens_consumed);
          return false;
        }
      }
    }
  }
  fmt::print("Fits rule_tuple_array_bracket.\n");
	return true;
}

bool Prp::rule_tuple_notation_no_bracket(){
  fmt::print("Hello from rule_tuple_notation_no_bracket.\n");
	/* bit_selection_notation+ */
  if(rule_bit_selection_notation()){
    while(rule_bit_selection_notation());
    return true;
  }
	
	return false;
}

bool Prp::rule_identifier(){  
  fmt::print("Hello from rule_identifier.\n");
  ast->down();
	if(!(scan_is_token(Token_id_register) || scan_is_token(Token_id_input) || scan_is_token(Token_id_output) || scan_is_token(Token_id_alnum) || scan_is_token(Token_id_label))){
    ast->up(Prp_rule_identifier);
		return false;
	}
	
	ast->add(Prp_rule_identifier, scan_token());
	debug_consume(); // consume the ID, need to check for optional "?"
	
	if(scan_is_token(Token_id_qmark)){
    ast->add(Prp_rule_identifier, scan_token());
		debug_consume();
	}
	ast->up(Prp_rule_identifier);
	fmt::print("Fits rule_identifier.\n");
	return true;
}

/* FIXME: support for string constants */
bool Prp::rule_constant(){
  int tokens_consumed = 0;
  fmt::print("Hello from rule_constant.\n");
  if(scan_is_token(Token_id_minus)){
    debug_consume();
    tokens_consumed++;
  }
	if(scan_is_token(Token_id_num)){
    std::cout << "Found a constant: " << scan_text() << std::endl; 
		debug_consume();
		return true;
	}
	go_back(tokens_consumed);
	return false;
}

bool Prp::rule_assignment_operator(){
  fmt::print("Hello from rule_assignment_operator.\n");
  int tokens_consumed = 0;
  
  ast->down();

	if(scan_is_token(Token_id_coloneq) || scan_is_token(Token_id_eq) || scan_is_token(Pyrope_id_as)){
    ast->add(Prp_rule_assignment_operator, scan_token());
    debug_consume(); // consume the operator
    fmt::print("Fits rule_assignment_operator.\n");
    ast->up(Prp_rule_assignment_operator);
		return true;
	}
	
	/* op= tokens*/
  if (scan_is_token(Token_id_mult) || scan_is_token(Token_id_plus) || scan_is_token(Token_id_minus)){
    ast->add(Prp_rule_assignment_operator, scan_token());
    debug_consume();
    tokens_consumed++;
    if (scan_is_token(Token_id_eq)){
      ast->add(Prp_rule_assignment_operator, scan_token());
      debug_consume();
      fmt::print("Fits rule_assignment_operator.\n");
      ast->up(Prp_rule_assignment_operator);
      return true;
    }
    go_back(tokens_consumed);
    ast->up(Prp_rule_assignment_operator);
    return false;
  }
	
  go_back(tokens_consumed);
  tokens_consumed = 0;
	
	/* left and right shift */
  if (scan_is_token(Token_id_lt)){
    ast->add(Prp_rule_assignment_operator, scan_token());
    debug_consume();
    tokens_consumed++;
    if(scan_is_token(Token_id_lt)){
      ast->add(Prp_rule_assignment_operator, scan_token());
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_eq)){
        ast->add(Prp_rule_assignment_operator, scan_token());
        debug_consume();
        fmt::print("Fits rule_assignment_operator.\n");
        ast->up(Prp_rule_assignment_operator);
        return true;
      }
    }
    go_back(tokens_consumed);
    ast->up(Prp_rule_assignment_operator);
    return false;
  }
  
  go_back(tokens_consumed);
  tokens_consumed = 0;
  
  if (scan_is_token(Token_id_gt)){
    ast->add(Prp_rule_assignment_operator, scan_token());
    debug_consume();
    tokens_consumed++;
    if(scan_is_token(Token_id_gt)){
      ast->add(Prp_rule_assignment_operator, scan_token());
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_eq)){
        ast->add(Prp_rule_assignment_operator, scan_token());
        debug_consume();
        fmt::print("Fits rule_assignment_operator.\n");
        ast->up(Prp_rule_assignment_operator);
        return true;
      }
    }
  }
	go_back(tokens_consumed);
  ast->up(Prp_rule_assignment_operator);
	return false;
}

bool Prp::rule_tuple_by_notation(){
  int tokens_consumed = 0;
  fmt::print("Hello from rule_tuple_by_notation.\n");
  if (scan_is_token(Pyrope_id_by)){
    debug_consume();
    tokens_consumed++;
    if(rule_lhs_var_name()){
      fmt::print("Fits rule_tuple_by_notation.\n");
      return true;
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_bit_selection_bracket(){
  fmt::print("Hello from rule_bit_selection_bracket.\n");
  bool next = true;
  int tokens_consumed = 0;
  
  // zero or more of the following
  while(next){
    next = false;
    
    if(scan_is_token(Token_id_obr)){
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_obr)){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_cbr)){
          debug_consume();
          tokens_consumed++;
          if(scan_is_token(Token_id_cbr)){
            debug_consume();
            tokens_consumed++;
            next = true;
          }
          else{
            go_back(tokens_consumed);
            return false;
          }
        }
        
        else if(rule_logical_expression() || rule_tuple_notation_no_bracket()){
          if(scan_is_token(Token_id_cbr)){
            debug_consume();
            tokens_consumed++;
            if(scan_is_token(Token_id_cbr)){
              debug_consume();
              tokens_consumed++;
              next = true;
            }
          }
        }
        
        else{
          go_back(tokens_consumed);
          return false;
        }
        
      }
    }
  }
  fmt::print("Fits rule_bit_selection_bracket.\n");
  return true;
}

bool Prp::rule_logical_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  ast->down();
  
  fmt::print("Hello from rule_logical_expression.\n");
  if (rule_relational_expression()){
    /* zero or more of the following */
    while(next){
      fmt::print("rule_logical_expression: looking for a logical operator. Next token:\n");
      next = scan_is_token(Pyrope_id_or) || scan_is_token(Pyrope_id_and);
      if(next){
        ast->down();
        ast->add(Prp_rule_logical_expression, scan_token());
        ast->up(Prp_rule_logical_expression);
        debug_consume();
        tokens_consumed++;
        if (!rule_relational_expression()){
          go_back(tokens_consumed);
          ast->up(Prp_rule_logical_expression);
          return false;
        }
      }
    }
    fmt::print("Fits rule_logical_expression.\n");
    ast->up(Prp_rule_logical_expression);
    return true;
  }
  go_back(tokens_consumed);
  ast->up(Prp_rule_logical_expression);
  return false;
}

bool Prp::rule_relational_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  fmt::print("Hello from rule_relational_expression.\n");
  if(rule_additive_expression()){
    /* zero or more of the following */
    while(next){
      fmt::print("rule_relational_expression: looking for a relational operator. Next token:\n");
      dump_token();
      next = scan_is_token(Token_id_le) || scan_is_token(Token_id_ge) || scan_is_token(Token_id_lt) || scan_is_token(Token_id_gt) || scan_is_token(Token_id_same) || scan_is_token(Token_id_diff) || scan_is_token(Pyrope_id_is);
      fmt::print("rule_relational_expression: did we find our operator? {}\n", scan_is_token(Token_id_same));
      if(next){
        ast->add(Prp_rule_relational_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if (!rule_additive_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    fmt::print("Fits rule_relational_expression.\n");
    return true;
  }
  go_back(tokens_consumed);
  return false;
}

/* FIXME: need to add overload notation */
bool Prp::rule_additive_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  fmt::print("Hello from rule_additive_expression.\n");
  
  if(rule_bitwise_expression()){
    /* zero or more of the following */
    while(next){
      fmt::print("Entering main loop of rule_additive_expression; current token: \n");
      // dump_token();
      next = scan_is_token(Token_id_plus);
      if(next){
        ast->down();
        ast->add(Prp_rule_additive_expression, scan_token());
        ast->up(Prp_rule_additive_expression);
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_plus)){ // increment operator
          ast->add(Prp_rule_additive_expression, scan_token());
          debug_consume();
          tokens_consumed++;
        }
        if(!rule_bitwise_expression()){
          fmt::print("rule_additive_expression: going back1.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
      
      next = scan_is_token(Token_id_mult);
      if(next){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_mult)){ // ** operator
          ast->add(Prp_rule_additive_expression, scan_token());
          debug_consume();
          tokens_consumed++;
          if(!rule_bitwise_expression()){
            fmt::print("rule_additive_expression: going back2.\n");
            go_back(tokens_consumed);
            return false;
          }
        }
      }
      
      next = scan_is_token(Token_id_lt);
      if(next){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_lt)){
          ast->add(Prp_rule_additive_expression, scan_token());
          debug_consume();
          tokens_consumed++;
          if(!rule_bitwise_expression()){
            fmt::print("rule_additive_expression: going back3.\n");
            go_back(tokens_consumed);
            return false;
          }
        }
        else { // unlike the previous, one token isn't enough
          fmt::print("rule_additive_expression: going back4.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
      
      next = scan_is_token(Token_id_gt);
      if(next){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_gt)){
          ast->add(Prp_rule_additive_expression, scan_token());
          debug_consume();
          tokens_consumed++;
          if(!rule_bitwise_expression()){
            fmt::print("rule_additive_expression: going back5.\n");
            go_back(tokens_consumed);
            //return false;
          }
        }
        else {
          fmt::print("rule_additive_expression: going back6.\n");
          go_back(tokens_consumed);
          //return false;
        }
      }
      
      next = (scan_is_token(Token_id_minus) || scan_is_token(Pyrope_id_union) || scan_is_token(Pyrope_id_intersect));
      if(next){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if(!rule_bitwise_expression()){
          fmt::print("rule_additive_expression: going back7.\n");
          go_back(tokens_consumed);
          return false;
        }
        else{
          fmt::print("rule_additive_expression: going back8.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    
    /* optional */
    if(scan_is_token(Token_id_dot)){
      ast->add(Prp_rule_additive_expression, scan_token());
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_dot)){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        /* FIXME (maybe): this is optional, but it must either fully fit rule_additive_expression or
         not fit it at all, this will miss if there is an incorrectly described additive expression
         because it will just return false, the same as if it weren't there at all. */
        fmt::print("rule_additive_expression: danger.\n");
        rule_additive_expression();
      }
    }
    fmt::print("Fits rule_additive_expression.\n");
    return true;
  }
  go_back(tokens_consumed);
  fmt::print("Doesn't fit rule_additive_expression.\n");
  return false;
}

bool Prp::rule_bitwise_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  fmt::print("Hello from rule_bitwise_expression().\n");
  if(rule_multiplicative_expression()){
    /* zero or more of the following */
    while(next){
      next = scan_is_token(Token_id_pipe) || scan_is_token(Token_id_and) || scan_is_token(Token_id_xor);
      if(next){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if(!rule_multiplicative_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    fmt::print("Fits rule_bitwise_expression.\n");
    return true;
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_multiplicative_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  fmt::print("Hello from rule_multiplicative_expression.\n");
  if(rule_unary_expression()){
    while(next){
      next = scan_is_token(Token_id_mult) || scan_is_token(Token_id_div);
      if(next){
        ast->add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        tokens_consumed++;
        if(!rule_unary_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    fmt::print("Fits rule_multiplicative_expression.\n");
    return true;
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_unary_expression(){
  int tokens_consumed = 0;
  fmt::print("Hello from rule_unary_expression().\n");
  if(rule_factor()){
    fmt::print("Fits rule_unary_expression.\n");
    return true;
  }
  
  if(scan_is_token(Token_id_bang)){
    debug_consume();
    tokens_consumed++;
    if(rule_factor()){
      fmt::print("Fits rule_unary_expression.\n");
      return true;
    }
  }
  go_back(tokens_consumed);
  return false;
}

bool Prp::rule_factor(){
  int tokens_consumed = 0;
  
  if(rule_rhs_expression()){
    fmt::print("Fits rule_factor (option 2).\n");
    return true;
  }
  
  fmt::print("Hello from rule_factor.\n");
  if(scan_is_token(Token_id_op)){
    fmt::print("rule_factor: found an open parenthesis.\n");
    debug_consume();
    tokens_consumed++;
    if(rule_logical_expression()){
      if(scan_is_token(Token_id_cp)){
        debug_consume();
        tokens_consumed++;
        rule_bit_selection_bracket(); // same problem as we saw in rule_additive_expression
        return true;
      }
      else{
        go_back(tokens_consumed);
        return false;
      }
    }
    else{
      go_back(tokens_consumed);
      return false;
    }
  }
  
  go_back(tokens_consumed);
  tokens_consumed = 0;
  go_back(tokens_consumed);
  return false;
}

/* FIXME: Incomplete */
bool Prp::rule_overload_notation(){
  return false;
}

/* FIXME: Incomplete */
bool Prp::rule_rhs_expression(){
  fmt::print("Hello from rule_rhs_expression().\n");
  return rule_lhs_expression();
}

void Prp::elaborate(){
  patch_pass(pyrope_keyword);
  ast = std::make_unique<Ast_parser>(get_buffer(), Prp_rule);
  ast->down();
  fmt::print("Starting to parse\n");
	while(!scan_is_end()){
		dump_token();
		eat_comments();
		if(scan_is_end()) return;
		if(!rule_top()){
			fmt::print("Something went wrong.\n");
			return;
		}
	}
	fmt::print("Finished Parsing\n");
  ast->up(Prp_rule_code_blocks);

  ast->each_bottom_first_fast(
  std::bind(&Prp::ast_handler, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  ast = nullptr;
}

/* Consumes a token and dumps the new one */
bool Prp::debug_consume(){
  fmt::print("Consuming token: ");
  dump_token();
  bool ok = scan_next();
  return ok;
}

/* Unconsumes a token and dumps it */
bool Prp::debug_unconsume(){
  fmt::print("Unconsuming token: ");
  bool ok = scan_prev();
  dump_token();
  return ok;
}

bool Prp::go_back(int num_tok){
  int i;
  bool ok;
  for(i=0;i<num_tok;i++){
    ok = debug_unconsume();
  }
  return ok;
}

void Prp::consume_block(Rule_id rid, int num_tok){
  int i;
  
  for(i=0;i<num_tok;i++){
    ast->add(rid, scan_token());
    debug_consume();
  }
}

/*void Prp::go_up(int num_levels){
  int i;
  for(i=1;i<num_levels;i++){
    ast->up_null();
  }
}*/

void Prp::ast_handler(const Tree_index &parent, const Tree_index &self, const Ast_parser_node &node){
  auto rule_value = node.rule_id;
  std::string rule_name;
  switch(rule_value){
    case Prp_invalid:
      rule_name.assign("Invalid");
      break;
    case Prp_rule:
      rule_name.assign("Program");
      break;
    case Prp_rule_code_blocks:
      rule_name.assign("Top level");
      break;
    case Prp_rule_code_block_int:
      rule_name.assign("Code block");
      break;
    case Prp_rule_assignment_expression:
      rule_name.assign("Assignment expression");
      break;
    case Prp_rule_logical_expression:
      rule_name.assign("Logical expression");
      break;
    case Prp_rule_relational_expression:
      rule_name.assign("Relational expression");
      break;
    case Prp_rule_additive_expression:
      rule_name.assign("Additive expression");
      break;
    case Prp_rule_bitwise_expression:
      rule_name.assign("Bitwise expression");
      break;
    case Prp_rule_multiplicative_expression:
      rule_name.assign("Multiplicative expression");
      break;
    case Prp_rule_unary_expression:
      rule_name.assign("Unary expressiion");
      break;
    case Prp_rule_factor:
      rule_name.assign("Factor");
      break;
    case Prp_rule_tuple_by_notation:
      rule_name.assign("Tuple by notation");
      break;
    case Prp_rule_tuple_notation_no_bracket:
      rule_name.assign("Tuple notation non bracket");
      break;
    case Prp_rule_tuple_notation:
      rule_name.assign("Tuple notation");
      break;
    case Prp_rule_tuple_notation_with_object:
      rule_name.assign("Tuple notation with object");
      break;
    case Prp_rule_range_notation:
      rule_name.assign("Range notation");
      break;
    case Prp_rule_bit_selection_bracket:
      rule_name.assign("Bit selection bracket");
      break;
    case Prp_rule_bit_selection_notation:
      rule_name.assign("Bit selection notation");
      break;
    case Prp_rule_tuple_array_bracket:
      rule_name.assign("Tuple array bracket");
      break;
    case Prp_rule_tuple_array_notation:
      rule_name.assign("Tuple array notation");
      break;
    case Prp_rule_lhs_expression:
      rule_name.assign("LHS expression");
      break;
    case Prp_rule_lhs_var_name:
      rule_name.assign("LHS variable name");
      break;
    case Prp_rule_rhs_expression_property:
      rule_name.assign("RHS expression property");
      break;
    case Prp_rule_rhs_expression:
      rule_name.assign("RHS expression");
      break;
    case Prp_rule_identifier:
      rule_name.assign("Identifier");
      break;
    case Prp_rule_constant:
      rule_name.assign("Constant");
      break;
    case Prp_rule_assignment_operator:
      rule_name.assign("Assignment operator");
      break;
    case Prp_rule_tuple_dot_notation:
      rule_name.assign("Tuple dot notation");
      break;
    case Prp_rule_tuple_dot_dot:
      rule_name.assign("Tuple dot dot");
      break;
  }
  auto token_text = scan_text(node.token_entry);
  fmt::print("Rule name: {}, Token text: {}, Tree level: {}\n", rule_name, token_text, self.level);
}
