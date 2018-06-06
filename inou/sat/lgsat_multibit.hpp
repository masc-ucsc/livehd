class Destination {

private:
  int                           parent_ID;
  int                           child;
  std::string                   oprator;
  std::vector<std::vector<int>> operandlist;
  std::vector<std::vector<int>> operands_node;//operands vector
  bool                          SAT_RESULT_READY;
  int                           child_sat_result_edges;
  int                           child_edges;//child edges from upward
  std::vector<int>              sat_result;
  int                           operand_no; //no of operands of a node from back
  int                           U32const_operand_val; //const val of U32const
  std::vector<int>              pick_vec;
public:
  bool                          PICK_READY;
  Destination() {
    parent_ID                 = 0;
    child                     = 0;
    oprator                   = "NULL";
    operandlist               = {};
    operands_node             = {};
    sat_result                = {};
    pick_vec                  ={};
    SAT_RESULT_READY          = 0;
    child_sat_result_edges    = 0;
    child_edges               = 0;
    operand_no                =0;
    U32const_operand_val      =0;
    PICK_READY                =0;
     
    
  }
  std::vector<int> sat_operator_SUM(int node_B_ID,int oper,ezMiniSAT &sat, int nodeID);
  std::vector<int> sat_operator_with_Mux_Op(int oper,ezMiniSAT &sat, int nodeID, LGraph* g);
  std::vector<int> sat_operator_with_Flop_Op(int oper,ezMiniSAT &sat, int nodeID, LGraph* g);
  std::vector<int> sat_operator_with_Pick_Op(int node_B_ID,ezMiniSAT &sat, int operand_type, LGraph* g , int frombits, int pick_bits, std::vector<int> vec_pic);
  void             set_parent_ID(int node_ID);
  int              get_and_set_child_edges_sat_cnt( LGraph* g, int nodeID);
  void             increment_and_add_edges_with_operation(int node_B_ID, std::vector<int> input1,ezMiniSAT &sat, int operand_type, LGraph* g);
  bool             IS_all_inputs_per_node(LGraph* g, int nodeID);
  
  void             set_const_operand(int input) {
                    U32const_operand_val=input;
  }
                   
  int              get_const_operand() {
                  return    U32const_operand_val;
  }
  /* void             set_operand(std::vector<int> input, int port_no) {
                         operands_node[port_no]=input;
  }
 std::vector<int>  get_operand(int port_no) {
                      return operands_node[port_no];
 }*/

 void             set_operand(std::vector<int> input) {
                    operands_node.push_back(input);
 }
  std::vector<int> get_1st_operand() {
                   return operands_node[0];
  }
  std::vector<int> get_2nd_operand() {
               return operands_node[1];
    }
  std::vector<int> get_3rd_operand() {
               return operands_node[2];
    }

  void increment_operands(){
    operand_no++;
  }

  int get_operand_no(){
    return operand_no;
  }
  bool get_operands_READY(){
    std::cout<<" No of Child edges is :"<<child_edges;
    if( operand_no==2)
      return true;
    else
      return false;
  }

  void  set_child_edges_cnt( LGraph* g, int node_B_ID){
    int i=0;
    for(const auto &c:g->inp_edges(node_B_ID)) 
    	i++;		
      	//std::cout<<" Child EDGE of node ID "<<node_B_ID<<" ################### "<< i<<std::endl;
       child_edges=i;
  std::cout<<" Child EDGE of node ID "<<node_B_ID<<" ################### "<<child_edges <<std::endl;   
}

  int get_child_edges(){
    std::cout<<" Child_edges is: "<< child_edges ;
    return child_edges ;
      }

  int get_sat_result_child_edges(){
    return child_sat_result_edges; 
      }

  void set_sat_result_child_edges(int i){
     child_sat_result_edges=i;; 
      }
 
  int  get_parent_ID() {
    return parent_ID;
  }
  void set_SAT_RESULT_READY() {
    SAT_RESULT_READY = 1;
  }

  void reset_SAT_RESULT_READY() {
    SAT_RESULT_READY = 0;
  }
  bool get_SAT_RESULT_READY() {
    return SAT_RESULT_READY;
  }

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

  std::vector<int> Destination::sat_operator_SUM(int node_B_ID,int oper_type, ezMiniSAT &sat,int nodeID) {
   switch(oper_type) {

    case Sum_Op: {
      //operands_node.push_back(sat.vec_and(operands_node[0],operands_node[1]));
      //operands_node.push_back(sat.vec_xor(operands_node[0],operands_node[1]));
	 for(int i=0;i<operands_node.size();i++)
	{std::cout<<" \nInputs are "<<sat.to_string(operands_node[i][0]).c_str();
	}
	sat_result=sat.vec_or_multiarg(operands_node);
	 
	//sat_result = sat.vec_and_multiarg(operands_name);
         SAT_RESULT_READY = 1;
     
      std::cout << "\n\nFormula is for nodeID " << node_B_ID << " is : " << sat.to_string(sat_result[0]).c_str() << std::endl;
      std::cout<<"Sum_Op "<<std::endl;
     
      break;
    }
    case Mult_Op: {
      for(int i=0;i<operands_node.size();i++)
	std::cout<<" \nInputs are "<<sat.to_string(operands_node[i][0]).c_str();
      
      sat_result = sat.vec_and_multiarg(operands_node);
      SAT_RESULT_READY = 1;
      for(int i=0;i<operands_node.size();i++)
	std::cout<<" \nInputs are "<<sat.to_string(operands_node[i][0]).c_str();
        std::cout << "\n\nFormula is for nodeID " << node_B_ID << " is: " << sat.to_string(sat_result[0]).c_str() << std::endl;
        std::cout<<"Mult_Op "<<std::endl;
        break;
    }

   
    default:
      std::cout << " Unknown operation" << std::endl;
    }
    return sat_result;
  }

     
  bool IS_operator(int oper) {
  if((oper == Sum_Op) || (oper == Mult_Op)) {
    return true;
  } else
    return false;
}

  int Destination::get_and_set_child_edges_sat_cnt( LGraph* g, int nodeID) {
    int i=0;
     for(const auto &c:g->inp_edges(nodeID)) {
	int node_A_ID= (int)c.get_idx();      
	int node_B_ID=(int)nodeID;
	//A--->B
    if( !g->is_graph_input(node_A_ID)) {

       i++;
    }	}
  //std::cout<<" Child SAT  EDGE of node ID "<<nodeID<<" ################### "<< i<<std::endl;
    child_sat_result_edges=i;
    return  child_sat_result_edges;
}

  bool Destination::IS_all_inputs_per_node(LGraph* g, int nodeID){
    int i=0;
    for(const auto &c:g->inp_edges(nodeID)) {
	int node_A_ID= (int)c.get_idx();      
	int node_B_ID=(int)nodeID;
	//A--->B
    if( !g->is_graph_input(node_A_ID)) {

       i++;
       return false;
    }	}

  
    return true;
  
}


//for all input edges only:a+b+c 
  void Destination::increment_and_add_edges_with_operation(int node_B_ID, std::vector<int> input_1, ezMiniSAT &sat, int operand_type, LGraph* g ) {
     std::cout << "INPUT is for nodeID " << node_B_ID << " is : " << sat.to_string(input_1[0]).c_str() << std::endl;

  //std::vector<std::vector<int>> operands_name={}; 
     set_child_edges_cnt(  g, node_B_ID );
    if(Get_Child_Cnt() <get_child_edges()) {
      std::cout << "\nInside INPUT is for nodeID " << node_B_ID << " is : " << sat.to_string(input_1[0]).c_str() << std::endl;
      Increment_Child();
      operands_node.push_back(input_1);
      increment_operands();
  }
  std::cout << "Now child_cnt for " << node_B_ID << " is:" << Get_Child_Cnt() << std::endl;
  

  // if(Get_Child_Cnt() == get_and_set_child_edges_cnt(  g,  node_B_ID)) {

  if(Get_Child_Cnt() == 2 && Get_Child_Cnt() == get_child_edges()&& IS_all_inputs_per_node( g, node_B_ID )) {
   
    switch(operand_type) {
    case Sum_Op: {
      for(int i=0;i<operands_node.size();i++)
	{std::cout<<" \nInputs are "<<sat.to_string(operands_node[i][0]).c_str();
	}
     
	sat_result=sat.vec_or_multiarg(operands_node);
        SAT_RESULT_READY = 1;
        std::cout << "\n\nFormula is for nodeID " << node_B_ID << " is : " << sat.to_string(sat_result[0]).c_str() << std::endl;
        std::cout<<"Sum_Op "<<std::endl;
	break;
    }
    case Mult_Op: {
      for(int i=0;i<operands_node.size();i++)
	std::cout<<" \nInputs are "<<sat.to_string(operands_node[i][0]).c_str();
         sat_result = sat.vec_and_multiarg(operands_node);
         SAT_RESULT_READY = 1;
      for(int i=0;i<operands_node.size();i++)
	std::cout<<" \nInputs are "<<sat.to_string(operands_node[i][0]).c_str();
        std::cout << "\n\nFormula is for nodeID " << node_B_ID << " is: " << sat.to_string(sat_result[0]).c_str() << std::endl;
        std::cout<<"Mult_Op "<<std::endl;
        break;
    }

   
    default:
      std::cout << " Unknown operation " << std::endl;

      
    }//case
   }//child_cnt==2
}

  std::vector<int>  Destination::sat_operator_with_Mux_Op(int node_B_ID,ezMiniSAT &sat, int operand_type, LGraph* g ) {
    int sel=0;
    std::vector<int> input1={};
    std::vector<int> input2={};
    std::vector<int> input3={};
    
   if(operand_type==Mux_Op && get_operand_no()==get_child_edges()) {

     for(const auto &c:g->inp_edges(node_B_ID)) { 
       if((int)c.get_inp_pin().get_pid()==0){
	 std::cout<<" \nPiD is:"<<(int)c.get_inp_pin().get_pid();

         input1 = operands_node[(int)c.get_inp_pin().get_pid()] ;
	 std::cout << "INPUT 1 is for nodeID " << node_B_ID << " is : " << sat.to_string(input1[0]).c_str() << std::endl;
     }
       if((int)c.get_inp_pin().get_pid()==2){
	 std::cout<<" PiD is:"<<(int)c.get_inp_pin().get_pid();
	
	 input2 = operands_node[(int)c.get_inp_pin().get_pid()] ;
	 std::cout << "INPUT 2 is for nodeID " << node_B_ID << " is : " << sat.to_string(input2[0]).c_str() << std::endl; 
     }
       if((int)c.get_inp_pin().get_pid()==1) {
	 std::cout<<" PiD is:"<<(int)c.get_inp_pin().get_pid();
	 input3 = operands_node[(int)c.get_inp_pin().get_pid()];
	  sel =input3[0];
	 std::cout << "sel is for nodeID " << node_B_ID << " is : " << sat.to_string(sel).c_str() << std::endl;
     }
   }//for loop 
     std::cout<<"Mux_Op2"<<std::endl;
     //std::cout << "INPUT 1 is for nodeID " << node_B_ID << " is : " << sat.to_string(input1[0]).c_str() << std::endl;
  
     std::cout << "INPUT1  is for nodeID " << node_B_ID << " is : " << sat.to_string(input1[0]).c_str() << std::endl;
     std::cout << "sel is for nodeID " << node_B_ID << " is : " << sat.to_string(sel).c_str() << std::endl;
     std::cout << "INPUT 2 is for nodeID " << node_B_ID << " is : " << sat.to_string(input2[0]).c_str() << std::endl;
     //std::cout << "sel is for nodeID " << node_B_ID << " is : " << sat.to_string(sel).c_str() << std::endl;
     
      std::cout<<"Mux_Op2"<<std::endl;
      sat_result =sat.vec_mux(input1, input2,sel);
      SAT_RESULT_READY = 1;
      return sat_result;

  } else {
    std::cout << " BAD *************************" << std::endl;
    return {} ;
   }
}
  std::vector<int>  Destination::sat_operator_with_Flop_Op(int node_B_ID,ezMiniSAT &sat, int operand_type, LGraph* g ) {
    std::vector<int> result={};
    std::vector<int> clk={};
    std::vector<int> D={};
    std::vector<int> Reset_value={};
    std::vector<int> Reset={};
    int              Reset_bit=0;
    int              R=0;
    
   if(operand_type==Flop_Op && get_operand_no()==get_child_edges()) {

     for(const auto &c:g->inp_edges(node_B_ID)) { 
       
       if((int)c.get_inp_pin().get_pid()==0) {
	 std::cout<<" \nPiD is:"<<(int)c.get_inp_pin().get_pid();
	 clk = operands_node[(int)c.get_inp_pin().get_pid()];
	  
	 std::cout << "sel is for nodeID " << node_B_ID << " is : " << sat.to_string(clk[0]).c_str() << std::endl;
     }

        if((int)c.get_inp_pin().get_pid()==1){
	 std::cout<<" \nPiD is:"<<(int)c.get_inp_pin().get_pid();

         D = operands_node[(int)c.get_inp_pin().get_pid()] ;
	 std::cout << "INPUT 1 is for nodeID " << node_B_ID << " is : " << sat.to_string(D[0]).c_str() << std::endl;
     }

    if((int)c.get_inp_pin().get_pid()==3) {
	 std::cout<<" \nPiD is:"<<(int)c.get_inp_pin().get_pid();
	 Reset = operands_node[(int)c.get_inp_pin().get_pid()];
	 R= Reset[0];
	 Reset_bit=1;
	 std::cout << "Reset is for nodeID " << node_B_ID << " is : " << R << std::endl;
     }
   if((int)c.get_inp_pin().get_pid()==4) {
	 std::cout<<" PiD is:"<<(int)c.get_inp_pin().get_pid();
	 Reset_value = operands_node[(int)c.get_inp_pin().get_pid()];
	  
	 std::cout << "Reset Value is for nodeID " << node_B_ID << " is : " << sat.to_string(Reset_value[0]).c_str() << std::endl;
     }
 }

     std::cout<<"  Reset bit is .."<<Reset_bit<<std::endl;
     if(Reset_bit==1)
       { result=D;
	 std::cout<<" FlipFlop Reset enabled..."<<std::endl;
	 result=sat.vec_flop(D, Reset_value, R);
       }
     else
       result=D;
     
     return result;
   }

  
  else {
    std::cout << " BAD *************************" << std::endl;
    return {} ;
   } 
 }
  std::vector<int>  Destination::sat_operator_with_Pick_Op(int node_B_ID,ezMiniSAT &sat, int operand_type, LGraph* g , int frombits, int pick_bits, std::vector<int> vec_pick) {

    std::cout<<" Entering Pick Function"<<std::endl;
    std::vector<int> input_vector={};
  /*for(const auto &c:g->inp_edges(node_B_ID)) { 
		   if((int)c.get_inp_pin().get_pid()==0) {
		      std::cout<<" \nPiD is:"<<(int)c.get_inp_pin().get_pid();
		       input_vector = operands_node[(int)c.get_inp_pin().get_pid()] ;
                     std::cout << "INPUT 1 is for nodeID " << node_B_ID << " is : " << sat.to_string(input_vector[0]).c_str() << std::endl;
		   }

		   }*/
  std::vector<int> result=  sat.vec_pick(vec_pick, frombits, pick_bits);
  //std::cout<<"PICK result is: "<<sat.to_string(result[0]).c_str() << std::endl;
  return result;
}
