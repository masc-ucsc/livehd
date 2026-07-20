//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cell.hpp"

#include "attrs.hpp"
#include "hhds/attrs/name.hpp"
#include "iassert.hpp"

namespace {
// Pre-register every LiveHD attribute tag at static-init. The HHDS
// attribute registry is not thread-safe on first-touch (two threads
// racing to register the same tag both find the registry empty and try
// to insert). Pre-registering before main() keeps the lazy attr() path
// on the early-return branch. Mirrors the pattern in lnast/lnast.cpp.
struct Livehd_attr_init {
  Livehd_attr_init() {
    hhds::register_attr_tag<hhds::attrs::name_t>("hhds::attrs::name");
    hhds::register_attr_tag<livehd::attrs::bits_t>("livehd::attrs::bits");
    hhds::register_attr_tag<livehd::attrs::pin_offset_t>("livehd::attrs::pin_offset");
    hhds::register_attr_tag<livehd::attrs::pin_name_t>("livehd::attrs::pin_name");
    hhds::register_attr_tag<livehd::attrs::pin_delay_t>("livehd::attrs::pin_delay");
    hhds::register_attr_tag<livehd::attrs::pin_signed_t>("livehd::attrs::pin_signed");
    hhds::register_attr_tag<livehd::attrs::color_t>("livehd::attrs::color");
    hhds::register_attr_tag<livehd::attrs::hier_color_t>("livehd::attrs::hier_color");
    hhds::register_attr_tag<livehd::attrs::coloring_info_t>("livehd::attrs::coloring_info");
    hhds::register_attr_tag<livehd::attrs::match_t>("livehd::attrs::match");
    hhds::register_attr_tag<livehd::attrs::proven_t>("livehd::attrs::proven");
    hhds::register_attr_tag<livehd::attrs::runtime_check_t>("livehd::attrs::runtime_check");
    hhds::register_attr_tag<livehd::attrs::place_t>("livehd::attrs::place");
    // source provenance rides hhds::attrs::srcid (self-registering)
    hhds::register_attr_tag<livehd::attrs::const_value_t>("livehd::attrs::const_value");
    hhds::register_attr_tag<livehd::attrs::pin_const_value_t>("livehd::attrs::pin_const_value");
    hhds::register_attr_tag<livehd::attrs::lut_t>("livehd::attrs::lut");
    hhds::register_attr_tag<livehd::attrs::time_range_t>("livehd::attrs::time_range");
    hhds::register_attr_tag<livehd::attrs::pending_time_t>("livehd::attrs::pending_time");
  }
};
[[maybe_unused]] const Livehd_attr_init livehd_attr_init_{};
}  // namespace

Ntype::_init Ntype::_static_initializer;

