/*
 *  ezSAT -- A simple and easy to use CNF generator for SAT solvers
 *
 *  Copyright (C) 2013  Clifford Wolf <clifford@clifford.at>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

// Coded by Nilufar Ferdous
#include "ezminisat.hpp"
#include <stdio.h>
#include <vector>
#define MY_MACRO(varname) int varname = getCurrentTime();
void print_results(bool satisfiable, const std::vector<bool> &modelValues) {
  if(!satisfiable) {
    printf("not satisfiable.\n\n");
  } else {
    printf("satisfiable:");
    for(auto val : modelValues)
      printf(" %d", val ? 1 : 0);
    printf("\n\n");
  }
}

int main() {
  ezMiniSAT sat;

  std::vector<int>  modelExpressions;
  std::vector<bool> modelValues;
  bool              satisfiable;

  // std::cout<<" Creating Varname"<< varname ;
  std::vector<int> A_vec = sat.vec_var("a", 2);
  std::vector<int> B_vec = sat.vec_var("b", 2);
  std::vector<int> C_vec = sat.vec_var("c", 2);

  sat.vec_append(modelExpressions, A_vec);
  sat.vec_append(modelExpressions, B_vec);
  sat.vec_append(modelExpressions, C_vec);

  std::vector<std::vector<int>> formula_2;
  formula_2.push_back(sat.vec_or(A_vec, B_vec));
  formula_2.push_back(sat.vec_or(A_vec, C_vec));
  // formula_2.push_back(sat.vec_and(B_vec,C_vec));
  std::vector<int> a_arg_2 = sat.vec_and_multiarg(formula_2);
  printf("\nMultiargument Expressions: %s\n", sat.to_string(a_arg_2[0]).c_str());

  std::vector<int> formula;
  formula.push_back(sat.OR(A_vec[0], B_vec[0]));
  formula.push_back(sat.XOR(B_vec[0], C_vec[0], A_vec[0]));
  formula.push_back(sat.AND(B_vec[0], C_vec[0], A_vec[0]));
  int a = sat.expression(ezMiniSAT::OpAnd, formula);
  printf("\nLookUp Expressions: %s\n", sat.to_string(a).c_str());

  std::vector<std::vector<int>> formula_arg;
  formula_arg.push_back(sat.vec_or(A_vec, B_vec));
  formula_arg.push_back(sat.vec_xor(B_vec, C_vec));
  formula_arg.push_back(sat.vec_and(B_vec, C_vec));
  std::vector<int> a_arg = sat.vec_and_multiarg(formula_arg);
  printf("\nMultiargument Expressions: %s\n", sat.to_string(a_arg[0]).c_str());

  // Multiarguments And(a,b,c....)
  std::vector<std::vector<int>> formula_arg_and;
  formula_arg_and.push_back(sat.vec_or(A_vec, B_vec));
  formula_arg_and.push_back(sat.vec_xor(B_vec, C_vec));
  // formula_arg_and.push_back(sat.vec_and(B_vec,C_vec));
  std::vector<int> a_arg_and = sat.vec_and_multiarg(formula_arg_and);
  printf("\nMultiargument Expressions for and_arg: %s\n", sat.to_string(a_arg_and[0]).c_str());

  std::vector<int> pos_active = sat.vec_and(sat.vec_not(A_vec), sat.vec_or(sat.vec_and(B_vec, A_vec), sat.vec_or(B_vec, A_vec)));
  std::vector<int> neg_active = sat.vec_and(A_vec, sat.vec_and(B_vec, C_vec));
  std::vector<int> impossible = sat.vec_or(pos_active, neg_active);
  satisfiable                 = sat.solve(modelExpressions, modelValues, impossible);
  printf("\npos_active_multibit: %s\n", sat.to_string(pos_active[0]).c_str());
  printf("\nneg_active_multibit: %s\n", sat.to_string(neg_active[0]).c_str());
  printf("\nimpossible_multibit: %s\n", sat.to_string(impossible[0]).c_str());
  std::cout << "\nResult of MultiBit Vector of SAT SOLVING:*******" << std::endl;
  print_results(satisfiable, modelValues);

  //********************************
  std::vector<int>  modelExpressions_vec;
  std::vector<bool> modelValues_vec;
  satisfiable = 0;

  std::vector<int> a_vec = sat.vec_var("a", 2);
  std::vector<int> b_vec = sat.vec_var("b", 2);
  std::vector<int> c_vec = sat.vec_var("c", 2);

  sat.vec_append(modelExpressions_vec, a_vec);
  sat.vec_append(modelExpressions_vec, b_vec);
  std::vector<int> sat1 = sat.vec_or(a_vec, b_vec);

  std::vector<int> formula_sat = sat.vec_or(
      sat.vec_or(sat.vec_or(sat.vec_or(sat.vec_and(sat.vec_or(sat.vec_or(a_vec, b_vec), a_vec), sat.vec_or(a_vec, b_vec)),
                                       sat.vec_or(a_vec, b_vec)),
                            a_vec),
                 b_vec),
      a_vec);

  satisfiable = sat.solve(modelExpressions_vec, modelValues_vec, formula_sat);
  printf("\nFormula_multibit: %s\n", sat.to_string(formula_sat[0]).c_str());
  print_results(satisfiable, modelValues_vec);
  printf("\n");

  // 3 input AOI-Gate
  // 'pos_active' encodes the condition under which the pullup path of the gate is active
  // 'neg_active' encodes the condition under which the pulldown path of the gate is active
  // 'impossible' encodes the condition that both or none of the above paths is active
  int pos_active_1bit = sat.AND(sat.NOT("A"), sat.OR(sat.NOT("B"), sat.NOT("C")));
  int neg_active_1bit = sat.OR("A", sat.AND("B", "C"));
  int impossible_1bit = sat.IFF(pos_active_1bit, neg_active_1bit);

  std::vector<int>  modelVars_1bit;
  std::vector<bool> modelValues_1bit;

  modelVars_1bit.push_back(sat.VAR("A"));
  modelVars_1bit.push_back(sat.VAR("B"));
  modelVars_1bit.push_back(sat.VAR("C"));

  std::cout << "Result of 1 bitSAT SOLVING:*******" << std::endl;
  printf("pos_active_1bit: %s\n", sat.to_string(pos_active_1bit).c_str());
  satisfiable = sat.solve(modelVars_1bit, modelValues_1bit, pos_active_1bit);
  print_results(satisfiable, modelValues_1bit);

  printf("neg_active_1bit: %s\n", sat.to_string(neg_active_1bit).c_str());
  satisfiable = sat.solve(modelVars_1bit, modelValues_1bit, neg_active_1bit);
  print_results(satisfiable, modelValues_1bit);

  printf("impossible_1bit: %s\n", sat.to_string(impossible_1bit).c_str());
  satisfiable = sat.solve(modelVars_1bit, modelValues_1bit, impossible_1bit);
  print_results(satisfiable, modelValues_1bit);

  return 0;
}
