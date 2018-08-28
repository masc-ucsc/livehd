//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef CF2DF_STATE_H_
#define CF2DF_STATE_H_

#include "lgraph.hpp"
#include "lgedge.hpp"
#include <string>
#include <unordered_map>

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

class CF2DF_State {
public:
  //sh comment out to avoid generating redundant dfg nodes in %s = $a + $b
  //CF2DF_State(LGraph *l, bool rwf = true) : lgref(l), fluid(rwf) { }
  CF2DF_State(LGraph *l, bool rwf = false) : lgref(l), fluid(rwf) { }
  CF2DF_State(const CF2DF_State &s) : lgref(s.lgref), auxtab(s.auxtab), registers(s.registers), fluid(s.fluid) { }
  CF2DF_State copy() const { return CF2DF_State(*this); }
  virtual ~CF2DF_State() {

  }

  void     set_alias(const std::string &v, Index_ID n);
  Index_ID get_alias(const std::string &v) const { return auxtab.at(v); }
  bool     has_alias(const std::string &v) const { return auxtab.find(v) != auxtab.end(); }
  void     print_aux(){for(const auto &iter:auxtab) fmt::print("auxtab:{:>10} -> {}\n",iter.first, iter.second);};

  const std::unordered_map<std::string, Index_ID> &get_auxtab() const { return auxtab; }
  std::unordered_map<std::string, Index_ID> outputs() const;
  std::unordered_map<std::string, Index_ID> inputs() const;

  void add_register(const std::string &v, Index_ID n) { registers[v] = n; }
  bool fluid_df() const { return fluid; }

  bool is_input(const std::string &v) const { return lgref->is_graph_input(auxtab.at(v)); }
  bool is_output(const std::string &v) const { return lgref->is_graph_output(auxtab.at(v)); }
  //void set_id2id_subg   (const Index_ID &key, const Index_ID &value)     {id2id_subg[key] = value;}
  //void set_name2id_subg (const std::string &key, const Index_ID &value)  {name2id_subg[key] = value;}
  //void set_auxtab       (const std::string &key, const Index_ID &value)  {aux_table[key] = value;}
  //const std::unordered_map<Index_ID, Index_ID>&     get_id2id_subg()           {return id2id_subg;}
  //const std::unordered_map<std::string, Index_ID>&  get_name2id_subg()         {return name2id_subg;}
  //const std::unordered_map<std::string, Index_ID>&  get_auxtab()               {return aux_table;}


private:  typedef bool(*filter)(const CF2DF_State *, const std::string &);
  std::unordered_map<std::string, Index_ID> filter_util(filter fproc) const;
  LGraph *lgref;
  std::unordered_map<std::string, Index_ID> auxtab;
  std::unordered_map<std::string, Index_ID> registers;
  bool fluid;
  //std::unordered_map<Index_ID, Index_ID>    id2id_subg;
  //std::unordered_map<std::string, Index_ID> name2id_subg;
  //new 2018/08/23
  //std::unordered_map<std::string, Index_ID> aux_table;
};

#endif