Ntype::_init::_init() {
  // Sparse iteration: Ntype_op values are no longer contiguous (bit 0 of
  // the underlying value is reserved for is_loop_last, see cell.hpp). Op
  // indices between valid ops behave as no-ops here — sink_*[op] slots
  // stay livehd::Port_invalid / "invalid" and get_sink_name_slow returns
  // "invalid" for them, so the inner loop's `pin_name == "invalid"`
  // check skips them. Starting at 1 still skips Ntype_op::Invalid (== 0).
  for (uint8_t op = 1; op < static_cast<uint8_t>(Ntype_op::Last_invalid); ++op) {
    for (auto& e : sink_name2pid) {
      e[op] = livehd::Port_invalid;
    }

    for (auto& e : sink_pid2name) {
      e[op] = "invalid";
    }

    int n_sinks = 0;
    for (hhds::Port_id pid = 0; pid < Ntype::Memory_port_stride; ++pid) {
      auto pin_name = Ntype::get_sink_name_slow(static_cast<Ntype_op>(op), pid);
      if (pin_name.empty() || pin_name == "invalid") {
        continue;
      }

      ++n_sinks;

      sink_pid2name[pid][op] = pin_name;

      auto [it, inserted] = name2pid.emplace(pin_name, pid);
      if (!inserted) {
        I(it->second == pid);  // same name should always have same PID
      }

      if (static_cast<Ntype_op>(op) != Ntype_op::Memory && is_unlimited_sink(static_cast<Ntype_op>(op)) && pid >= 10) {
        continue;
      }

      // First-claim-wins on the per-op first-char slot: two sink names of the
      // same op may share a leading char (Flop posclk/pipe_min/pipe_max all
      // start with 'p'); the later pins resolve through get_sink_pid's slow
      // path (global name2pid + per-op verify) instead of this table.
      if (sink_name2pid[pin_name[0]][op] == livehd::Port_invalid) {
        sink_name2pid[pin_name[0]][op] = pid;
      }
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

    hhds::Port_id pid;
    (void)pid;
    // Check that common case is fine

    // First-char slot invariant. Multi-driver sinks rename a/b -> "as"/"bs"
    // (see get_sink_name_slow) but their leading char is still 'a'/'b', so the
    // pid stays 0/1 here and the get_sink_pid fast path is unaffected.
    pid = sink_name2pid['a'][op];
    assert(pid == livehd::Port_invalid || pid == 0);

    pid = sink_name2pid['b'][op];
    assert(pid == livehd::Port_invalid || pid == 1);

    pid = sink_name2pid['c'][op];
    assert(pid == livehd::Port_invalid || pid == 2);

    pid = sink_name2pid['d'][op];
    assert(pid == livehd::Port_invalid || pid == 3);

    pid = sink_name2pid['e'][op];
    assert(pid == livehd::Port_invalid || pid == 4);

    pid = sink_name2pid['f'][op];
    assert(pid == livehd::Port_invalid || pid == 5);
  }

  // cell_name_sv is sparse; "invalid" placeholders at gap indices must not
  // overwrite cell_name_map["invalid"] (which legitimately maps to
  // Ntype_op::Invalid == 0).
  for (size_t pos = 0; pos < cell_name_sv.size(); ++pos) {
    auto e = cell_name_sv[pos];
    if (pos != 0 && e == "invalid") {
      continue;
    }
    cell_name_map[std::string{e}] = static_cast<Ntype_op>(pos);
  }
}

constexpr std::string_view Ntype::get_sink_name_slow(Ntype_op op, hhds::Port_id pid) {
  switch (op) {
    case Ntype_op::Invalid: return "invalid"; break;
    case Ntype_op::Sum:
    case Ntype_op::LT:
    case Ntype_op::GT:
      // a,b are multi-driver sinks (drivers folded: Sum sums, LT/GT reduce) ->
      // 's'-suffixed names. Keep in sync with Ntype::is_sink_single_driver.
      if (pid == 0) {
        return "as";
      } else if (pid == 1) {
        return "bs";
      }
      return "invalid";
      break;
    case Ntype_op::Mult:
    case Ntype_op::And:
    case Ntype_op::Or:
    case Ntype_op::Xor:
    case Ntype_op::Ror:
    case Ntype_op::EQ:
      // single multi-driver sink a -> "as".
      if (pid == 0) {
        return "as";
      }
      return "invalid";
      break;
    case Ntype_op::Not:
      if (pid == 0) {
        return "a";
      }
      return "invalid";
      break;
    case Ntype_op::Sext:
    case Ntype_op::Div:
    case Ntype_op::SRA:
    case Ntype_op::SHL:
      // a,b are single-driver positional operands -> plain names. (SHL no longer
      // folds multiple one-hot shift amounts on b; that runtime form was removed.)
      if (pid == 0) {
        return "a";
      } else if (pid == 1) {
        return "b";
      }
      return "invalid";
      break;
    case Ntype_op::Nconst:  // No drivers to Constants
      return "invalid";
      break;
    case Ntype_op::Mux:     // unlimited case: 1,2,3,4,5.... // Y = (pid0 == true) ? pid2 : pid1
    case Ntype_op::Hotmux:  // unlimited case: pid0 = one-hot sel, pid1..N = values
      if (pid == 0) {
        return "s";
      }
      [[fallthrough]];
    case Ntype_op::IO:
    case Ntype_op::LUT:  // unlimited case: 1,2,3,4,5....
    case Ntype_op::Sub:  // unlimited case: 1,2,3,4,5....
      assert(is_unlimited_sink(op));
      switch (pid) {
        case 0 : return "p0";
        case 1 : return "p1";
        case 2 : return "p2";
        case 3 : return "p3";
        case 4 : return "p4";
        case 5 : return "p5";
        case 6 : return "p6";
        case 7 : return "p7";
        case 8 : return "p8";
        case 9 : return "p9";
        case 10: return "p10";  // >10 handled with loop at get_sink_pid
        default: return "invalid";
      }
      return "invalid";
      break;
    case Ntype_op::Memory:
      switch (pid) {
        case 0 : return "addr";       // runtime  x n_ports
        case 1 : return "bits";       // comptime x 1
        case 2 : return "clock_pin";  // runtime  x 1 or n_ports
        case 3 : return "din";        // runtime  x n_ports
        case 4 : return "enable";     // runtime  x n_ports
        case 5 : return "fwd";        // comptime x 1 -- per-WRITE-port forwarding mask (bit j forwards write port j)
        case 6 : return "posclk";     // comptime x 1
        case 7 : return "type";       // comptime x 1 (0:async, 1:sync: 2:array)
        case 8 : return "wensize";    // comptime x 1  -- number of Write Enable bits
        case 9 : return "size";       // comptime x 1
        case 10: return "rdport";     // comptime x n_ports (1 rd, 0 wr)
        case 11:
          return "init";  // comptime x 1 -- contents (entry 0 in the low `bits`, row-major); a reg array with a bound reset
                          // restores it via per-entry write ports (tolg). For a WHOLE-ARRAY cell (the `update` pin is
                          // driven) `init` is RUNTIME-capable and carries the reset-value bus (entry 0 in the low `bits`).
        // Whole-array pins (cell has these driven => one `update`/`read_all` bus instead of N per-entry ports).
        case 12: return "update";         // runtime  x 1 -- whole-array next-state bus (size*bits, entry 0 low)
        case 13: return "update_enable";  // runtime  x 1 -- optional bulk-update enable (absent => always-on)
        case 14: return "reset";          // runtime  x 1 -- 1-bit reset condition (active high; tolg pre-inverts negreset)
        // pid 15 reserved (keeps Memory_port_stride a power of two).
        default: return "invalid";
      }
      break;
    case Ntype_op::Flop:
      switch (pid) {
        case 0 : return "async";
        case 1 : return "initial";  // reset value
        case 2 : return "clock_pin";
        case 3 : return "din";
        case 4 : return "enable";
        case 5 : return "negreset";
        case 6 : return "posclk";
        case 7 : return "reset_pin";
        // Pipeline depth range (comptime const pins). Unset =>
        // depth 1 / don't check (today's behavior bit-for-bit). One Flop
        // cell models the whole N-deep shift register — depth is a cell
        // PARAMETER, never replicated cells. min==max==d => fixed depth-d;
        // min<max => the tool owns the choice within the range (cgen
        // realizes `pipe_min` stages; LG pass1 narrows by sigma; slop
        // re-rolls per seed). Both names share posclk's leading 'p' — the
        // get_sink_pid slow path resolves them via the global name table.
        case 8 : return "pipe_min";  // comptime x 1
        case 9 : return "pipe_max";  // comptime x 1
        default: return "invalid";
      }
      break;
    case Ntype_op::Latch:
      // Pin ids are deliberately IDENTICAL to Flop's (2f-latch M2): a latch is
      // modelled as a flop-with-enable that commits at the CLOSING edge of its
      // transparent window, so every consumer that already resolves a flop's
      // control pins by name resolves a latch's the same way and the shared
      // commit-class analysis needs no per-op special case. `pipe_min`/
      // `pipe_max` are deliberately absent: a latch has no pipeline depth.
      //
      // `posclk` ON A LATCH IS THE ENABLE POLARITY, NOT A CLOCK (user ruling,
      // 2026-07-20). A latch has an `enable` signal and the only question is
      // whether it is active HIGH or active LOW:
      //     unset / true  -> enable is active HIGH: transparent while enable==1,
      //                      so it COMMITS on that net's FALL
      //     known-false   -> enable is active LOW:  transparent while enable==0,
      //                      so it COMMITS on that net's RISE
      // The name is inherited from Flop (where pid 6 genuinely means posedge)
      // and is admittedly a poor fit here; it is kept ANYWAY because
      // find_sink_pin() returns invalid SILENTLY for an unknown pin name, so a
      // rename that missed one call site would silently drop the polarity —
      // precisely the double-negation miscompile class this task exists to
      // prevent. Pyrope source may spell the attribute `enable_high`, which
      // reads correctly and maps here.
      //
      // NOTE there is no separate "gate net" pin: the gate IS the enable
      // driver, so the commit class reads (enable net, edge-from-posclk). A
      // second polarity/identity source would recreate the very disagreement
      // the ruling above collapses. `clock_pin` is reserved (see below) but
      // tolg still REFUSES it on a latch — no consumer gives it meaning yet.
      switch (pid) {
        case 0 : return "async";     // reserved for M7 (async set/reset latches)
        case 1 : return "initial";   // reset / power-on value
        case 2 : return "clock_pin"; // reserved; NOT the gate (the enable is)
        // No 1 to keep din at pos 3 (a,b,c)
        case 3 : return "din";
        case 4 : return "enable";
        case 5 : return "negreset";  // reserved for M7
        case 6 : return "posclk";    // ENABLE POLARITY — see the note above
        case 7 : return "reset_pin"; // reserved for M7
        default: return "invalid";
      }
      break;
    case Ntype_op::Fflop:  // Fluid-flop
      switch (pid) {
        case 0 : return "valid";
        case 1 : return "initial";  // reset value
        case 2 : return "clock_pin";
        case 3 : return "din";
        case 5 : return "stop";  // stop from next cycle
        case 7 : return "reset_pin";
        default: return "invalid";
      }
      break;
    case Ntype_op::Get_mask:
      switch (pid) {
        case 0 : return "a";     // input net to get bits
        case 2 : return "mask";  // bit position
        default: return "invalid";
      }
      break;
    case Ntype_op::Set_mask:
      switch (pid) {
        case 0 : return "a";     // input net to set bits
        case 2 : return "mask";  // bit position
        case 4 : return "value";
        default: return "invalid";
      }
      break;
    case Ntype_op::AttrSet:
      switch (pid) {
        case 0 : return "parent";
        case 4 : return "value";
        case 5 : return "field";
        default: return "invalid";
      }
      break;
    // Unused op indices (gaps in the sparse encoding) fall through here
    // silently. The init loop in _init treats "invalid" as "skip this slot".
    default: return "invalid";
  }

  return "invalid";
}
