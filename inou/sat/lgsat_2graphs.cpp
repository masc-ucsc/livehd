//std::cout<<" BISMILLAH HIR RAHMAN NIR RAHIM"<<std::endl;
//std::cout<<" ALLAHUMMA SALLE  ALA SAIYEDENA MUHAMMADEO WASILATY ILAKA WA ALIHI WA SALLIM"<<std::endl;
//std::cout<<" BISMILLAH HIR RAHMAN NIR RAHIM"<<std::endl;
//std::cout<<" LA ILAHA ILLALLAHU MUHAMMADUR RASULULLAH"<<std::endl;

#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <random>
#include "ezsat/ezminisat.hpp"
#include "lgraph.hpp"
#include "inou.hpp"
#include "lgedgeiter.hpp"
#include "inou_sat.hpp"



class Destination {
  
private:
  
  int parent_ID;
  int child;
  int child_sat;
  std::string oprator;
  std::string input2_sat_string;
  std::vector<std::string>operandlist;
  std::vector<int> operandlist_sat;
  int sat_result;
  bool Is_operator_1st_input_sat;
  bool Is_operator_2nd_input_sat;
  bool Is_input_2nd_operand;
public:
  
  Destination(){ parent_ID=0; child=0;child_sat=0,oprator="NULL";operandlist={"NULL","NULL"};  operandlist_sat={0,0};sat_result=0;Is_operator_1st_input_sat=0;Is_operator_2nd_input_sat=0;Is_input_2nd_operand=0;input2_sat_string="NULL";}
  int sat_operator(int oper, std::string a, std::string b,ezMiniSAT& sat);
  int sat_operator_internal (int oper, int a, int b,ezMiniSAT& sat);
  int sat_operator_internal_string_input(int oper_type, int  a, std::string b, ezMiniSAT& sat);
  void set_parent_ID(int node_ID);
  int get_parent_ID(){
    return parent_ID;
  }


  bool Is_input_2nd_operand_sat(){
     return Is_input_2nd_operand;
  }

   void set_is_input_2nd_operand_sat(){
      Is_input_2nd_operand =1;
  }

  void Set_2nd_Operand_sat_string(std::string input2){
    input2_sat_string= input2;
    
  }
  
  std::string Get_2nd_Operand_sat_string(){
    return input2_sat_string;
    
  }


 void set_is_operator_2nd_input(bool val){
    Is_operator_2nd_input_sat=val;

  }

  bool get_is_operator_2nd_input() {
    return Is_operator_2nd_input_sat;

  }
  
  
  void set_is_operator_1st_input() {
    Is_operator_1st_input_sat=1;

  }

  bool get_is_operator_1st_input() {
    return Is_operator_1st_input_sat;

  }
  void Increment_and_Add_Child_with_sat(int node_B_ID, std::string input2,ezMiniSAT& sat ,int operand_type);

  std::string  Get_1st_Operand() {
    return operandlist[0];
  }
  std::string  Get_2nd_Operand() {
    return operandlist[1];
  }

  int Get_Child_Cnt() {
    return child;
  }

  void Increment_and_add_Child (std::string input) {
    Set_1st_Operand(input);
    child++;
}
  void Increment_Child () {
   
    child++;
    std::cout<<" Child cnt ="<< child<<std::endl;
}
  
int Get_Child_sat_Cnt() {
    return child_sat;
  }

  void Increment_Child_Sat () {
    						      
    child_sat++;
    //std::cout<<"Now  Incrementing Child_sat cnt to  ="<< child_sat<<std::endl;						      
}
  void Set_1st_Operand_sat(int input){
    operandlist_sat[0]=input;
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }
 void Set_2nd_Operand_sat(int input){
    operandlist_sat[1]=input;
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }

   int Get_1st_Operand_sat(){
    return operandlist_sat[0];
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }
 int  Get_2nd_Operand_sat(){
    return operandlist_sat[1];
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }
  
  void Set_1st_Operand(std::string input){
    operandlist[0]=input;
    //std::cout<<" Setting Operand1 is:"<<input<<"........"<<std::endl;
  }

  void Set_2nd_Operand(std::string input){
    operandlist[1]=input;
    //std::cout<<" Setting Operand2 is:"<<input<<"........"<<std::endl;
  }

