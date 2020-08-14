//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "firrtl.pb.h"
#include "lconst.hpp"
#include "lnast.hpp"
#include "mmap_tree.hpp"
#include "lgraph.hpp"
#include "pass.hpp"

class Inou_firrtl : public Pass {
protected:
  //----------- FOR toLNAST ----------
  std::string_view create_temp_var(Lnast &lnast);
  std::string_view get_new_seq_name(Lnast &lnast);
  std::string      get_full_name(Lnast &lnast, Lnast_nid &parent_node, const std::string &term, const bool is_rhs);

  // Helper Functions (for handling specific cases)
  void     create_bitwidth_dot_node(Lnast &lnast, uint32_t bw, Lnast_nid &parent_node, const std::string &port_id);
  uint32_t get_bit_count(const firrtl::FirrtlPB_Type type);
  void     init_wire_dots(Lnast &lnast, const firrtl::FirrtlPB_Type &type, const std::string &id,
                          Lnast_nid &parent_node);  // const firrtl::FirrtlPB_Statement_Wire& expr, Lnast_nid& parent_node);
  void     init_reg_dots(Lnast &lnast, const firrtl::FirrtlPB_Type &type, const std::string &id,
                         const firrtl::FirrtlPB_Expression& clock, const firrtl::FirrtlPB_Expression& reset,
                         const firrtl::FirrtlPB_Expression& init,  Lnast_nid& parent_node);
  void     init_reg_ref_dots(Lnast &lnast, const std::string &id, const firrtl::FirrtlPB_Expression& clock,
                             const firrtl::FirrtlPB_Expression& reset, const firrtl::FirrtlPB_Expression& init,
                             uint32_t bitwidth, Lnast_nid& parent_node);
  void     PreCheckForMem(Lnast &lnast, Lnast_nid &stmt_node, const firrtl::FirrtlPB_Statement& stmt);
  void     InitMemory   (Lnast &lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_Memory& mem);
  void     InitCMemory  (Lnast &lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_CMemory& cmem);
  void     HandleMemPortPre(Lnast &lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_MemoryPort& mport);
  void     HandleMemPort(Lnast &lnast, Lnast_nid& parent_node, const firrtl::FirrtlPB_Statement_MemoryPort& mport);
  void     PortDirInference(const std::string& port_name, const std::string& mem_name, const bool is_rhs);
  void     create_module_inst(Lnast &lnast, const firrtl::FirrtlPB_Statement_Instance &inst, Lnast_nid &parent_node);

