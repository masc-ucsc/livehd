#include <ctype.h>
#include <algorithm>
#include <cstdio>
#include <iostream>

#include "prp.hpp"

void Prp::eat_comments(){
	while (scan_is_token(Token_id_comment) && !scan_is_end()) scan_next();
}

/* Like code_block_int
 * incomplete
 */
bool Prp::rule_top(){
	//printf("Hello from rule_top.\n");
	bool ok = rule_assignment_expression();
  //printf("Ok: %d\n", ok);
	if(ok) return true;
	
	return false;
}

/* incomplete */
bool Prp::rule_assignment_expression(){
	//printf("Hello from rule_assignment_expression.\n");
	if(rule_constant()){
		return false;
	}
	
	bool next = rule_lhs_expression();
	
	if(next){
		next = rule_assignment_operator();
		if(next){
			next = rule_rhs_expression_property() || rule_logical_expression();
      //printf("Fits rule_assignment_expression.\n");
			return true;
		}
	}
	return false;
}

/* NEED TO ADD: range notation rule */
bool Prp::rule_lhs_expression(){
  //printf("Hello from rule_lhs_expression.\n");
  return rule_tuple_notation();
}

/* incomplete */
bool Prp:: rule_rhs_expression_property(){
  //printf("Hello from rule_rhs_expression_property.\n");
  if(scan_is_token(Token_id_label)){
    debug_consume(); // consume the label
    rule_tuple_notation(); // optional
    return true;
  }
	return false;
}

bool Prp::rule_tuple_notation(){
  //printf("Hello from rule_tuple_notation.\n");
	int tokens_consumed = 0;
  
	/* first option */
	bool next = scan_is_token(Token_id_op);
	
	if(next){
		debug_consume(); // consume the LPAR
		tokens_consumed++;
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
	
  go_back(tokens_consumed);
	
	/* second option */
	next = scan_is_token(Token_id_op);
	tokens_consumed = 0;
	
	if(next){
		debug_consume(); // consume the LPAR
		tokens_consumed++;
		next = rule_rhs_expression_property() || rule_logical_expression();
		if(next){
			/* can be any number of the following */
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
		
  go_back(tokens_consumed);
  
  /* third option */
  next = scan_is_token(Token_id_op);
  tokens_consumed = 0;
  
  if(next){
    debug_consume();
    tokens_consumed++;
    next = scan_is_token(Token_id_cp);
    return next;
  }
	
	/* fourth option */
  
  return rule_bit_selection_notation();
}

bool Prp::rule_bit_selection_notation(){
  //printf("Hello from rule_bit_selection_notation.\n");
	bool next = rule_tuple_dot_notation();
	if (next){
		next = rule_bit_selection_bracket();
    if(next) //printf("Fits rule_bit_selection_notation.\n");
    return next;
	}
	return false;
}

bool Prp::rule_tuple_dot_notation(){
  //printf("Hello from rule_tuple_dot_notation.\n");
	bool next = rule_tuple_array_notation();
	if(next){
		next = rule_tuple_dot_dot();
		return next;
	}
	return false;
}

bool Prp::rule_tuple_dot_dot(){
  //printf("Hello from rule_tuple_dot_dot.\n");
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
  //printf("Fits rule_tuple_dot_dot.\n");
  return true;
}

bool Prp::rule_tuple_array_notation(){
  //printf("Hello from rule_tuple_array_notation.\n");
	bool next = rule_lhs_var_name();
	if(next){
		next = rule_tuple_array_bracket();
    if(next) //printf("Fits rule_tuple_array_notation.\n");
		return next;
	}
	return false;
}

bool Prp::rule_lhs_var_name(){
  //printf("Hello from rule_lhs_var_name.\n");
	bool next = rule_identifier() || rule_constant();
  if(next) //printf("Fits rule_lhs_var_name.\n");
	return next;
}

bool Prp::rule_tuple_array_bracket(){
  //printf("Hello from rule_tuple_array_bracket.\n");
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
  //printf("Fits rule_tuple_array_bracket.\n");
	return true;
}

bool Prp::rule_tuple_notation_no_bracket(){
  //printf("Hello from rule_tuple_notation_no_bracket.\n");
	/* bit_selection_notation+ */
	bool next = false;
	do{
		next = rule_bit_selection_notation();
	}while(rule_bit_selection_notation());
	
	return next;
}

bool Prp::rule_identifier(){
  //printf("Hello from rule_identifier.\n");
	if(!(scan_is_token(Token_id_register) || scan_is_token(Token_id_input) || scan_is_token(Token_id_output) || scan_is_token(Token_id_alnum) || scan_is_token(Token_id_label))){
		return false;
	}
	
	debug_consume(); // check for optional "?"
	
	if(scan_is_token(Token_id_qmark)){
		debug_consume();
	}
	//printf("Fits rule_identifier.\n");
	return true;
}

/* NEED TO ADD: support for string constants */
bool Prp::rule_constant(){
  //printf("Hello from rule_constant.\n");
  if(scan_is_token(Token_id_minus)){
    debug_consume();
  }
	if(scan_is_token(Token_id_num)){
    std::cout << "Found a constant: " << scan_text() << std::endl; 
		debug_consume();
		return true;
	}
	
	return false;
}

bool Prp::rule_assignment_operator(){
  //printf("Hello from rule_assignment_operator.\n");
  int tokens_consumed = 0;

	if(scan_is_token(Token_id_coloneq) || scan_is_token(Token_id_eq) || scan_is_token(Pyrope_id_as)){
    debug_consume(); // consume the operator
    //printf("Fits rule_assignment_operator.\n");
		return true;
	}
	
	/* op= tokens*/
  if (scan_is_token(Token_id_mult) || scan_is_token(Token_id_plus) || scan_is_token(Token_id_minus)){
    debug_consume();
    tokens_consumed++;
    if (scan_is_token(Token_id_eq)){
      debug_consume();
      //printf("Fits rule_assignment_operator.\n");
      return true;
    }
    return false;
  }
	
  go_back(tokens_consumed);
  tokens_consumed = 0;
	
	/* left and right shift */
  if (scan_is_token(Token_id_lt)){
    debug_consume();
    tokens_consumed++;
    if(scan_is_token(Token_id_lt)){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_eq)){
          debug_consume();
          //printf("Fits rule_assignment_operator.\n");
          return true;
        }
    }
    return false;
  }
  
  go_back(tokens_consumed);
  tokens_consumed = 0;
  
  if (scan_is_token(Token_id_gt)){
    debug_consume();
    tokens_consumed++;
    if(scan_is_token(Token_id_gt)){
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_eq)){
        debug_consume();
        //printf("Fits rule_assignment_operator.\n");
        return true;
      }
    }
    return false;
  }
	
	return false;
}