  void Set_Operator( int oper)
  { oprator =oper;
    //std::cout<< " Setting Inside Operator"<<std::endl;
  }

  void Set_result( int result){
    sat_result=result;

   }
 int  Get_sat_result(){
    return sat_result ;

   }
  

};


  



void Destination::set_parent_ID(int node_ID) {
    parent_ID=node_ID;
  
  }

int Destination::sat_operator(int oper_type, std::string a, std::string b, ezMiniSAT& sat  ) {
  if(child==2) {
    int sat_val;
    switch(oper_type) {
      
    case Sum_Op :{sat_val=sat.OR(a,b);
             	   std::cout<<" Sum Op....."<<std::endl;	
	          break;}
           
    case Mult_Op:{sat_val=sat.AND(a,b);
		 std::cout<<" Sum Op....."<<std::endl;
		 break;}

    default: std::cout<<" Unknown operation"<<std::endl;
		       
    }
    return sat_val;
    }
  else return 0;
  }
int Destination::sat_operator_internal_string_input(int oper_type, int  a, std::string b, ezMiniSAT& sat  ) {
  //std::cout<<" CHILD COUNT in internal operator sat input function:"<<child<<std::endl;
  //std::cout<<"Input A in sat opertaor internal function  is : "<<sat.to_string(a).c_str()<<std::endl;
 
    int sat_val;
    switch(oper_type) {
      
    case Sum_Op :{sat_val=sat.OR(a,b);
	//std::cout<<"Formula is : "<<sat.to_string(a).c_str()<<std::endl;	
	          break;}
           
    case Mult_Op:{sat_val=sat.AND(a,b);

	std::cout<<"Formula is : "<<sat.to_string(a).c_str()<<std::endl;
	break;}

    default: std::cout<<" Unknown operation"<<std::endl;
		       
    }
    return sat_val;
    
  }


int Destination::sat_operator_internal (int oper_type, int  a, int b, ezMiniSAT& sat  ) {
  std::cout<<" Entering...SAT OPERATOR INTERNAL :"<<std::endl;
  if(child_sat==2) {
    int sat_val;
    switch(oper_type) {
      
    case Sum_Op :{sat_val=sat.OR(a,b);
	std::cout<<"Formula is : "<<sat.to_string(sat_val).c_str()<<std::endl;	
	          break;}
           
    case Mult_Op:{sat_val=sat.AND(a,b);

	std::cout<<"Formula is : "<<sat.to_string(sat_val).c_str()<<std::endl;
	break;}

    default: std::cout<<" Unknown operation"<<std::endl;
		       
    }
    return sat_val;
    }

  else { std::cout<< " BAD *************************"<<std::endl;
    return 0;
  }
  }







void Destination::Increment_and_Add_Child_with_sat(int node_B_ID, std::string input2,  ezMiniSAT& sat ,int operand_type ) {

  std::cout<<"Inside Increment and add child for NodeID: "<<node_B_ID<<" is:"<<Get_Child_Cnt()<<std::endl;
  
  if( Get_Child_Cnt()==1) {
    
        Increment_Child();
	std::string input1=Get_1st_Operand();
	//std::cout<<" Getting.......1st operand"<<input1;
        Set_2nd_Operand(input2);
	std::cout<<" Setting 2 Children for nodeID : "<<node_B_ID<<": "<<input1 <<"  " << input2<<std::endl;
	//std::cout<<" Child count is :"<< Get_Child_Cnt()<<std::endl;

	switch (operand_type) {
	case Sum_Op :{sat_result=sat.OR(input1,input2);
	    std::cout<<std::endl;
	    std::cout<<"Formula is for nodeID "<< node_B_ID<<" is : "<<sat.to_string(sat_result).c_str()<<std::endl;
	    //std::cout<<input1<<"Sum_Op "<<input<<std::endl;
	    //std::cout<<"SAT.OR("<<input1<<","<<input2<<")"<<std::endl;
	          break;}
           
	    case Mult_Op:{
	      sat_result=sat.AND(input1,input2);
	   
	      //std::cout<<input1<<" Mult_Op "<<input<<std::endl;
	      std::cout<<"\nFormula is for nodeID "<< node_B_ID<< " is: "<<sat.to_string(sat_result).c_str()<<std::endl;
	      //std::cout<<" SAT.MUL("<<input1<<","<<input2<<")"<<std::endl;
		 break; }
             
              default: std::cout<<" Unknown operation "<<std::endl;
		       
		//sat_result=sat.OR(input1,input);
		//std::cout<<" SAT result for ParentID:"<<node_B_ID<<input1<<"/"<<input<<"is: "<< sat_result<<std::endl;

	} }

      if(Get_Child_Cnt()==0) {
       Increment_Child();
       Set_1st_Operand(input2);
       //std::cout<<" Setting 1st Child for nodeID is : "<<input<<std::endl;
    }



    }


