// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mem_lower.hpp"

#include <format>
#include <map>
#include <optional>
#include <set>
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
enum Mem_off {
  kAddr    = 0,
  kBits    = 1,
  kClk     = 2,
  kDin     = 3,
  kEnable  = 4,
  kFwd     = 5,
  kPosclk  = 6,
  kType    = 7,
  kWensize = 8,
  kSize    = 9,
  kRdport  = 10,
  kInit    = 11,
  kUndef   = 15
};  // 12/13/14 = whole-array update/enable/reset (unsupported here)

// A one-hot mask constant with only bit `b` set (MSB-first binary string).
spool_ptr<Dlop> bit_mask(int b) {
  return Dlop::from_binary(std::string("1") + std::string(static_cast<size_t>(b), '0'), /*unsigned_result=*/true);
}

// Node factory that stamps the memory's color on every gate it builds so the new
// logic lands in the memory's original partition region.
//
// Everything below works on LANES (a `masksize`-wide write-enable group, the
// whole word when wensize==1), never on single bits: abc_map bit-blasts a
// bits-wide Mux into exactly one AND-OR per bit, and Get_mask/Set_mask/Concat
// with constant masks are pure wiring there (and a slice / `{}` in cgen, a
// concat/extract in the lec encoder). The old per-bit getbit + and2 + mux +
// concat plumbing cost 97 nodes per (entry, port) on a 32-bit word (122k
// input_nodes for one 32x32 tile, report_9681 finding 2) for the same AIG.
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
  // Lane `l` (w bits, LSB-first) of the `total`-bit value p: bits
  // [l*w, (l+1)*w). A single-lane value is returned as is; otherwise a
  // Get_mask with the contiguous lane mask, which every consumer treats as a
  // slice (zero gates).
  hhds::Pin_class getlane(const hhds::Pin_class& p, int l, int w, int total) {
    if (w == total && l == 0) {
      return p;
    }
    auto n = mk(Ntype_op::Get_mask);
    gu::setup_sink_by_name(n, "a").connect_driver(p);
    gu::setup_sink_by_name(n, "mask").connect_driver(gu::create_const(g, *Dlop::get_mask_value((l + 1) * w - 1, l * w)));
    return dw(n, w);
  }
  // Fit an arbitrary driver to an unsigned w-bit value (truncate / zero-extend),
  // the same view the old per-bit getbit(p, b), b < w, fold took of a port's
  // data. Free when the driver already is exactly that.
  hhds::Pin_class fit(const hhds::Pin_class& p, int w) {
    if (gu::bits_of(p) == w && gu::is_unsign(p)) {
      return p;
    }
    auto n = mk(Ntype_op::Get_mask);
    gu::setup_sink_by_name(n, "a").connect_driver(p);
    gu::setup_sink_by_name(n, "mask").connect_driver(gu::create_const(g, *Dlop::get_mask_value(w - 1, 0)));
    return dw(n, w);
  }
  // Pack equal-width lanes (LSB first, each `w` bits) into one unsigned
  // lanes.size()*w-bit value through a Concat cell. The cell's sinks are
  // interleaved (value, width) pairs MSB-FIRST (graph/node_util.hpp Concat
  // contract), so cell lane i carries lanes[n-1-i]. Pins are created in
  // descending pid order: hhds keeps a node's pin list sorted, and ascending
  // creation rescans the growing list per pin (O(n^2) on a wide read_all).
  hhds::Pin_class pack(const std::vector<hhds::Pin_class>& lanes, int w) {
    const int n = static_cast<int>(lanes.size());
    if (n == 1) {
      return lanes[0];
    }
    auto c      = mk(Ntype_op::Concat);
    auto wconst = konst_i(w);
    for (int i = n - 1; i >= 0; --i) {
      const auto data_pid = static_cast<hhds::Port_id>(2 * i);
      wconst.connect_sink(c.create_sink_pin(data_pid + 1));
      lanes[static_cast<size_t>(n - 1 - i)].connect_sink(c.create_sink_pin(data_pid));
    }
    return dw(c, n * w);
  }
};

struct Port {
  hhds::Pin_class addr, din, en, clk;
  int             block = 0;
  int             role  = -1;  // 1 = read, 0 = write, -1 = infer
};

int const_i(const hhds::Pin_class& d, int def) { return d.is_const() ? static_cast<int>(gu::const_of(d).to_just_i64()) : def; }