bool Prp::rule_tuple_by_notation(){
  //printf("Hello from rule_tuple_by_notation.\n");
  if (scan_is_token(Pyrope_id_by)){
    debug_consume();
    if(rule_lhs_var_name()){
      //printf("Fits rule_tuple_by_notation.\n");
      return true;
    }
    debug_unconsume();
  }
  
  return false;
}

bool Prp::rule_bit_selection_bracket(){
  //printf("Hello from rule_bit_selection_bracket.\n");
  bool next = true;
  int tokens_consumed = 0;
  
  // zero or more of the following
  while(next){
    next = false;
    
    if(scan_is_token(Token_id_ob)){
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_ob)){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_cb)){
          debug_consume();
          tokens_consumed++;
          if(scan_is_token(Token_id_cb)){
            debug_consume();
            tokens_consumed++;
            next = true;
          }
        }
        
        else if(rule_logical_expression() || rule_tuple_notation_no_bracket()){
          if(scan_is_token(Token_id_cb)){
            debug_consume();
            tokens_consumed++;
            if(scan_is_token(Token_id_cb)){
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
  //printf("Fits rule_bit_selection_bracket.\n");
  return true;
}

bool Prp::rule_logical_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  //printf("Hello from rule_logical_expression.\n");
  if (rule_relational_expression()){
    /* zero or more of the following */
    while(next){
      next = scan_is_token(Pyrope_id_or) || scan_is_token(Pyrope_id_and);
      if(next){
        debug_consume();
        tokens_consumed++;
        if (!rule_relational_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    //printf("Fits rule_logical_expression.\n");
    return true;
  }
  return false;
}

bool Prp::rule_relational_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  //printf("Hello from rule_relational_expression.\n");
  if(rule_additive_expression()){
    /* zero or more of the following */
    while(next){
      next = scan_is_token(Token_id_le) || scan_is_token(Token_id_ge) || scan_is_token(Token_id_lt) || scan_is_token(Token_id_gt) || scan_is_token(Token_id_same) || scan_is_token(Token_id_diff) || scan_is_token(Pyrope_id_is);
      if(next){
        debug_consume();
        tokens_consumed++;
        if (!rule_additive_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    //printf("Fits rule_relational_expression.\n");
    return true;
  }
  return false;
}

/* NEED TO ADD: overload notation */
bool Prp::rule_additive_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  //printf("Hello from rule_additive_expression.\n");
  
  if(rule_bitwise_expression()){
    /* zero or more of the following */
    while(next){
      //printf("Entering main loop of rule_additive_expression; current token: \n");
      // dump_token();
      next = scan_is_token(Token_id_plus);
      if(next){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_plus)){ // increment operator
          debug_consume();
          tokens_consumed++;
        }
        if(!rule_bitwise_expression()){
          //printf("rule_additive_expression: going back1.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
      
      next = scan_is_token(Token_id_mult);
      if(next){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_mult)){ // ** operator
          debug_consume();
          tokens_consumed++;
          if(!rule_bitwise_expression()){
            //printf("rule_additive_expression: going back2.\n");
            go_back(tokens_consumed);
            return false;
          }
        }
      }
      
      next = scan_is_token(Token_id_lt);
      if(next){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_lt)){
          debug_consume();
          tokens_consumed++;
          if(!rule_bitwise_expression()){
            //printf("rule_additive_expression: going back3.\n");
            go_back(tokens_consumed);
            return false;
          }
        }
        else { // unlike the previous, one token isn't enough
          //printf("rule_additive_expression: going back4.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
      
      next = scan_is_token(Token_id_gt);
      if(next){
        debug_consume();
        tokens_consumed++;
        if(scan_is_token(Token_id_gt)){
          debug_consume();
          tokens_consumed++;
          if(!rule_bitwise_expression()){
            //printf("rule_additive_expression: going back5.\n");
            go_back(tokens_consumed);
            return false;
          }
        }
        else {
          //printf("rule_additive_expression: going back6.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
      
      next = (scan_is_token(Token_id_minus) || scan_is_token(Pyrope_id_union) || scan_is_token(Pyrope_id_intersect));
      if(next){
        debug_consume();
        tokens_consumed++;
        if(!rule_bitwise_expression()){
          //printf("rule_additive_expression: going back7.\n");
          go_back(tokens_consumed);
          return false;
        }
        else{
          //printf("rule_additive_expression: going back8.\n");
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    
    /* optional */
    if(scan_is_token(Token_id_dot)){
      debug_consume();
      tokens_consumed++;
      if(scan_is_token(Token_id_dot)){
        debug_consume();
        tokens_consumed++;
        /* NEED TO FIX: this is optional, but it must either fully fit rule_additive_expression or
         not fit it at all, this will miss if there is an incorrectly described additive expression
         because it will just return false, the same as if it weren't there at all. */
        //printf("rule_additive_expression: danger.\n");
        rule_additive_expression();
      }
    }
    //printf("Fits rule_additive_expression.\n");
    return true;
  }
  return false;
}

bool Prp::rule_bitwise_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  //printf("Hello from rule_bitwise_expression().\n");
  if(rule_multiplicative_expression()){
    /* zero or more of the following */
    while(next){
      next = scan_is_token(Token_id_pipe) || scan_is_token(Token_id_and) || scan_is_token(Token_id_xor);
      if(next){
        debug_consume();
        tokens_consumed++;
        if(!rule_multiplicative_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    //printf("Fits rule_bitwise_expression.\n");
    return true;
  }
  return false;
}

bool Prp::rule_multiplicative_expression(){
  int tokens_consumed = 0;
  bool next = true;
  
  //printf("Hello from rule_multiplicative_expression.\n");
  if(rule_unary_expression()){
    while(next){
      next = scan_is_token(Token_id_mult) || scan_is_token(Token_id_div);
      if(next){
        if(!rule_unary_expression()){
          go_back(tokens_consumed);
          return false;
        }
      }
    }
    //printf("Fits rule_multiplicative_expression.\n");
    return true;
  }
  
  return false;
}

bool Prp::rule_unary_expression(){
  //printf("Hello from rule_unary_expression().\n");
  if(rule_factor()){
    //printf("Fits rule_unary_expression.\n");
    return true;
  }
  
  if(scan_is_token(Token_id_bang)){
    debug_consume();
    if(rule_factor()){
      //printf("Fits rule_unary_expression.\n");
      return true;
    }
    debug_unconsume();
  }
  
  return false;
}

bool Prp::rule_factor(){
  int tokens_consumed = 0;
  
  //printf("Hello from rule_factor.\n");
  if(scan_is_token(Token_id_ob)){
    debug_consume();
    tokens_consumed++;
    if(rule_logical_expression()){
      if(scan_is_token(Token_id_cb)){
        debug_consume();
        tokens_consumed++;
        rule_bit_selection_bracket(); // same problem as we saw in rule_additive_expression
        //printf("Fits rule_factor.\n");
        return true;
      }
    }
  }
  
  go_back(tokens_consumed);
  
  if(rule_rhs_expression()){
    //printf("Fits rule_factor.\n");
    return true;
  }
  return false;
}

/* INCOMPLETE*/
bool Prp::rule_rhs_expression(){
  //printf("Hello from rule_rhs_expression().\n");
  return rule_lhs_expression();
}

void Prp::elaborate(){
  patch_pass(pyrope_keyword);
  //printf("Beginning elaboration.\n");
	while(!scan_is_end()){
		dump_token();
		eat_comments();
		if(scan_is_end()) return;
		if(!rule_top()){
			//printf("Something went wrong.\n");
			return;
		}
	}
}

/* Consumes a token and dumps the new one */
bool Prp::debug_consume(){
  //printf("Consuming token: ");
  // dump_token();
  bool ok = scan_next();
  return ok;
}

/* Unconsumes a token and dumps it */
bool Prp::debug_unconsume(){
  //printf("Unconsuming token: ");
  bool ok = scan_prev();
  // dump_token();
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
