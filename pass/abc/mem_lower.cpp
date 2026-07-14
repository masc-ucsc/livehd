// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mem_lower.hpp"

#include <format>
#include <map>
#include <string>
#include <vector>

#include "cell.hpp"
#include "diag.hpp"
#include "dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace livehd::abc {

namespace {

// graph/cell.hpp Memory_port_stride: sink pids are laid out in blocks of 16,
// raw_pid = port*16 + field. Field offsets within a block:
constexpr int kMemStride = 16;
enum Mem_off { kAddr = 0, kBits = 1, kClk = 2, kDin = 3, kEnable = 4, kFwd = 5, kPosclk = 6, kType = 7, kWensize = 8,
               kSize = 9, kRdport = 10, kInit = 11 };  // 12/13/14 = whole-array update/enable/reset (unsupported here)

// A one-hot mask constant with only bit `b` set (MSB-first binary string).
spool_ptr<Dlop> bit_mask(int b) {
  return Dlop::from_binary(std::string("1") + std::string(static_cast<size_t>(b), '0'), /*unsigned_result=*/true);
}

// Node factory that stamps the memory's color on every gate it builds so the new
// logic lands in the memory's original partition region.
struct Builder {
  hhds::Graph& g;
  int32_t      color;
  bool         has_color;

  hhds::Node_class mk(Ntype_op op) {
    auto n = gu::create_typed_node(g, op);
    if (has_color) {
      gu::set_color(n, color);
    }
    return n;
  }
  hhds::Pin_class d1(const hhds::Node_class& n) {  // 1-bit unsigned driver pin 0
    auto d = n.create_driver_pin(0);
    gu::set_bits(d, 1);
    gu::set_unsign(d);
    return d;
  }
  hhds::Pin_class dw(const hhds::Node_class& n, int w) {  // w-bit unsigned driver pin 0
    auto d = n.create_driver_pin(0);
    gu::set_bits(d, w);
    gu::set_unsign(d);
    return d;
  }
  hhds::Pin_class konst_i(int64_t v) { return gu::create_const(g, *Dlop::create_integer(v)); }

