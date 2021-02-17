// This file is distributed under the LICENSE.vcd-writer License. See LICENSE for details.
#include "vcd_writer.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "vcd_utils.hpp"

namespace vcd {

// -----------------------------

// -----------------------------
struct VCDHeader {
  static const unsigned    kws = 4;
  static const std::string kwnames[kws];

  TimeScale     timescale_quan;
  TimeScaleUnit timescale_unit;
  std::string   kw_values[kws];

  VCDHeader()                  = delete;
  VCDHeader(VCDHeader &&)      = default;
  VCDHeader(const VCDHeader &) = delete;
  VCDHeader(TimeScale _timescale_quan, TimeScaleUnit _timescale_unit, const std::string &date, const std::string &comment,
            const std::string &version)
      : timescale_quan{_timescale_quan}, timescale_unit{_timescale_unit}, kw_values{timescale(), date, comment, version} {}

  std::string timescale() const {
    const std::string TIMESCALE_UNITS[] = {"s", "ms", "us", "ns", "ps", "fs"};
    return std::to_string(int(timescale_quan)) + " " + TIMESCALE_UNITS[int(timescale_unit)];
  }
};

// -----------------------------
HeadPtr makeVCDHeader(TimeScale timescale_quan, TimeScaleUnit timescale_unit, const std::string &date, const std::string &comment,
                      const std::string &version) {
  return HeadPtr{new VCDHeader(timescale_quan, timescale_unit, date, comment, version)};
}

// -----------------------------
const std::string VCDHeader::kwnames[VCDHeader::kws] = {"$timescale", "$date", "$comment", "$version"};

// -----------------------------
void VCDHeaderDeleter::operator()(VCDHeader *p) { delete p; }

// -----------------------------
struct VCDScope {
  std::string       name;
  ScopeType         type;
  std::list<VarPtr> vars;

  VCDScope(const std::string &_name, ScopeType _type) : name(_name), type(_type) {}
};

// -----------------------------
bool ScopePtrHash::operator()(const ScopePtr &l, const ScopePtr &r) const { return (l->name < r->name); }

// -----------------------------
// VCD variable details needed to call :meth:`VCDWriter.change()`.
class VCDVariable {
  VCDVariable()                    = delete;
  VCDVariable(VCDVariable &&)      = default;
  VCDVariable(const VCDVariable &) = delete;

protected:
  VCDVariable(const std::string &name, VariableType type, unsigned size, ScopePtr scope, unsigned next_var_id);

  std::string  name;   // human-readable name
  VariableType type;   // VCD variable type, one of `VariableTypes`
  unsigned     size;   // size of variable, in bits
  ScopePtr     scope;  // pointer to scope string
  std::string  ident;  // internal ID used in VCD output stream

  //! string representation of variable types
  static const std::string var_types[];

public:
  //! string representation of variable declartion in VCD
  std::string declartion() const;
  //! string representation of value change record in VCD

