#include "graph_core.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <vector>
#include <iterator>

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

void Graph_core::Entry16::set_master_root() { master_root = 1; }

uint8_t Graph_core::test_master_root(const Index_id master_root_id) const {
  const Entry16 *return_type = reinterpret_cast<const Entry16 *>(table.data());
  return return_type[master_root_id].test_master_root();
}
/*  Set the boolean value of the node to 0
 *  This value means that that node is master
 *
 *  @params none
 *  @return void
 */

void Graph_core::Entry16::set_master() { master_root = 0; }

/*  In the master_root that is created set the type of
 *  the node to what is passed in
 *
 *  @params uint8_t type
 *  @returns void
 */
void Graph_core::Entry16::set_type(uint8_t type) { pid_bits_or_type = type; }

/* Return the type that was set in set_type
 *
 * @params Index_id master_root_id
 * @returns uint8_t type or 1 for failure
 */

uint8_t Graph_core::get_type(const Index_id master_root_id) const{
   const Entry16 *return_type = reinterpret_cast<const Entry16*>(table.data());
   if(return_type[master_root_id].is_master_root() == false){
     return 34;
   }
   //return return_type[master_root_id].get_type();
   return 32;
}

/*  Set the type of any node indicated by the master root id
 *  if master root, find the correct node in the table and set the type
 *  if not a master root do nothing
 *
 *  @params const Index_id master_root_id, uint8_t type
 *  @returns void
 */

