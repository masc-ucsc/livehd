#ifndef AUX_TREE_H_
#define AUX_TREE_H_

#include <string>
#include "meta/lgraph.hpp"
#include "core/lgedge.hpp"

class Aux_node{
public:
  Aux_node():lchild(nullptr),rchild(nullptr),parent(nullptr) {};
  Aux_node* lchild;
  Aux_node* rchild;
  Aux_node* parent;
  void set_alias(const std::string &v, Index_ID n);
private:
  std::unordered_map<std::string, Index_ID> auxtab;
};

class Aux_tree{
public:
  Aux_tree():root(nullptr){};
  explicit  Aux_tree(Aux_node *node):root(node)   {};
  void      set_child(Aux_node *parent, Aux_node *child, bool branch);
  void      set_parent(Aux_node *parent, Aux_node *child);
  Aux_node* get_parent(Aux_node *child);
  void      delete_child(Aux_node *parent, bool branch);

private:
  Aux_node *root;
};


#endif
//Aux_node(std::unordered_map<std::string, Index_ID> auxtab_in):lchild(nullptr),rchild(nullptr),parent(nullptr),auxtab(auxtab_in){};