  friend class VCDWriter;  // A friend class can access private and protected members of other class in which it is declared as
                           // friend.
  friend struct VarPtrHash;
  friend struct VarPtrEqual;
};

// -----------------------------
const std::string VCDVariable::var_types[] = {"wire",   "reg",   "string",  "parameter", "integer", "real",   "realtime",
                                              "time",   "event", "supply0", "supply1",   "tri",     "triand", "trior",
                                              "trireg", "tri0",  "tri1",    "wand",      "wor"};

// -----------------------------
size_t VarPtrHash::operator()(const VarPtr &p) const {
  std::hash<std::string> h;
  return (h(p->name) ^ (h(p->scope->name) << 1));
}
bool VarPtrEqual::operator()(const VarPtr &a, const VarPtr &b) const {
  return (a->name == b->name) && (a->scope->name == b->scope->name);
}

// -----------------------------
// One-bit VCD scalar is a 4-state variable and thus may have one of
// `VCDValues`. An empty *value* is the same as `VCDValues::UNDEF`
struct VCDScalarVariable : public VCDVariable {
  VCDScalarVariable(const std::string &_name, VariableType _type, unsigned _size, ScopePtr _scope, unsigned next_var_id)
      : VCDVariable(_name, _type, _size, _scope, next_var_id) {}
};

// -----------------------------
// String variable as known by GTKWave. Any `string` (character-chain)
// can be displayed as a change.This type is only supported by GTKWave.
struct VCDStringVariable : public VCDVariable {
  VCDStringVariable(const std::string &_name, VariableType _type, unsigned _size, ScopePtr _scope, unsigned next_var_id)
      : VCDVariable(_name, _type, _size, _scope, next_var_id) {}
};

// -----------------------------
// Real (IEEE-754 double-precision floating point) variable. Values must
// be numeric and can't be `VCDValues::UNDEF` or `VCDValues::HIGHV` states
struct VCDRealVariable : public VCDVariable {
  VCDRealVariable(const std::string &_name, VariableType _type, unsigned _size, ScopePtr _scope, unsigned next_var_id)
      : VCDVariable(_name, _type, _size, _scope, next_var_id) {}
};

// -----------------------------
// Bit vector variable type for the various non-scalar and non-real
// variable types, including integer, register, wire, etc.
struct VCDVectorVariable : public VCDVariable {
  VCDVectorVariable(const std::string &_name, VariableType _type, unsigned _size, ScopePtr _scope, unsigned next_var_id)
      : VCDVariable(_name, _type, _size, _scope, next_var_id) {}
};

// -----------------------------
struct VarSearch {
  VCDScope          vcdscope = {"", ScopeType::module};
  ScopePtr          ptrscope = {&vcdscope, [](VCDScope *) {}};
  VCDScalarVariable vcd_var   = {"", VCDWriter::var_def_type, 0, ptrscope, 0};
  VarPtr            ptr_var   = {&vcd_var, [](VCDScalarVariable *) {}};

