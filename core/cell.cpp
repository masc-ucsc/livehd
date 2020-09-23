//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cell.hpp"

Cell::_init Cell::_static_initializer;

Cell::_init::_init() {
  for (uint8_t op = 1; op < static_cast<uint8_t>(Cell_op::Last_invalid); ++op) {

    for(auto i=0;i<256;++i) {
      sink_name2pid[i][op]=-1;
    }
    for(auto i=0u;i<sink_pid2name.size();++i) {
      sink_pid2name[i][op]="invalid";
    }

    int pid=0;
    while(true) {
      assert(pid<12); // 11 max for Memory
      auto pin_name = Cell::get_sink_name_slow(static_cast<Cell_op>(op), pid);
      if (pin_name.empty() || pin_name == "invalid")
        break;

      assert(sink_name2pid[pin_name[0]][op] == -1); // No double assign

      sink_pid2name[pid][op] = pin_name;
      sink_name2pid[pin_name[0]][op] = pid;

      assert(pid == Cell::get_sink_pid(static_cast<Cell_op>(op), pin_name));
      assert(pin_name == Cell::get_sink_name(static_cast<Cell_op>(op), pid));

      ++pid;
    }

    // Check that common case is fine

    pid = sink_name2pid['a'][op];
    assert(pid==-1 || pid == 0);

    pid = sink_name2pid['b'][op];
    assert(pid==-1 || pid == 1);

    pid = sink_name2pid['c'][op];
    assert(pid==-1 || pid == 2);

    pid = sink_name2pid['d'][op];
    assert(pid==-1 || pid == 3);

    pid = sink_name2pid['e'][op];
    assert(pid==-1 || pid == 4);

    pid = sink_name2pid['A'][op];
    assert(pid==-1 || pid == 0);

    pid = sink_name2pid['B'][op];
    assert(pid==-1 || pid == 1);
  }
}

constexpr std::string_view Cell::get_sink_name_slow(Cell_op op, int pid) {
  switch(op) {
    case Cell_op::Invalid:
      return "invalid";
      break;
    case Cell_op::Sum:
    case Cell_op::LT:
    case Cell_op::GT:
    case Cell_op::EQ:
      if (pid==0)
        return "A";
      else if (pid==1)
        return "B";
      return "invalid";
      break;
    case Cell_op::Mult:
    case Cell_op::And:
    case Cell_op::Or:
    case Cell_op::Xor:
    case Cell_op::Rand:
    case Cell_op::Ror:
      if (pid==0)
        return "A";
      return "invalid";
      break;
    case Cell_op::Not:
    case Cell_op::Tposs:
      if (pid==0)
        return "a";
      return "invalid";
      break;
    case Cell_op::Div:
    case Cell_op::SHL:
    case Cell_op::SRA:
      if (pid==0)
        return "a";
      else if (pid==1)
        return "b";
      return "invalid";
      break;
    case Cell_op::Const: // No drivers to Constants
      return "invalid";
      break;
    case Cell_op::Mux:   // unlimited case: 1,2,3,4,5....
    case Cell_op::LUT:   // unlimited case: 1,2,3,4,5....
    case Cell_op::Sub:   // unlimited case: 1,2,3,4,5....
      assert(is_unlimited_sink(op));
      switch(pid) {
        case 0: return "0";
        case 1: return "1";
        case 2: return "2";
        case 3: return "3";
        case 4: return "4";
        case 5: return "5";
        case 6: return "6";
        case 7: return "7";
        case 8: return "8";
        case 9: return "9"; // >9 handled with loop at get_sink_pid
        default: return "invalid";
      }
      return "invalid";
      break;
    case Cell_op::Memory:
      switch(pid) {
        case 0: return "addr";
        case 1: return "bits";
        case 2: return "clock";
        case 3: return "data_in";
        case 4: return "enable";
        case 5: return "fwd";
        case 6: return "posclk";
        case 7: return "latency";
        case 8: return "wmask";
        case 9: return "size";
        case 10: return "mode";
        default: return "invalid";
      }
      break;
    case Cell_op::Sflop:
    case Cell_op::Aflop:
      switch(pid) {
        case 0: return "reset";
        case 1: return "initial";  // reset value
        case 2: return "clock";
        case 3: return "din";
        case 4: return "enable";
        case 5: return "posclk";
        case 6: return "negreset";
        default: return "invalid";
      }
      break;
    case Cell_op::Latch:
      switch(pid) {
        case 0: return "posclk";
                // No 1 to keep din at pos 3 (a,b,c)
        case 3: return "din";
        case 4: return "enable";
        default: return "invalid";
      }
      break;
    case Cell_op::Fflop:
      switch(pid) {
        case 0: return "reset";
        case 1: return "initial";  // reset value
        case 2: return "clock";
        case 3: return "din";
        case 4: return "valid";
        case 5: return "stop";     // stop from next cycle
        default: return "invalid";
      }
      break;
    case Cell_op::TupAdd:
      switch(pid) {
        case 0: return "tuple_name";      // tuple name
        case 2: return "position";
        case 3: return "value";     // stop from next cycle
                // no 4 to keep f 5
        case 5: return "field";     // tuple field
        default: return "invalid";
      }
      break;
    case Cell_op::TupGet:
      switch(pid) {
        case 0: return "tuple_name";
        case 2: return "position";
                // no 3,4 to keep f 5
        case 5: return "field";
        default: return "invalid";
      }
      break;
    case Cell_op::AttrSet:
      switch(pid) {
        case 0: return "name";  // variable name
        case 2: return "chain";
        case 3: return "value";
        case 5: return "field";
        default: return "invalid";
      }
      break;
    case Cell_op::AttrGet:
      switch(pid) {
        case 0: return "var_name";
        case 5: return "field";
        default: return "invalid";
      }
      break;
    case Cell_op::IO:
    case Cell_op::CompileErr:
      switch(pid) {
        case 0: return "A";
        case 1: return "B";
        case 2: return "C";
        case 3: return "D";
        case 4: return "E";
        case 5: return "F";
        case 6: return "G";
        case 7: return "H";
        case 8: return "I";
        case 9: return "J";
        case 10: return "K";
        default: return "invalid";
      }
      return "invalid";
      break;
    default:
      assert(false);
      return "invalid";
  }

  assert(false);
  return "invalid";
}

#ifdef NDEBUG
// asserts break the constexpr check
//
//This should be compile time constants (check assembly)
int cell_code_check_code1() {
  constexpr auto pid = Cell::get_sink_pid(Cell_op::Sum,"B");
  static_assert(pid==1);
  return pid;
}

int cell_code_check_code2() {
  constexpr auto pid = Cell::get_sink_pid(Cell_op::Mux,"321");
  static_assert(pid==321);
  return pid;
}
#endif

