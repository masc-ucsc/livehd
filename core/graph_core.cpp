#include "graph_core.hpp"

#include <iostream>
#include <map>
#include <queue>
#include <string>

#define SUCCESS 1

Graph_core::Graph_core(std::string_view path, std::string_view name) {
   (void)path;
   (void)name;
   // std::queue<Index_id> deletedEdges;
}

/*  set the boolean value of the master root to 1
 *  This means that that node is a master_root
 *
 *  @params none
 *  @returns void
 */

void Graph_core::Entry16::set_master_root(){
   master_root = 1;
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
 *  @params uint_8t type
 *  @returns void
 */

void Graph_core::Entry16::set_type(uint8_t type){
   pid_bits_or_type = type;
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

/*
Port_ID Graph_core::get_pid(const Index_id master_root_id) const{

   Entry16 *pidNode = reinterpret_cast<Entry16*>(table.data());
   if(pidNode[master_root_id].is_master_root() == true){
     return 0;
   }else{
     return (pidNode[master_root_id].get_pid());
   }

}
*/

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

   if(table.size() <= (master_root_id >> 2)){
     //static cast did not work here: invalid conversion from Entry64 type to Entry16 type
     const Entry16 *boolNode = reinterpret_cast<Entry16*>(table.data());
     return boolNode[master_root_id].is_master_root();
   }else{
     return false;
   }

   //const Entry16 *data = static_cast<Entry16 *>(table[master_root_id].is_master_root());
   //return data[master_root_id].is_master_root();

   /*
   if(static_cast<Entry16*>(table[master_root_id].is_master_root()) == true){
     return true;
   }else{
     return false;
   }
  */
}


/*  Create a master_root node
 *  set the boolean value of the node to 1
 *  set the type of the node
 *  create a master root id based on size of table
 *  place the node in the table using emplace back
 *
 *  @params uint8_t type
 *  @returns Index ID of the node
 */

Index_id Graph_core::create_master_root(uint8_t type){
   //const Entry16 *m = static cast<const Entry16 *>(table.data());
   //const Entry16 *m = static_cast<Entry16 *>(table[master_root_id].is_master_root());

   //I(table.size() > (master_root_id>>2));

   Entry16 m;
   m.set_master_root();
   Index_id id = table.size();
   m.set_type(type);

   //table16.emplace_back(m);

   //index into the table 64(master_root_id) and check if it is a master root or not, using the vector functions
   //get a master root id that is shifted over by 2

   // use pointers to deal with mismatching type
   // emplace back is part of std vector
   return id;
}

/*  Create a master and. point to master root m
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

   //table16.emplace_back(newMaster);

   // who is master root and then you have the master root to point to the master

   return master_id;
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

   Entry16 *addNode = reinterpret_cast<Entry16*>(table.data());

   addNode[sink_id].edge_storage

   //if(boolNode[master_root_id].is_master_root() == 0){

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
   //}else{

   //}
}
*/
