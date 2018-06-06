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
#include "lgsat_multibit.hpp"

int  get_child_edges_no( LGraph* g, int node_B_ID){
  int i=0;
  for(const auto &c:g->inp_edges(node_B_ID)) 
    	i++;		
     return i;
  
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
 
  Options::setup(argc, argv);
  ezMiniSAT                 sat;
  ezMiniSAT                 sat_1;
  ezMiniSAT                 sat_2;
  int                       bits;
 
  std::vector<std::string> input_names;
  std::vector<std::string> input_names_alone;
  std::vector<Destination> dest_list(313);
  std::vector<int>         modelVars;
  std::vector<bool>        modelValues;
  bool                     satisfiable;
  std::vector<int>         modelVars_flop;
  std::vector<bool>        modelValues_flop;
  bool                     satisfiable_flop;
  std::vector<int>         sat_result_formula = {};
  std::vector<int>         input_flop_vec_var={};

  int                      cur_parent = 0;
  std::vector<int>         sat_res    = {};
  std::vector<int>          input_vec ={}; 
  Inou_trivial              trivial;
  
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
        fprintf(stderr, "From node %d o_pin:%d -> node%d inp_pin:%d TSTbck\n", (int)c.get_idx(), (int)c.get_out_pin().get_pid(),(int)idx,(int)c.get_inp_pin().get_pid());
      }
    }

    //lima is gifted from here..........!!!!
    for(auto idx : g->backward()) {
      //nodeA->nodeB
      for(const auto &c : g->inp_edges(idx)) {
        fprintf(stderr, "node%d -> node%d TSTbck\n", (int)c.get_idx(), (int)idx);
        // **************************************************
  int     node_A_ID = (int)c.get_idx();
  int     node_parent_ID = node_A_ID;
  int     node_B_ID      = (int)idx;
        //A-->B
  dest_list[node_A_ID].set_parent_ID(node_B_ID);
  int     oprator_B = g->node_type_get(idx).op;
  int     operand_type_B = g->node_type_get(idx).op;
  int     oprator_A = g->node_type_get(c.get_idx()).op;
        //std::cout<<"Entering With no OutputNode for "<< node_B_ID<<std::endl;
  if(g->is_graph_output(node_B_ID)){
    std::cout<<" Output node is :"<<node_B_ID<<std::endl; 
 }
	
  if(!g->is_graph_output(node_B_ID)){
    std::cout<<" Output@@@@@@@@@@@@2 node is :"<<node_B_ID<<std::endl; 
        if(IS_operator(oprator_B)) {
          dest_list[node_B_ID].Set_Operator(oprator_B);
          }

   if(IS_operator(oprator_A)) {
          dest_list[node_A_ID].Set_Operator(oprator_A);
	   
   }

	//Is_graph_const_input:A-->B; B is a constant input
   if(g->node_type_get(node_A_ID).op==U32Const_Op){
     //std::vector<int> const_input_sat=sat.vec_const_unsigned(g->node_value_get(node_A_ID),g->get_bits(node_A_ID) );
	dest_list[node_B_ID].increment_operands();
	int     const_input=g->node_value_get(node_A_ID);
	if(g->node_type_get(node_B_ID).op==Pick_Op)
	    dest_list[node_B_ID].set_const_operand(const_input);
	}

          //Is_graph_input()
   if(g->is_graph_input(node_A_ID)) {
        //A-->B
      std::string     input_A = g->get_graph_input_name(node_A_ID);
          bits=g->get_bits(node_A_ID);
	  
          input_names.push_back(input_A);
	
      bool           input_duplicate = 0;
          sort(input_names.begin(), input_names.end());
         
          for(int i = 1; i < input_names.size(); i++) {
            if(input_names[i - 1] == input_A) {
              input_duplicate = 1;
	                                                                                         }
          }
          std::vector<int> input_A_vec;
          if(input_duplicate == 0) {
            input_A_vec = sat.vec_var(input_A, bits);
            sat.vec_append(modelVars, sat.vec_var(input_A, bits));
          } else {
            input_A_vec = sat.vec_var(input_A, bits);
          }
	  
      int          input_port_ID=0;
	  input_port_ID=(int)c.get_inp_pin().get_pid();
	  dest_list[node_B_ID].increment_and_add_edges_with_operation(node_B_ID, input_A_vec, sat, operand_type_B,g);
	
          }//if(g->is_graph_input(node_A_ID)

	 if((g->node_type_get(node_B_ID).op==Pick_Op)&&(dest_list[node_B_ID].get_operand_no()==get_child_edges_no(g,node_B_ID))){
	  std::vector<int> vec_pick=dest_list[node_B_ID].get_1st_operand();
	      int pick_bits= g->get_bits(node_B_ID);
	      int U32Const_val=dest_list[node_B_ID].get_const_operand();
	      //std::cout<<"Constant VAlu eof Pick is :"<< U32Const_val << std::endl;
	      sat_result_formula=dest_list[node_B_ID].sat_operator_with_Pick_Op( node_B_ID,sat,Pick_Op,  g , U32Const_val, pick_bits,vec_pick);
	      dest_list[node_B_ID].set_SAT_RESULT_READY();
	      //std::cout<<" Setting Pick operand "<<sat.to_string(sat_result_formula[0]).c_str() << std::endl;
	      dest_list[node_B_ID].Set_result(sat_result_formula);
	     
	      }
		
        if(dest_list[node_B_ID].get_SAT_RESULT_READY()==1) {
           cur_parent = dest_list[node_B_ID].get_parent_ID();
	   //std::cout<< "CURRENT PARENT IS "<<cur_parent<<std::endl;
	   sat_res = dest_list[node_B_ID].Get_sat_result();
	   //std::cout << " GETTING SAT RESULT for nodeID:"<<node_B_ID <<sat.to_string(sat_res[0]).c_str() << std::endl;
	   dest_list[cur_parent].set_operand(sat_res);
	   //std::vector<int> get_operand=dest_list[cur_parent].get_1st_operand();
	   //std::cout<<"  1st Operand for nodeID:"<<cur_parent<< "is"<<sat.to_string(get_operand[0]).c_str() << std::endl;
	   //std::cout << " AFTER Setting operand for nodeID: " << cur_parent<<sat.to_string(sat_res[0]).c_str() << std::endl;
	   dest_list[cur_parent].increment_operands();						   
           while((dest_list[cur_parent].get_operand_no()==get_child_edges_no(g,cur_parent)) && !(g->is_graph_output(cur_parent))) {
	       
	     //std::cout<<" Inside While Loop Interation ......\n";
	     //std::cout<<"get_operand_cnt is "<<dest_list[cur_parent].get_operand_no()<<" and get_child_cnt is :"<<get_child_edges_no(g,cur_parent)<<std::endl;
	     //std::cout << " When Current_Parent child sat count is " << cur_parent << ":" << dest_list[cur_parent].Get_Child_Cnt() << std::endl;
	     //std::cout<<" Before Entering Flop.... ";
	      int root= dest_list[cur_parent].get_parent_ID();
	      std::vector<int> re={};
	      re=dest_list[cur_parent].get_1st_operand();
	      //std::cout<< " Inputs for nodeID :" << cur_parent<< "are : "<<sat.to_string(re[0]).c_str()<<std::endl;
	      
	      if(g->node_type_get(cur_parent).op==Flop_Op){
               int                  oper_type_B = g->node_type_get(cur_parent).op;
	       std::vector<int>     flop_result_formula={};
	       std::vector<int>     input1 = {};
               std::vector<int>     input2 = {};
	      input1 = dest_list[cur_parent].get_1st_operand();
	      input2 = dest_list[cur_parent].get_2nd_operand();
	      flop_result_formula=dest_list[cur_parent].sat_operator_with_Flop_Op(cur_parent,sat,oper_type_B,g );
              std::cout << "SAT Formula for Flop_Op:" << sat.to_string(flop_result_formula[0]).c_str() << std::endl;
              satisfiable_flop = sat.solve(modelVars_flop, modelValues_flop, flop_result_formula);
              print_results(satisfiable_flop, modelValues_flop);
	      cur_parent = dest_list[cur_parent].get_parent_ID();
	   
	      //set a new variable for the flop output  
	      std::string input_flop_var="abc";
	      input_flop_vec_var= sat.vec_var(input_flop_var, bits);
	      sat.vec_append(modelVars, sat.vec_var(input_flop_var, bits));
	      dest_list[cur_parent].set_operand(input_flop_vec_var);
	       dest_list[cur_parent].increment_operands();
	      }//flop

	      
	      if(g->node_type_get(cur_parent).op==Mult_Op||g->node_type_get(cur_parent).op==Sum_Op ){
	      std::vector<int> input1 = {};
              std::vector<int> input2 = {};
	      input1 = dest_list[cur_parent].get_1st_operand();
	      input2 = dest_list[cur_parent].get_2nd_operand();
	      int             oper_type_B = g->node_type_get(cur_parent).op;

	      std::cout << " Inputs for nodeID are : " << cur_parent<< sat.to_string(input1[0]).c_str() << "+ "<<sat.to_string(input2[0]).c_str()<< std::endl;
	      sat_result_formula = dest_list[cur_parent].sat_operator_SUM(cur_parent,oper_type_B, sat,cur_parent );
	      std::cout << " Setting SAT result for nodeID: " << cur_parent<< sat.to_string(sat_result_formula[0]).c_str() << std::endl;
	      cur_parent = dest_list[cur_parent].get_parent_ID();
	      dest_list[cur_parent].increment_operands();
	      dest_list[cur_parent].set_operand(sat_result_formula);
	      
	      
	      }//Sum_Op
	      std::cout<<" Before Entering Muxing.... ";
	      if(g->node_type_get(cur_parent).op==Mux_Op) {
	      std::cout<<" Entering Muxing.... ";
	      int oper_type_B = g->node_type_get(cur_parent).op;
	      std::cout<<" Entering Muxing Function call ";
	      sat_result_formula = dest_list[cur_parent].sat_operator_with_Mux_Op(cur_parent,sat,oper_type_B,g );
	      std::cout<<" Leaving Muxing.... ";
	      //std::cout << " Setting SAT result for nodeID: " << cur_parent<< sat.to_string(sat_result_formula[0]).c_str() << std::endl;
	      cur_parent = dest_list[cur_parent].get_parent_ID();
	      dest_list[cur_parent].increment_operands();
	      std::cout<<" Setting Mux operand  is :"<<sat.to_string(sat_result_formula[0]).c_str() << std::endl;
	      dest_list[cur_parent].set_operand(sat_result_formula);

	      }//Mux_op

	    if((g->node_type_get(cur_parent).op==Pick_Op)){
	     int U32Const_val=dest_list[cur_parent].get_const_operand(); 
	     std::vector<int> vec_pick={};
	      vec_pick= dest_list[cur_parent].get_1st_operand();
	     std::cout<<" Pick 1st Operand for nodeID:"<<cur_parent<< "is"<<sat.to_string(vec_pick[0]).c_str() << std::endl;
	     std::cout<<" Uconst32 value for nodeID: "<<cur_parent<<"is"<<U32Const_val<< std::endl;
	     int pick_bits= g->get_bits(cur_parent);
	      std::cout<<" pick_bits value for nodeID: "<<cur_parent<<"is"<<pick_bits<< std::endl;
	     sat_result_formula=dest_list[cur_parent].sat_operator_with_Pick_Op( cur_parent,sat,Pick_Op,  g , U32Const_val, pick_bits, vec_pick); 
	     cur_parent = dest_list[cur_parent].get_parent_ID();
	     dest_list[cur_parent].increment_operands();
	     std::cout<<" Setting Pick operand "<<sat.to_string(sat_result_formula[0]).c_str() << std::endl;
	     dest_list[cur_parent].set_operand(sat_result_formula);
	    
	      
	   }


	      

	      if(g->is_graph_output(root)&&(g->node_type_get(cur_parent).op!=Flop_Op)){
	         std::cout<< "OUTPUT NODE is :"<<cur_parent<<"*********************************";
	         std::cout<<" Size of Formula vector is "<<int(sat_result_formula.size()); 
		//TODO:
	         satisfiable = sat.solve(modelVars, modelValues, sat_result_formula);
	         print_results(satisfiable, modelValues);

	        }


	 }//while
	      
  
	} //if READY
	
       
    }//if !g->output
	
      }// g->inp_edges(idx))
    }//g->backward()) 
  }
} 









