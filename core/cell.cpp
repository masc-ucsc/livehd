//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cell.hpp"

#include "iassert.hpp"
#include "lgraph_base_core.hpp"

Ntype::_init Ntype::_static_initializer;

Ntype::_init::_init() {

  mmap_lib::str::setup();

  for (uint8_t op = 1; op < static_cast<uint8_t>(Ntype_op::Last_invalid); ++op) {
    for (auto& e : sink_name2pid) {
      e[op] = Port_invalid;
    }

    for (auto& e : sink_pid2name) {
      e[op] = "invalid";
    }

    int n_sinks = 0;
    for (int pid = 0; pid < 12; ++pid) {
      auto pin_name = Ntype::get_sink_name_slow(static_cast<Ntype_op>(op), pid);
      if (pin_name.empty() || pin_name == "invalid")
        continue;

      ++n_sinks;

      assert(is_unlimited_sink(static_cast<Ntype_op>(op)) || pid > 10 || sink_name2pid[pin_name[0]][op] == Port_invalid
             || sink_name2pid[pin_name[0]][op] == pid);  // No double assign

      sink_pid2name[pid][op] = pin_name;

      auto [it, inserted] = name2pid.emplace(pin_name, pid);
      if (!inserted) {
        I(it->second == pid);  // same name should always have same PID
      }

      if (static_cast<Ntype_op>(op) != Ntype_op::Memory && is_unlimited_sink(static_cast<Ntype_op>(op)) && pid >= 10)
        continue;

      sink_name2pid[pin_name[0]][op] = pid;
      assert(pid == Ntype::get_sink_pid(static_cast<Ntype_op>(op), pin_name));
      assert(pin_name == Ntype::get_sink_name(static_cast<Ntype_op>(op), pid));
    }

    if (n_sinks == 1) {
      ntype2single_input[op] = true;
      I(sink_pid2name[0][op] != "invalid");
    } else {
      ntype2single_input[op] = false;
    }

    // special sink case
    sink_name2pid['$'][static_cast<int>(Ntype_op::Sub)] = 0;

    int pid;
    (void)pid;
    // Check that common case is fine

    pid = sink_name2pid['a'][op];
    assert(pid == Port_invalid || pid == 0);

    pid = sink_name2pid['b'][op];
    assert(pid == Port_invalid || pid == 1);

    pid = sink_name2pid['c'][op];
    assert(pid == Port_invalid || pid == 2);

    pid = sink_name2pid['d'][op];
    assert(pid == Port_invalid || pid == 3);

    pid = sink_name2pid['e'][op];
    assert(pid == Port_invalid || pid == 4);

    pid = sink_name2pid['f'][op];
    assert(pid == Port_invalid || pid == 5);

    pid = sink_name2pid['A'][op];
    assert(pid == Port_invalid || pid == 0);

    pid = sink_name2pid['B'][op];
    assert(pid == Port_invalid || pid == 1);
  }

  int pos = 0;
  for (auto e : cell_name_sv) {
    auto e_str           = mmap_lib::str(e);
    cell_name[pos]       = e_str;
    cell_name_map[e_str] = static_cast<Ntype_op>(pos);
    ++pos;
  }
}

