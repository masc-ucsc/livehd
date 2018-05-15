#include "ezminisat.hpp"
#include "inou.hpp"
#include "inou_sat.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include <iostream>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

class Destination {

private:
  int                           parent_ID;
  int                           child;
  int                           child_sat;
  std::string                   oprator;
  std::string                   input2_sat_string;
  std::vector<std::vector<int>> operandlist;
  std::vector<std::vector<int>> operandlist_sat;
  std::vector<int>              operand_bit_no;
  std::vector<int>              sat_result;
  bool                          Is_operator_1st_input_sat;
  bool                          Is_operator_2nd_input_sat;
  bool                          Is_input_2nd_operand;
  bool                          SAT_RESULT;

public:
  Destination() {
    parent_ID = 0;
    child     = 0;
    child_sat = 0, oprator = "NULL";
    operandlist               = {"NULL", "NULL"};
    operandlist_sat           = {};
    operand_bit_no            = {};
    sat_result                = {};
    Is_operator_1st_input_sat = 0;
    Is_operator_2nd_input_sat = 0;
    Is_input_2nd_operand      = 0;
    input2_sat_string         = "NULL";
    SAT_RESULT                = 0;
  }
  std::vector<int> sat_operator(int oper, std::vector<int> a, std::vector<int> b, ezMiniSAT &sat);
  std::vector<int> sat_operator_internal(int oper, std::vector<int> a, std::vector<int> b, ezMiniSAT &sat);
  std::vector<int> sat_operator_internal_string_input(int oper_type, std::vector<int> a, std::vector<int> b_string, ezMiniSAT &sat);
  void             set_parent_ID(int node_ID);
  int              get_parent_ID() {
    return parent_ID;
  }
  void set_SAT_RESULT() {
    SAT_RESULT = 1;
  }

  void reset_SAT_RESULT() {
    SAT_RESULT = 0;
  }
  bool get_SAT_RESULT() {
    return SAT_RESULT;
  }

  bool Is_input_2nd_operand_sat() {
    return Is_input_2nd_operand;
  }

  void set_is_input_2nd_operand_sat() {
    Is_input_2nd_operand = 1;
  }

  void Set_2nd_Operand_sat_string(std::string input2) {
    input2_sat_string = input2;
  }

  std::string Get_2nd_Operand_sat_string() {
    return input2_sat_string;
  }

  void set_is_operator_2nd_input(bool val) {
    Is_operator_2nd_input_sat = val;
  }

  bool get_is_operator_2nd_input() {
    return Is_operator_2nd_input_sat;
  }

  void set_is_operator_1st_input() {
    Is_operator_1st_input_sat = 1;
  }

  bool get_is_operator_1st_input() {
    return Is_operator_1st_input_sat;
  }
  void Increment_and_Add_Child_with_sat(int node_B_ID, std::vector<int> input2, ezMiniSAT &sat, int operand_type);

  std::vector<int> Get_1st_Operand() {
    return operandlist[0];
  }
  std::vector<int> Get_2nd_Operand() {
    return operandlist[1];
  }

  int Get_Child_Cnt() {
    return child;
  }

  void Increment_and_add_Child(std::vector<int> input) {
    Set_1st_Operand(input);
    child++;
  }
  void Increment_Child() {

    child++;
    std::cout << " Child cnt =" << child << std::endl;
  }

  int Get_Child_sat_Cnt() {
    return child_sat;
  }

  void Increment_Child_Sat() {
    child_sat++;
    //std::cout<<"Now  Incrementing Child_sat cnt to  ="<< child_sat<<std::endl;
  }
  void Set_1st_Operand_sat(std::vector<int> input) {
    operandlist_sat.push_back(input);
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }
  void Set_2nd_Operand_sat(std::vector<int> input) {
    operandlist_sat.push_back(input);
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }
  std::vector<int> Get_1st_Operand_sat() {
    return operandlist_sat[0];
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }
  std::vector<int> Get_2nd_Operand_sat() {
    return operandlist_sat[1];
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }

  void Set_1st_Operand(std::vector<int> input) {
    operandlist.push_back(input);
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }

  void Set_2nd_Operand(std::vector<int> input) {
    std::cout << " Setting Operand2 is......entering" << std::endl;
    operandlist.push_back(input);
    std::cout << " Setting Operand2 is:...exiting........" << std::endl;
  }

  void Set_Operator(int oper) {
    oprator = oper;
    //std::cout<< " Setting Inside Operator"<<std::endl;
  }

  void Set_result(std::vector<int> result) {
    sat_result = result;
  }
  std::vector<int> Get_sat_result() {
    return sat_result;
  }
};

void Destination::set_parent_ID(int node_ID) {
  parent_ID = node_ID;
}

std::vector<int> Destination::sat_operator(int oper_type, std::vector<int> a, std::vector<int> b, ezMiniSAT &sat) {
  if(child == 2) {
    std::vector<int> sat_val;
    switch(oper_type) {

    case Sum_Op: {
      sat_val = sat.vec_or(a, b);
      std::cout << " Sum Op....." << std::endl;
      break;
    }

    case Mult_Op: {
      sat_val = sat.vec_and(a, b);
      std::cout << " Sum Op....." << std::endl;
      break;
    }

    default:
      std::cout << " Unknown operation" << std::endl;
    }
    return sat_val;
  } else
    return {};
}
std::vector<int> Destination::sat_operator_internal_string_input(int oper_type, std::vector<int> a, std::vector<int> b_string, ezMiniSAT &sat) {
  std::cout << " CHILD SAT COUNT in internal operator sat input function:" << child_sat << std::endl;
  std::cout << "Input A in sat opertaor internal function  is : " << sat.to_string(a[0]).c_str() << "and string input2 is " << sat.to_string(b_string[0]).c_str() << std::endl;
  //std::vector<int> b=sat.vec_var(b_string,bits);

  if(child_sat == 2) {
    std::vector<int> sat_val;
    switch(oper_type) {

    case Sum_Op: {
      //std::cout<<"Formula is SUM: "<<sat.to_string(a).c_str()<<std::endl;
      sat_val = sat.vec_or(a, b_string);
      std::cout << "Formula is SUM: " << sat.to_string(sat_val[0]).c_str() << std::endl;
      break;
    }

    case Mult_Op: {
      sat_val = sat.vec_and(a, b_string);
      std::cout << "Formula is  MULT: " << sat.to_string(sat_val[0]).c_str() << std::endl;
      break;
    }

    default:
      std::cout << " Unknown operation" << std::endl;
    }
    return sat_val;

  } else {
    std::cout << " BAD *************************" << std::endl;
    return {};
  }
}

std::vector<int> Destination::sat_operator_internal(int oper_type, std::vector<int> a, std::vector<int> b, ezMiniSAT &sat) {
  std::cout << " Entering...SAT OPERATOR INTERNAL :" << std::endl;
  if(child_sat == 2) {
    std::vector<int> sat_val;
    switch(oper_type) {

    case Sum_Op: {
      sat_val = sat.vec_or(a, b);
      std::cout << "Formula is : " << sat.to_string(sat_val[0]).c_str() << std::endl;
      break;
    }

    case Mult_Op: {
      sat_val = sat.vec_and(a, b);
      std::cout << "Formula is : " << sat.to_string(sat_val[0]).c_str() << std::endl;
      break;
    }

    default:
      std::cout << " Unknown operation" << std::endl;
    }
    return sat_val;
  }

  else {
    std::cout << " BAD *************************" << std::endl;
    return {};
  }
}

