#ifndef CF2DF_STATE_H_
#define CF2DF_STATE_H_

#include "lgraph.hpp"
#include "lgedge.hpp"
#include "symbol_table.hpp"
#include <string>
#include <unordered_map>

const std::string READ_MARKER   = "pyrrd__";
const std::string WRITE_MARKER  = "pyrwt__";
const std::string VALID_MARKER  = "pyrvd__";
const std::string RETRY_MARKER  = "pyrrt__";
const std::string TEMP_MARKER   = "tmp__";

//const char *LOGICAL_AND_OP = "AND";
//const char *LOGICAL_OR_OP  = "OR";
//const char *LOGICAL_NOT_OP = "NOT";

#define LOGICAL_AND_OP "AND"
#define LOGICAL_OR_OP  "OR"
#define LOGICAL_NOT_OP "NOT"

const char REGISTER_MARKER = '@';
const char INPUT_MARKER = '$';
const char OUTPUT_MARKER = '%';
const Port_ID REG_INPUT = 'D';
const Port_ID REG_OUTPUT = 'Q';

class CF2DF_State {
public:
  CF2DF_State(LGraph *l, bool rwf = true) : lgref(l), fluid(rwf) { }
  CF2DF_State(const CF2DF_State &s) : lgref(s.lgref), last_refs(s.last_refs), registers(s.registers), fluid(s.fluid) { }
  CF2DF_State copy() const { return CF2DF_State(*this); }

  void update_reference(const std::string &v, Index_ID n);
  Index_ID get_reference(const std::string &v) const { return last_refs.at(v); }
  bool has_reference(const std::string &v) const { return last_refs.find(v) != last_refs.end(); }

  const std::unordered_map<std::string, Index_ID> &references() const { return last_refs; }
  std::unordered_map<std::string, Index_ID> outputs() const;
  std::unordered_map<std::string, Index_ID> inputs() const;

  void add_register(const std::string &v, Index_ID n) { registers[v] = n; }
  bool fluid_df() const { return fluid; }

  Symbol_Table &symbol_table() { return table; }

  bool is_input(const std::string &v) const { return lgref->is_graph_input(last_refs.at(v)); }
  bool is_output(const std::string &v) const { return lgref->is_graph_output(last_refs.at(v)); }

private:
  typedef bool(*filter)(const CF2DF_State *, const std::string &);
  std::unordered_map<std::string, Index_ID> filter_util(filter fproc) const;

  LGraph *lgref;
  std::unordered_map<std::string, Index_ID> last_refs;
  std::unordered_map<std::string, Index_ID> registers;
  Symbol_Table table;
  bool fluid;
};

#endif
