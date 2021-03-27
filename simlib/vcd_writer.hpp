// This file is distributed under the LICENSE.vcd-writer License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vcd {

static unsigned global_timestamp;

namespace utils {
// -----------------------------
std::string format(const char *fmt, ...);
std::string now();
}  // namespace utils

// -----------------------------
// valid VCD scope types
enum class ScopeType : char { begin, fork, function, module, task };
// valid VCD variable types
enum class VariableType : char {
  wire,
  reg,
  string,
  parameter,
  integer,
  real,
  realtime,
  time,
  event,
  supply0,
  supply1,
  tri,
  triand,
  trior,
  trireg,
  tri0,
  tri1,
  wand,
  wor
};
// valid timescale numbers
enum class TimeScale : char { ONE = 1, TEN = 10, HUNDRED = 100 };
// valid timescale units
enum class TimeScaleUnit : char { s, ms, us, ns, ps, fs };
// -----------------------------
enum VCDValues : char { ONE = '1', ZERO = '0', UNDEF = 'x', HIGHV = 'z' };

using TimeStamp = unsigned;
using VarValue  = std::string;

// -----------------------------
class VCDException : public std::exception {
public:
  explicit VCDException(const std::string &message) : m_message("VCD error: " + message) {}
  virtual const char *what() const throw() { return m_message.c_str(); }

private:
  const std::string m_message;
};

class VCDPhaseException : public VCDException {
public:
  explicit VCDPhaseException(const std::string &message) : VCDException(message) {}
};

class VCDTypeException : public VCDException {
public:
  explicit VCDTypeException(const std::string &message) : VCDException(message) {}
};

// -----------------------------
struct VCDScope;
using ScopePtr = std::shared_ptr<VCDScope>;
struct ScopePtrHash {
  bool operator()(const ScopePtr &l, const ScopePtr &r) const;
};

// -----------------------------
class VCDVariable;
using VarPtr = std::shared_ptr<VCDVariable>;
struct VarPtrHash {
  size_t operator()(const VarPtr &p) const;
};
struct VarPtrEqual {
  bool operator()(const VarPtr &a, const VarPtr &b) const;
};

// -----------------------------
struct VarSearch;
using VarSearchPtr = std::shared_ptr<VarSearch>;

// -----------------------------
struct VCDHeader;
struct VCDHeaderDeleter {
  void operator()(VCDHeader *p);
};
using HeadPtr = std::unique_ptr<VCDHeader, VCDHeaderDeleter>;

// -----------------------------
HeadPtr makeVCDHeader(TimeScale timescale_quan = TimeScale::ONE, TimeScaleUnit timescale_unit = TimeScaleUnit::ns,
                      const std::string &date = utils::now(), const std::string &comment = "", const std::string &version = "");

// -----------------------------
// Writer of a Value Change Dump file
// A VCD file captures time-ordered changes to the value of variables
class VCDWriter {
  FILE *    ofile;
  TimeStamp timestamp;
  HeadPtr   header;

  // settings
  std::string filename;
  std::string scope_sep;
  ScopeType   scope_def_type;

  // check changes of vars' values
  std::unordered_map<VarPtr, VarValue> vars_prevs;

  // state
  bool closed;
  bool dumping;
  bool registering;
  // gen var idents (internal names)
  unsigned     next_var_id;
  VarSearchPtr search;

  std::set<ScopePtr, ScopePtrHash>                    scopes;
  std::unordered_set<VarPtr, VarPtrHash, VarPtrEqual> vars;

public:
  VCDWriter(const std::string &filename, HeadPtr &&header = {}, unsigned init_timestamp = 0u);

  ~VCDWriter() {
    flush();
    fclose(this->ofile);
  }