  hhds::Pin_class eq(const hhds::Pin_class& a, const hhds::Pin_class& b) {  // 1-bit a==b
    auto n = mk(Ntype_op::EQ);
    gu::setup_sink_by_name(n, "as").connect_driver(a);
    gu::setup_sink_by_name(n, "as").connect_driver(b);
    return d1(n);
  }
  hhds::Pin_class and2(const hhds::Pin_class& a, const hhds::Pin_class& b) {  // 1-bit a&b
    auto n = mk(Ntype_op::And);
    gu::setup_sink_by_name(n, "as").connect_driver(a);
    gu::setup_sink_by_name(n, "as").connect_driver(b);
    return d1(n);
  }
  // w-bit: sel ? t : f   (Mux Y = s ? p2 : p1)
  hhds::Pin_class mux(const hhds::Pin_class& sel, const hhds::Pin_class& f, const hhds::Pin_class& t, int w) {
    auto n = mk(Ntype_op::Mux);
    gu::setup_sink_by_name(n, "s").connect_driver(sel);
    gu::setup_sink_by_name(n, "p1").connect_driver(f);
    gu::setup_sink_by_name(n, "p2").connect_driver(t);
    return dw(n, w);
  }
  hhds::Pin_class getbit(const hhds::Pin_class& p, int b) {  // 1-bit p[b]
    if (b == 0 && gu::bits_of(p) == 1) {
      return p;
    }
    auto n = mk(Ntype_op::Get_mask);
    gu::setup_sink_by_name(n, "a").connect_driver(p);
    gu::setup_sink_by_name(n, "mask").connect_driver(gu::create_const(g, *bit_mask(b)));
    return d1(n);
  }
  // concat 1-bit lanes[0..w-1] (LSB first) into a w-bit unsigned value
  hhds::Pin_class concat(const std::vector<hhds::Pin_class>& lanes) {
    int w = static_cast<int>(lanes.size());
    if (w == 1) {
      return lanes[0];
    }
    hhds::Pin_class acc = konst_i(0);
    for (int b = 0; b < w; ++b) {
      auto sm = mk(Ntype_op::Set_mask);
      gu::setup_sink_by_name(sm, "a").connect_driver(acc);
      gu::setup_sink_by_name(sm, "mask").connect_driver(gu::create_const(g, *bit_mask(b)));
      gu::setup_sink_by_name(sm, "value").connect_driver(lanes[b]);
      acc = sm.create_driver_pin(0);
      gu::set_bits(acc, b + 1);
      gu::set_unsign(acc);
    }
    return acc;  // w bits
  }
};

struct Port {
  hhds::Pin_class addr, din, en, clk;
  int             block = 0;
  int             role  = -1;  // 1 = read, 0 = write, -1 = infer
};

int const_i(const hhds::Pin_class& d, int def) {
  return gu::is_const_pin(d) ? static_cast<int>(gu::hydrate_const(d).to_just_i64()) : def;
}

// Lower one Memory node into flops + comb. Returns false (node left intact) for
// shapes not handled here (whole-array cells, negedge, type==2 arrays).
bool lower_one(hhds::Graph& g, const hhds::Node_class& mem) {
  int             bits = 0, size = 0, mtype = 0, wensize = 1, posclk = 1;
  int64_t         fwd = 0;
  hhds::Pin_class init_drv;
  bool            whole_array = false;
  std::map<int, Port> ports;
  for (auto e : mem.inp_edges()) {
    int  raw  = static_cast<int>(e.sink.get_port_id());
    int  off  = raw % kMemStride;
    int  pidx = raw / kMemStride;
    auto drv  = e.driver;
    switch (off) {
      case kAddr: ports[pidx].addr = drv; ports[pidx].block = pidx; break;
      case kClk: ports[pidx].clk = drv; ports[pidx].block = pidx; break;
      case kDin: ports[pidx].din = drv; ports[pidx].block = pidx; break;
      case kEnable: ports[pidx].en = drv; ports[pidx].block = pidx; break;
      case kRdport: ports[pidx].role = const_i(drv, -1); ports[pidx].block = pidx; break;
      case kBits: bits = const_i(drv, bits); break;
      case kFwd: fwd = gu::is_const_pin(drv) ? gu::hydrate_const(drv).to_just_i64() : 0; break;
      case kPosclk: posclk = const_i(drv, posclk); break;
      case kType: mtype = const_i(drv, mtype); break;
      case kWensize: wensize = const_i(drv, wensize); break;
      case kSize: size = const_i(drv, size); break;
      case kInit: init_drv = drv; break;
      default: whole_array = true; break;  // update/update_enable/reset bus
    }
  }

  auto bail = [&](std::string_view why) {
    livehd::diag::warn("pass.abc", "memory-unlowered", "unsupported")
        .msg("pass.abc memory=true: memory in '{}' not bit-blasted ({}) — kept as a native instance",
             std::string{g.get_name()}, why)
        .emit();
    return false;
  };
  if (bits <= 0 || size <= 0) {
    return bail("missing bits/size");
  }
  if (whole_array) {
    return bail("whole-array (update/reset) memory");
  }
  if (mtype == 2) {
    return bail("type==2 array memory");
  }
  if (posclk == 0) {
    return bail("negedge clock");
  }
  int masksize = wensize > 0 ? bits / wensize : bits;
  if (masksize <= 0 || masksize * wensize != bits) {
    return bail("non-uniform write-mask granularity");
  }

  std::vector<Port> wr, rd;
  for (auto& [pidx, p] : ports) {
    p.block  = pidx;
    int role = p.role;
    if (role < 0) {  // infer when the rdport const is absent: a din pin => write
      role = p.din.is_invalid() ? 1 : 0;
    }
    (role == 1 ? rd : wr).push_back(p);
  }
  int n_wr = static_cast<int>(wr.size());

  int32_t color     = gu::has_color(mem) ? gu::color_of(mem) : 0;
  bool    has_color = gu::has_color(mem);
  Builder B{g, color, has_color};

  std::string base = std::string{gu::node_name_of(mem)};
  if (base.empty()) {
    base = std::format("mem{}", mem.get_debug_nid());
  }

  // storage: one bits-wide flop per entry, power-on init from the `init` pin.
  bool has_init = !init_drv.is_invalid() && gu::is_const_pin(init_drv);
  Dlop init_val = has_init ? gu::hydrate_const(init_drv) : Dlop{};
  // A read-only memory (a ROM: no write ports) with init contents cannot be
  // bit-blasted soundly: cgen emits a flop's init only under a reset (the init IS
  // the reset value), so a resetless storage flop would power on as X and the
  // reads — which see nothing but the init, no write ever overwrites it — would
  // diverge from the source ROM. Keep it native; its cgen_memory boundary models
  // the init exactly. (A WRITABLE memory's power-on state is a reachability
  // don't-care that writes establish, so those still bit-blast.)
  if (has_init && wr.empty()) {
    return bail("read-only memory with init contents (ROM) — bit-blasting would drop the ROM data");
  }
  // The Memory carries a single shared clock on port 0 (pid 2); per-port clock
  // pins may be absent on read ports (only the yosys frontend wires RD_CLK). Fall
  // back to that shared clock everywhere, mirroring cgen's base_clock_dpin.
  hhds::Pin_class shared_clk;
  for (auto& [pidx, p] : ports) {
    if (!p.clk.is_invalid()) {
      shared_clk = p.clk;  // ascending pidx => port 0's (shared) clock first
      break;
    }
  }
  hhds::Pin_class wclk = (!wr.empty() && !wr.front().clk.is_invalid()) ? wr.front().clk : shared_clk;

  std::vector<hhds::Pin_class>  data_q(size);
  std::vector<hhds::Node_class> data_flop(size);
  for (int en = 0; en < size; ++en) {
    auto F = B.mk(Ntype_op::Flop);
    F.attr(hhds::attrs::name).set(std::format("{}__mem{}", base, en));
    data_q[en]    = B.dw(F, bits);
    data_flop[en] = F;
    if (!wclk.is_invalid()) {
      wclk.connect_sink(gu::setup_sink_by_name(F, "clock_pin"));
    }
    if (has_init) {
      std::string s(static_cast<size_t>(bits), '0');  // MSB-first
      for (int b = 0; b < bits; ++b) {
        if (init_val.bit_test(static_cast<size_t>(en) * bits + b)) {
          s[bits - 1 - b] = '1';
        }
      }
      gu::create_const(g, *Dlop::from_binary(s, /*unsigned_result=*/true)).connect_sink(gu::setup_sink_by_name(F, "initial"));
    }
  }

  auto en_group_bit = [&](const Port& p, int bit) -> hhds::Pin_class {
    if (p.en.is_invalid()) {
      return B.konst_i(1);  // no enable pin => always written
    }
    return B.getbit(p.en, bit / masksize);
  };

  // write next-state: for each entry, fold the write ports in ASCENDING order so
  // the highest-numbered enabled port wins a same-address collision (cgen).
  for (int en = 0; en < size; ++en) {
    std::vector<hhds::Pin_class> nb(bits);
    for (int b = 0; b < bits; ++b) {
      nb[b] = B.getbit(data_q[en], b);  // hold
    }
    for (const auto& p : wr) {
      auto match = B.eq(p.addr, B.konst_i(en));  // waddr == en
      for (int b = 0; b < bits; ++b) {
        auto sel  = B.and2(en_group_bit(p, b), match);
        auto dinb = B.getbit(p.din, b);
        nb[b]     = B.mux(sel, nb[b], dinb, 1);  // sel ? din : hold
      }
    }
    B.concat(nb).connect_sink(gu::setup_sink_by_name(data_flop[en], "din"));
  }

  // read ports: address mux (Hotmux over the one-hot address) -> forwarding ->
  // optional read-latency register. dout driver pid = n_wr + read-rank (cgen).
  std::map<int, hhds::Pin_class> read_dout;
  for (int r = 0; r < static_cast<int>(rd.size()); ++r) {
    const auto& p = rd[r];
    std::vector<hhds::Pin_class> onehot(size);
    for (int en = 0; en < size; ++en) {
      onehot[en] = B.eq(p.addr, B.konst_i(en));
    }
    auto hm = B.mk(Ntype_op::Hotmux);
    gu::setup_sink_by_name(hm, "s").connect_driver(B.concat(onehot));
    for (int en = 0; en < size; ++en) {
      gu::setup_sink_by_name(hm, std::format("p{}", en + 1)).connect_driver(data_q[en]);
    }
    hhds::Pin_class dmem = B.dw(hm, bits);
    // read-enable: a disabled read yields 0 here (cgen models it as X, a
    // don't-care). Skip the gate when the enable is a constant-true.
    if (!p.en.is_invalid() && !(gu::is_const_pin(p.en) && gu::hydrate_const(p.en).is_known_true())) {
      dmem = B.mux(B.getbit(p.en, 0), B.konst_i(0), dmem, bits);
    }
    // forwarding: apply forwarding write ports HIGH-to-LOW index so the lowest
    // (port 0) ends up outermost and wins (cgen forward priority).
    std::vector<hhds::Pin_class> db(bits);
    for (int b = 0; b < bits; ++b) {
      db[b] = B.getbit(dmem, b);
    }
    for (int ji = n_wr - 1; ji >= 0; --ji) {
      if (((fwd >> ji) & 1) == 0) {
        continue;
      }
      const auto& wp     = wr[ji];
      auto        amatch = B.eq(wp.addr, p.addr);  // waddr == raddr
      for (int b = 0; b < bits; ++b) {
        auto sel  = B.and2(en_group_bit(wp, b), amatch);
        auto dinb = B.getbit(wp.din, b);
        db[b]     = B.mux(sel, db[b], dinb, 1);
      }
    }
    hhds::Pin_class dout = B.concat(db);
    if (mtype == 1) {  // synchronous read: register the resolved value once
      auto F = B.mk(Ntype_op::Flop);
      F.attr(hhds::attrs::name).set(std::format("{}__rdlat{}", base, p.block));
      auto q      = B.dw(F, bits);
      auto rd_clk = p.clk.is_invalid() ? shared_clk : p.clk;  // shared-clock fallback
      if (!rd_clk.is_invalid()) {
        rd_clk.connect_sink(gu::setup_sink_by_name(F, "clock_pin"));
      }
      dout.connect_sink(gu::setup_sink_by_name(F, "din"));
      dout = q;
    }
    read_dout[n_wr + r] = dout;
  }

  // rewire the memory's read-data consumers onto the new douts, then drop it.
  bool ok = true;
  for (const auto& out : mem.out_edges()) {
    int pid = static_cast<int>(out.driver.get_port_id());
    auto it = read_dout.find(pid);
    if (it == read_dout.end() || it->second.is_invalid()) {
      ok = false;  // an output we did not model (e.g. read_all) — cannot lower
      break;
    }
  }
  if (!ok) {
    // Leave the (already-built) helper logic dangling-but-harmless and keep the
    // native memory: dead logic is dropped by cprop/dce downstream.
    return bail("unmodeled memory output (read_all / async whole-array read)");
  }
  for (const auto& out : mem.out_edges()) {
    read_dout[static_cast<int>(out.driver.get_port_id())].connect_sink(out.sink);
  }
  mem.del_node();
  return true;
}

}  // namespace

int lower_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  int lowered = 0;
  for (const auto& gp : graphs) {
    if (!gp) {
      continue;
    }
    std::vector<hhds::Node_class> mems;
    for (auto n : gp->forward_class()) {
      if (gu::type_op_of(n) == Ntype_op::Memory) {
        mems.push_back(n);
      }
    }
    for (const auto& m : mems) {
      if (lower_one(*gp, m)) {
        ++lowered;
      }
    }
  }
  return lowered;
}

}  // namespace livehd::abc
