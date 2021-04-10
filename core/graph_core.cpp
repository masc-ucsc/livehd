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

void Graph_core::Entry16::set_master_root() { master_root = 1; }

void Graph_core::Entry16::set_master() { master_root = 0; }

void Graph_core::Entry16::set_type(uint8_t type) { pid_bits_or_type = type; }

// Create a master root node
Index_id Graph_core::create_master_root(uint8_t type) {
  Entry16 m;
  // set the fields

  m.set_master_root();  // set it as master root
  // pid zero is already set to zero, because there is type or pid bits
  Index_id id = table16.size();
  m.set_type(type);
  table16.emplace_back(m);  // emplace back is more efficient
  // use pointers to deal with mismatching type

  // set_type(master_root_id, type, m);
  // use push_back to store new master root in the table
  // push back is part of std vector
  return id;
}

// Create a master and point to master root m
Index_id Graph_core::create_master(const Index_id master_root_id, const Port_ID pid) {
  Entry16 newMaster;
  // set the fields

  newMaster.set_master();            // set it as master
  newMaster.pid_bits_or_type = pid;  // set pid whichever is argument
  newMaster.ptrs             = master_root_id;
  // who is master root and then you have the master root to point to the master
  table16.push_back(newMaster);
  // use push_back to store new master in the table
  return SUCCESS;
}
