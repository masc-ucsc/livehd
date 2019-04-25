#ifndef AUX_TREE_H_
#define AUX_TREE_H_

#include <string>
#include <string_view>

#include "lgedge.hpp"
#include "lgraph.hpp"

constexpr std::string_view READ_MARKER  = "prprd__";
constexpr std::string_view WRITE_MARKER = "prpwt__";
constexpr std::string_view VALID_MARKER = "prpvd__";
constexpr std::string_view RETRY_MARKER = "prprt__";
constexpr std::string_view TEMP_MARKER  = "tmp__";

#define LOGICAL_AND_OP "AND"
#define LOGICAL_OR_OP "OR"
#define LOGICAL_NOT_OP "NOT"

const char    REGISTER_MARKER  = '@';
const char    INPUT_MARKER     = '$';
const char    OUTPUT_MARKER    = '%';
const char    REFERENCE_MARKER = '\\';
const char    POS_CONST_MARKER = '0';
const char    NEG_CONST_MARKER = '-';
const Port_ID REG_INPUT        = 'D';
const Port_ID REG_OUTPUT       = 'Q';

class Aux {
public:
  Aux():lchild(nullptr), rchild(nullptr), parent(nullptr){};
  virtual ~Aux(){};
  //SH:FIXME: should move to private data member and use setter/getter function?
  Aux *lchild;
  Aux *rchild;
  Aux *parent;

  void set_alias(std::string_view v, Node_pin n);
  Node_pin get_alias(std::string_view v) const {
    return auxtab.at(v);
  }
  bool has_alias(std::string_view v) const {
    return auxtab.find(v) != auxtab.end();
  }
  void del_alias(std::string_view v) {
    auxtab.erase(v);
  }
  void set_pending(std::string_view v, Node_pin n);
  Node_pin get_pending(const std::string &v) const {
    return pendtab.at(v);
  }
  bool has_pending(std::string_view v) const {
    return pendtab.find(v) != pendtab.end();
  }
  void del_pending(std::string_view v) {
    pendtab.erase(v);
  }
  void print_aux();
  const absl::flat_hash_map<std::string, Node_pin> &get_auxtab() const {
    return auxtab;
  }
  const absl::flat_hash_map<std::string, Node_pin> &get_pendtab() const {
    return pendtab;
  }

private:
  absl::flat_hash_map<std::string, Node_pin> auxtab;
  absl::flat_hash_map<std::string, Node_pin> pendtab;
};

class Aux_tree {
public:
  Aux_tree()
      : root_aux(nullptr){};
  virtual ~Aux_tree(){};
  explicit Aux_tree(Aux *aux)
      : root_aux(aux) {
    auxes_stack.push_back(aux);
  };
  void            set_parent_child(Aux *parent, Aux *child, bool branch);
  void            set_parent(Aux *parent, Aux *child);
  const Aux*      get_parent(const Aux *child) const;
  void            disconnect_child(Aux *parent, Aux *child, bool branch);
  bool            is_root_aux(const Aux *auxtab) const; // for chained parents aux_tabs checking
  Aux *           get_root();

  //SH:FIXME: dangerous approach to return handles(i.e., pointer, reference etc) of a object. Other approach?
  //SH:FIXME: at least change to return const to avoid client write
  //SH:FIXME: another risk is the Aux object might not exist anymore -> returning a garbage.
  Aux*            get_cur_aux() const;
  void            set_alias(std::string_view v, Node_pin n);
  bool            has_alias(std::string_view v) const;
  Node_pin        get_alias(std::string_view v) const;
  void            update_alias(std::string_view v, Node_pin n);
  void            set_pending(std::string_view v, Node_pin n);
  bool            has_pending(std::string_view v) const;
  Node_pin        get_pending(std::string_view v) const;
  void            update_pending(std::string_view v, Node_pin n);
  void            print_cur_aux();
  void            auxes_stack_pop() {auxes_stack.pop_back();};

private:
  Aux *              root_aux;
  std::vector<Aux *> auxes_stack; // for tracking latest aux_node
  bool               check_global_alias    (const Aux *aux, std::string_view v) const;
  bool               check_global_pending  (const Aux *aux, std::string_view v) const;
  Node_pin           get_global_alias      (const Aux *aux, std::string_view v) const;
  Node_pin           get_global_pending    (const Aux *aux, std::string_view v) const;
};

#endif