bool IS_operator(int oper) {
  if ((oper==Sum_Op)||(oper==Mult_Op)) {
    return true;}
  else
    return false;
}

void print_results(bool satisfiable, const std::vector<bool> &modelValues)
{
	if (!satisfiable) {
		printf("not satisfiable.\n\n");
	} else {
		printf("satisfiable:");
		for (auto val : modelValues)
			printf(" %d", val ? 1 : 0);
		printf("\n\n");
	}
}


   
  

int main(int argc, const char **argv) {
  std::cout<<" Entering Sat_pass_pack in main........";
  Sat_pass_pack pack(argc, argv);
  std::cout<<" After Sat_pass_pack in main........";
  //Options::setup(argc, argv);
  ezMiniSAT sat;
  //int lgraph_no=2;
  //int no=1;
  
  
    // Options::setup(argc, argv);
    //std::cout<<"Enter lgraph no: "<<no<<" and enter the command below"<<std::endl;
  std::vector<Destination>dest_list(313) ;
  std::vector<int> modelVars;
  std::vector<bool> modelValues;
  bool satisfiable;
  int sat_result_formula=0;

  //Inou_trivial trivial;
  //if (!trivial.is_graph_name_provided()) {
  //fmt::print("Specify a graph to be dump with --graph_name\n");
  //exit(-1);
  //}

  //Options::setup_lock();

  //std::vector<LGraph *> rvec = trivial.generate();

  //for(auto &g:rvec) {
  // g->print_stats();
  // g->dump();

  //std::cout<<" name of the graph :"<< top;
  //std::cout<<" Entering in main..........Sat_pass_pack........";

  // Sat_pass_pack pack(argc, argv);
  std::cout<<" After Sat_pass_pack........";
  
  std::string top = "lgraph_add";
  if(top.substr(0,7) == "lgraph_")
    top = top.substr(7);

  // std::cout<<" name of the graph :"<< top;
  //std::cout<<" Entering Sat_pass_pack........";


  //Sat_pass_pack pack(argc, argv);
   std::cout<<" Exting........";
  LGraph* g  = LGraph::open_lgraph(pack.sat_lgdb, top);
  //LGraph* modified  = LGraph::open_lgraph(pack.modified_lgdb, top);

  //LGraph* synth  = LGraph::open_lgraph(pack.synth_lgdb, top);
  std::cout<<" Exiting........";
   g->print_stats();
   g->dump();
fprintf(stderr,"digraph fwd_%s {\n", g->get_name().c_str());

    for(auto idx:g->fast()) {
      if (g->is_graph_input(idx))
        fprintf(stderr,"node%d[label =\"%d $%s\"];\n",(int)idx, (int)idx, g->get_graph_input_name(idx));
      else if (g->is_graph_output(idx))
        fprintf(stderr,"node%d[label =\"%d %%%s\"];\n",(int)idx, (int)idx, g->get_graph_output_name(idx));
      else
        fprintf(stderr,"node%d[label =\"%d %s\"];\n",(int)idx, (int)idx, g->node_type_get(idx).get_name().c_str());
    }

     
    //lima is gifted from here..........!!!!
    for(auto idx:g->backward()) {
      //nodeA->nodeB
      for(const auto &c:g->inp_edges(idx)) {
        fprintf(stderr,"node%d -> node%d TSTbck\n"
		, (int)c.get_idx(), (int)idx);
	// **************************************************
	int node_A_ID= (int)c.get_idx();      
	int node_parent_ID=node_A_ID; 
	int node_B_ID=(int)idx;
        //A-->B
	dest_list[node_A_ID].set_parent_ID(node_B_ID);
	//std::cout<<" Setting parent for " <<node_A_ID <<" is "<< node_B_ID<<std::endl;

	int oprator_B=g->node_type_get(idx).op;

	if(IS_operator(oprator_B)) {
	        dest_list[node_B_ID].Set_Operator(oprator_B);
	  //dest_list[node_B_ID].Increment_Child ();
	}

       int oprator_A=g->node_type_get(c.get_idx()).op;

       if(IS_operator(oprator_A)) {
        	dest_list[node_A_ID].Set_Operator(oprator_A);
		if(dest_list[node_B_ID].Get_Child_Cnt()==0||dest_list[node_B_ID].Get_Child_sat_Cnt()==0)
		dest_list[node_B_ID].set_is_operator_1st_input();
       }

       if(g->is_graph_input(node_A_ID)) {
	if(dest_list[node_B_ID].Get_Child_Cnt()==1||dest_list[node_B_ID].Get_Child_sat_Cnt()==1)
 	        dest_list[node_B_ID].set_is_operator_2nd_input(1);
	  
	 }

       
       //SAT child cnt++ if 2 childs : operator and input

       
       //int cur_parent_for_child=dest_list[node_A_ID].get_parent_ID();
	 
       if(g->is_graph_input(node_A_ID)&& dest_list[node_B_ID].get_is_operator_1st_input())
	 {      std::string input_A_string= g->get_graph_input_name(node_A_ID);

	         std::cout<<" Lima******************"<<std::endl;
	         dest_list[node_B_ID].Increment_Child_Sat();
	         dest_list[node_B_ID].Set_2nd_Operand(input_A_string);
		 dest_list[node_B_ID].set_is_input_2nd_operand_sat();
		 
	 }



       
        //TODO sat recursive
	if(g->is_graph_input(node_A_ID)) {
         
	  //A-->B
	  std::string input_A= g->get_graph_input_name(node_A_ID);
	   modelVars.push_back(sat.VAR(input_A));
	  int operand_type_B=g->node_type_get(idx).op;
	 
	  int cur_parent=0;
	  int sat_res=0;
	  dest_list[node_B_ID].Increment_and_Add_Child_with_sat(node_B_ID, input_A, sat,operand_type_B );
	 
          if( dest_list[node_B_ID].Get_Child_Cnt()==2) {
	    
	   cur_parent=dest_list[node_B_ID].get_parent_ID();
	   std::cout<<"\n current parent child sat No for parentID:"<<cur_parent<<" is: "<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;

	    if(dest_list[cur_parent].Get_Child_sat_Cnt()==1) {
	      if(dest_list[cur_parent].Is_input_2nd_operand_sat() ){
		sat_res=dest_list[node_B_ID].Get_sat_result();
		  dest_list[cur_parent].Set_1st_Operand_sat(sat_res);
		  dest_list[cur_parent].Increment_Child_Sat();
	      }
	      else {
		sat_res=dest_list[node_B_ID].Get_sat_result();
	    dest_list[cur_parent].Set_2nd_Operand_sat(sat_res);
	    dest_list[cur_parent].Increment_Child_Sat();
	    //dest_list[cur_parent].Set_2nd_Operand("SAT_RESULT");
            std::cout<<" GETting SAT RESULT for nodeID: "<< cur_parent<<sat.to_string(sat_res).c_str()<<std::endl;
	    
	    std::cout<<"\n current parent child sat(1) for "<<cur_parent<<" is :"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
	      }
	  } 
	  if(dest_list[cur_parent].Get_Child_sat_Cnt()==0) {
	    sat_res=dest_list[node_B_ID].Get_sat_result();//previous SAT_RESULT OF node_id B
	    //std::cout<<" SAT result is: "<< sat.to_string(sat_res).c_str()<<std::endl;
	    dest_list[cur_parent].Set_1st_Operand_sat(sat_res);
	    int sat_17_res= dest_list[cur_parent].Get_1st_Operand_sat();
	    std::cout<<" PRINTING 1st input sat for nodeID: "<<cur_parent<< sat.to_string(sat_17_res).c_str()<<std::endl;
	   
	    dest_list[cur_parent].Increment_Child_Sat();
	     dest_list[cur_parent].Set_1st_Operand("SAT_RESULT");
	    std::cout<<"\n current parent child sat(0) for "<<cur_parent<<" is :"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;

	    std::cout<<" Set_1st_Operand....SAT_RESULT"<<std::endl;

    
	 
	  
          }   //  if( dest_list[node_B_ID].Get_Child_Cnt()==2)


	  }
              	  
      	  if( IS_operator(operand_type_B)) {
	    std::cout<<" Outside Loop Interation ......\n";
	    int i=1;
	    std::cout<<" child sat count of nodeID: "<<cur_parent<<" is: "<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
	 
	    while(dest_list[cur_parent].Get_Child_sat_Cnt()==2) {
	      //std::cout<<" Inside Loop Interation ......\n";
	        std::cout<<" When Current_Parent child sat count is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
		int  input1=0;
	      
	       if(dest_list[cur_parent].Get_1st_Operand()=="SAT_RESULT") {
		 std::cout<<" PRINTING SAT_RESULT.............."<<std::endl;
			 

	       input1=dest_list[cur_parent].Get_1st_Operand_sat();

	       std::cout<<" PRINTING SAT_RESULT input1 for node ID"<<cur_parent<<sat.to_string(input1).c_str()<<std::endl;						  
	       int input2=dest_list[cur_parent].Get_2nd_Operand_sat();
	       std::cout<<" PRINTING SAT_RESULT input2 for node ID"<<cur_parent<<sat.to_string(input2).c_str()<<std::endl;
	       int oper_type_B=g->node_type_get(cur_parent).op;

	       //std::cout<<" Before........";
	       sat_result_formula= dest_list[cur_parent].sat_operator_internal(oper_type_B,input1,input2,sat);
	        std::cout<<" PRINTING Formula for SAT_RESULT input2 for node ID"<<cur_parent<<sat.to_string( sat_result_formula).c_str()<<std::endl;
	       dest_list[cur_parent].Set_result(sat_result_formula);
               //dest_list[cur_parent].Set_1st_Operand("SAT_RESULT");
	       
		  }
	       if(dest_list[cur_parent].Is_input_2nd_operand_sat())

		 {
		     input1=dest_list[cur_parent].Get_1st_Operand_sat();
		   std::cout<<" GET SAT_RESULT as input1 for nodeID:*******"<< cur_parent<<sat.to_string(input1).c_str()<<std::endl;
		   std::cout<<" PRINTING INPUT.............."<<std::endl;
		   std::string input2_str =dest_list[cur_parent].Get_2nd_Operand();
		   std::cout<<"PRINTING 2nd INPUT : "<<input2_str<<std::endl;
		   int oper_type_B_str=g->node_type_get(cur_parent).op;
		   sat_result_formula= dest_list[cur_parent].sat_operator_internal_string_input(oper_type_B_str,input1,input2_str,sat);
		   std::cout<<" Result for nodeID:*******"<< cur_parent<<sat.to_string(sat_result_formula).c_str()<<std::endl;
		   dest_list[cur_parent].Set_result(sat_result_formula);

		   
		 }
	      
	       cur_parent=dest_list[cur_parent].get_parent_ID();
	       std::cout<<"Before Entering Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;

	        if(dest_list[cur_parent].Get_Child_sat_Cnt()==1) {

                  if(dest_list[cur_parent].Is_input_2nd_operand_sat() ){
		    //sat_res=dest_list[node_B_ID].Get_sat_result();
		  dest_list[cur_parent].Set_1st_Operand_sat(sat_result_formula);
		  dest_list[cur_parent].Increment_Child_Sat();
	      }
            


                  else { dest_list[cur_parent].Set_2nd_Operand_sat(sat_result_formula);
		  dest_list[cur_parent].Increment_Child_Sat();
		   std::cout<<" Setting SAT result (1):"<< sat.to_string(sat_result_formula).c_str()<<std::endl;
		  //std::cout<<"Inside Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
	       //std::cout<<" Loop Interation for"<< i<<"times for input "<<input1<<"and"<<input2<<endl;
		 }




		}
		
               if(dest_list[cur_parent].Get_Child_sat_Cnt()==0) 
		 {dest_list[cur_parent].Set_1st_Operand_sat(sat_result_formula);
		   std::cout<<" Setting SAT result(0)for parentID: "<<cur_parent<<"is : "<< sat.to_string(sat_result_formula).c_str()<<std::endl;
		   dest_list[cur_parent].Increment_Child_Sat();
		   dest_list[cur_parent].Set_1st_Operand("SAT_RESULT");
		  //std::cout<<"Inside Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
		 }
		   
	      
		  i++;
	    }//while

       } } } } 

	std::cout<<"Printing the total formula as:"<<sat.to_string(sat_result_formula).c_str()<<std::endl;
    satisfiable = sat.solve(modelVars, modelValues, sat_result_formula);
    print_results(satisfiable, modelValues);
    
    /*    
  Options::setup(argc, argv);
  //ezMiniSAT sat;
  //int lgraph_no=2;
  //int no=1;
  
  
    // Options::setup(argc, argv);
    //std::cout<<"Enter lgraph no: "<<no<<" and enter the command below"<<std::endl;
  std::vector<Destination>dest_list_1(313) ;
  std::vector<int> modelVars_1;
  std::vector<bool> modelValues_1;
   satisfiable=0;
   int sat_result_formula_1=0;

  Inou_trivial trivial_1;
 
  Options::setup_lock();

  std::vector<LGraph *> rvec_1 = trivial_1.generate();
  std::cout<<" Lima..........Entering 2nd graph!"<<std::endl;
    for(auto &g:rvec_1) {
     g->print_stats();
     g->dump();

fprintf(stderr,"digraph fwd_%s {\n", g->get_name().c_str());

    for(auto idx:g->fast()) {
      if (g->is_graph_input(idx))
        fprintf(stderr,"node%d[label =\"%d $%s\"];\n",(int)idx, (int)idx, g->get_graph_input_name(idx));
      else if (g->is_graph_output(idx))
        fprintf(stderr,"node%d[label =\"%d %%%s\"];\n",(int)idx, (int)idx, g->get_graph_output_name(idx));
      else
        fprintf(stderr,"node%d[label =\"%d %s\"];\n",(int)idx, (int)idx, g->node_type_get(idx).get_name().c_str());
    }

     
    //lima is gifted from here..........!!!!
    for(auto idx:g->backward()) {
      //nodeA->nodeB
      for(const auto &c:g->inp_edges(idx)) {
        fprintf(stderr,"node%d -> node%d TSTbck\n"
		, (int)c.get_idx(), (int)idx);
	// **************************************************
	int node_A_ID= (int)c.get_idx();      
	int node_parent_ID=node_A_ID; 
	int node_B_ID=(int)idx;
        //A-->B
	dest_list_1[node_A_ID].set_parent_ID(node_B_ID);
	//std::cout<<" Setting parent for " <<node_A_ID <<" is "<< node_B_ID<<std::endl;

	int oprator_B=g->node_type_get(idx).op;

	if(IS_operator(oprator_B)) {
	        dest_list_1[node_B_ID].Set_Operator(oprator_B);
	  //dest_list[node_B_ID].Increment_Child ();
	}

       int oprator_A=g->node_type_get(c.get_idx()).op;

       if(IS_operator(oprator_A)) {
        	dest_list_1[node_A_ID].Set_Operator(oprator_A);
		if(dest_list_1[node_B_ID].Get_Child_Cnt()==0||dest_list[node_B_ID].Get_Child_sat_Cnt()==0)
		dest_list_1[node_B_ID].set_is_operator_1st_input();
       }

       if(g->is_graph_input(node_A_ID)) {
	if(dest_list_1[node_B_ID].Get_Child_Cnt()==1||dest_list[node_B_ID].Get_Child_sat_Cnt()==1)
 	        dest_list_1[node_B_ID].set_is_operator_2nd_input(1);
	  
	 }

       
       //SAT child cnt++ if 2 childs : operator and input

       
       //int cur_parent_for_child=dest_list[node_A_ID].get_parent_ID();
	 
       if(g->is_graph_input(node_A_ID)&& dest_list_1[node_B_ID].get_is_operator_1st_input())
	 {      std::string input_A_string= g->get_graph_input_name(node_A_ID);

	         std::cout<<" Lima******************"<<std::endl;
	         dest_list_1[node_B_ID].Increment_Child_Sat();
	         dest_list_1[node_B_ID].Set_2nd_Operand(input_A_string);
		 dest_list_1[node_B_ID].set_is_input_2nd_operand_sat();
		 
	 }



       
        //TODO sat recursive
	if(g->is_graph_input(node_A_ID)) {
         
	  //A-->B
	  std::string input_A= g->get_graph_input_name(node_A_ID);
	   modelVars_1.push_back(sat.VAR(input_A));
	  int operand_type_B=g->node_type_get(idx).op;
	 
	  int cur_parent=0;
	  int sat_res=0;
	  dest_list_1[node_B_ID].Increment_and_Add_Child_with_sat(node_B_ID, input_A, sat,operand_type_B );
	 
          if( dest_list_1[node_B_ID].Get_Child_Cnt()==2) {
	    
	   cur_parent=dest_list_1[node_B_ID].get_parent_ID();
	   std::cout<<"\n current parent child sat No for parentID:"<<cur_parent<<" is: "<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;

	    if(dest_list_1[cur_parent].Get_Child_sat_Cnt()==1) {
	      if(dest_list_1[cur_parent].Is_input_2nd_operand_sat() ){
		sat_res=dest_list_1[node_B_ID].Get_sat_result();
		  dest_list_1[cur_parent].Set_1st_Operand_sat(sat_res);
		  dest_list_1[cur_parent].Increment_Child_Sat();
	      }
	      else {
		sat_res=dest_list_1[node_B_ID].Get_sat_result();
	    dest_list_1[cur_parent].Set_2nd_Operand_sat(sat_res);
	    dest_list_1[cur_parent].Increment_Child_Sat();
	    //dest_list[cur_parent].Set_2nd_Operand("SAT_RESULT");
            std::cout<<" GETting SAT RESULT for nodeID: "<< cur_parent<<sat.to_string(sat_res).c_str()<<std::endl;
	    
	    std::cout<<"\n current parent child sat(1) for "<<cur_parent<<" is :"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
	      }
	  } 
	  if(dest_list_1[cur_parent].Get_Child_sat_Cnt()==0) {
	    sat_res=dest_list_1[node_B_ID].Get_sat_result();//previous SAT_RESULT OF node_id B
	    // std::cout<<" SAT result is: "<< sat.to_string(sat_res).c_str()<<std::endl;
	    dest_list_1[cur_parent].Set_1st_Operand_sat(sat_res);
	    int sat_17_res= dest_list_1[cur_parent].Get_1st_Operand_sat();
	    std::cout<<" PRINTING 1st input sat for nodeID: "<<cur_parent<< sat.to_string(sat_17_res).c_str()<<std::endl;
	   
	    dest_list_1[cur_parent].Increment_Child_Sat();
	     dest_list_1[cur_parent].Set_1st_Operand("SAT_RESULT");
	    std::cout<<"\n current parent child sat(0) for "<<cur_parent<<" is :"<<dest_list_1[cur_parent].Get_Child_sat_Cnt()<<std::endl;

	    std::cout<<" Set_1st_Operand....SAT_RESULT"<<std::endl;

    
	 
	  
          }   //  if( dest_list[node_B_ID].Get_Child_Cnt()==2)


	  }
              	  
      	  if( IS_operator(operand_type_B)) {
	    std::cout<<" Outside Loop Interation ......\n";
	    int i=1;
	    std::cout<<" child sat count of nodeID: "<<cur_parent<<" is: "<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
	 
	    while(dest_list_1[cur_parent].Get_Child_sat_Cnt()==2) {
	      //std::cout<<" Inside Loop Interation ......\n";
	        std::cout<<" When Current_Parent child sat count is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
		int  input1=0;
	      
	       if(dest_list_1[cur_parent].Get_1st_Operand()=="SAT_RESULT") {
		 std::cout<<" PRINTING SAT_RESULT.............."<<std::endl;
			 

	       input1=dest_list_1[cur_parent].Get_1st_Operand_sat();

								  //std::cout<<" PRINTING SAT_RESULT for node ID"<<cur_parent<<sat.to_string(input1).c_str()<<std::endl;						  
	       int input2=dest_list_1[cur_parent].Get_2nd_Operand_sat();
	       int oper_type_B=g->node_type_get(cur_parent).op;

	       //std::cout<<" Before........";
	       sat_result_formula_1= dest_list_1[cur_parent].sat_operator_internal(oper_type_B,input1,input2,sat);
	       dest_list_1[cur_parent].Set_result(sat_result_formula_1);
               //dest_list[cur_parent].Set_1st_Operand("SAT_RESULT");
	       
		  }
	       if(dest_list_1[cur_parent].Is_input_2nd_operand_sat())

		 {
		     input1=dest_list_1[cur_parent].Get_1st_Operand_sat();
		     //std::cout<<" GET SAT RESULT for nodeID: "<< cur_parent<<sat.to_string(input1).c_str()<<std::endl;
		   std::cout<<" PRINTING INPUT.............."<<std::endl;
		   std::string input2_str =dest_list_1[cur_parent].Get_2nd_Operand();
		   std::cout<<"PRINTING 2nd INPUT : "<<input2_str<<std::endl;
		   int oper_type_B_str=g->node_type_get(cur_parent).op;
		   sat_result_formula_1= dest_list_1[cur_parent].sat_operator_internal_string_input(oper_type_B_str,input1,input2_str,sat);
		   dest_list_1[cur_parent].Set_result(sat_result_formula_1);

		   
		 }
	      
	       cur_parent=dest_list_1[cur_parent].get_parent_ID();
	       std::cout<<"Before Entering Current-parent child_sat no is "<<cur_parent<<":"<<dest_list_1[cur_parent].Get_Child_sat_Cnt()<<std::endl;

	        if(dest_list_1[cur_parent].Get_Child_sat_Cnt()==1) {

                  if(dest_list_1[cur_parent].Is_input_2nd_operand_sat() ){
		    //sat_res=dest_list[node_B_ID].Get_sat_result();
		  dest_list_1[cur_parent].Set_1st_Operand_sat(sat_result_formula_1);
		  dest_list_1[cur_parent].Increment_Child_Sat();
	      }
            


                  else { dest_list_1[cur_parent].Set_2nd_Operand_sat(sat_result_formula_1);
		  dest_list_1[cur_parent].Increment_Child_Sat();
		   std::cout<<" Setting SAT result (1):"<< sat.to_string(sat_result_formula_1).c_str()<<std::endl;
		  //std::cout<<"Inside Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
	       //std::cout<<" Loop Interation for"<< i<<"times for input "<<input1<<"and"<<input2<<endl;
		 }




		}




		
               if(dest_list[cur_parent].Get_Child_sat_Cnt()==0) 
		 {dest_list[cur_parent].Set_1st_Operand_sat(sat_result_formula_1);
		   std::cout<<" Setting SAT result(0) :"<< sat.to_string(sat_result_formula_1).c_str()<<std::endl;
		   dest_list[cur_parent].Increment_Child_Sat();
		  //std::cout<<"Inside Current-parent child_sat no is "<<cur_parent<<":"<<dest_list[cur_parent].Get_Child_sat_Cnt()<<std::endl;
		 }
		   
	      
		  i++;
	    }//while

       } } } } }

	std::cout<<"Printing the total formula as:"<<sat.to_string(sat_result_formula_1).c_str()<<std::endl;
	//satisfiable = sat.solve(modelVars_1, modelValues_1, sat_result_formula_1);
    int xor_formula;
    xor_formula=sat.XOR(sat_result_formula,sat_result_formula_1);
    std::cout<<" Printing Formula for XORing"<<std::endl;
    std::cout<<sat.to_string(xor_formula).c_str()<<std::endl;
    //print_results(xor_formula, modelValues_1);
    satisfiable = sat.solve(modelVars_1, modelValues_1, xor_formula);
    print_results(satisfiable, modelValues_1);
    */
    std::cout<<" Ending.............";


}  
  