  void HandleMuxAssign(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node,
                       const std::string &lhs_of_asg);
  void HandleValidIfAssign(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node,
                           const std::string &lhs_of_asg);
  void HandleNEQOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleUnaryOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleAndReducOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleOrReducOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleXorReducOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleNegateOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleConvOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleExtractBitsOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                           const std::string &lhs);
  void HandleHeadOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleTailOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleConcatOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandlePadOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void HandleTwoExprPrimOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                           const std::string &lhs);
  void HandleStaticShiftOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node,
                           const std::string &lhs);
  void HandleTypeConvOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void AttachExprStrToNode(Lnast &lnast, const std::string_view access_str, Lnast_nid &parent_node);

  std::string_view HandleBundVecAcc(Lnast &lnast, const firrtl::FirrtlPB_Expression expr, Lnast_nid &parent_node, const bool is_rhs);
  std::string_view CreateDotsSelsFromStr(Lnast& ln, Lnast_nid& parent_node, const std::string& flattened_str);
  std::string FlattenExpression(Lnast &ln, Lnast_nid &parent_node, const firrtl::FirrtlPB_Expression &expr);

  // Deconstructing Protobuf Hierarchy
  void create_io_list(const firrtl::FirrtlPB_Type &type, uint8_t dir, const std::string &port_id,
                      std::vector<std::tuple<std::string, uint8_t, uint32_t>> &vec);
  void ListPortInfo(Lnast &lnast, const firrtl::FirrtlPB_Port &port, Lnast_nid parent_node);

  void PrintPrimOp(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, const std::string &symbol, Lnast_nid &parent_node);
  void ListPrimOpInfo(Lnast &lnast, const firrtl::FirrtlPB_Expression_PrimOp &op, Lnast_nid &parent_node, const std::string &lhs);
  void InitialExprAdd(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node, const std::string &lhs_unalt);
  std::string ReturnExprString(Lnast &lnast, const firrtl::FirrtlPB_Expression &expr, Lnast_nid &parent_node, const bool is_rhs);

  void ListStatementInfo(Lnast &lnast, const firrtl::FirrtlPB_Statement &stmt, Lnast_nid &parent_node);
  void PerformLateMemAssigns(Lnast &lnast, Lnast_nid& parent_node);

  void PopulateAllModsIO(Eprp_var& var, const firrtl::FirrtlPB_Circuit &circuit, const std::string& file_name);
  void AddPortToMap(const std::string &mod_id, const firrtl::FirrtlPB_Type &type, uint8_t dir, const std::string &port_id, Sub_node& sub, uint64_t &inp_pos, uint64_t &out_pos);
  void AddPortToSub(Sub_node& sub, uint64_t &inp_pos, uint64_t &out_pos, const std::string& port_id, const uint8_t& dir);
  Sub_node AddModToLibrary(Eprp_var& var, const std::string& mod_name, const std::string& file_name);

  std::string ConvertBigIntToStr(const firrtl::FirrtlPB_BigInt& bigint);

  void ListUserModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module &module, const std::string& file_name);
  void GrabExtModuleInfo(const firrtl::FirrtlPB_Module_ExternalModule& emod);
  void ListModuleInfo(Eprp_var &var, const firrtl::FirrtlPB_Module &module, const std::string& file_name);
  void IterateModules(Eprp_var &var, const firrtl::FirrtlPB_Circuit &circuit, const std::string& file_name);
  void IterateCircuits(Eprp_var &var, const firrtl::FirrtlPB &firrtl_input, const std::string& file_name);

  static void toLNAST(Eprp_var &var);

  //----------- FOR toFIRRTL ----------
  static void toFIRRTL(Eprp_var &var);
  void        do_tofirrtl(std::shared_ptr<Lnast> ln, firrtl::FirrtlPB_Circuit *circuit);
  void        process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx_smts, firrtl::FirrtlPB_Module_UserModule *umod);
  void        process_ln_stmt(Lnast &ln, const Lnast_nid &lnidx_smts, firrtl::FirrtlPB_Statement_When *when, uint8_t pos_to_add_to);

  bool process_ln_assign_op(Lnast &ln, const Lnast_nid &lnidx_assign, firrtl::FirrtlPB_Statement *fstmt);
  void process_tup_asg(Lnast &ln, const Lnast_nid& lnidx_asg, const std::string_view &lhs, firrtl::FirrtlPB_Statement *fstmt);
  void process_ln_nary_op(Lnast &ln, const Lnast_nid &lnidx_assign, firrtl::FirrtlPB_Statement *fstmt);
  void process_ln_not_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);
  void process_ln_par_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);
  firrtl::FirrtlPB_Statement_When *process_ln_if_op(Lnast &ln, const Lnast_nid &lnidx_if);
  void                             process_ln_range_op(Lnast &ln, const Lnast_nid &lnidx_op);
  void                             process_ln_bitsel_op(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);
  bool                             process_ln_dot(Lnast &ln, const Lnast_nid &lnidx_op, firrtl::FirrtlPB_Statement *fstmt);

  void handle_attr_assign(Lnast &ln, const Lnast_nid &lhs, const Lnast_nid &rhs);
  void handle_sign_attr  (Lnast &ln, const std::string_view &var_name, const Lnast_nid &rhs);
  void handle_clock_attr (Lnast &ln, const std::string_view &var_name, const Lnast_nid &rhs);
  void handle_async_attr (Lnast &ln, const std::string_view &var_name, const Lnast_nid &rhs);
  void handle_reset_attr (Lnast &ln, const std::string_view &var_name, const Lnast_nid &rhs);
  firrtl::FirrtlPB_Expression_SubField* make_subfield_expr(std::string name);

  uint8_t process_op_children(Lnast &ln, const Lnast_nid &lnidx_if, const std::string &firrtl_op);
  void    make_assignment(Lnast &ln, const Lnast_nid &lnidx_lhs, firrtl::FirrtlPB_Expression *expr_rhs,
                          firrtl::FirrtlPB_Statement *fstmt);

  // Helper Functions
  bool is_inp(const std::string_view str);
  bool is_outp(const std::string_view str);
  bool is_reg(const std::string_view str);
  bool is_wire(const std::string_view str);
  void create_connect_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression *rhs_expr,
                           firrtl::FirrtlPB_Statement *fstmt);
  void create_node_stmt(Lnast &ln, const Lnast_nid &lhs, firrtl::FirrtlPB_Expression *rhs_expr, firrtl::FirrtlPB_Statement *fstmt);
  std::string get_firrtl_name_format(Lnast &ln, const Lnast_nid &lnidx);
  std::string strip_prefixes(const std::string_view str);
  void        add_refcon_as_expr(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression *expr);
  void        add_const_as_ilit(Lnast &ln, const Lnast_nid &lnidx, firrtl::FirrtlPB_Expression_IntegerLiteral *ilit);
  firrtl::FirrtlPB_Expression_PrimOp_Op get_firrtl_oper_code(const Lnast_ntype &op_type);

  // Finding Circuit Components
  void                         FindCircuitComps(Lnast &ln, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         SearchNode(Lnast &ln, const Lnast_nid &parent_node, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         CheckTuple(Lnast &ln, const Lnast_nid &tup_node, firrtl::FirrtlPB_Module_UserModule *umod);
  void                         CheckRefForComp(Lnast &ln, const Lnast_nid &ref_node, firrtl::FirrtlPB_Module_UserModule *umod);
  firrtl::FirrtlPB_Type *      CreateTypeObject(uint32_t bitwidth);
  firrtl::FirrtlPB_Expression *CreateULitExpr(const uint32_t &val);
  void             CreateSubmodInstance(Lnast &ln, const Lnast_nid &fcall_node, firrtl::FirrtlPB_Module_UserModule *umod);
  std::string_view ConvergeFCallNames(const std::string_view func_out, const std::string_view func_inp);

private:
  //----------- FOR toLNAST ----------
  absl::flat_hash_set<std::string> input_names;
  absl::flat_hash_set<std::string> output_names;
  absl::flat_hash_set<std::string> register_names;
  absl::flat_hash_set<std::string> memory_names;
  absl::flat_hash_set<std::string> async_rst_names;

  // Maps an instance name to the module name.
  absl::flat_hash_map<std::string, std::string> inst_to_mod_map;
  // Maps (module name + I/O name) pair to direction of that I/O in that module.
  absl::flat_hash_map<std::pair<std::string, std::string>, uint8_t> mod_to_io_dir_map;
  /* Maps module name to list of tuples of (signal name + signal biwdith + signal dir).
   * Used when a submodule inst is created, have to specify bw of all IO in module. */
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::tuple<std::string, uint32_t, uint8_t>>> mod_to_io_map;
  // Map used by external modules to indicate parameters names + values.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::pair<std::string, std::string>>> emod_to_param_map;
  // Map name of memory to tuple of (__fwd true/false, read latency, write latency)
  absl::flat_hash_map<std::string, std::tuple<bool, std::string_view, std::string_view>> mem_props_map;
  // Map of memory port ids made in Memory Port statements to memory block name.
  absl::flat_hash_map<std::string, std::string> dangling_ports_map;
  // Map which holds all of the ports that need late assigns (and their direction).
  enum MPORT_DIR { READ, WRITE, READ_WRITE,
                   READP, WRITEP, READ_WRITEP,
                   READI, WRITEI, READ_WRITEI, INFER };
  absl::flat_hash_map<std::string, MPORT_DIR> late_assign_ports;


  uint32_t temp_var_count;
  uint32_t seq_counter;

  //----------- FOR toFIRRTL ---------
  absl::flat_hash_map<std::string, firrtl::FirrtlPB_Port *>      io_map;
  absl::flat_hash_map<std::string, firrtl::FirrtlPB_Statement *> reg_wire_map;
  // Contains wires that need to be renamed when found (mainly due to func. calls)
  absl::flat_hash_map<std::string, std::string> wire_rename_map;

  /* Since range and bit_sel nodes are separated, this map is used to track
   * the name used by the range node and then the low/high nodes. */
  absl::flat_hash_map<std::string, std::pair<Lnast_nid, Lnast_nid>> name_to_range_map;

  // This tracks the LHS name used for a dot node then the two other children.
  absl::flat_hash_map<std::string, std::pair<Lnast_nid, Lnast_nid>> dot_map;

  // This indicates which register have async reset (if not on here, it's sync)
  absl::flat_hash_set<std::string_view> async_regs;

public:
  Inou_firrtl(const Eprp_var &var);

  static void setup();
};