void Graph_core::set_type(const Index_id master_root_id, uint8_t type) {
  Entry16 *typeNode = reinterpret_cast<Entry16 *>(table.data());
  if (typeNode[master_root_id].is_master_root() == true) {
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

Port_ID Graph_core::get_pid(const Index_id master_root_id) const {
  const Entry16 *pidNode = reinterpret_cast<const Entry16 *>(table.data());
  if (pidNode[master_root_id].is_master_root() == true) {
    return 0;
  }
  return (pidNode[master_root_id].get_pid());
}

/* Check if the node is a master_root or not
 *
 * @params const Index_id master_root_id
 * @returns bool
 */

bool Graph_core::is_master_root(const Index_id master_root_id) {
  // use the function is_master_root() inside the Entry16 class
  // return true or false
  // use the vector functions to find the right node
  //
  // USE AN ENUM INSTEAD OF BOOL?

  //if(table.size() > (master_root_id >> 2)){ //check the condition and on ln 76

     const Entry16 *boolNode = reinterpret_cast<const Entry16*>(table.data());
     return boolNode[master_root_id].is_master_root();

   //if(table[master_root_id].is_master_root() == true){
   //  return true;
   //}

   //}else{
   //  return false;
   //}
}
/*  Create a master_root node
 *  set the boolean value of the node to 1
 *  set the type of the node
 *  create a master root id based on the size of the table
 *  place the node in the table using emplace back
 *
 *  @params uint8_t type
 *  @returns Index_id of the node
 */

Index_id Graph_core::create_master_root(uint8_t type) {
  Entry16 m;

   m.set_master_root();
   m.set_type(type);

   Entry16 *root_pointer = &m;
   const Entry64 *emplace_root_element = reinterpret_cast<const Entry64 *>(root_pointer);

   table.emplace_back(*emplace_root_element);
   Index_id id = table.size();

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

Index_id Graph_core::create_master(const Index_id master_root_id, const Port_ID pid) {
   Entry16 newMaster;

   newMaster.set_master();
   newMaster.pid_bits_or_type = pid;
   newMaster.ptrs = master_root_id;
   // who is master root and then you have the master root to point to the master
   Entry16 *master_pointer = &newMaster;
   const Entry64 *emplace_master_element = reinterpret_cast<const Entry64*>(master_pointer);

   table.emplace_back(*emplace_master_element);
   Index_id master_id = table.size();

   return master_id;
}

/* function that inserts values into edge_storage given the relative indexes
 * finds the next empty spot and inserts the value
 *
 * @params uint8_t rel_index
 * @returns 0 if success or 1 if fail
 */

uint8_t Graph_core::Entry16::insert_edge(uint8_t rel_index) {
  for (uint8_t i = 0; i < 10; ++i) {
     if (edge_storage[i] == 0) {
       edge_storage[i] = rel_index;
       return 0;  // success
     }
  }
  return 1;
}

/* function that deletes values from the edge storage of an Entry16
 *
 * @params uint8_t rel_index
 * @returns 0 if success and 1 if empty
 */
uint8_t Graph_core::Entry16::delete_edge(uint8_t rel_index){

   uint8_t index = binary_search(0, 9, rel_index);
   if(index == 10){
     return 1;
   }

   int i;
   for(i = index; i < 9; ++i){ // dont want to segfault so use n-1
       edge_storage[i] = edge_storage[i + 1];
   }
   return 0;
}

/* function that inserts values into overflow's edge_storage given the relative indexes
 * finds the next empty spot and inserts the value
 *
 * @params uint8_t rel_index
 * @returns 0 if success or 1 if fail
 */

uint8_t Graph_core::Entry64::insert_edge(Index_id insert_id){
   // overflow is full
   if(last_byte() != 0){
     // loop through to see if any edge > insert edge
     for (unsigned long i = 0; i < sizeof(edge_storage); ++i){
       // if a greater edge is found
       if(insert_id < edge_storage[i]) {
         // get the last element
         auto temp = last_byte();
         delete_edge();                  // remove that edge from the edge
         insert_edge(insert_id);         // now that edge storage is not empty we can insert
         return temp;                    // return temp to add_edge to make a new overflow
       }
     }
     return 1; // no greater edge found create new overflow

   }else if (edge_storage[1] == 0){ // empty
     edge_storage[1] = insert_id;
     return 0;
   }else{ // not empty not full

     int i;
     for(i = 62; i <= 0; --i){
       if(edge_storage[i] > insert_id){
         edge_storage[i + 1] = edge_storage[i];
       }else if(edge_storage[i] == 0){ // does this case save time?
         //do nothing
       }else if(edge_storage[i] == insert_id){ // do I need this case at all?
         edge_storage[i + 1] = edge_storage[i];
         edge_storage[i] = insert_id;
       }else{
         edge_storage[i + 1] = insert_id;
       }
     }
     return 0;
   }
}

/* function that deletes values from the edge storage of an Entry64
 *
 * @params uint8_t rel_index
 * @returns 0 if success and 1 if empty
 */

///*
uint8_t Graph_core::Entry64::delete_edge(){
   for(int i = sizeof(edge_storage) - 1; i >= 0; --i){
     if(edge_storage[i] != 0){
       edge_storage[i] = 0;
       return 0; // success
     }
   }
   return 1;
}
//*/

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

void Graph_core::add_edge(const Index_id sink_id, const Index_id driver_id){

   Entry16 *add_node = reinterpret_cast<Entry16*>(table.data());

   // max delta size is 32bits
   // when testing do sequence of 32 bit numbers
   //master_root sink_id and driver_id are indices


   uint8_t rel_index1 = driver_id - sink_id;
   uint8_t rel_index2 = sink_id - driver_id; // we dont care if negative

   auto inp_count = 0;
   auto out_count = 0;

   uint8_t inp_overflow = add_node[sink_id].insert_edge(rel_index1);

   while(inp_overflow >= 1){ // input is full create overflow node
     Entry64 input;

     inp_overflow = input.insert_edge(driver_id);

     input.set_input();
     input.creator_pointer = sink_id;
     input.overflow_next = 0xF;

     table.emplace_back(input);
     auto overflow_index = table.size();
     add_node[sink_id].overflow_next = overflow_index;

     if(inp_count > 0){
       // if this isn't the first overflow created
       // need to make the prev overflow point the current one
       table[overflow_index - 1].overflow_next = overflow_index;
     }

     ++inp_count;
   }

   uint8_t out_overflow = add_node[driver_id].insert_edge(rel_index2);

   while(out_overflow >= 1){ // input is full create overflow node

     if(add_node[driver_id].overflow_next == 0xF){ // if next pointer is null
       Entry64 output;

       out_overflow = output.insert_edge(sink_id);

       output.set_output();
       output.creator_pointer = driver_id;
       output.overflow_next = 0xF;

       table.emplace_back(output);
       auto overflow_index = table.size();
       add_node[sink_id].overflow_next = overflow_index;

       if(out_count > 0){
         // if this isn't the first overflow created
         // need to make the prev overflow point the current one
         table[overflow_index - 1].overflow_next = overflow_index;
       }
       ++out_count;

     }else{ // use existing overflow



     }
   }
}

/* helper function that returns the index in the table of the overflow
 *
 * @params Index_id overflow_index, uint8_t type
 * @returns 0 if fail and otherwise returns the correct index in the table
 */

uint8_t Graph_core::check_overflow_index(Index_id overflow_index, uint8_t type){
   uint8_t correct_overflow = overflow_index;

   while(table[correct_overflow].last_byte() != type){ //check whether the type of the overflow is wrong
     //if wrong
     if(table[correct_overflow].overflow_next == 0x7){ // check if the next pointer is null
       return 0; // no other overflow and wrong type
     }else{
       correct_overflow = table[correct_overflow].overflow_next;
     }
   }
   //otherwise return whatever was passed
   return overflow_index;
}

uint8_t Graph_core::Entry16::binary_search(uint8_t i, uint8_t j, uint8_t rel_index){

   uint8_t m;
   if(i < j){
     m = (i + j)/2;
     if(rel_index == edge_storage[m]){
       return m;
     }else if (rel_index < edge_storage[m]){
       return binary_search(i, m - 1, rel_index);
     }else{
       return binary_search(m + 1, j, rel_index);
     }
   }
   return j+1; // wanted to return -1 but cant so instead used j+1 as out of bounds
}

/*  Remove an bidirectional edge to a node
 *  Store the deltas in the edge_storage of each Entry 16 or in the overflow
 *
 *  @params const Index_ID sink_id, const Index_ID driver_id
 *  @returns void
 */

/*
void Graph_core::del_edge(const Index_id sink_id, const Index_id driver_id){

   Entry16 *del_node = reinterpret_cast<Entry16*>(table.data());

   //if it point to an overflow use the pointer to get there
   // need to delete all overflow next pointers back to 0xF
   // need to set next64_free
   // start with the sink and then delete for the driver

   if(del_node[sink_id].overflow_next != 0xF){ // overflow was created
     // while entry 64 overflow next is not null
     // iterate through the overflow

     auto overflow_index = check_overflow_index(del_node[sink_id].overflow_next, 0x80);

     // TODO add error checking and delete the pointer in the correct locations
   }else{
     // delete the edge from the sink and driver in respective edge storages
     uint8_t rel_index1 = driver_id - sink_id;
     uint8_t rel_index2 = sink_id - driver_id;

     uint8_t inp_overflow = del_node[sink_id].delete_edge(rel_index1);
     uint8_t out_overflow = del_node[driver_id].delete_edge(rel_index2);
     // TODO error checking
   }
}

//*/