// The address of a port when it is a plain non-negative integer constant, i.e.
// decidable at build time (nullopt = keep the runtime compare). A negative or
// x-bearing constant address stays on the runtime EQ path so its (degenerate)
// value is compared bit-exactly rather than second-guessed here. The value may
// be >= size: an out-of-range constant write selects no entry and an
// out-of-range constant read yields 0 (the value the one-hot Hotmux returns
// when no arm hits), matching inou/cgen's inline array emission which skips
// such a write and slang, which rejects the index at elaboration anyway.
std::optional<int64_t> const_addr(const hhds::Pin_class& a) {
  if (!a.is_const()) {
    return std::nullopt;
  }
  const auto& v = gu::const_of(a);
  if (v.has_unknowns() || v.is_negative() || !v.is_just_i64()) {
    return std::nullopt;
  }
  return v.to_just_i64();
}

// Lower one Memory node into flops + comb. Returns false (node left intact) for
// shapes not handled here (whole-array cells, negedge, type==2 arrays) and for
// a memory above `max_bits` storage bits (0 = no limit).
bool lower_one(hhds::Graph& g, const hhds::Node_class& mem, uint64_t max_bits) {
  int                 bits = 0, size = 0, mtype = 0, wensize = 1, posclk = 1;
  spool_ptr<Dlop>     fwd;  // per-(read,write) matrix; arbitrary precision
  hhds::Pin_class     init_drv;
  bool                whole_array   = false;
  bool                undef_refined = false;  // ordering="none" matrix dropped by the bit-blast
  std::map<int, Port> ports;
  for (auto e : mem.inp_edges()) {
    int  raw  = static_cast<int>(e.sink.get_port_id());
    int  off  = raw % kMemStride;
    int  pidx = raw / kMemStride;
    auto drv  = e.driver;
    switch (off) {
      case kAddr:
        ports[pidx].addr  = drv;
        ports[pidx].block = pidx;
        break;
      case kClk:
        ports[pidx].clk   = drv;
        ports[pidx].block = pidx;
        break;
      case kDin:
        ports[pidx].din   = drv;
        ports[pidx].block = pidx;
        break;
      case kEnable:
        ports[pidx].en    = drv;
        ports[pidx].block = pidx;
        break;
      case kRdport:
        ports[pidx].role  = const_i(drv, -1);
        ports[pidx].block = pidx;
        break;
      case kBits: bits = const_i(drv, bits); break;
      case kFwd:
        if (drv.is_const()) {
          fwd = Dlop::clone(gu::const_of(drv));
        }
        break;
      case kPosclk : posclk = const_i(drv, posclk); break;
      case kType   : mtype = const_i(drv, mtype); break;
      case kWensize: wensize = const_i(drv, wensize); break;
      case kSize   : size = const_i(drv, size); break;
      case kInit   : init_drv = drv; break;
      case kUndef:
        // ordering="none" (undefined read-during-write). Bit-blasting cannot
        // carry an x, so the netlist REFINES it to the committed value -- which
        // is what dropping the matrix here already does. Sound in the direction
        // lec runs: the abc netlist is the IMPL and the unmapped design the REF,
        // and every refinement of a ref-side don't-care proves. (Falling through
        // to `default` would set whole_array and refuse to bit-blast the memory
        // at all.) Reported below rather than dropped silently: the choice is
        // only sound in ONE lec direction, and it makes the emitted RTL differ
        // (x vs. the committed value) depending on whether abc ran.
        if (drv.is_const() && !drv.is_known_false()) {
          undef_refined = true;
        }
        break;
      default: whole_array = true; break;  // update/update_enable/reset bus
    }
  }

  std::string base = std::string{gu::node_name_of(mem)};
  if (base.empty()) {
    base = std::format("mem{}", mem.get_debug_nid());
  }

  auto bail = [&](std::string_view why) {
    livehd::diag::warn("pass.abc", "memory-unlowered", "unsupported")
        .msg("pass.abc memory=true: memory '{}' in '{}' not bit-blasted ({}) — kept as a native instance",
             base,
             std::string{g.get_name()},
             why)
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
  // memory_max_bits: a bit-blasted memory is one DFF cell per storage bit plus
  // its write muxes, so a big SRAM-class array would swamp ABC (and is a macro
  // in any real flow, never standard cells). Above the limit it stays a native
  // instance, as memory=false keeps every memory. A note, not a warning: the
  // outcome is the documented one, the user just needs to see which memory it
  // was to raise the limit deliberately.
  if (max_bits > 0 && static_cast<uint64_t>(bits) * static_cast<uint64_t>(size) > max_bits) {
    livehd::diag::info("pass.abc", "memory-max-bits", "unsupported")
        .msg("pass.abc memory=true: memory '{}' in '{}' ({} x {} = {} bits) is above memory_max_bits={} — kept as a native "
             "instance",
             base,
             std::string{g.get_name()},
             size,
             bits,
             static_cast<uint64_t>(bits) * static_cast<uint64_t>(size),
             max_bits)
        .emit();
    return false;
  }

  std::vector<Port> wr, rd;
  for (auto& [pidx, p] : ports) {
    p.block  = pidx;
    int role = p.role;
    if (role < 0) {  // infer when the rdport const is absent: a din pin => write
      role = p.din.is_invalid() ? 1 : 0;
    }
    if (p.addr.is_invalid() || (role != 1 && p.din.is_invalid())) {
      return bail(std::format("port {} has no {} pin", pidx, p.addr.is_invalid() ? "address" : "write-data"));
    }
    (role == 1 ? rd : wr).push_back(p);
  }
  int n_wr = static_cast<int>(wr.size());
  int n_rd = static_cast<int>(rd.size());
  // `fwd` is a per-(read,write) matrix (graph/cell.cpp): bit r*n_wr + w says
  // read port r forwards write port w. Dlop::bit_test is arbitrary precision,
  // so wide (many-port) shapes do not truncate.
  auto fwd_bit = [&](int r, int w) -> int { return (fwd && fwd->bit_test(r * n_wr + w)) ? 1 : 0; };

  // Which outputs are consumed: read port r drives pid n_wr + r (cgen), the
  // whole-array read drives the reserved Memory_readall_pid. Decided BEFORE
  // any node is built so an unmodelable output bails with nothing dangling.
  const int                ra_pid = static_cast<int>(Ntype::Memory_readall_pid);
  std::set<int>            out_pids;
  for (const auto& out : mem.out_edges()) {
    int pid = static_cast<int>(out.driver.get_port_id());
    if (pid != ra_pid && (pid < n_wr || pid >= n_wr + n_rd)) {
      return bail(std::format("unmodeled memory output pid {}", pid));
    }
    out_pids.insert(pid);
  }

  // Past every bail(): this memory IS being bit-blasted, so an ordering="none"
  // matrix is about to be refined away. Say so once, here, rather than have the
  // netlist quietly disagree with the un-mapped design's emitted RTL.
  if (undef_refined) {
    livehd::diag::warn("pass.abc", "memory-undef-refined", "unsupported")
        .msg(
            "pass.abc memory=true: memory in '{}' declares ordering=\"none\" (undefined read-during-write), but a "
            "bit-blasted netlist cannot carry an x — the collision window is REFINED to the committed value. The "
            "emitted RTL therefore differs from the un-mapped design (x there), and lec is only sound with this "
            "netlist as the IMPL side. Set pass.abc memory=false to keep it a native memory instance",
            std::string{g.get_name()})
        .emit();
  }

  int32_t color     = gu::has_color(mem) ? gu::color_of(mem) : 0;
  bool    has_color = gu::has_color(mem);
  Builder B{g, color, has_color};

  // storage: one bits-wide flop per entry, power-on init from the `init` pin.
  bool has_init = init_drv.is_const();
  Dlop init_val = has_init ? gu::const_of(init_drv) : Dlop{};
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

  // Constant-address ports are decided here, at build time, instead of feeding
  // an all-constant EQ per (entry, port) to ABC. Besides the node count, that
  // shape was the one the EQ width bug (abc_map.cpp, EQ case) miscompiled:
  // the bedrock multi-write tiles (br_fifo_shared_dynamic_flops: 32 write
  // ports, wr_addr_k = k) selected every same-parity entry per port.
  std::vector<std::optional<int64_t>> wr_caddr(n_wr), rd_caddr(n_rd);
  for (int ji = 0; ji < n_wr; ++ji) {
    wr_caddr[ji] = const_addr(wr[ji].addr);
  }
  for (int r = 0; r < n_rd; ++r) {
    rd_caddr[r] = const_addr(rd[r].addr);
  }

  // Per write port, per lane: the data lane and the enable bit, built ONCE and
  // shared by every entry the port can reach (a runtime-address port reaches all
  // of them). An INVALID pin stands for "always" throughout: no enable pin, or
  // an address match decided true at build time.
  std::vector<std::vector<hhds::Pin_class>> wr_din_lane(n_wr, std::vector<hhds::Pin_class>(wensize));
  std::vector<std::vector<hhds::Pin_class>> wr_en_bit(n_wr, std::vector<hhds::Pin_class>(wensize));
  for (int ji = 0; ji < n_wr; ++ji) {
    const auto& p   = wr[ji];
    auto        din = B.fit(p.din, bits);
    for (int l = 0; l < wensize; ++l) {
      wr_din_lane[ji][l] = B.getlane(din, l, masksize, bits);
      if (p.en.is_invalid()) {
        continue;  // no enable pin => always written
      }
      if (p.en.is_const()) {  // fold a constant enable here rather than mask a literal
        wr_en_bit[ji][l] = B.konst_i(gu::const_of(p.en).bit_test(static_cast<size_t>(l)) ? 1 : 0);
      } else {
        wr_en_bit[ji][l] = B.getbit(p.en, l);
      }
    }
  }
  // sel = a & b with an invalid operand meaning true.
  auto and_opt = [&](const hhds::Pin_class& a, const hhds::Pin_class& b) -> hhds::Pin_class {
    if (a.is_invalid()) {
      return b;
    }
    if (b.is_invalid()) {
      return a;
    }
    return B.and2(a, b);
  };
  // Fold one write into a lane: no mux at all when the select is decided at
  // build time (true: the data replaces the lane; false: nothing), one
  // masksize-wide Mux otherwise. The mux WRAPS the previous value, so the last
  // port folded is the outermost and wins a same-address collision.
  auto fold_lane = [&](hhds::Pin_class& lane, const hhds::Pin_class& sel, const hhds::Pin_class& din_l) {
    if (sel.is_invalid() || sel.is_known_true()) {
      lane = din_l;
      return;
    }
    if (sel.is_known_false()) {
      return;
    }
    lane = B.mux(sel, lane, din_l, masksize);
  };

  // write next-state: for each entry, fold the write ports in ASCENDING order so
  // the highest-numbered enabled port wins a same-address collision (cgen). A
  // constant-address port is folded into ITS entry only — the others never see
  // it (no EQ, no mux) — and is skipped entirely when the address is out of
  // range. The order of the fold is the priority, so ports are skipped, never
  // reordered.
  for (int en = 0; en < size; ++en) {
    std::vector<hhds::Pin_class> lane(wensize);
    for (int l = 0; l < wensize; ++l) {
      lane[l] = B.getlane(data_q[en], l, masksize, bits);  // hold
    }
    bool touched = false;
    for (int ji = 0; ji < n_wr; ++ji) {
      if (wr_caddr[ji] && *wr_caddr[ji] != en) {
        continue;  // a constant address elsewhere (or out of range) never touches this entry
      }
      hhds::Pin_class match;  // invalid = matches at build time (constant address == en)
      if (!wr_caddr[ji]) {
        match = B.eq(wr[ji].addr, B.konst_i(en));  // waddr == en
      }
      touched = true;
      for (int l = 0; l < wensize; ++l) {
        fold_lane(lane[l], and_opt(wr_en_bit[ji][l], match), wr_din_lane[ji][l]);
      }
    }
    // An entry no port can ever write holds its power-on value: din = Q.
    auto nb = touched ? B.pack(lane, masksize) : data_q[en];
    nb.connect_sink(gu::setup_sink_by_name(data_flop[en], "din"));
  }

  // read ports: address mux -> forwarding -> optional read-latency register.
  // dout driver pid = n_wr + read-rank (cgen). A read nobody consumes builds
  // nothing.
  std::map<int, hhds::Pin_class> read_dout;
  for (int r = 0; r < n_rd; ++r) {
    if (!out_pids.contains(n_wr + r)) {
      continue;
    }
    const auto&     p = rd[r];
    hhds::Pin_class dmem;
    if (rd_caddr[r]) {
      // A constant address is a plain wire onto the entry's Q (out of range: 0,
      // what the one-hot Hotmux below yields when no arm hits).
      dmem = (*rd_caddr[r] < size) ? data_q[static_cast<size_t>(*rd_caddr[r])] : B.konst_i(0);
    } else {
      // Hotmux over the one-hot address decode. Measured a wash against a
      // binary mux tree once mapped (report_9681 finding 4: abc_map builds both
      // as AND-OR covers), so the one-hot form stays.
      std::vector<hhds::Pin_class> onehot(size);
      for (int en = 0; en < size; ++en) {
        onehot[en] = B.eq(p.addr, B.konst_i(en));
      }
      auto hm = B.mk(Ntype_op::Hotmux);
      gu::setup_sink_by_name(hm, "s").connect_driver(B.pack(onehot, 1));
      for (int en = 0; en < size; ++en) {
        gu::setup_sink_by_name(hm, std::format("p{}", en + 1)).connect_driver(data_q[en]);
      }
      dmem = B.dw(hm, bits);
    }
    // read-enable: a disabled read yields 0 here (cgen models it as X, a
    // don't-care). Skip the gate when the enable is a constant-true.
    if (!p.en.is_invalid() && !p.en.is_known_true()) {
      dmem = p.en.is_known_false() ? B.konst_i(0) : B.mux(B.getbit(p.en, 0), B.konst_i(0), dmem, bits);
    }
    // forwarding: fold the forwarding write ports in ASCENDING order so the
    // HIGHEST-numbered enabled port ends up outermost and wins — the same
    // priority as the write next-state fold above, as cgen/cgen_sim/lec all
    // use. A const/const address pair is decided here: unequal never collides
    // (no EQ, no mux), equal collides whenever the port is enabled.
    std::vector<hhds::Pin_class> lane(wensize);
    for (int l = 0; l < wensize; ++l) {
      lane[l] = B.getlane(dmem, l, masksize, bits);
    }
    bool touched = false;
    for (int ji = 0; ji < n_wr; ++ji) {
      // `fwd` is a per-(read,write) matrix (graph/cell.cpp): bit r*n_wr + ji.
      if (fwd_bit(r, ji) == 0) {
        continue;
      }
      hhds::Pin_class amatch;  // invalid = equal constant addresses
      if (rd_caddr[r] && wr_caddr[ji]) {
        if (*rd_caddr[r] != *wr_caddr[ji]) {
          continue;
        }
      } else {
        amatch = B.eq(wr[ji].addr, p.addr);  // waddr == raddr
      }
      touched = true;
      for (int l = 0; l < wensize; ++l) {
        fold_lane(lane[l], and_opt(wr_en_bit[ji][l], amatch), wr_din_lane[ji][l]);
      }
    }
    hhds::Pin_class dout = touched ? B.pack(lane, masksize) : dmem;
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

  // Whole-array read (the reserved Memory_readall_pid driver, size*bits wide):
  // the concatenation of the entry flops with entry 0 in the LOW bits — the
  // layout graph/cell.cpp fixes for `init`/`update`, inou/cgen emits (`assign
  // <ra> = <array>` over a `reg [size-1:0][bits-1:0]`, i.e. {data[size-1], ...,
  // data[0]}) and pass/lec encodes (CONCAT of SELECT(a_cur, i), i ascending
  // into the high bits). It reads the COMMITTED contents, never a same-cycle
  // write: that is the lec encoder's a_cur, and cgen refuses a read_all memory
  // with a non-zero fwd/undef matrix, so no forwarding can apply here.
  if (out_pids.contains(ra_pid)) {
    read_dout[ra_pid] = B.pack(data_q, bits);
  }

  // rewire the memory's read-data consumers onto the new douts, then drop it.
  for (const auto& out : mem.out_edges()) {
    read_dout.at(static_cast<int>(out.driver.get_port_id())).connect_sink(out.sink);
  }
  mem.del_node();
  return true;
}

}  // namespace

int lower_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, uint64_t max_bits) {
  int lowered = 0;
  for (const auto& gp : graphs) {
    if (!gp) {
      continue;
    }
    std::vector<hhds::Node_class> mems;
    for (auto n : gp->body().nodes(hhds::Node_order::forward)) {
      if (gu::type_op_of(n) == Ntype_op::Memory) {
        mems.push_back(n);
      }
    }
    for (const auto& m : mems) {
      if (lower_one(*gp, m, max_bits)) {
        ++lowered;
      }
    }
  }
  return lowered;
}

}  // namespace livehd::abc
