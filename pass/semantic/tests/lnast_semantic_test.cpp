
#include "semantic_check.hpp"
#include "lnast.hpp"
#include <iostream>

int main(void) {

  int line_num, pos1, pos2 = 0;
  Lnast* lnast = new Lnast();
  std::cout << "Working\n";
  Semantic_check s;

  // // Testing Assign Operations ========================================================================================

  // auto idx_root    = Lnast_node::create_top    ("top", line_num, pos1, pos2);
  // auto node_stmts  = Lnast_node::create_stmts  ("stmts0", line_num, pos1, pos2);
  // auto node_assign = Lnast_node::create_assign ("assign", line_num, pos1, pos2);
  // auto node_target = Lnast_node::create_ref    ("val", line_num, pos1, pos2);
  // auto node_const  = Lnast_node::create_const  ("0d1023u10", line_num, pos1, pos2);

  // lnast->set_root(idx_root);
  
  // auto idx_stmts   = lnast->add_child(lnast->get_root(), node_stmts);
  // auto idx_assign  = lnast->add_child(idx_stmts,  node_assign);
  // auto idx_target  = lnast->add_child(idx_assign, node_target);
  // auto idx_const   = lnast->add_child(idx_assign, node_const);

  // Warning: val

  // ==================================================================================================================

  // Testing N-ary + U-nary Operations ================================================================================ 

  // auto idx_root    = Lnast_node::create_top       ("top",  line_num, pos1, pos2);
  // auto node_stmts  = Lnast_node::create_stmts     ("stmts",  line_num, pos1, pos2);
  // auto node_minus  = Lnast_node::create_minus     ("minus",  line_num, pos1, pos2);
  // auto node_lhs1   = Lnast_node::create_ref       ("___a", line_num, pos1, pos2);
  // auto node_op1    = Lnast_node::create_ref       ("x",    line_num, pos1, pos2);
  // auto node_op2    = Lnast_node::create_const     ("0d1",  line_num, pos1, pos2);

  // auto node_plus   = Lnast_node::create_plus      ("plus",  line_num, pos1, pos2);
  // auto node_lhs2   = Lnast_node::create_ref       ("___b", line_num, pos1, pos2);
  // auto node_op3    = Lnast_node::create_ref       ("___a", line_num, pos1, pos2);
  // auto node_op4    = Lnast_node::create_const     ("0d3",  line_num, pos1, pos2);
  // auto node_op5    = Lnast_node::create_const     ("0d2",  line_num, pos1, pos2);

  // auto node_dpa    = Lnast_node::create_dp_assign ("dp_assign",  line_num, pos1, pos2);
  // auto node_lhs3   = Lnast_node::create_ref       ("total", line_num, pos1, pos2);
  // auto node_op6    = Lnast_node::create_ref       ("___b",  line_num, pos1, pos2);

  // lnast->set_root(idx_root);

  // auto idx_stmts   = lnast->add_child(lnast->get_root(), node_stmts);
  // auto idx_minus   = lnast->add_child(idx_stmts, node_minus);
  // auto idx_lhs1    = lnast->add_child(idx_minus, node_lhs1);
  // auto idx_op1     = lnast->add_child(idx_minus, node_op1); 
  // auto idx_op2     = lnast->add_child(idx_minus, node_op2);

  // auto idx_plus    = lnast->add_child(idx_stmts, node_plus);
  // auto idx_lhs2    = lnast->add_child(idx_plus,  node_lhs2);
  // auto idx_op3     = lnast->add_child(idx_plus,  node_op3);
  // auto idx_op4     = lnast->add_child(idx_plus,  node_op4);
  // auto idx_op5     = lnast->add_child(idx_plus,  node_op5);

  // auto idx_assign  = lnast->add_child(idx_stmts,  node_dpa);
  // auto idx_lhs3    = lnast->add_child(idx_assign, node_lhs3);
  // auto idx_op6     = lnast->add_child(idx_assign, node_op6);

  // Warning: total

  // ==================================================================================================================

  // Testing If Operation =============================================================================================
  
  // auto idx_root   = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  // auto idx_if     = lnast->add_child(idx_stmts0, Lnast_node::create_if    ("if",  line_num, pos1, pos2));

  // auto idx_cstmts = lnast->add_child(idx_if,     Lnast_node::create_cstmts("cstmts",  line_num, pos1, pos2));
  // auto idx_gt     = lnast->add_child(idx_cstmts, Lnast_node::create_gt    ("gt",  line_num, pos1, pos2));
  // auto idx_lhs1   = lnast->add_child(idx_gt,     Lnast_node::create_ref   ("___a",  line_num, pos1, pos2));
  // auto idx_op1    = lnast->add_child(idx_gt,     Lnast_node::create_ref   ("a",  line_num, pos1, pos2));
  // auto idx_op2    = lnast->add_child(idx_gt,     Lnast_node::create_const ("0d3",  line_num, pos1, pos2));

  // auto idx_cond1  = lnast->add_child(idx_if,     Lnast_node::create_cond  ("cond",  line_num, pos1, pos2));
  // auto condition  = lnast->add_child(idx_cond1,  Lnast_node::create_ref   ("___a", line_num, pos1, pos2));

  // auto idx_stmts1 = lnast->add_child(idx_if,     Lnast_node::create_stmts ("stmts1",  line_num, pos1, pos2));
  // auto idx_plus   = lnast->add_child(idx_stmts1, Lnast_node::create_plus  ("plus",  line_num, pos1, pos2));
  // auto idx_lhs2   = lnast->add_child(idx_plus,   Lnast_node::create_ref   ("___b",  line_num, pos1, pos2));
  // auto idx_op3    = lnast->add_child(idx_plus,   Lnast_node::create_ref   ("a",  line_num, pos1, pos2));  
  // auto idx_op4    = lnast->add_child(idx_plus,   Lnast_node::create_const ("0d1",  line_num, pos1, pos2));

  // auto idx_assign = lnast->add_child(idx_stmts1, Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  // auto idx_lhs3   = lnast->add_child(idx_assign, Lnast_node::create_ref   ("a",  line_num, pos1, pos2));
  // auto idx_op5    = lnast->add_child(idx_assign, Lnast_node::create_ref   ("___b",  line_num, pos1, pos2));

  // No Warnings 

  // Testing If Operation (inefficient) ===============================================================================

  auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  lnast->set_root(idx_root);

  auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  auto idx_if      = lnast->add_child(idx_stmts0,  Lnast_node::create_if    ("if",  line_num, pos1, pos2));

  auto idx_cstmts  = lnast->add_child(idx_if,      Lnast_node::create_cstmts("cstmts",  line_num, pos1, pos2));
  auto idx_gt      = lnast->add_child(idx_cstmts,  Lnast_node::create_gt    ("gt",  line_num, pos1, pos2));
  auto idx_lhs1    = lnast->add_child(idx_gt,      Lnast_node::create_ref   ("___a",  line_num, pos1, pos2));
  auto idx_op1     = lnast->add_child(idx_gt,      Lnast_node::create_ref   ("a",  line_num, pos1, pos2));
  auto idx_op2     = lnast->add_child(idx_gt,      Lnast_node::create_const ("0d3",  line_num, pos1, pos2));

  auto idx_cond1   = lnast->add_child(idx_if,      Lnast_node::create_cond  ("cond",  line_num, pos1, pos2));
  auto condition   = lnast->add_child(idx_cond1,   Lnast_node::create_ref   ("___a", line_num, pos1, pos2)); 

  auto idx_stmts1  = lnast->add_child(idx_if,      Lnast_node::create_stmts ("stmts1",  line_num, pos1, pos2));
  
  auto idx_assign1 = lnast->add_child(idx_stmts1,  Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref   ("___b",  line_num, pos1, pos2));
  auto idx_op3     = lnast->add_child(idx_assign1,  Lnast_node::create_const("0d1",  line_num, pos1, pos2));

  auto idx_assign2 = lnast->add_child(idx_stmts1,  Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  auto idx_lhs3    = lnast->add_child(idx_assign2,  Lnast_node::create_ref  ("___c",  line_num, pos1, pos2));
  auto idx_op4     = lnast->add_child(idx_assign2,  Lnast_node::create_ref  ("___b",  line_num, pos1, pos2));

  auto idx_plus    = lnast->add_child(idx_stmts1,  Lnast_node::create_plus  ("plus",  line_num, pos1, pos2));
  auto idx_lhs4    = lnast->add_child(idx_plus,    Lnast_node::create_ref   ("___d",  line_num, pos1, pos2));
  auto idx_op5     = lnast->add_child(idx_plus,    Lnast_node::create_ref   ("___c",  line_num, pos1, pos2));  
  auto idx_op6     = lnast->add_child(idx_plus,    Lnast_node::create_const ("0d1",  line_num, pos1, pos2));

  auto idx_assign3 = lnast->add_child(idx_stmts1,  Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  auto idx_lhs5    = lnast->add_child(idx_assign3,  Lnast_node::create_ref  ("a",  line_num, pos1, pos2));
  auto idx_op7     = lnast->add_child(idx_assign3,  Lnast_node::create_ref  ("___d",  line_num, pos1, pos2));

  // Warning: ___c

  // ==================================================================================================================

  // Testing If Operation =============================================================================================

  // auto idx_root   = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  // auto idx_if      = lnast->add_child(idx_stmts0, Lnast_node::create_if    ("if",  line_num, pos1, pos2));

  // auto idx_cstmts1 = lnast->add_child(idx_if,     Lnast_node::create_cstmts("cstmts",  line_num, pos1, pos2));
  // auto idx_gt      = lnast->add_child(idx_cstmts1, Lnast_node::create_gt    ("gt",  line_num, pos1, pos2)); 
  // auto idx_lhs1    = lnast->add_child(idx_gt,     Lnast_node::create_ref   ("___a",  line_num, pos1, pos2));
  // auto idx_op1     = lnast->add_child(idx_gt,     Lnast_node::create_ref   ("a",  line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_gt,     Lnast_node::create_const ("0d10",  line_num, pos1, pos2));

  // auto idx_cond1   = lnast->add_child(idx_if,     Lnast_node::create_cond  ("___a",  line_num, pos1, pos2));
  // auto condition1  = lnast->add_child(idx_cond1,  Lnast_node::create_ref   ("___a", line_num, pos1, pos2));

  // auto idx_stmts1  = lnast->add_child(idx_if,     Lnast_node::create_stmts ("stmts1",  line_num, pos1, pos2));
  // auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  // auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref   ("b",  line_num, pos1, pos2));
  // auto idx_op3     = lnast->add_child(idx_assign1, Lnast_node::create_const ("0d3",  line_num, pos1, pos2));

  // auto idx_cstmts2 = lnast->add_child(idx_if,     Lnast_node::create_cstmts("cstmts2",  line_num, pos1, pos2));
  // auto idx_lt      = lnast->add_child(idx_cstmts2, Lnast_node::create_lt    ("lt",  line_num, pos1, pos2)); 
  // auto idx_lhs3    = lnast->add_child(idx_lt,     Lnast_node::create_ref   ("___b",  line_num, pos1, pos2));
  // auto idx_op4     = lnast->add_child(idx_lt,     Lnast_node::create_ref   ("a",  line_num, pos1, pos2));
  // auto idx_op5     = lnast->add_child(idx_lt,     Lnast_node::create_const ("0d1",  line_num, pos1, pos2));

  // auto idx_cond2   = lnast->add_child(idx_if,     Lnast_node::create_cond  ("___b",  line_num, pos1, pos2));
  // auto condition2  = lnast->add_child(idx_cond2,  Lnast_node::create_ref   ("___b", line_num, pos1, pos2));


  // auto idx_stmts2  = lnast->add_child(idx_if,     Lnast_node::create_stmts ("stmts2",  line_num, pos1, pos2));
  // auto idx_assign2 = lnast->add_child(idx_stmts2, Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  // auto idx_lhs4    = lnast->add_child(idx_assign2, Lnast_node::create_ref   ("b",  line_num, pos1, pos2));
  // auto idx_op6     = lnast->add_child(idx_assign2, Lnast_node::create_const ("0d2",  line_num, pos1, pos2));
      
  // auto idx_stmts3  = lnast->add_child(idx_if,     Lnast_node::create_stmts ("stmts3",  line_num, pos1, pos2));
  // auto idx_assign3 = lnast->add_child(idx_stmts3, Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  // auto idx_lhs5    = lnast->add_child(idx_assign3, Lnast_node::create_ref   ("b",  line_num, pos1, pos2));
  // auto idx_op7     = lnast->add_child(idx_assign3, Lnast_node::create_const ("0d3",  line_num, pos1, pos2));
  
  // Warning: b

  // ==================================================================================================================

  // For Loop Operation ===============================================================================================
  
  // auto idx_root      = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0    = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0", line_num, pos1, pos2));
  // auto idx_tup      = lnast->add_child(idx_stmts0, Lnast_node::create_tuple  ("tuple", line_num, pos1, pos2)); 
  // auto idx_tname     = lnast->add_child(idx_tup,    Lnast_node::create_ref   ("___b", line_num, pos1, pos2));

  // auto idx_assign1   = lnast->add_child(idx_tup,    Lnast_node::create_assign("assign", line_num, pos1, pos2));
  // auto idx_lhs1      = lnast->add_child(idx_assign1, Lnast_node::create_ref  ("___range_begin", line_num, pos1, pos2));
  // auto idx_op1       = lnast->add_child(idx_assign1, Lnast_node::create_const("0d0", line_num, pos1, pos2));

  // auto idx_assign2   = lnast->add_child(idx_tup,    Lnast_node::create_assign("assign", line_num, pos1, pos2));
  // auto idx_lhs2      = lnast->add_child(idx_assign2, Lnast_node::create_ref  ("___range_end", line_num, pos1, pos2));
  // auto idx_op2       = lnast->add_child(idx_assign2, Lnast_node::create_const("0d3", line_num, pos1, pos2));

  // auto idx_for       = lnast->add_child(idx_stmts0, Lnast_node::create_for   ("for", line_num, pos1, pos2));
  // auto idx_stmts1    = lnast->add_child(idx_for,    Lnast_node::create_stmts ("stmts1", line_num, pos1, pos2)); 
  // auto idx_itr       = lnast->add_child(idx_for,    Lnast_node::create_ref   ("i", line_num, pos1, pos2));
  // auto idx_itr_range = lnast->add_child(idx_for,    Lnast_node::create_ref   ("___b", line_num, pos1, pos2));

  // auto idx_select1   = lnast->add_child(idx_stmts1, Lnast_node::create_select("select", line_num, pos1, pos2));  
  // auto idx_lhs3      = lnast->add_child(idx_select1, Lnast_node::create_ref  ("___d", line_num, pos1, pos2));
  // auto idx_op3       = lnast->add_child(idx_select1, Lnast_node::create_ref  ("tup_foo", line_num, pos1, pos2));
  // auto idx_op4       = lnast->add_child(idx_select1, Lnast_node::create_ref  ("i", line_num, pos1, pos2));

  // auto idx_minus     = lnast->add_child(idx_stmts1, Lnast_node::create_minus ("minus", line_num, pos1, pos2));  
  // auto idx_lhs4      = lnast->add_child(idx_minus,  Lnast_node::create_ref   ("___g", line_num, pos1, pos2));
  // auto idx_op5       = lnast->add_child(idx_minus,  Lnast_node::create_const  ("0d3", line_num, pos1, pos2));
  // auto idx_op6       = lnast->add_child(idx_minus,  Lnast_node::create_ref   ("i", line_num, pos1, pos2));

  // auto idx_select2   = lnast->add_child(idx_stmts1, Lnast_node::create_select("select", line_num, pos1, pos2));  
  // auto idx_lhs5      = lnast->add_child(idx_select2, Lnast_node::create_ref  ("___f", line_num, pos1, pos2));
  // auto idx_op7       = lnast->add_child(idx_select2, Lnast_node::create_ref  ("tup_bar", line_num, pos1, pos2));
  // auto idx_op8       = lnast->add_child(idx_select2, Lnast_node::create_ref  ("___g", line_num, pos1, pos2));

  // auto idx_assign3   = lnast->add_child(idx_stmts1, Lnast_node::create_assign("assign", line_num, pos1, pos2));
  // auto idx_lhs6      = lnast->add_child(idx_assign3, Lnast_node::create_ref  ("___d", line_num, pos1, pos2));
  // auto idx_op9       = lnast->add_child(idx_assign3, Lnast_node::create_ref  ("___f", line_num, pos1, pos2));

  // Warning: ___range_begin, ___range_end

  // ==================================================================================================================

  // While Loop Operation =============================================================================================

  // auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));

  // auto idx_while   = lnast->add_child(idx_stmts0,  Lnast_node::create_while("while",  line_num, pos1, pos2));
  // auto idx_cond    = lnast->add_child(idx_while,   Lnast_node::create_cond ("___a",  line_num, pos1, pos2));
  // auto idx_stmts1  = lnast->add_child(idx_while,   Lnast_node::create_stmts("stmts",  line_num, pos1, pos2));
  // auto idx_ref     = lnast->add_child(idx_cond,    Lnast_node::create_ref  ("true", line_num, pos1, pos2));

  // auto idx_minus   = lnast->add_child(idx_stmts1, Lnast_node::create_minus ("minus", line_num, pos1, pos2));  
  // auto idx_lhs1    = lnast->add_child(idx_minus,  Lnast_node::create_ref   ("___b", line_num, pos1, pos2));
  // auto idx_op1     = lnast->add_child(idx_minus,  Lnast_node::create_const ("0d3", line_num, pos1, pos2));

  // auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_node::create_assign("assign", line_num, pos1, pos2));
  // auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref  ("total", line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_assign1, Lnast_node::create_ref  ("___b", line_num, pos1, pos2));

  // Warning: total

  // ==================================================================================================================

  // Func Def Operation ===============================================================================================
  
  // auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  // auto idx_func    = lnast->add_child(idx_stmts0, Lnast_node::create_func_def("func_def",  line_num, pos1, pos2));

  // auto idx_fname   = lnast->add_child(idx_func,   Lnast_node::create_ref   ("func_xor",  line_num, pos1, pos2));
  // auto idx_cond    = lnast->add_child(idx_func,   Lnast_node::create_cond  ("condition",  line_num, pos1, pos2));
  // auto idx_stmts1  = lnast->add_child(idx_func,   Lnast_node::create_stmts ("stmts",  line_num, pos1, pos2));
  // auto idx_io1     = lnast->add_child(idx_func,   Lnast_node::create_ref   ("$a",  line_num, pos1, pos2));
  // auto idx_io2     = lnast->add_child(idx_func,   Lnast_node::create_ref   ("$b",  line_num, pos1, pos2));
  // auto idx_io3     = lnast->add_child(idx_func,   Lnast_node::create_ref   ("%out",  line_num, pos1, pos2));

  // auto idx_ref     = lnast->add_child(idx_cond,    Lnast_node::create_ref  ("true", line_num, pos1, pos2));

  // auto idx_xor     = lnast->add_child(idx_stmts1, Lnast_node::create_xor   ("xor",  line_num, pos1, pos2));
  // auto idx_lhs_1   = lnast->add_child(idx_xor,    Lnast_node::create_ref   ("___b",  line_num, pos1, pos2));
  // auto idx_op1_1   = lnast->add_child(idx_xor,    Lnast_node::create_ref   ("$a",  line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_xor,    Lnast_node::create_ref   ("$b",  line_num, pos1, pos2));

  // auto idx_assign  = lnast->add_child(idx_stmts1, Lnast_node::create_assign("assign",  line_num, pos1, pos2));
  // auto idx_lhs_2   = lnast->add_child(idx_assign, Lnast_node::create_ref   ("%out",  line_num, pos1, pos2));
  // auto idx_op1_2   = lnast->add_child(idx_assign, Lnast_node::create_ref   ("___b",  line_num, pos1, pos2));

  // Warning: func_xor

  // ==================================================================================================================

  // Conditional Func Def Operation ===================================================================================
  
  // auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0   = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));  auto idx_func     = lnast->add_child(idx_stmts0, Lnast_node::create_func_def("stmts0",  line_num, pos1, pos2));

  // auto idx_fname    = lnast->add_child(idx_func,    Lnast_node::create_ref    ("func_xor",  line_num, pos1, pos2));
  // auto idx_cond     = lnast->add_child(idx_func,    Lnast_node::create_cond   ("$valid",  line_num, pos1, pos2));
  // auto idx_ref      = lnast->add_child(idx_cond,    Lnast_node::create_ref    ("true", line_num, pos1, pos2));
  // auto idx_stmts1   = lnast->add_child(idx_func,    Lnast_node::create_stmts  ("stmts1",  line_num, pos1, pos2));
  // auto idx_io1      = lnast->add_child(idx_func,    Lnast_node::create_ref    ("$a",  line_num, pos1, pos2));
  // auto idx_io2      = lnast->add_child(idx_func,    Lnast_node::create_ref    ("$b",  line_num, pos1, pos2));
  // auto idx_io3      = lnast->add_child(idx_func,    Lnast_node::create_ref    ("___a",  line_num, pos1, pos2));
  // auto idx_io4      = lnast->add_child(idx_func,    Lnast_node::create_ref    ("%out",  line_num, pos1, pos2));

  // auto idx_gt       = lnast->add_child(idx_stmts1,  Lnast_node::create_gt     ("gt", line_num, pos1, pos2));
  // auto idx_lh1      = lnast->add_child(idx_gt,      Lnast_node::create_ref    ("___a", line_num, pos1, pos2));
  // auto idx_op1      = lnast->add_child(idx_gt,      Lnast_node::create_ref    ("a", line_num, pos1, pos2));
  // auto idx_op2      = lnast->add_child(idx_gt,      Lnast_node::create_const  ("0d3", line_num, pos1, pos2));

  // auto idx_xor      = lnast->add_child(idx_stmts1,  Lnast_node::create_xor    ("xor",  line_num, pos1, pos2)); 
  // auto idx_lhs2     = lnast->add_child(idx_xor,     Lnast_node::create_ref    ("___b",  line_num, pos1, pos2));
  // auto idx_op3      = lnast->add_child(idx_xor,     Lnast_node::create_ref    ("$a",  line_num, pos1, pos2));
  // auto idx_op4      = lnast->add_child(idx_xor,     Lnast_node::create_ref    ("$b",  line_num, pos1, pos2));

  // auto idx_assign1  = lnast->add_child(idx_stmts1,  Lnast_node::create_assign ("assign",  line_num, pos1, pos2));
  // auto idx_lhs3     = lnast->add_child(idx_assign1, Lnast_node::create_ref    ("%out",  line_num, pos1, pos2));
  // auto idx_op5      = lnast->add_child(idx_assign1, Lnast_node::create_ref    ("___b",  line_num, pos1, pos2));

  // Warning: func_xor

  // ==================================================================================================================

  // Implicit Func Call Operation =====================================================================================

  // auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  // auto idx_func    = lnast->add_child(idx_stmts0 , Lnast_node::create_func_def("func_def",  line_num, pos1, pos2));

  // auto idx_fname   = lnast->add_child(idx_func   , Lnast_node::create_ref      ("func_xor",  line_num, pos1, pos2));
  // auto idx_cond    = lnast->add_child(idx_func   , Lnast_node::create_cond     ("true",  line_num, pos1, pos2));

  // auto idx_ref     = lnast->add_child(idx_cond   , Lnast_node::create_const    ("true", line_num, pos1, pos2));

  // auto idx_stmts1  = lnast->add_child(idx_func   , Lnast_node::create_stmts    ("stmts1",  line_num, pos1, pos2));
  // auto idx_io1     = lnast->add_child(idx_func   , Lnast_node::create_ref      ("$a",  line_num, pos1, pos2));
  // auto idx_io2     = lnast->add_child(idx_func   , Lnast_node::create_ref      ("$b",  line_num, pos1, pos2));
  // auto idx_io3     = lnast->add_child(idx_func   , Lnast_node::create_ref      ("$out",  line_num, pos1, pos2));

  // auto idx_xor     = lnast->add_child(idx_stmts1 , Lnast_node::create_xor      ("xor",  line_num, pos1, pos2)); 
  // auto idx_lhs1    = lnast->add_child(idx_xor    , Lnast_node::create_ref      ("___b",  line_num, pos1, pos2));
  // auto idx_op1     = lnast->add_child(idx_xor    , Lnast_node::create_ref      ("$a",  line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_xor    , Lnast_node::create_ref      ("$b",  line_num, pos1, pos2)); 

  // auto idx_assign1 = lnast->add_child(idx_stmts1 , Lnast_node::create_assign   ("assign1",  line_num, pos1, pos2));
  // auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref      ("%out",  line_num, pos1, pos2));
  // auto idx_op3     = lnast->add_child(idx_assign1, Lnast_node::create_ref      ("___b",  line_num, pos1, pos2));

  // auto idx_assign2 = lnast->add_child(idx_stmts0 , Lnast_node::create_assign   ("assign2",  line_num, pos1, pos2));
  // auto idx_lhs3    = lnast->add_child(idx_assign2, Lnast_node::create_ref      ("func_xor",  line_num, pos1, pos2));
  // auto idx_op4     = lnast->add_child(idx_assign2, Lnast_node::create_ref      ("___a",  line_num, pos1, pos2));

  // auto idx_tup     = lnast->add_child(idx_stmts0 , Lnast_node::create_tuple    ("tuple",  line_num, pos1, pos2)); 
  // auto idx_tname   = lnast->add_child(idx_tup    , Lnast_node::create_ref      ("___d",  line_num, pos1, pos2));
  // auto idx_assign3 = lnast->add_child(idx_tup    , Lnast_node::create_assign   ("assign3",  line_num, pos1, pos2));
  // auto idx_lhs4    = lnast->add_child(idx_assign3, Lnast_node::create_ref      ("null",  line_num, pos1, pos2));
  // auto idx_op5     = lnast->add_child(idx_assign3, Lnast_node::create_ref      ("$foo",  line_num, pos1, pos2));

  // auto idx_assign4 = lnast->add_child(idx_tup    , Lnast_node::create_assign   ("assign4",  line_num, pos1, pos2));
  // auto idx_lhs5    = lnast->add_child(idx_assign4, Lnast_node::create_ref      ("null",  line_num, pos1, pos2));
  // auto idx_op6     = lnast->add_child(idx_assign4, Lnast_node::create_ref      ("$bar",  line_num, pos1, pos2)); 

  // auto idx_fcall   = lnast->add_child(idx_stmts0 , Lnast_node::create_func_call("func_call",  line_num, pos1, pos2));
  // auto idx_lhs6    = lnast->add_child(idx_fcall  , Lnast_node::create_ref      ("my_xor",  line_num, pos1, pos2));
  // auto idx_target  = lnast->add_child(idx_fcall  , Lnast_node::create_ref      ("func_xor",  line_num, pos1, pos2));
  // auto idx_arg     = lnast->add_child(idx_fcall  , Lnast_node::create_ref      ("___d",  line_num, pos1, pos2));

  // auto idx_dot     = lnast->add_child(idx_stmts0 , Lnast_node::create_dot      ("dot",  line_num, pos1, pos2)); 
  // auto idx_lhs7    = lnast->add_child(idx_dot    , Lnast_node::create_ref      ("___f",  line_num, pos1, pos2));
  // auto idx_op7     = lnast->add_child(idx_dot    , Lnast_node::create_ref      ("my_xor",  line_num, pos1, pos2));
  // auto idx_op8     = lnast->add_child(idx_dot    , Lnast_node::create_ref      ("out",  line_num, pos1, pos2));

  // auto idx_assign5 = lnast->add_child(idx_stmts0 , Lnast_node::create_assign   ("assign5",  line_num, pos1, pos2));
  // auto idx_lhs8    = lnast->add_child(idx_assign5, Lnast_node::create_ref      ("%out",  line_num, pos1, pos2));
  // auto idx_op9     = lnast->add_child(idx_assign5, Lnast_node::create_ref      ("___f",  line_num, pos1, pos2));

  // Warning: null

  // ==================================================================================================================

  // Explicit Func Call Operation =====================================================================================

  // auto idx_root   = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  // auto idx_func    = lnast->add_child(idx_stmts0 , Lnast_node::create_func_def("func_def",  line_num, pos1, pos2));

  // auto idx_fname   = lnast->add_child(idx_func   , Lnast_node::create_ref      ("func_xor",  line_num, pos1, pos2));
  // auto idx_cond    = lnast->add_child(idx_func   , Lnast_node::create_cond     ("true",  line_num, pos1, pos2));
  // auto idx_ref     = lnast->add_child(idx_cond   , Lnast_node::create_const    ("true", line_num, pos1, pos2));
  // auto idx_stmts1  = lnast->add_child(idx_func   , Lnast_node::create_stmts    ("stmts1",  line_num, pos1, pos2));
  // auto idx_io1     = lnast->add_child(idx_func   , Lnast_node::create_ref      ("$a",  line_num, pos1, pos2));
  // auto idx_io2     = lnast->add_child(idx_func   , Lnast_node::create_ref      ("$b",  line_num, pos1, pos2));
  // auto idx_io3     = lnast->add_child(idx_func   , Lnast_node::create_ref      ("$out",  line_num, pos1, pos2));

  // auto idx_xor     = lnast->add_child(idx_stmts1 , Lnast_node::create_xor      ("xor",  line_num, pos1, pos2)); 
  // auto idx_lhs1    = lnast->add_child(idx_xor    , Lnast_node::create_ref      ("___b",  line_num, pos1, pos2));
  // auto idx_op1     = lnast->add_child(idx_xor    , Lnast_node::create_ref      ("$a",  line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_xor    , Lnast_node::create_ref      ("$b",  line_num, pos1, pos2));

  // auto idx_assign1 = lnast->add_child(idx_stmts1 , Lnast_node::create_assign   ("assign",  line_num, pos1, pos2));
  // auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref      ("%out",  line_num, pos1, pos2));
  // auto idx_op3     = lnast->add_child(idx_assign1, Lnast_node::create_ref      ("___b",  line_num, pos1, pos2));

  // auto idx_assign2 = lnast->add_child(idx_stmts0 , Lnast_node::create_assign   ("assign",  line_num, pos1, pos2));
  // auto idx_lhs3    = lnast->add_child(idx_assign2, Lnast_node::create_ref      ("func_xor",  line_num, pos1, pos2));
  // auto idx_op4     = lnast->add_child(idx_assign2, Lnast_node::create_ref      ("___a",  line_num, pos1, pos2));

  // auto idx_tup     = lnast->add_child(idx_stmts0 , Lnast_node::create_tuple    ("tuple",  line_num, pos1, pos2)); 
  // auto idx_tname   = lnast->add_child(idx_tup    , Lnast_node::create_ref      ("___d",  line_num, pos1, pos2));
  // auto idx_assign3 = lnast->add_child(idx_tup    , Lnast_node::create_assign   ("assign",  line_num, pos1, pos2));
  // auto idx_lhs4    = lnast->add_child(idx_assign3, Lnast_node::create_ref      ("a",  line_num, pos1, pos2));
  // auto idx_op5     = lnast->add_child(idx_assign3, Lnast_node::create_ref      ("$foo",  line_num, pos1, pos2));

  // auto idx_assign4 = lnast->add_child(idx_tup    , Lnast_node::create_assign   ("assign",  line_num, pos1, pos2));
  // auto idx_lhs5    = lnast->add_child(idx_assign4, Lnast_node::create_ref      ("b",  line_num, pos1, pos2));
  // auto idx_op6     = lnast->add_child(idx_assign4, Lnast_node::create_ref      ("$bar",  line_num, pos1, pos2));

  // auto idx_fcall   = lnast->add_child(idx_stmts0 , Lnast_node::create_func_call("func_call",  line_num, pos1, pos2));
  // auto idx_lhs6    = lnast->add_child(idx_fcall  , Lnast_node::create_ref      ("my_xor",  line_num, pos1, pos2));
  // auto idx_target  = lnast->add_child(idx_fcall  , Lnast_node::create_ref      ("func_xor",  line_num, pos1, pos2));
  // auto idx_arg     = lnast->add_child(idx_fcall  , Lnast_node::create_ref      ("___d",  line_num, pos1, pos2));

  // auto idx_dot     = lnast->add_child(idx_stmts0 , Lnast_node::create_dot      ("dot",  line_num, pos1, pos2)); 
  // auto idx_lhs7    = lnast->add_child(idx_dot    , Lnast_node::create_ref      ("___j",  line_num, pos1, pos2));
  // auto idx_op7     = lnast->add_child(idx_dot    , Lnast_node::create_ref      ("my_xor",  line_num, pos1, pos2));
  // auto idx_op8     = lnast->add_child(idx_dot    , Lnast_node::create_ref      ("out",  line_num, pos1, pos2));

  // auto idx_assign5 = lnast->add_child(idx_stmts0 , Lnast_node::create_assign   ("assign",  line_num, pos1, pos2));
  // auto idx_lhs8    = lnast->add_child(idx_assign5, Lnast_node::create_ref      ("%out",  line_num, pos1, pos2));
  // auto idx_op9     = lnast->add_child(idx_assign5, Lnast_node::create_ref      ("___j",  line_num, pos1, pos2));

  // Warning: a, b

  // ==================================================================================================================

  // Tuple Operation ==================================================================================================

  // auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));
  // auto idx_plus    = lnast->add_child(idx_stmts0,  Lnast_node::create_plus    ("plus",  line_num, pos1, pos2)); 
  // auto idx_lhs1    = lnast->add_child(idx_plus,    Lnast_node::create_ref     ("___d",  line_num, pos1, pos2));
  // auto idx_op1     = lnast->add_child(idx_plus,    Lnast_node::create_ref     ("cat",  line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_plus,    Lnast_node::create_const   ("0d2",  line_num, pos1, pos2));

  // auto idx_tup     = lnast->add_child(idx_stmts0,  Lnast_node::create_tuple   ("tuple",  line_num, pos1, pos2)); 
  // auto idx_tname   = lnast->add_child(idx_tup,     Lnast_node::create_ref     ("tup",  line_num, pos1, pos2));
  // auto idx_assign1 = lnast->add_child(idx_tup,     Lnast_node::create_assign  ("assign1",  line_num, pos1, pos2));
  // auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref     ("foo",  line_num, pos1, pos2));
  // auto idx_op3     = lnast->add_child(idx_assign1, Lnast_node::create_const   ("0d1",  line_num, pos1, pos2));

  // auto idx_assign2 = lnast->add_child(idx_tup,     Lnast_node::create_assign  ("assign2",  line_num, pos1, pos2));
  // auto idx_lhs3    = lnast->add_child(idx_assign2, Lnast_node::create_ref     ("bar",  line_num, pos1, pos2));
  // auto idx_op4     = lnast->add_child(idx_assign2, Lnast_node::create_ref     ("___d",  line_num, pos1, pos2));

  // Warning: tup, foo, bar

  // ==================================================================================================================

  // Tuple Concat Operation ===========================================================================================

  // auto idx_root    = Lnast_node::create_top("top",  line_num, pos1, pos2);
  // lnast->set_root(idx_root);

  // auto idx_stmts0  = lnast->add_child(lnast->get_root(), Lnast_node::create_stmts ("stmts0",  line_num, pos1, pos2));

  // auto idx_plus    = lnast->add_child(idx_stmts0, Lnast_node::create_plus    ("plus",  line_num, pos1, pos2));
  // auto idx_lhs1    = lnast->add_child(idx_plus,   Lnast_node::create_ref     ("___d",  line_num, pos1, pos2));
  // auto idx_op1     = lnast->add_child(idx_plus,   Lnast_node::create_ref     ("cat",  line_num, pos1, pos2));
  // auto idx_op2     = lnast->add_child(idx_plus,   Lnast_node::create_const   ("0d2",  line_num, pos1, pos2));

  // auto idx_tup1    = lnast->add_child(idx_stmts0,  Lnast_node::create_tuple  ("tup1",  line_num, pos1, pos2));
  // auto idx_tname1  = lnast->add_child(idx_tup1,    Lnast_node::create_ref    ("tup",  line_num, pos1, pos2));

  // auto idx_assign1 = lnast->add_child(idx_tup1,    Lnast_node::create_assign ("assign1",  line_num, pos1, pos2));
  // auto idx_lhs2    = lnast->add_child(idx_assign1, Lnast_node::create_ref    ("foo",  line_num, pos1, pos2));
  // auto idx_op3     = lnast->add_child(idx_assign1, Lnast_node::create_const  ("0d1",  line_num, pos1, pos2));

  // auto idx_assign2 = lnast->add_child(idx_tup1,    Lnast_node::create_assign ("assign2",  line_num, pos1, pos2));
  // auto idx_lhs3    = lnast->add_child(idx_assign2, Lnast_node::create_ref    ("bar",  line_num, pos1, pos2));
  // auto idx_op4     = lnast->add_child(idx_assign2, Lnast_node::create_ref    ("___d",  line_num, pos1, pos2));


  // auto idx_tup2    = lnast->add_child(idx_stmts0,  Lnast_node::create_tuple  ("tup2",  line_num, pos1, pos2));
  // auto idx_tname2  = lnast->add_child(idx_tup2,    Lnast_node::create_ref    ("___f",  line_num, pos1, pos2));
  // auto idx_assign3 = lnast->add_child(idx_tup2,    Lnast_node::create_assign ("assign3",  line_num, pos1, pos2));
  // auto idx_lhs4    = lnast->add_child(idx_assign3, Lnast_node::create_ref    ("null",  line_num, pos1, pos2));
  // auto idx_op7     = lnast->add_child(idx_assign3, Lnast_node::create_const  ("0d4",  line_num, pos1, pos2));

  // auto idx_assign4 = lnast->add_child(idx_tup2,    Lnast_node::create_assign ("assign4",  line_num, pos1, pos2));
  // auto idx_lhs     = lnast->add_child(idx_assign4, Lnast_node::create_ref    ("null",  line_num, pos1, pos2));
  // auto idx_op8     = lnast->add_child(idx_assign4, Lnast_node::create_ref    ("dog",  line_num, pos1, pos2));

  // auto idx_tconcat = lnast->add_child(idx_stmts0,  Lnast_node::create_tuple_concat ("tconcat",  line_num, pos1, pos2));
  // auto idx_lhs5    = lnast->add_child(idx_tconcat, Lnast_node::create_ref    ("___e",  line_num, pos1, pos2));
  // auto idx_op9     = lnast->add_child(idx_tconcat, Lnast_node::create_ref    ("tup",  line_num, pos1, pos2));
  // auto idx_opa     = lnast->add_child(idx_tconcat, Lnast_node::create_ref    ("___f",  line_num, pos1, pos2));

  // auto idx_assign5 = lnast->add_child(idx_stmts0,  Lnast_node::create_assign ("assign5",  line_num, pos1, pos2));
  // auto idx_lhs6    = lnast->add_child(idx_assign5, Lnast_node::create_ref    ("tup",  line_num, pos1, pos2));
  // auto idx_opb     = lnast->add_child(idx_assign5, Lnast_node::create_ref    ("___e",  line_num, pos1, pos2));

  // Warning: foo, bar, null

  // ==================================================================================================================

  s.do_check(lnast);
  return 0;
}