  // Register a VCD variable and return its mark to change value further.
  // Remember, all VCD variables must be registered prior to any value changes.
  // Note, *size* may be `0`, some types ("int", "real", "event") have a default size
  VarPtr register_var(const std::string &scope,                                 // Variable belongs within the hierarchical scope
                      const std::string &name,                                  // Human-readable variable idetifier
                      VariableType       type                  = var_def_type,  // Verilog data type of variable
                      unsigned           size                  = 0,             // Size of variable, in bits
                      const VarValue &   init                  = {VCDValues::ZERO},  // Initial value (optional)
                      bool               duplicate_names_check = true);                            // speed-up (optimisation)

  // Register a VCD variable and return its mark to change value further.
  // Remember, all VCD variables must be registered prior to any value changes.
  // Note, *size* may be `0`, some types ("int", "real", "event") have a default size
  VarPtr register_passed_var(const std::string &scope,  // Variable belongs within the hierarchical scope
                             const std::string &name,   // Human-readable variable idetifier
                             VariableType       type                  = var_def_type,       // Verilog data type of variable
                             unsigned           size                  = 0,                  // Size of variable, in bits
                             const VarValue &   init                  = {VCDValues::ZERO},  // Initial value (optional)
                             bool               duplicate_names_check = true);                            // speed-up (optimisation)

  // Change variable's value in VCD stream.
  // Call this method, for all variables changed on this *timestamp*.
  // It is okay to call it multiple times with the same *timestamp*,
  // but never call with a past *timestamp*
  // Return:  *true* if new_value is dumped into VCD file,
  //         *false* if new_value is not changed from priveios *timestamp* for a given var
  bool change(VarPtr _var, const VarValue &_value) { return change(_var, _value, false); }

  bool change(const std::string &scope, const std::string &name, const VarValue &value);

  // Suspend dumping to VCD file
  void dump_off(TimeStamp current) {
    if (dumping && !registering && vars_prevs.size())
      dump_off_int(current);
    dumping = false;
  }
  // Resume dumping to VCD file
  void dump_on(TimeStamp current) {
    if (!dumping && !registering && vars_prevs.size())
      fprintf(ofile, "#%d", current);
    dump_values("$dumpon");
    dumping = true;
  }

  // Flush any buffered VCD data to output file.
  // If the VCD header has not already been written, calling `flush()` will force
  // the header to be written thus disallowing any further variable registrations.
  void flush(const TimeStamp *current = NULL) {
    if (closed)
      throw VCDPhaseException{"Cannot flush() after close()"};
    if (registering)
      finalize_registration();
    if (current != NULL && *current > timestamp)
      fprintf(ofile, "#%d", *current);
    fflush(ofile);
  }
  // Close VCD writer. Any buffered VCD data is flushed to the output file.
  // After `close()`, NO variable registration or value changes will be accepted.
  // Note, the output file-stream will be closed in destructor of `VCDWriter`
  void close(const TimeStamp *final = NULL) {
    if (closed)
      return;
    flush(final);
    closed = true;
  }

  //! VCD viewer applications may display different scope types differently
  void set_scope_type(std::string &scope, ScopeType);

  void set_scope_default_type(ScopeType new_type) { scope_def_type = new_type; }

  void set_scope_sep(const std::string &_scope_sep) {
    if (scope_sep.size() == 0 || scope_sep == _scope_sep)
      return;
    scope_sep = _scope_sep;
  }
  //! get VCD Variable (if it is registered var() != NULL)
  VarPtr var(const std::string &scope, const std::string &name) const;

  static const VariableType var_def_type = VariableType::wire;

protected:
  bool change(VarPtr, const VarValue &, bool);
  void dump_off_int(TimeStamp timestamp);
  //        void _dump_values();
  void dump_values(const std::string &keyword);
  void scope_declaration(const std::string &scope, size_t sub_beg, size_t sub_end = std::string::npos);
  //! Dump VCD header into file
  void write_header();
  //! Turn to dumping phase, no more variables regestration allowed
  void finalize_registration();
};

// -----------------------------
using WriterPtr = std::shared_ptr<VCDWriter>;
// -----------------------------
VCDWriter *initialize_vcd_writer();
void       advance_to_posedge();
void       advance_to_negedge();
void       advance_to_comb();

}  // namespace vcd