void Destination::Increment_and_Add_Child_with_sat(int node_B_ID, std::vector<int> input2, ezMiniSAT &sat, int operand_type) {

  std::cout << "Inside Increment and add child for NodeID: " << node_B_ID << " is:" << Get_Child_Cnt() << std::endl;

  if(Get_Child_Cnt() == 1) {

    Increment_Child();
    std::vector<int> input1 = Get_1st_Operand();
    //std::cout<<" Getting.......1st operand"<<input1;
    Set_2nd_Operand(input2);
    std::cout << " Setting 2 Children for nodeID : " << node_B_ID << ": " << input1[0] << "  " << input2[0] << std::endl;
    //std::cout<<" Child count is :"<< Get_Child_Cnt()<<std::endl;

    switch(operand_type) {
    case Sum_Op: {
      sat_result = sat.vec_or(input1, input2);
      std::cout << std::endl;
      std::cout << "Formula is for nodeID " << node_B_ID << " is : " << sat.to_string(sat_result[0]).c_str() << std::endl;
      //std::cout<<input1<<"Sum_Op "<<input<<std::endl;
      //std::cout<<"SAT.OR("<<input1<<","<<input2<<")"<<std::endl;
      break;
    }
    case Mult_Op: {
      sat_result = sat.vec_and(input1, input2);
      //std::cout<<input1<<" Mult_Op "<<input<<std::endl;
      std::cout << "\nFormula is for nodeID " << node_B_ID << " is: " << sat.to_string(sat_result[0]).c_str() << std::endl;
      //std::cout<<" SAT.MUL("<<input1<<","<<input2<<")"<<std::endl;
      break;
    }

    default:
      std::cout << " Unknown operation " << std::endl;

      //sat_result=sat.OR(input1,input);
      //std::cout<<" SAT result for ParentID:"<<node_B_ID<<input1<<"/"<<input<<"is: "<< sat_result<<std::endl;
    }
  }

  if(Get_Child_Cnt() == 0) {
    Increment_Child();
    Set_1st_Operand(input2);
    //std::cout<<" Setting 1st Child for nodeID is : "<<input<<std::endl;
  }
}

bool IS_operator(int oper) {
  if((oper == Sum_Op) || (oper == Mult_Op)) {
    return true;
  } else
    return false;
}

void print_results(bool satisfiable, const std::vector<bool> &modelValues) {
  if(!satisfiable) {
    printf("not satisfiable.\n\n");
  } else {
    printf("satisfiable:");
    for(auto val : modelValues)
      printf(" %d ", val ? 1 : 0);
    printf("\n\n");
  }
}

