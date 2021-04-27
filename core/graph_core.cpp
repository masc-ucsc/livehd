#include "graph_core.hpp"

#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <algorithm>
#include <vector>

#define SUCCESS 1

Graph_core::Graph_core(std::string_view path, std::string_view name) {
  (void)path;
  (void)name;
  // std::queue<Index_id> deletedEdges;
}

/*  Set the boolean value of the node to 1
 *  This value means that that node is master_root
 *
 *  @params none
 *  @return void
 */

void Graph_core::Entry16::set_master_root(){
   master_root = 1;
}

uint8_t Graph_core::test_master_root(const Index_id master_root_id) const{
   const Entry16 *return_type = reinterpret_cast<const Entry16*>(table.data());
   return return_type[master_root_id].test_master_root();
}
/*  Set the boolean value of the node to 0
 *  This value means that that node is master
 *
 *  @params none
 *  @return void
 */

void Graph_core::Entry16::set_master(){
   master_root = 0;
}

/*  In the master_root that is created set the type of
 *  the node to what is passed in
 *
 *  @params uint8_t type
 *  @returns void
 */
void Graph_core::Entry16::set_type(uint8_t type){
   pid_bits_or_type = type;
}

/* Return the type that was set in set_type
 *
 * @params Index_id master_root_id
 * @returns uint8_t type
 */

uint8_t Graph_core::get_type(const Index_id master_root_id) const{
   const Entry16 *return_type = reinterpret_cast<const Entry16*>(table.data());
   if(return_type[master_root_id].is_master_root() == false){
     return -1;
   }
   return return_type[master_root_id].get_type();
}

/*  Set the type of any node indicated by the master root id
 *  if master root, find the correct node in the table and set the type
 *  if not a master root do nothing
 *
 *  @params const Index_id master_root_id, uint8_t type
 *  @returns void
 */

void Graph_core::set_type(const Index_id master_root_id, uint8_t type){
   Entry16 *typeNode = reinterpret_cast<Entry16*>(table.data());
   if(typeNode[master_root_id].is_master_root() == true){
     typeNode[master_root_id].set_type(type);
   }
}

/* Get the PID of a given node
 * If it is a master root return 0
 * If it is a master node return the pid
 *
 * @params const Index_id master_root_id
 * @returns Port_ID (pid)
 */


Port_ID Graph_core::get_pid(const Index_id master_root_id) const{

   const Entry16 *pidNode = reinterpret_cast<const Entry16*>(table.data());
   if(pidNode[master_root_id].is_master_root() == true){
     return 0;
   }
   return (pidNode[master_root_id].get_pid());
}

/* Check if the node is a master_root or not
 *
 * @params const Index_id master_root_id
 * @returns bool
 */

bool Graph_core::is_master_root(const Index_id master_root_id){
   //use the function is_master_root() inside the Entry16 class
   //return true or false
   //use the vector functions to find the right node
   //
   //USE AN ENUM INSTEAD OF BOOL?

   if(table.size() <= (master_root_id >> 2)){ //check the condition and on ln 76

     const Entry16 *boolNode = reinterpret_cast<const Entry16*>(table.data());
     return boolNode[master_root_id].is_master_root();
   }else{
     return false;
   }
}
/*  Create a master_root node
 *  set the boolean value of the node to 1
 *  set the type of the node
 *  create a master root id based on the size of the table
 *  place te node in the table using emplace back
 *
 *  @params uint8_t type
 *  @returns Index_id of the node
 */

Index_id Graph_core::create_master_root(uint8_t type){
   Entry16 m;

   m.set_master_root();
   Index_id id = table.size();
   m.set_type(type);

   Entry16 *root_pointer = &m;
   const Entry64 *emplace_root_element = reinterpret_cast<const Entry64*>(root_pointer);

   table.emplace_back(*emplace_root_element);

   return id;
}

/*  Create a master and point to master root m
 *  set the boolean value of the node to 0
 *  set the pid bits of the node
 *  create the master root id based on the size of table
 *  place the node in the table using emplace back
 *
 *  @params const Index_ID master_root_id, const Port_ID pid
 *  @returns Index_ID of the node
 */

Index_id Graph_core::create_master(const Index_id master_root_id, const Port_ID pid){
   Entry16 newMaster;

   newMaster.set_master();
   newMaster.pid_bits_or_type = pid;
   newMaster.ptrs = master_root_id;
   Index_id master_id = table16.size();

   // who is master root and then you have the master root to point to the master
   Entry16 *master_pointer = &newMaster;
   const Entry64 *emplace_master_element = reinterpret_cast<const Entry64*>(master_pointer);

   table.emplace_back(*emplace_master_element);

   return master_id;
}

/* function that inserts values into edge_storage given the relative indexes
 * finds the next empty spot and inserts the value
 *
 * @params uint8_t rel_index
 * @returns 0 if sucess or -1 if fail
 */

uint8_t Graph_core::Entry16::insert_edge(uint8_t rel_index){
   for(uint8_t i = 16; i >= 5; i--){
     if(edge_storage[i] != 0 && i == 5){
       return 1; // fail
     }else if(edge_storage[i] == 0){
       edge_storage[i] = rel_index;
       return 0; // success
     }
   }
   return 1;
}

/*  Add an bidirectional edge to a node
 *  Store the deltas in the edge_storage of each Entry 16 or in the overflow
 *
 *  The first edge is added into the edge storage of the Entry 16 stored in the table with
 *  the given sink_id. Then that Entry is accessed and the delta is added into that edge storage
 *  if the delta does not fit in the designated space then an overflow is created for specifically
 *  inputs or outputs and the delta is then placed there
 *
 *  @params const Index_ID sink_id, const Index_ID driver_id
 *  @returns void
 */

/*
void Graph_core::add_edge(const Index_id sink_id, const Index_id driver_id){

   Entry16 *add_node = reinterpret_cast<Entry16*>(table.data());

   std::vector<Entry64>::iterator find_inp_index = std::find(table.begin(), table.end(), sink_id);
   auto sink_index = std::distance(table.begin(), find_inp_index);

   std::vector<Entry64>::iterator find_out_index = std::find(table.begin(), table.end(), driver_id);
   auto driver_index = std::distance(table.begin(), find_out_index);

   // because this is a function in the same class we need to modify the edge list

   int rel_index1 = driver_index - sink_index;
   int rel_index2 = sink_index - driver_index;

   //add_node[sink_id].insert_edge(rel_index1);
   //add_node[driver_id].insert_edge(rel_index2);

   //if(add_node[sink_id].insert_edge(rel_index1) == 1){ // make overflow
     //Entry64 inp_overflow;
     //inp_overflow.set_input();
     //table.emplace_back(inp_overflow);
   //}

   //if(add_node[driver_id].insert_edge(rel_index2) == 1){ // make overflow
     //Entry64 out_overflow;
     //out_overflow.set_output();
     //table.emplace_back(out_overflow);
   //}
   // get the index if the node using vector functions

     // each node has to know its own Index in the table
     // when creating nodes that position is returned but
     // unknown to the node itself i think
     //
     // For this reason I erased what I had and have this pseudocode which can be filled
     // once i get that question answered. Also are the relative indexes unsigned
     //
     // if master root (I don't think i need this if statement)
     //      table @input's edge storage has a relative index of
     //      destination index minus its own index
     //
     //      table @destination's edge storage has a relative index of
     //      input index minus its own index
}
//*/