  VarSearch() = delete;
  VarSearch(ScopeType _scope_def_type) : vcdscope("", _scope_def_type) {}
};

// -----------------------------
VCDWriter::VCDWriter(const std::string &_filename, HeadPtr &&_header, unsigned init_timestamp)
    : timestamp(init_timestamp)
    , header((_header) ? std::move(_header) : makeVCDHeader())
    , filename(_filename)
    , scope_sep(".")
    , scope_def_type(ScopeType::module)
    , closed(false)
    , dumping(true)
    , registering(true)
    , next_var_id(0)
    , search(std::make_shared<VarSearch>(ScopeType::module)) {

  if (!header) throw VCDTypeException{"Invalid pointer to header"};

  ofile = fopen(filename.c_str(), "w");
  if (!ofile) throw VCDTypeException{utils::format("Can't open file '%s' for writing", filename.c_str())};
}

// -----------------------------
VarPtr VCDWriter::register_var(const std::string &scope, const std::string &name, VariableType type, unsigned size,
                               const VarValue &init, bool duplicatenames_check) {
  VarPtr pvar;
  if (closed) throw VCDPhaseException{"Cannot register after close()"};
  if (!registering) throw VCDPhaseException{utils::format("Cannot register new var '%s', registering finished", name.c_str())};

  if (scope.size() == 0 || name.size() == 0)
    throw VCDTypeException{utils::format("Empty scope '%s' or name '%s'", scope.c_str(), name.c_str())};

  search->vcdscope.name = scope;  //"a" goes to vcdscope.name
  auto curscope          = scopes.find(search->ptrscope);
  if (curscope == scopes.end()) {
    auto res = scopes.insert(std::make_shared<VCDScope>(scope, scope_def_type));
    if (!res.second) throw VCDPhaseException{utils::format("Cannot insert scope '%s'", scope.c_str())};
    curscope = res.first;  // first is the shared ptr of type VCD scope and second is a bool
  }

  auto sz = [&size](unsigned def) { return (size ? size : def); };

  VarValue init_value(init);
  switch (type) {
    case VariableType::integer:
    case VariableType::realtime:
      if (sz(64) == 1)
        pvar = VarPtr(new VCDScalarVariable(name, type, 1, *curscope, next_var_id));
      else
        pvar = VarPtr(new VCDVectorVariable(name, type, sz(64), *curscope, next_var_id));
      if (init_value.size() == 1 && init_value[0] == VCDValues::ZERO && size > init_value.size())
        init_value = "b" + std::string(size, VCDValues::ZERO);
      break;

    case VariableType::real:
      pvar = VarPtr(new VCDRealVariable(name, type, sz(64), *curscope, next_var_id));
      if (init_value.size() == 1 && init_value[0] == VCDValues::ZERO) init_value = "0.0";
      break;

    case VariableType::string: pvar = VarPtr(new VCDStringVariable(name, type, sz(1), *curscope, next_var_id)); break;
    case VariableType::event: pvar = VarPtr(new VCDScalarVariable(name, type, 1, *curscope, next_var_id)); break;

    case VariableType::wire:
      if (!size)
        throw VCDTypeException{
            utils::format("Must supply size for type '%s' of var '%s'", VCDVariable::var_types[(int)type].c_str(), name.c_str())};
      if (sz(64) == 1)
        pvar = VarPtr(new VCDScalarVariable(name, type, 1, *curscope, next_var_id));
      else
        pvar = VarPtr(new VCDVectorVariable(name, type, size, *curscope, next_var_id));
      if (init_value.size() == 1 && init_value[0] == VCDValues::ZERO && size > init_value.size())
        init_value = "b" + std::string(size, VCDValues::ZERO);
      break;
    default:
      if (!size)
        throw VCDTypeException{
            utils::format("Must supply size for type '%s' of var '%s'", VCDVariable::var_types[(int)type].c_str(), name.c_str())};
      pvar = VarPtr(new VCDVectorVariable(name, type, size, *curscope, next_var_id));
      if (init_value.size() == 1 && (init_value[0] == VCDValues::UNDEF || init_value[0] == VCDValues::ZERO) &&
          size > init_value.size())
        init_value = "b" + std::string(size, VCDValues::ZERO);
      break;
  }
  if (type != VariableType::event) change(pvar, init_value, true);

  if (duplicatenames_check && vars.find(pvar) != vars.end())
    throw VCDTypeException{utils::format("Duplicate var '%s' in scope '%s'", name.c_str(), scope.c_str())};

  vars.insert(pvar);
  (**curscope).vars.push_back(pvar);
  // Only alter state after change_func() succeeds
  next_var_id++;
  return pvar;
}


// -----------------------------
VarPtr VCDWriter::register_passed_var(const std::string &scope, const std::string &name, VariableType type, unsigned size,
                               const VarValue &init, bool duplicatenames_check) {

  const std::string parentname = scope.substr(0,scope.find_last_of(scope_sep));
  /*for unique ident::
   *if (scope has scope_sep)
   * then {extract parent name}
   */
  unsigned int unique_var_id = 0;
  VarPtr pvar;
  if (closed) throw VCDPhaseException{"Cannot register after close()"};
  if (!registering) throw VCDPhaseException{utils::format("Cannot register new var '%s', registering finished", name.c_str())};

  if (scope.size() == 0 || name.size() == 0)
    throw VCDTypeException{utils::format("Empty scope '%s' or name '%s'", scope.c_str(), name.c_str())};

  search->vcdscope.name = scope;  //"a" goes to vcdscope.name
  auto curscope          = scopes.find(search->ptrscope);
  if (curscope == scopes.end()) {
    auto res = scopes.insert(std::make_shared<VCDScope>(scope, scope_def_type));
    if (!res.second) throw VCDPhaseException{utils::format("Cannot insert scope '%s'", scope.c_str())};
    curscope = res.first;  // first is the shared ptr of type VCD scope and second is a bool
  }

  auto sz = [&size](unsigned def) { return (size ? size : def); };
  auto parentscope = scopes.begin();
   for(auto e =scopes.begin(); e!=scopes.end(); e++) {

       if ((*e)->name == parentname) {
           parentscope=e;
           break;
       }
   }

   for (auto var_iter = (**parentscope).vars.begin(); var_iter!=(**parentscope).vars.end(); var_iter++) {
       auto randm = *var_iter;
       if(name== (*var_iter)->name){
           unique_var_id =stoul((*var_iter)->ident, 0, 16);
       }
   }
  VarValue init_value(init);
  switch (type) {
    case VariableType::integer:
    case VariableType::realtime:
      if (sz(64) == 1)
        pvar = VarPtr(new VCDScalarVariable(name, type, 1, *curscope, unique_var_id));
      else
        pvar = VarPtr(new VCDVectorVariable(name, type, sz(64), *curscope, unique_var_id));
      if (init_value.size() == 1 && init_value[0] == VCDValues::ZERO && size > init_value.size())
        init_value = "b" + std::string(size, VCDValues::ZERO);
      break;

    case VariableType::real:
      pvar = VarPtr(new VCDRealVariable(name, type, sz(64), *curscope, unique_var_id));
      if (init_value.size() == 1 && init_value[0] == VCDValues::ZERO) init_value = "0.0";
      break;

    case VariableType::string: pvar = VarPtr(new VCDStringVariable(name, type, sz(1), *curscope, unique_var_id)); break;
    case VariableType::event: pvar = VarPtr(new VCDScalarVariable(name, type, 1, *curscope, unique_var_id)); break;

    case VariableType::wire:
      if (!size)
        throw VCDTypeException{
            utils::format("Must supply size for type '%s' of var '%s'", VCDVariable::var_types[(int)type].c_str(), name.c_str())};
      if (sz(64) == 1)
        pvar = VarPtr(new VCDScalarVariable(name, type, 1, *curscope, unique_var_id));
      else
        pvar = VarPtr(new VCDVectorVariable(name, type, size, *curscope, unique_var_id));
      if (init_value.size() == 1 && init_value[0] == VCDValues::ZERO && size > init_value.size())
        init_value = "b" + std::string(size, VCDValues::ZERO);
      break;
    default:
      if (!size)
        throw VCDTypeException{
            utils::format("Must supply size for type '%s' of var '%s'", VCDVariable::var_types[(int)type].c_str(), name.c_str())};
      pvar = VarPtr(new VCDVectorVariable(name, type, size, *curscope, unique_var_id));
      if (init_value.size() == 1 && (init_value[0] == VCDValues::UNDEF || init_value[0] == VCDValues::ZERO) &&
          size > init_value.size())
        init_value = "b" + std::string(size, VCDValues::ZERO);
      break;
  }
  if (type != VariableType::event) change(pvar, init_value, true);

  if (duplicatenames_check && vars.find(pvar) != vars.end())
    throw VCDTypeException{utils::format("Duplicate var '%s' in scope '%s'", name.c_str(), scope.c_str())};

  vars.insert(pvar);
  (**curscope).vars.push_back(pvar);

  return pvar;
}
// -----------------------------
bool VCDWriter::change(VarPtr var, const VarValue &value, bool reg) {
  if (global_timestamp < timestamp)
    throw VCDPhaseException{utils::format("Out of order value change var '%s'", var->name.c_str())};
  else if (closed)
    throw VCDPhaseException{"Cannot change value after close()"};

  if (!var) throw VCDTypeException{"Invalid VCDVariable"};

  std::string change_value = (value.size() ? value : ('b' + std::string(var->size, VCDValues::ZERO) + ' '));
  //    std::string change_value = var->change_record(value);

  if (global_timestamp > timestamp) {
    if (registering) finalize_registration();
    if (dumping) fprintf(ofile, "#%d\n", global_timestamp);
    timestamp = global_timestamp;
  }

  // if value changed
  if (vars_prevs.find(var) != vars_prevs.end())  // not entered in 1st entry @ main:544 //vars_prev is unordered_map of (varptr, str) pairs
  {
    if (vars_prevs[var] == change_value) return false;
    vars_prevs[var] = change_value;  // entered for #0

  } else if (!reg)
    throw VCDTypeException{utils::format("VCDVariable '%s' do not registered", var->name.c_str())};
  else

    vars_prevs.insert(std::make_pair(var, change_value));  // registering: inserted the pair for the first time

  // dump it into file
  if (dumping && !registering) {  // not entered in 1st entry

    if (change_value.size() != 1) {
      fprintf(ofile, "%s %s\n", change_value.c_str(), var->ident.c_str());
    } else {
      fprintf(ofile, "%s%s\n", change_value.c_str(), var->ident.c_str());
    }
    //            fprintf(ofile, "%s%s\n", change_value.c_str(), var->ident.c_str());
  }
  return true;
}

// -----------------------------
bool VCDWriter::change(const std::string &scope, const std::string &name, const VarValue &value) {
  return change(var(scope, name), value, false);
}

// -----------------------------
VarPtr VCDWriter::var(const std::string &scope, const std::string &name) const {
  //!!! speed optimisation !!!
  // ScopePtr pscope = std::make_shared<VCDScope>(scope, scope_def_type);
  // auto itscope = scopes.find(pscope);
  // if (itscope == scopes.end())
  //    throw VCDPhaseException{ format("Such scope '%s' does not exist", scope.c_str()) };
  // VarPtr pvar = std::make_shared<VCDScalarVariable>(name, VCDWriter::var_deftype, 0, *itscope, 0);
  // auto it_var = vars.find(pvar);
  search->vcdscope.name = scope;
  search->vcd_var.name  = name;
  auto it_var             = vars.find(search->ptr_var);
  if (it_var == vars.end())
    throw VCDPhaseException{utils::format("The var '%s' in scope '%s' does not exist", name.c_str(), scope.c_str())};
  return *it_var;
}

void VCDWriter::set_scope_type(std::string &scope, ScopeType scopetype) {
  ScopePtr pscope = std::make_shared<VCDScope>(scope, scope_def_type);
  auto     it     = scopes.find(pscope);
  if (it == scopes.end()) throw VCDPhaseException{utils::format("Such scope '%s' does not exist", scope.c_str())};
  (**it).type = scopetype;
}

// -----------------------------
void VCDWriter::dump_off_int(TimeStamp _timestamp) {
  fprintf(ofile, "#%d\n", _timestamp);
  fprintf(ofile, "$dumpoff\n");
  for (const auto &p : vars_prevs) {
    const char *ident = p.first->ident.c_str();
    const char *value = p.second.c_str();

    if (value[0] == 'r') {
    }  // real variables cannot have "z" or "x" state
    else if (value[0] == 'b') {
      fprintf(ofile, "bx %s\n", ident);
    }
    // else if (value[0] == 's')
    //{ fprintf(ofile, "sx %s\n", ident); }
    else {
      fprintf(ofile, "x%s\n", ident);
    }
  }
  fprintf(ofile, "$end\n");
}

void VCDWriter::dump_values(const std::string &keyword) {
  fprintf(ofile, "%s\n", keyword.c_str());
  // TODO : events should be excluded
  for (const auto &p : vars_prevs) {
    const char *ident = p.first->ident.c_str();
    const char *value = p.second.c_str();
    if (p.second.size() != 1) {
      fprintf(ofile, "%s %s\n", value, ident);
    } else {
      fprintf(ofile, "%s%s\n", value,
              ident);  // the space gives the spacing between value and identifier in the dumped file
    }
  }
  fprintf(ofile, "$end\n");
}

void VCDWriter::scope_declaration(const std::string &scope, size_t sub_beg, size_t sub_end) {
  const std::string SCOPEtypeS[] = {"begin", "fork", "function", "module", "task"};
  auto              scopename    = scope.substr(sub_beg, sub_end - sub_beg);
  auto              scopetype    = SCOPEtypeS[int(scope_def_type)].c_str();
  fprintf(ofile, "$scope %s %s $end\n", scopetype, scopename.c_str());
}

void VCDWriter::write_header() {
  for (auto i = 0u; i < VCDHeader::kws; ++i) {
    auto kwname  = VCDHeader::kwnames[i];
    auto kwvalue = header->kw_values[i];
    if (!kwvalue.size()) continue;
    vcd::utils::replace_new_lines(kwvalue, "\n\t");
    fprintf(ofile, "%s %s $end\n", kwname.c_str(), kwvalue.c_str());
  }

  // nested scope
  size_t      n = 0, n_prev = 0;
  std::string scope_prev = "";
  for (auto &s : scopes)  // sorted
  {
    const std::string &scope = s->name;
    // scope print close
    if (scope_prev.size()) {
      n_prev = 0;
      n      = scope_prev.find(scope_sep);
      n      = (n == std::string::npos) ? scope_prev.size() : n;
      // equal prefix
      while (std::strncmp(scope.c_str(), scope_prev.c_str(), n) == 0) {
        n_prev = n + scope_sep.size();
        n      = scope_prev.find(scope_sep, n_prev);
        if (n == std::string::npos) break;
      }
      // last
      if (n_prev != (scope_prev.size() + scope_sep.size())) fprintf(ofile, "$upscope $end\n");
      // close
      n = scope_prev.find(scope_sep, n_prev);
      while (n != std::string::npos) {
        fprintf(ofile, "$upscope $end\n");
        n = scope_prev.find(scope_sep, n + scope_sep.size());
      }
    }

    // scope print open
    n = scope.find(scope_sep, n_prev);
    while (n != std::string::npos) {
      scope_declaration(scope, n_prev, n);
      n_prev = n + scope_sep.size();
      n      = scope.find(scope_sep, n_prev);
    }
    // last
    scope_declaration(scope, n_prev);

    // dump variable declartion
    for (const auto &var : s->vars) fprintf(ofile, "%s\n", var->declartion().c_str());

    scope_prev = scope;
  }

  // scope print close (rest)
  if (scope_prev.size()) {
    // last
    fprintf(ofile, "$upscope $end\n");
    n = scope_prev.find(scope_sep);
    while (n != std::string::npos) {
      fprintf(ofile, "$upscope $end\n");
      n = scope_prev.find(scope_sep, n + scope_sep.size());
    }
  }

  fprintf(ofile, "$enddefinitions $end\n");
  // do not need anymore
  header.reset(nullptr);
}

void VCDWriter::finalize_registration() {
  assert(registering);
  write_header();
  if (vars_prevs.size()) {
    fprintf(ofile, "#%d\n", timestamp);
    dump_values("$dumpvars");
    if (!dumping) dump_off_int(timestamp);
  }
  registering = false;
}

// -----------------------------
VCDVariable::VCDVariable(const std::string &_name, VariableType _type, unsigned _size, ScopePtr _scope, unsigned next_var_id)
    : name(_name), type(_type), size(_size), scope(_scope) {
  std::stringstream ss;
  ss << std::hex << next_var_id;
  ident = ss.str();
}

std::string VCDVariable::declartion() const {
  return utils::format("$var %s %d %s %s $end", var_types[int(type)].c_str(), size, ident.c_str(), name.c_str());
}

// -----------------------------
//  :Warning: *value* is string where all characters must be one of `VCDValues`.
//  An empty  *value* is the same as `VCDValues::UNDEF`
//------------------------------
// class initializer {
// public:
VCDWriter *initialize_vcd_writer() {
  // = clock();
  const char *var = getenv("SIMLIB_DUMPDIR");
  std::string dump_file{"SIMLIB_VCD.vcd"};
  if (var) {
    std::string dir{var};
    dump_file = dir + "/" + "SIMLIB_VCD.vcd";
  }
  static VCDWriter writer{
      dump_file, makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now(), "This is the VCD file", "generated by SimLibVCD")};
  return &writer;
}
//};
// static VCDWriter& vcd_writer_m = initialize_vcd_writer();
void advance_to_posedge() {
  global_timestamp += 5;
}
void advance_to_negedge() {
  global_timestamp += 3;
}
void advance_to_comb() {
  global_timestamp += 2;
}

}  // namespace vcd