int main(int argc, const char **argv) {
  //std::cout<<" Entering Sat_pass_pack in main........";
  //Sat_pass_pack pack(argc, argv);
  //std::cout<<" After Sat_pass_pack in main........";
  Options::setup(argc, argv);
  ezMiniSAT sat;
  ezMiniSAT sat_1;
  ezMiniSAT sat_2;
  int       bits;
  std::cout << "Please Enter the number of bits :" << std::endl;
  std::cin >> bits;
  std::vector<std::string> input_names;
  std::vector<std::string> input_names_alone;
  std::vector<Destination> dest_list(313);
  std::vector<int>         modelVars;
  std::vector<bool>        modelValues;
  bool                     satisfiable;
  std::vector<int>         sat_result_formula = {};

  Inou_trivial trivial;
  if(!trivial.is_graph_name_provided()) {
    fmt::print("Specify a graph to be dump with --graph_name\n");
    exit(-1);
  }

  Options::setup_lock();

  std::vector<LGraph *> rvec = trivial.generate();

  for(auto &g : rvec) {
    g->print_stats();
    g->dump();

    std::cout << " Exiting........";
    g->print_stats();
    g->dump();
    fprintf(stderr, "digraph fwd_%s {\n", g->get_name().c_str());

    for(auto idx : g->fast()) {
      if(g->is_graph_input(idx))
        fprintf(stderr, "node%d[label =\"%d $%s\"];\n", (int)idx, (int)idx, g->get_graph_input_name(idx));
      else if(g->is_graph_output(idx))
        fprintf(stderr, "node%d[label =\"%d %%%s\"];\n", (int)idx, (int)idx, g->get_graph_output_name(idx));
      else
        fprintf(stderr, "node%d[label =\"%d %s\"];\n", (int)idx, (int)idx, g->node_type_get(idx).get_name().c_str());
    }

    for(auto idx : g->backward()) {
      for(const auto &c : g->inp_edges(idx)) {
        fprintf(stderr, "lima_node%d:%d -> node%d:%d TSTbck\n", (int)c.get_idx(), (int)idx);
      }
    }

    //lima is gifted from here..........!!!!
    for(auto idx : g->backward()) {
      //nodeA->nodeB
      for(const auto &c : g->inp_edges(idx)) {
        fprintf(stderr, "node%d -> node%d TSTbck\n", (int)c.get_idx(), (int)idx);
        // **************************************************
        int node_A_ID      = (int)c.get_idx();
        int node_parent_ID = node_A_ID;
        int node_B_ID      = (int)idx;
        //A-->B
        dest_list[node_A_ID].set_parent_ID(node_B_ID);
        //std::cout<<" Setting parent for " <<node_A_ID <<" is "<< node_B_ID<<std::endl;

        int oprator_B = g->node_type_get(idx).op;

        if(IS_operator(oprator_B)) {
          dest_list[node_B_ID].Set_Operator(oprator_B);
          //dest_list[node_B_ID].Increment_Child ();
        }

        int oprator_A = g->node_type_get(c.get_idx()).op;

        if(IS_operator(oprator_A)) {
          dest_list[node_A_ID].Set_Operator(oprator_A);
          if(dest_list[node_B_ID].Get_Child_Cnt() == 0 || dest_list[node_B_ID].Get_Child_sat_Cnt() == 0)
            dest_list[node_B_ID].set_is_operator_1st_input();
        }

        if(g->is_graph_input(node_A_ID)) {
          if(dest_list[node_B_ID].Get_Child_Cnt() == 1 || dest_list[node_B_ID].Get_Child_sat_Cnt() == 1)
            dest_list[node_B_ID].set_is_operator_2nd_input(1);
        }

        //SAT child cnt++ if 2 childs : operator and input
        //int cur_parent_for_child=dest_list[node_A_ID].get_parent_ID();

        if(g->is_graph_input(node_A_ID) && dest_list[node_B_ID].get_is_operator_1st_input()) {
          std::string input_A_string = g->get_graph_input_name(node_A_ID);
          input_names.push_back(input_A_string);

          std::cout << "ALONE&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& ........" << input_A_string << std::endl;
          bool input_duplicate = 0;
          sort(input_names.begin(), input_names.end());
          std::cout << " ALONE$$$$$$$$$$Input name size########is : " << input_names.size();
          for(int i = 1; i < input_names.size(); i++) {
            if(input_names[i - 1] == input_A_string) {
              input_duplicate = 1;
              std::cout << " ALONE$$$$$$$DUPLICATE for in new input: " << input_A_string << " and loop: " << i << "value is: " << input_names[i - 1] << " ################################" << std::endl;
            }
          }
          std::vector<int> input_A_str_vec;
          if(input_duplicate == 0) {
            input_A_str_vec = sat.vec_var(input_A_string, bits);
            sat.vec_append(modelVars, input_A_str_vec);
          } else {
            input_A_str_vec = sat.vec_var(input_A_string, bits);
          }

          //std::vector<int> input_A_str_vec=sat.vec_var(input_A_string,bits);
          //sat.vec_append(modelVars,input_A_str_vec);
          // std::cout<<" Lima******************"<<std::endl;
          dest_list[node_B_ID].Increment_Child_Sat();
          //std::cout<<" Lima******************"<<std::endl;

          dest_list[node_B_ID].Set_2nd_Operand(input_A_str_vec);
          //std::cout<<" Lima******************"<<std::endl;
          dest_list[node_B_ID].set_is_input_2nd_operand_sat();
          //std::cout<<" Lima exting******************"<<std::endl;
        }

        //TODO sat recursive
        if(g->is_graph_input(node_A_ID)) {

          //A-->B
          std::string input_A = g->get_graph_input_name(node_A_ID);

          input_names.push_back(input_A);
          std::cout << "Input pushing to vector ........" << input_A << std::endl;
          bool input_duplicate = 0;
          sort(input_names.begin(), input_names.end());
          std::cout << " Input name size######################################################is : " << input_names.size();
          for(int i = 1; i < input_names.size(); i++) {
            if(input_names[i - 1] == input_A) {
              input_duplicate = 1;
              std::cout << " DUPLICATE for in new input: " << input_A << " and loop: " << i << "value is: " << input_names[i - 1] << " ###########################################################" << std::endl;
            }
          }
          std::vector<int> input_A_vec;
          if(input_duplicate == 0) {
            input_A_vec = sat.vec_var(input_A, bits);
            sat.vec_append(modelVars, sat.vec_var(input_A, bits));
          } else {
            input_A_vec = sat.vec_var(input_A, bits);
          }
          int operand_type_B = g->node_type_get(idx).op;

          int              cur_parent = 0;
          std::vector<int> sat_res    = {};
          dest_list[node_B_ID].Increment_and_Add_Child_with_sat(node_B_ID, input_A_vec, sat, operand_type_B);

          if(dest_list[node_B_ID].Get_Child_Cnt() == 2) {

            cur_parent = dest_list[node_B_ID].get_parent_ID();
            std::cout << "\n current parent child sat No for parentID:" << cur_parent << " is: " << dest_list[cur_parent].Get_Child_sat_Cnt() << std::endl;

            if(dest_list[cur_parent].Get_Child_sat_Cnt() == 1) {
              if(dest_list[cur_parent].Is_input_2nd_operand_sat()) {
                std::cout << " 2nd operand is input!!!" << std::endl;
                sat_res = dest_list[node_B_ID].Get_sat_result();
                std::cout << " Got .......2nd operand  input!!!" << std::endl;
                dest_list[cur_parent].Set_1st_Operand_sat(sat_res);
                std::cout << " exiting .......2nd operand  input!!!" << std::endl;
                dest_list[cur_parent].Increment_Child_Sat();
              } else {
                sat_res = dest_list[node_B_ID].Get_sat_result();
                dest_list[cur_parent].Set_2nd_Operand_sat(sat_res);
                dest_list[cur_parent].Increment_Child_Sat();
                //dest_list[cur_parent].Set_2nd_Operand("SAT_RESULT");
                std::cout << " GETting SAT RESULT for nodeID: " << cur_parent << sat.to_string(sat_res[0]).c_str() << std::endl;

                std::cout << "\n current parent child sat(1) for " << cur_parent << " is :" << dest_list[cur_parent].Get_Child_sat_Cnt() << std::endl;
              }
            }
            if(dest_list[cur_parent].Get_Child_sat_Cnt() == 0) {
              sat_res = dest_list[node_B_ID].Get_sat_result(); //previous SAT_RESULT OF node_id B
              //std::cout<<" SAT result is: "<< sat.to_string(sat_res).c_str()<<std::endl;
              dest_list[cur_parent].Set_1st_Operand_sat(sat_res);
              std::vector<int> sat_17_res = dest_list[cur_parent].Get_1st_Operand_sat();
              std::cout << " PRINTING 1st input sat for nodeID: " << cur_parent << sat.to_string(sat_17_res[0]).c_str() << std::endl;

              dest_list[cur_parent].Increment_Child_Sat();
              dest_list[cur_parent].set_SAT_RESULT();
              std::cout << "\n current parent child sat(0) for " << cur_parent << " is :" << dest_list[cur_parent].Get_Child_sat_Cnt() << std::endl;

              std::cout << " Set_1st_Operand....SAT_RESULT" << std::endl;

            } //  if( dest_list[node_B_ID].Get_Child_Cnt()==2)
          }

          if(IS_operator(operand_type_B)) {
            std::cout << " Outside Loop Interation ......\n";
            int i = 1;
            std::cout << " child sat count of nodeID: " << cur_parent << " is: " << dest_list[cur_parent].Get_Child_sat_Cnt() << std::endl;

            while(dest_list[cur_parent].Get_Child_sat_Cnt() == 2) {
              //std::cout<<" Inside Loop Interation ......\n";
              std::cout << " When Current_Parent child sat count is " << cur_parent << ":" << dest_list[cur_parent].Get_Child_sat_Cnt() << std::endl;
              std::vector<int> input1 = {};
              std::vector<int> input2 = {};
              if(dest_list[cur_parent].get_SAT_RESULT() == 1) {
                std::cout << " PRINTING SAT_RESULT.............." << std::endl;
                input1 = dest_list[cur_parent].Get_1st_Operand_sat();

                std::cout << " PRINTING SAT_RESULT input1 for node ID" << cur_parent << sat.to_string(input1[0]).c_str() << std::endl;
                input2 = dest_list[cur_parent].Get_2nd_Operand_sat();
                std::cout << " PRINTING SAT_RESULT input2 for node ID" << cur_parent << sat.to_string(input2[0]).c_str() << std::endl;
                int oper_type_B = g->node_type_get(cur_parent).op;
                //std::cout<<" Before........";
                sat_result_formula = dest_list[cur_parent].sat_operator_internal(oper_type_B, input1, input2, sat);
                std::cout << " PRINTING Formula for SAT_RESULT input2 for node ID" << cur_parent << sat.to_string(sat_result_formula[0]).c_str() << std::endl;
                dest_list[cur_parent].Set_result(sat_result_formula);
                //dest_list[cur_parent].Set_1st_Operand("SAT_RESULT");
              }
              if(dest_list[cur_parent].Is_input_2nd_operand_sat()) {
                input1 = dest_list[cur_parent].Get_1st_Operand_sat();
                std::cout << " GET SAT_RESULT as input1 for nodeID:*******" << cur_parent << sat.to_string(input1[0]).c_str() << std::endl;
                std::cout << " PRINTING INPUT.............." << std::endl;
                std::vector<int> input2_vec_str = dest_list[cur_parent].Get_2nd_Operand();
                std::cout << "PRINTING 2nd INPUT : " << sat.to_string(input2_vec_str[0]).c_str() << std::endl;
                int oper_type_B_str = g->node_type_get(cur_parent).op;
                //std::vector<int> input2_vec_str=sat.vec_var(input2_str,bits);
                sat_result_formula = dest_list[cur_parent].sat_operator_internal_string_input(oper_type_B_str, input1, input2_vec_str, sat);
                std::cout << " Result for nodeID:*******" << cur_parent << sat.to_string(sat_result_formula[0]).c_str() << std::endl;
                dest_list[cur_parent].Set_result(sat_result_formula);
              }

              cur_parent = dest_list[cur_parent].get_parent_ID();
              std::cout << "Before Entering Current-parent child_sat no is " << cur_parent << ":" << dest_list[cur_parent].Get_Child_sat_Cnt() << std::endl;
              if(dest_list[cur_parent].Get_Child_sat_Cnt() == 1) {

                if(dest_list[cur_parent].Is_input_2nd_operand_sat()) {
                  //sat_res=dest_list[node_B_ID].Get_sat_result();
                  dest_list[cur_parent].Set_1st_Operand_sat(sat_result_formula);
                  dest_list[cur_parent].Increment_Child_Sat();
                }

                else {
                  dest_list[cur_parent].Set_2nd_Operand_sat(sat_result_formula);
                  dest_list[cur_parent].Increment_Child_Sat();
                  std::cout << " Setting SAT result (1):" << sat.to_string(sat_result_formula[0]).c_str() << std::endl;
                  //std::cout<<"Inside Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
                  //std::cout<<" Loop Interation for"<< i<<"times for input "<<input1<<"and"<<input2<<endl;
                }
              }

              if(dest_list[cur_parent].Get_Child_sat_Cnt() == 0) {
                dest_list[cur_parent].Set_1st_Operand_sat(sat_result_formula);
                std::cout << " Setting SAT result(0)for parentID: " << cur_parent << "is : " << sat.to_string(sat_result_formula[0]).c_str() << std::endl;
                dest_list[cur_parent].Increment_Child_Sat();
                dest_list[cur_parent].set_SAT_RESULT();
                //std::cout<<"Inside Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
              }

              i++;
            } //while
          }
        }
      }
    }
  }

  std::cout << "SAT Formula is:" << sat.to_string(sat_result_formula[0]).c_str() << std::endl;
  satisfiable = sat.solve(modelVars, modelValues, sat_result_formula);
  print_results(satisfiable, modelValues);
  //ADD lgraph:
  std::vector<int>  modelExpressions_vec;
  std::vector<bool> modelValues_vec;
  bool              satisfiable_1 = 0;
  std::vector<int>  a_vec         = sat_1.vec_var("a", bits);
  std::vector<int>  b_vec         = sat_1.vec_var("b", bits);
  std::vector<int>  c_vec         = sat_1.vec_var("c", bits);
  sat_1.vec_append(modelExpressions_vec, a_vec);
  sat_1.vec_append(modelExpressions_vec, b_vec);
  std::vector<int> formula_sat_add = sat_1.vec_or(sat_1.vec_or(sat_1.vec_or(sat_1.vec_or(sat_1.vec_and(sat_1.vec_or(sat_1.vec_or(a_vec, b_vec), a_vec), sat_1.vec_or(a_vec, b_vec)), sat_1.vec_or(a_vec, b_vec)), a_vec), b_vec), a_vec);
  satisfiable_1                    = sat_1.solve(modelExpressions_vec, modelValues_vec, formula_sat_add);
  //printf("\n\n**** Comparing and Verifiing Results ****\n\nFor --lgraph_name add:\n\nFormula(Not derived from ADD Lgraph->derived directly from expression) for multibits: %s\n", sat_1.to_string(formula_sat_add[0]).c_str());
  //print_results(satisfiable, modelValues_vec);
  //ADD_SAT lgraph:
  std::vector<int>  modelExpressions_vec_2;
  std::vector<bool> modelValues_vec_2;
  bool              satisfiable_2 = 0;
  std::vector<int>  a_vec_2       = sat_2.vec_var("a", bits);
  std::vector<int>  b_vec_2       = sat_2.vec_var("b", bits);
  std::vector<int>  c_vec_2       = sat_2.vec_var("c", bits);
  sat_2.vec_append(modelExpressions_vec_2, a_vec_2);
  sat_2.vec_append(modelExpressions_vec_2, b_vec_2);
  std::vector<int> formula_sat_add_2 = sat_2.vec_or(sat_2.vec_or(sat_2.vec_and(a_vec_2, b_vec_2), sat_2.vec_or(a_vec_2, b_vec_2)), a_vec_2);
  satisfiable_2                      = sat_2.solve(modelExpressions_vec_2, modelValues_vec_2, formula_sat_add_2);
  printf("\n\nFor--lgraph_name satsmall:\n\nFormula(Not derived from ADD_SAT Lgraph->derived directly from expression) for multibits: %s\n", sat.to_string(formula_sat_add_2[0]).c_str());
  print_results(satisfiable_2, modelValues_vec_2);

  //Derived from lgraph Directly:
  std::cout << "\n**************Deriving From lgraph:***************\nFormula Devired from lgraph is:" << sat.to_string(sat_result_formula[0]).c_str() << std::endl;
  print_results(satisfiable, modelValues);
  printf("\n");

  std::cout << " \nEnds here!";
}
