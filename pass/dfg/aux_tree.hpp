#ifndef AUX_TREE_H_
#define AUX_TREE_H_

#include <string>
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

  void      set_alias    (const std::string &v, Index_ID n);
  Index_ID  get_alias    (const std::string &v) const { return auxtab.at(v); }
  bool      has_alias    (const std::string &v) const { return auxtab.find(v) != auxtab.end();}
  void      del_alias    (const std::string &v)       { auxtab.erase(v);}
  void      set_pending  (const std::string &v, Index_ID n);
  Index_ID  get_pending  (const std::string &v) const { return pendtab.at(v); }
  bool      has_pending  (const std::string &v) const { return pendtab.find(v) != pendtab.end();}
  void      del_pending  (const std::string &v)       { pendtab.erase(v);}
  void      print_aux();
  const std::unordered_map<std::string, Index_ID> &get_auxtab() const  { return auxtab; }
  const std::unordered_map<std::string, Index_ID> &get_pendtab() const { return pendtab; }

private:
  std::unordered_map<std::string, Index_ID> auxtab;
  std::unordered_map<std::string, Index_ID> pendtab;
};

class Aux_tree{
public:
  Aux_tree():root_auxnd(nullptr){};
  explicit         Aux_tree         (Aux_node *auxnd):root_auxnd(auxnd){ auxes_stack.push_back(auxnd); };
  void             set_parent_child (Aux_node *parent, Aux_node *child, bool branch);
  void             set_parent       (Aux_node *parent, Aux_node *child);
  const Aux_node  *get_parent       (const Aux_node *child) const;
  void             delete_child     (Aux_node *parent, Aux_node *child, bool branch);
  bool             is_root_aux      (const Aux_node *auxtab) const;// for chained parents aux_tabs checking
  Aux_node *       get_root         ();
  Aux_node *       get_cur_auxnd    () const;
  void             set_alias        (const std::string &v, Index_ID n);
  bool             has_alias        (const std::string &v) const;
  Index_ID         get_alias        (const std::string &v) const;
  void             set_pending      (const std::string &v, Index_ID n);
  bool             has_pending      (const std::string &v) const;
  Index_ID         get_pending      (const std::string &v) const;
  void             print_cur_auxnd  ();
  void             auxes_stack_pop  (){auxes_stack.pop_back();};

private:
  Aux_node               *root_auxnd;
  std::vector<Aux_node*>  auxes_stack; //for tracking latest aux_node
  bool                    check_global_alias   (const Aux_node *auxnd, const std::string &v) const;
  Index_ID                get_global_alias     (const Aux_node *auxnd, const std::string &v) const;
  bool                    check_global_pending (const Aux_node *auxnd, const std::string &v) const;
  Index_ID                get_global_pending   (const Aux_node *auxnd, const std::string &v) const;
};


#endif
