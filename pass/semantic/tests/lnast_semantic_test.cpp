
#include <cstdlib>
#include <format>
#include <iostream>

#include "lnast.hpp"
#include "semantic_check.hpp"

int main(void) {
  int line_num = 33;
  int pos1     = 103;
  int pos2     = 130;

  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "\nAssign Operations Test\n\n";

    auto idx_root    = Lnast_ntype::create_top();
    auto node_stmts  = Lnast_ntype::create_stmts();
    auto node_assign = Lnast_ntype::create_assign();
    auto node_target = Lnast_node::create_ref("val");
    auto node_const  = Lnast_node::create_const("0d1023");

    lnast->set_root(idx_root);

    auto idx_stmts  = lnast->add_child(lnast->get_root(), node_stmts);
    auto idx_assign = lnast->add_child(idx_stmts, node_assign);
    lnast->add_child(idx_assign, node_target);
    lnast->add_child(idx_assign, node_const);

    // Warning: val

    s.do_check(lnast);
    std::cout << "End of Assign Operations Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "N-ary and U-nary Operations Test\n\n";

    auto idx_root   = Lnast_ntype::create_top();
    auto node_stmts = Lnast_ntype::create_stmts();
    auto node_minus = Lnast_ntype::create_minus();
    auto node_lhs1  = Lnast_node::create_ref("___a");
    auto node_op1   = Lnast_node::create_ref("x");
    auto node_op2   = Lnast_node::create_const("0d1");

    auto node_plus = Lnast_ntype::create_plus();
    auto node_lhs2 = Lnast_node::create_ref("___b");
    auto node_op3  = Lnast_node::create_ref("___a");
    auto node_op4  = Lnast_node::create_const("0d3");
    auto node_op5  = Lnast_node::create_const("0d2");

    auto node_dpa  = Lnast_ntype::create_dp_assign();
    auto node_lhs3 = Lnast_node::create_ref("total");
    auto node_op6  = Lnast_node::create_ref("___b");

    lnast->set_root(idx_root);

    auto idx_stmts = lnast->add_child(lnast->get_root(), node_stmts);
    auto idx_minus = lnast->add_child(idx_stmts, node_minus);
    lnast->add_child(idx_minus, node_lhs1);
    lnast->add_child(idx_minus, node_op1);
    lnast->add_child(idx_minus, node_op2);

    auto idx_plus = lnast->add_child(idx_stmts, node_plus);
    lnast->add_child(idx_plus, node_lhs2);
    lnast->add_child(idx_plus, node_op3);
    lnast->add_child(idx_plus, node_op4);
    lnast->add_child(idx_plus, node_op5);

    auto idx_assign = lnast->add_child(idx_stmts, node_dpa);
    lnast->add_child(idx_assign, node_lhs3);
    lnast->add_child(idx_assign, node_op6);

    // Warning: total

    s.do_check(lnast);
    std::cout << "End of N-ary and U-nary Operations Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "If Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_if     = lnast->add_child(idx_stmts0, Lnast_ntype::create_if());

    auto idx_gt = lnast->add_child(idx_if, Lnast_ntype::create_gt());
    lnast->add_child(idx_gt, Lnast_node::create_ref("___a"));
    lnast->add_child(idx_gt, Lnast_node::create_ref("a"));
    lnast->add_child(idx_gt, Lnast_node::create_const("0d3"));

    lnast->add_child(idx_if, Lnast_node::create_const("true"));

    auto idx_stmts1 = lnast->add_child(idx_if, Lnast_ntype::create_stmts());
    auto idx_plus   = lnast->add_child(idx_stmts1, Lnast_ntype::create_plus());
    lnast->add_child(idx_plus, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_plus, Lnast_node::create_ref("a"));
    lnast->add_child(idx_plus, Lnast_node::create_const("0d1"));

    auto idx_assign = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign, Lnast_node::create_ref("a"));
    lnast->add_child(idx_assign, Lnast_node::create_ref("___b"));

    // No Warnings

    s.do_check(lnast);
    std::cout << "End of If Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "If Operation (inefficient)\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_if     = lnast->add_child(idx_stmts0, Lnast_ntype::create_if());

    auto idx_gt = lnast->add_child(idx_if, Lnast_ntype::create_gt());
    lnast->add_child(idx_gt, Lnast_node::create_ref("a"));
    lnast->add_child(idx_gt, Lnast_node::create_ref("foo"));
    lnast->add_child(idx_gt, Lnast_node::create_const("0d3"));

    lnast->add_child(idx_if, Lnast_node::create_ref("a"));

    auto idx_stmts1 = lnast->add_child(idx_if, Lnast_ntype::create_stmts());

    auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("b"));
    lnast->add_child(idx_assign1, Lnast_node::create_const("0d1"));

    auto idx_assign2 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("c"));
    lnast->add_child(idx_assign2, Lnast_node::create_ref("b"));

    auto idx_assign3 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign3, Lnast_node::create_ref("a"));
    lnast->add_child(idx_assign3, Lnast_node::create_ref("c"));

    // Warning: c

    s.do_check(lnast);
    std::cout << "End of If Operation (inefficient)\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "If Operation (complex)\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_if     = lnast->add_child(idx_stmts0, Lnast_ntype::create_if());

    auto idx_gt = lnast->add_child(idx_if, Lnast_ntype::create_gt());
    lnast->add_child(idx_gt, Lnast_node::create_ref("___a"));
    lnast->add_child(idx_gt, Lnast_node::create_ref("a"));
    lnast->add_child(idx_gt, Lnast_node::create_const("0d10"));

    lnast->add_child(idx_if, Lnast_node::create_ref("___a"));

    auto idx_stmts1  = lnast->add_child(idx_if, Lnast_ntype::create_stmts());
    auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("b"));
    lnast->add_child(idx_assign1, Lnast_node::create_const("0d3"));

    auto idx_lt = lnast->add_child(idx_if, Lnast_ntype::create_lt());
    lnast->add_child(idx_lt, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_lt, Lnast_node::create_ref("a"));
    lnast->add_child(idx_lt, Lnast_node::create_const("0d1"));

    lnast->add_child(idx_if, Lnast_node::create_ref("___b"));

    auto idx_stmts2  = lnast->add_child(idx_if, Lnast_ntype::create_stmts());
    auto idx_assign2 = lnast->add_child(idx_stmts2, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("b"));
    lnast->add_child(idx_assign2, Lnast_node::create_const("0d2"));

    auto idx_stmts3  = lnast->add_child(idx_if, Lnast_ntype::create_stmts());
    auto idx_assign3 = lnast->add_child(idx_stmts3, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign3, Lnast_node::create_ref("b"));
    lnast->add_child(idx_assign3, Lnast_node::create_const("0d3"));

    // Warning: b

    s.do_check(lnast);
    std::cout << "End of If Operation (complex)\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "For Loop Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_tup    = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup, Lnast_node::create_ref("___b"));

    auto idx_assign1 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("___range_begin"));
    lnast->add_child(idx_assign1, Lnast_node::create_const("0d0"));

    auto idx_assign2 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("___range_end"));
    lnast->add_child(idx_assign2, Lnast_node::create_const("0d3"));

    auto idx_for    = lnast->add_child(idx_stmts0, Lnast_ntype::create_for());
    auto idx_stmts1 = lnast->add_child(idx_for, Lnast_ntype::create_stmts());
    lnast->add_child(idx_for, Lnast_node::create_ref("i"));
    lnast->add_child(idx_for, Lnast_node::create_ref("___b"));

    auto idx_minus = lnast->add_child(idx_stmts1, Lnast_ntype::create_minus());
    lnast->add_child(idx_minus, Lnast_node::create_ref("___g"));
    lnast->add_child(idx_minus, Lnast_node::create_const("0d3"));
    lnast->add_child(idx_minus, Lnast_node::create_ref("i"));

    auto idx_select2 = lnast->add_child(idx_stmts1, Lnast_ntype::create_tuple_get());
    lnast->add_child(idx_select2, Lnast_node::create_ref("___f"));
    lnast->add_child(idx_select2, Lnast_node::create_ref("tup_bar"));
    lnast->add_child(idx_select2, Lnast_node::create_ref("___g"));

    auto idx_select1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_select1, Lnast_node::create_ref("tup_foo"));
    lnast->add_child(idx_select1, Lnast_node::create_ref("i"));
    lnast->add_child(idx_select1, Lnast_node::create_ref("___f"));

    // Warning: ___range_begin, ___range_end

    s.do_check(lnast);
    std::cout << "End of For Loop Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "While Loop Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());

    auto idx_while = lnast->add_child(idx_stmts0, Lnast_ntype::create_while());
    lnast->add_child(idx_while, Lnast_node::create_ref("___a"));
    auto idx_stmts1 = lnast->add_child(idx_while, Lnast_ntype::create_stmts());

    auto idx_minus = lnast->add_child(idx_stmts1, Lnast_ntype::create_minus());
    lnast->add_child(idx_minus, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_minus, Lnast_node::create_const("0d3"));

    auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("total"));
    lnast->add_child(idx_assign1, Lnast_node::create_ref("___b"));

    // Warning: total

    s.do_check(lnast);
    std::cout << "End of While Loop Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Func Def Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_func   = lnast->add_child(idx_stmts0, Lnast_ntype::create_func_def());

    lnast->add_child(idx_func, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_func, Lnast_node::create_ref("condition"));
    auto idx_stmts1 = lnast->add_child(idx_func, Lnast_ntype::create_stmts());
    lnast->add_child(idx_func, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_func, Lnast_node::create_ref("$b"));
    lnast->add_child(idx_func, Lnast_node::create_ref("%out"));

    auto idx_xor = lnast->add_child(idx_stmts1, Lnast_ntype::create_bit_xor());
    lnast->add_child(idx_xor, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$b"));

    auto idx_assign = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign, Lnast_node::create_ref("%out"));
    lnast->add_child(idx_assign, Lnast_node::create_ref("___b"));

    // Warning: func_xor

    s.do_check(lnast);
    std::cout << "End of Func Def Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Conditional Func Def Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_func   = lnast->add_child(idx_stmts0, Lnast_ntype::create_func_def());

    lnast->add_child(idx_func, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_func, Lnast_node::create_ref("$valid"));
    auto idx_stmts1 = lnast->add_child(idx_func, Lnast_ntype::create_stmts());
    lnast->add_child(idx_func, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_func, Lnast_node::create_ref("$b"));
    lnast->add_child(idx_func, Lnast_node::create_ref("___a"));
    lnast->add_child(idx_func, Lnast_node::create_ref("%out"));

    auto idx_gt = lnast->add_child(idx_stmts1, Lnast_ntype::create_gt());
    lnast->add_child(idx_gt, Lnast_node::create_ref("___a"));
    lnast->add_child(idx_gt, Lnast_node::create_ref("a"));
    lnast->add_child(idx_gt, Lnast_node::create_const("0d3"));

    auto idx_xor = lnast->add_child(idx_stmts1, Lnast_ntype::create_bit_xor());
    lnast->add_child(idx_xor, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$b"));

    auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("%out"));
    lnast->add_child(idx_assign1, Lnast_node::create_ref("___b"));

    // Warning: func_xor

    s.do_check(lnast);
    std::cout << "End of Conditional Func Def Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Implicit Func Call Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_func   = lnast->add_child(idx_stmts0, Lnast_ntype::create_func_def());

    lnast->add_child(idx_func, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_func, Lnast_node::create_const("true"));

    auto idx_stmts1 = lnast->add_child(idx_func, Lnast_ntype::create_stmts());
    lnast->add_child(idx_func, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_func, Lnast_node::create_ref("$b"));
    lnast->add_child(idx_func, Lnast_node::create_ref("%out"));

    auto idx_xor = lnast->add_child(idx_stmts1, Lnast_ntype::create_bit_xor());
    lnast->add_child(idx_xor, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$b"));

    auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("%out"));
    lnast->add_child(idx_assign1, Lnast_node::create_ref("___b"));

    auto idx_assign2 = lnast->add_child(idx_stmts0, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_assign2, Lnast_node::create_ref("___a"));

    auto idx_tup = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup, Lnast_node::create_ref("___d"));
    auto idx_assign3 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign3, Lnast_node::create_ref("null"));
    lnast->add_child(idx_assign3, Lnast_node::create_ref("$foo"));

    auto idx_assign4 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign4, Lnast_node::create_ref("null"));
    lnast->add_child(idx_assign4, Lnast_node::create_ref("$bar"));

    auto idx_fcall = lnast->add_child(idx_stmts0, Lnast_ntype::create_func_call());
    lnast->add_child(idx_fcall, Lnast_node::create_ref("my_xor"));
    lnast->add_child(idx_fcall, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_fcall, Lnast_node::create_ref("___d"));

    auto idx_dot = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_get());
    lnast->add_child(idx_dot, Lnast_node::create_ref("%out"));
    lnast->add_child(idx_dot, Lnast_node::create_ref("my_xor"));
    lnast->add_child(idx_dot, Lnast_node::create_ref("out"));

    // Warning: None

    s.do_check(lnast);
    std::cout << "End of Implicit Func Call Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Explicit Func Call Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_func   = lnast->add_child(idx_stmts0, Lnast_ntype::create_func_def());

    lnast->add_child(idx_func, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_func, Lnast_node::create_const("true"));
    auto idx_stmts1 = lnast->add_child(idx_func, Lnast_ntype::create_stmts());
    lnast->add_child(idx_func, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_func, Lnast_node::create_ref("$b"));
    lnast->add_child(idx_func, Lnast_node::create_ref("%out"));

    auto idx_xor = lnast->add_child(idx_stmts1, Lnast_ntype::create_bit_xor());
    lnast->add_child(idx_xor, Lnast_node::create_ref("___b"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$a"));
    lnast->add_child(idx_xor, Lnast_node::create_ref("$b"));

    auto idx_assign1 = lnast->add_child(idx_stmts1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("%out"));
    lnast->add_child(idx_assign1, Lnast_node::create_ref("___b"));

    auto idx_assign2 = lnast->add_child(idx_stmts0, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_assign2, Lnast_node::create_ref("___a"));

    auto idx_tup = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup, Lnast_node::create_ref("___d"));
    auto idx_assign3 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign3, Lnast_node::create_ref("a"));
    lnast->add_child(idx_assign3, Lnast_node::create_ref("$foo"));

    auto idx_assign4 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign4, Lnast_node::create_ref("b"));
    lnast->add_child(idx_assign4, Lnast_node::create_ref("$bar"));

    auto idx_fcall = lnast->add_child(idx_stmts0, Lnast_ntype::create_func_call());
    lnast->add_child(idx_fcall, Lnast_node::create_ref("my_xor"));
    lnast->add_child(idx_fcall, Lnast_node::create_ref("func_xor"));
    lnast->add_child(idx_fcall, Lnast_node::create_ref("___d"));

    auto idx_dot = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_get());
    lnast->add_child(idx_dot, Lnast_node::create_ref("%out"));
    lnast->add_child(idx_dot, Lnast_node::create_ref("my_xor"));
    lnast->add_child(idx_dot, Lnast_node::create_ref("out"));

    // Warning: a, b

    s.do_check(lnast);
    std::cout << "End of Implicit Func Call Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Tuple Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());
    auto idx_plus   = lnast->add_child(idx_stmts0, Lnast_ntype::create_plus());
    lnast->add_child(idx_plus, Lnast_node::create_ref("___d"));
    lnast->add_child(idx_plus, Lnast_node::create_ref("cat"));
    lnast->add_child(idx_plus, Lnast_node::create_const("0d2"));

    auto idx_tup = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup, Lnast_node::create_ref("tup"));
    auto idx_assign1 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("foo"));
    lnast->add_child(idx_assign1, Lnast_node::create_const("0d1"));

    auto idx_assign2 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("bar"));
    lnast->add_child(idx_assign2, Lnast_node::create_ref("___d"));

    // Warning: tup, foo, bar

    s.do_check(lnast);
    std::cout << "End of Tuple Operation Test\n\n";
    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Tuple Concat Operation\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts0 = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());

    auto idx_plus = lnast->add_child(idx_stmts0, Lnast_ntype::create_plus());
    lnast->add_child(idx_plus, Lnast_node::create_ref("___d"));
    lnast->add_child(idx_plus, Lnast_node::create_ref("cat"));
    lnast->add_child(idx_plus, Lnast_node::create_const("0d2"));

    auto idx_tup1 = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup1, Lnast_node::create_ref("tup"));

    auto idx_assign1 = lnast->add_child(idx_tup1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign1, Lnast_node::create_ref("foo"));
    lnast->add_child(idx_assign1, Lnast_node::create_const("0d1"));

    auto idx_assign2 = lnast->add_child(idx_tup1, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("bar"));
    lnast->add_child(idx_assign2, Lnast_node::create_ref("___d"));

    auto idx_tup2 = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup2, Lnast_node::create_ref("___f"));
    auto idx_assign3 = lnast->add_child(idx_tup2, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign3, Lnast_node::create_ref("null"));
    lnast->add_child(idx_assign3, Lnast_node::create_const("0d4"));

    auto idx_assign4 = lnast->add_child(idx_tup2, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign4, Lnast_node::create_ref("null"));
    lnast->add_child(idx_assign4, Lnast_node::create_ref("dog"));

    auto idx_tconcat = lnast->add_child(idx_stmts0, Lnast_ntype::create_tuple_concat());
    lnast->add_child(idx_tconcat, Lnast_node::create_ref("___e"));
    lnast->add_child(idx_tconcat, Lnast_node::create_ref("tup"));
    lnast->add_child(idx_tconcat, Lnast_node::create_ref("___f"));

    auto idx_assign5 = lnast->add_child(idx_stmts0, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign5, Lnast_node::create_ref("tup"));
    lnast->add_child(idx_assign5, Lnast_node::create_ref("___e"));

    // Warning: foo, bar

    s.do_check(lnast);
    std::cout << "End of Tuple Concat Operation\n\n";

    delete lnast;
  }
  {
    Lnast*         lnast = new Lnast();
    Semantic_check s;

    std::cout << "Attribute Operation Test\n\n";

    auto idx_root = Lnast_ntype::create_top();
    lnast->set_root(idx_root);

    auto idx_stmts = lnast->add_child(lnast->get_root(), Lnast_ntype::create_stmts());

    auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_dot, Lnast_node::create_ref("foo"));
    lnast->add_child(idx_dot, Lnast_node::create_const("__bits"));
    lnast->add_child(idx_dot, Lnast_node::create_const("0d3"));

    auto idx_assign2 = lnast->add_child(idx_stmts, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign2, Lnast_node::create_ref("foo"));
    lnast->add_child(idx_assign2, Lnast_node::create_const("0d7"));

    auto idx_tup = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_add());
    lnast->add_child(idx_tup, Lnast_node::create_ref("___b"));

    auto idx_assign3 = lnast->add_child(idx_tup, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign3, Lnast_node::create_ref("__bits"));
    lnast->add_child(idx_assign3, Lnast_node::create_const("0d10"));

    auto idx_as = lnast->add_child(idx_stmts, Lnast_ntype::create_assign());
    lnast->add_child(idx_as, Lnast_node::create_ref("bar"));
    lnast->add_child(idx_as, Lnast_node::create_ref("___b"));

    auto idx_assign4 = lnast->add_child(idx_stmts, Lnast_ntype::create_assign());
    lnast->add_child(idx_assign4, Lnast_node::create_ref("bar"));
    lnast->add_child(idx_assign4, Lnast_node::create_const("0d123"));

    // Warning: bar

    s.do_check(lnast);
    std::cout << "End of Attribute Operation Test\n\n";
    delete lnast;
  }

  return 0;
}