#ifndef AUX_TREE_H_
#define AUX_TREE_H_

#include <string>
#include <nih/macros.h>
#include "meta/lgraph.hpp"
#include "core/lgedge.hpp"

const std::string READ_MARKER   = "pyrrd__";
const std::string WRITE_MARKER  = "pyrwt__";
const std::string VALID_MARKER  = "pyrvd__";
const std::string RETRY_MARKER  = "pyrrt__";
const std::string TEMP_MARKER   = "tmp__";

#define LOGICAL_AND_OP "AND"
#define LOGICAL_OR_OP  "OR"
#define LOGICAL_NOT_OP "NOT"

const char REGISTER_MARKER = '@';
const char INPUT_MARKER = '$';
const char OUTPUT_MARKER = '%';
const char REFERENCE_MARKER = '\\';
const Port_ID REG_INPUT = 'D';
const Port_ID REG_OUTPUT = 'Q';

class Aux_node{
public:
  Aux_node():lchild(nullptr),rchild(nullptr),parent(nullptr) {};
  virtual ~Aux_node(){}
  Aux_node* lchild;
  Aux_node* rchild;
  Aux_node* parent;

  void      set_alias(const std::string &v, Index_ID n);
  Index_ID  get_alias(const std::string &v) const { return auxtab.at(v); }
  bool      has_alias(const std::string &v) const { return auxtab.find(v) != auxtab.end();}
  void      print_aux();
  const std::unordered_map<std::string, Index_ID> &get_auxtab() const { return auxtab; }

private:
  std::unordered_map<std::string, Index_ID> auxtab;
};

class Aux_tree{
public:
  Aux_tree():root_auxnd(nullptr){};
  explicit           Aux_tree                (Aux_node *auxnd):root_auxnd(auxnd)   {};
  void               set_child               (Aux_node *parent, Aux_node *child, bool branch);
  void               set_parent              (Aux_node *parent, Aux_node *child);
  const Aux_node *   get_parent              (const Aux_node *child) const;
  void               delete_child            (Aux_node *parent, bool branch);
  bool               is_root_aux             (const Aux_node *auxtab) const;// for chained parents aux_tabs checking
  Aux_node *         get_root                ();
  void               set_alias               (const std::string &v, Index_ID n);
  bool               has_alias               (const std::string &v) const;
  Index_ID           get_alias               (const std::string &v) const;
  //std::vector<Aux_node *>  pre_order_trans   (Aux_node *node, std::vector<Aux_node*> &auxes_stack);

private:
  Aux_node               *root_auxnd;
  std::vector<Aux_node*>  auxes_stack; //for tracking latest aux_node
  bool         check_global_alias      (const Aux_node *auxnd, const std::string &v) const;
  Index_ID     get_global_alias        (const Aux_node *auxnd, const std::string &v) const;
  Aux_node *   get_latest_aux          () const;
};


#endif