constexpr mmap_lib::str Ntype::get_sink_name_slow(Ntype_op op, int pid) {
  switch (op) {
    case Ntype_op::Invalid: return "invalid"_str; break;
    case Ntype_op::Sum:
    case Ntype_op::LT:
    case Ntype_op::GT:
      if (pid == 0)
        return "A"_str;
      else if (pid == 1)
        return "B"_str;
      return "invalid"_str;
      break;
    case Ntype_op::Mult:
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Ror:
    case Ntype_op::EQ:
      if (pid == 0)
        return "A"_str;
      return "invalid"_str;
      break;
    case Ntype_op::Not:
      if (pid == 0)
        return "a"_str;
      return "invalid"_str;
      break;
    case Ntype_op::Sext:
    case Ntype_op::Div:
    case Ntype_op::SRA:
      if (pid == 0)
        return "a"_str;
      else if (pid == 1)
        return "b"_str;
      return "invalid"_str;
      break;
    case Ntype_op::SHL:  // n<<(a,b) == (n<<a)|(n<<b) : useful for onehot encoding for lgtuple inputs
      if (pid == 0)
        return "a"_str;
      else if (pid == 1)
        return "B"_str;
      return "invalid"_str;
      break;
    case Ntype_op::Const:  // No drivers to Constants
      return "invalid"_str;
      break;
    case Ntype_op::IO:
    case Ntype_op::Mux:  // unlimited case: 1,2,3,4,5.... // Y = (pid0 == true) ? pid2 : pid1
    case Ntype_op::LUT:  // unlimited case: 1,2,3,4,5....
    case Ntype_op::Sub:  // unlimited case: 1,2,3,4,5....
    case Ntype_op::CompileErr:
      assert(is_unlimited_sink(op));
      switch (pid) {
        case 0: return "0"_str;
        case 1: return "1"_str;
        case 2: return "2"_str;
        case 3: return "3"_str;
        case 4: return "4"_str;
        case 5: return "5"_str;
        case 6: return "6"_str;
        case 7: return "7"_str;
        case 8: return "8"_str;
        case 9: return "9"_str;
        case 10: return "10"_str;  // >10 handled with loop at get_sink_pid
        default: return "invalid"_str;
      }
      return "invalid"_str;
      break;
    case Ntype_op::Memory:
      switch (pid) {
        case 0: return "addr"_str;     // runtime  x n_ports
        case 1: return "bits"_str;     // comptime x 1
        case 2: return "clock"_str;    // runtime  x 1 or n_ports
        case 3: return "din"_str;      // runtime  x n_ports
        case 4: return "enable"_str;   // runtime  x n_ports
        case 5: return "fwd"_str;      // comptime x 1
        case 6: return "posclk"_str;   // comptime x 1
        case 7: return "type"_str;     // comptime x 1 (0:async, 1:sync: 2:array)
        case 8: return "wensize"_str;  // comptime x 1  -- number of Write Enable bits
        case 9: return "size"_str;     // comptime x 1
        case 10: return "rdport"_str;  // comptime x n_ports (1 rd, 0 wr)
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::Flop:
      switch (pid) {
        case 0: return "async"_str;
        case 1: return "initial"_str;  // reset value
        case 2: return "clock"_str;
        case 3: return "din"_str;
        case 4: return "enable"_str;
        case 5: return "negreset"_str;
        case 6: return "posclk"_str;
        case 7: return "reset"_str;
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::Latch:
      switch (pid) {
          // No 1 to keep din at pos 3 (a,b,c)
        case 3: return "din"_str;
        case 4: return "enable"_str;
        case 6: return "posclk"_str;
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::Fflop:  // Fluid-flop
      switch (pid) {
        case 0: return "valid"_str;
        case 1: return "initial"_str;  // reset value
        case 2: return "clock"_str;
        case 3: return "din"_str;
        case 5: return "stop"_str;  // stop from next cycle
        case 7: return "reset"_str;
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::Get_mask:
      switch (pid) {
        case 0: return "a"_str;     // input net to get bits
        case 2: return "mask"_str;  // bit position
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::Set_mask:
      switch (pid) {
        case 0: return "a"_str;     // input net to set bits
        case 2: return "mask"_str;  // bit position
        case 4: return "value"_str;
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::TupAdd:
      switch (pid) {
        case 0: return "parent"_str;  // tuple name
        case 4: return "value"_str;
        case 5: return "field"_str;  // position of tuple field
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::TupGet:
      switch (pid) {
        case 0: return "parent"_str;
        case 5: return "field"_str;  // SAME as AttrGet field to avoid rewire
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::AttrSet:
      switch (pid) {
        case 0: return "parent"_str;
        case 4: return "value"_str;
        case 5: return "field"_str;
        default: return "invalid"_str;
      }
      break;
    case Ntype_op::AttrGet:
      switch (pid) {
        case 0: return "parent"_str;
        case 5: return "field"_str;
        default: return "invalid"_str;
      }
      break;
    default: assert(false); return "invalid"_str;
  }

  return "invalid"_str;
}

bool Ntype::has_sink(Ntype_op op, mmap_lib::str str) {
  auto it = name2pid.find(str);
  if (it == name2pid.end()) {
    if (std::isdigit(str[0]) && is_unlimited_sink(op))
      return true;

    return false;
  }

  return sink_pid2name[it->second][static_cast<int>(op)] == str;
}

