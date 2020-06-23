#pragma once

#include "lnast.hpp"

class Semantic_pass {
protected:
  bool is_primitive_op(const Lnast_ntype node_type);
  bool is_tree_structs(const Lnast_ntype node_type);
  void check_primitive_ops(Lnast* lnast, const Lnast_nid &lnidx_opr, const Lnast_ntype node_type);
  void check_if_op(Lnast* lnast, const Lnast_nid &lnidx_opr);
  void check_for_op(Lnast* lnast, const Lnast_nid &lnidx_opr);
  void check_while_op(Lnast* lnast, const Lnast_nid &lnidx_opr);
  void check_func_def(Lnast* lnast, const Lnast_nid &lnidx_opr);
  void check_func_call(Lnast* lnast, const Lnast_nid &lnidx_opr);

public:
  // NOTE: Test does no consider tuple operations yet
  void semantic_check(Lnast* lnast);
};