// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "graph_access.hpp"

#include <sstream>

namespace lean_pass {
[[noreturn]] void fatal(const LeanCtx& /*ctx*/, const std::string& msg) { throw Emit_error("[ERROR] pass.lean: " + msg); }

std::string input_name_for_pin(const LeanCtx& ctx, const Node_pin& pin) {
  // pin_name_of resolves a graph-input pin's declared port name directly (via
  // the graph's IO maps); no need to identity-match against get_input_pin.
  auto pname = std::string(livehd::graph_util::pin_name_of(pin));
  if (!pname.empty() && ctx.input_width.contains(pname)) {
    return pname;
  }
  return {};
}

Node_pin resolve_resize_chain(const Node_pin& start) {
  Node_pin cur = start;
  for (int guard = 0; guard < 32; ++guard) {
    if (cur.is_invalid() || pin_is_input(cur) || pin_is_const(cur)) {
      return cur;
    }
    auto n  = pin_node(cur);
    auto op = node_op(n);
    auto es = inp_edges_ordered(n);
    if (op == Ntype_op::Or && es.size() == 1) {
      cur = es[0].driver;  // arity-1 Or is the emitter's resize
      continue;
    }
    if (op == Ntype_op::Get_mask && es.size() == 2) {
      // Transparent only when the mask is a constant all-ones: get_mask(a,-1) is zext(a).
      const auto& mask = es[1].driver;
      if (pin_is_const(mask)) {
        auto v = pin_const_value(mask);
        if (v.is_just_i64() && v.to_just_i64() == -1) {
          cur = es[0].driver;
          continue;
        }
      }
    }
    return cur;
  }
  return cur;
}

std::string sink_pin_name(const Edge& edge) {
  const auto sink_node = pin_node(edge.sink);
  return std::string(Ntype::get_sink_name(node_op(sink_node), edge.sink.get_port_id()));
}

uint32_t raw_pin_width(const Node_pin& pin) { return static_cast<uint32_t>(livehd::graph_util::bits_of(pin)); }

uint32_t raw_node_width(const Node& node) { return raw_pin_width(node.create_driver_pin(0)); }

Dlop node_const_value(const Node& node) { return livehd::graph_util::hydrate_const(node); }

bool node_output_is_signed(const Node& node) {
  auto n    = node;
  auto dpin = n.create_driver_pin(0);
  return !dpin.is_invalid() && !livehd::graph_util::is_unsign(dpin);
}

void check_width(const LeanCtx& ctx, const Node& node, uint32_t w, std::string_view what) {
  if (w == 0) {
    fatal(ctx,
          "node n_" + std::to_string(node_id(node)) + " (" + std::string(what)
              + ") has zero width; Lean BitVec generation requires positive widths.");
  }
  if (w > ctx.max_width) {
    fatal(ctx,
          "node n_" + std::to_string(node_id(node)) + " (" + std::string(what) + ") has width " + std::to_string(w)
              + " > max_width=" + std::to_string(ctx.max_width));
  }
}

uint32_t intrinsic_const_width(const Dlop& v) {
  if (!v.is_just_i64()) {
    return std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
  }
  const int64_t iv = v.to_just_i64();
  if (iv < 0) {
    // A negative literal needs its sign bit; get_bits() already counts it.
    return std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
  }
  return minimal_unsigned_const_width(v);
}

uint32_t pin_width(const LeanCtx& ctx, const Node_pin& pin, const Node& owner) {
  auto w = raw_pin_width(pin);

  if (pin_is_const(pin)) {
    const auto v = pin_const_value(pin);
    if (w == 0) {
      // UNSIZED constant: the value carries the width.  Returning 1 here (as this
      // did) modeled `0x6000000` as a single bit, i.e. as 0 -- and because the
      // fast model and the certificate share this function they truncated
      // IDENTICALLY, so step 5 proved and the LEC gate (which compares RTL to
      // LGraph, not the model to either) never looked.  The result was a model
      // that silently disagreed with the RTL and passed both gates.
      const auto iw = intrinsic_const_width(v);
      if (static_cast<size_t>(iw) > ctx.max_width) {
        fatal(ctx,
              "node n_" + std::to_string(node_id(owner)) + ": unsized constant needs " + std::to_string(iw)
                  + " bits > max_width=" + std::to_string(ctx.max_width));
      }
      return iw;
    }
    if (static_cast<size_t>(w) > ctx.max_width) {
      // Do NOT fall back to 1: silently remodeling a too-wide constant as one bit
      // is the same failure in a different disguise.
      fatal(ctx,
            "node n_" + std::to_string(node_id(owner)) + ": constant pin width " + std::to_string(w)
                + " > max_width=" + std::to_string(ctx.max_width));
    }
    // DECLARED width: honour it, but only if it can hold the value.  A value
    // above its declared unsigned width makes the attribute "a lie" and
    // "downstream emit/LEC silently diverges" -- graph/node_util.hpp:323 asserts
    // exactly this in debug builds.  Refuse rather than widen (which would
    // contradict the graph) or truncate (which would drop real bits).
    if (ctx.strict && !v.has_unknowns() && !v.is_negative() && intrinsic_const_width(v) > static_cast<uint32_t>(w)) {
      fatal(ctx, "node n_" + std::to_string(node_id(owner)) + ": constant pin declares width " + std::to_string(w)
                     + " but its value needs " + std::to_string(intrinsic_const_width(v))
                     + " bits (graph/node_util.hpp:323 -- the bits attribute does not hold the value). "
                       "Fix or explicitly truncate this constant upstream; pass.lean will not guess.");
    }
    return static_cast<uint32_t>(w);
  }

  if (w == 0 || static_cast<size_t>(w) > ctx.max_width) {
    if (ctx.strict) {
      check_width(ctx, owner, w, "pin");
    }
    return 1;
  }
  return static_cast<uint32_t>(w);
}

uint32_t data_dep_width(const LeanCtx& ctx, const Node_pin& pin, const Node& owner, uint32_t op_w) {
  const auto pw = pin_width(ctx, pin, owner);
  if (!pin_is_const(pin)) {
    return pw;
  }
  return std::max(pw, op_w);
}

uint32_t node_width(const LeanCtx& ctx, const Node& node) {
  auto w = raw_node_width(node);
  check_width(ctx, node, w, "node");
  return static_cast<uint32_t>(w);
}

uint32_t ceil_log2_u64(uint64_t v) {
  if (v <= 1) {
    return 1;
  }
  --v;
  uint32_t w = 0;
  while (v != 0) {
    ++w;
    v >>= 1;
  }
  return w == 0 ? 1 : w;
}

int64_t const_pin_int(const LeanCtx& ctx, const Node_pin& pin, const Node& owner, std::string_view field) {
  if (!pin_is_const(pin)) {
    fatal(ctx, "Memory node n_" + std::to_string(node_id(owner)) + " has non-constant " + std::string(field) + " policy pin.");
  }
  auto v = pin_const_value(pin);
  if (!v.is_just_i64()) {
    fatal(ctx, "Memory node n_" + std::to_string(node_id(owner)) + " has non-integer " + std::string(field) + " policy pin.");
  }
  return v.to_just_i64();
}

int64_t const_pin_int_or(const Node_pin& pin, int64_t dflt) {
  if (!pin_is_const(pin)) {
    return dflt;
  }
  auto v = pin_const_value(pin);
  return v.is_just_i64() ? v.to_just_i64() : dflt;
}

std::string memory_policy_summary(const Memory_info& mi) {
  std::ostringstream oss;
  oss << "memory node n_" << mi.nid << " bits=" << mi.bits << " size=" << mi.size << " addr_width=" << mi.addr_width
      << " type=" << mi.type << " fwd=" << mi.fwd << " undef=" << (mi.undef ? 1 : 0) << " posclk=" << mi.posclk
      << " wensize=" << mi.wensize << " rdports=" << mi.read_ports.size() << " wrports=" << mi.write_ports.size();
  return oss.str();
}

Memory_info parse_memory_info(LeanCtx& ctx, const Node& node) {
  Memory_info mi;
  mi.node = node;
  mi.nid  = node_id(node);

  const auto stride = static_cast<size_t>(Ntype::Memory_port_stride);
  for (const auto& e : inp_edges_ordered(node)) {
    const auto raw_pid = static_cast<size_t>(e.sink.get_port_id());
    const auto pname   = std::string(Ntype::get_sink_name(Ntype_op::Memory, raw_pid % stride));
    const auto port_id = raw_pid / stride;
    if (mi.ports.size() <= port_id) {
      mi.ports.resize(port_id + 1);
    }
    mi.ports[port_id].port_id = port_id;

    if (pname == "bits") {
      const auto v = const_pin_int(ctx, e.driver, node, pname);
      if (v <= 0) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has non-positive bits.");
      }
      mi.bits = static_cast<uint32_t>(v);
    } else if (pname == "size") {
      const auto v = const_pin_int(ctx, e.driver, node, pname);
      if (v <= 0) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has non-positive size.");
      }
      mi.size = static_cast<uint64_t>(v);
    } else if (pname == "wensize") {
      const auto v = const_pin_int(ctx, e.driver, node, pname);
      if (v < 0) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has negative wensize.");
      }
      mi.wensize = static_cast<uint32_t>(v);
    } else if (pname == "type") {
      mi.type = const_pin_int(ctx, e.driver, node, pname);
    } else if (pname == "fwd") {
      // NOT lenient: `fwd` IS load-bearing (memory_raw_read_port picks the
      // pre- or post-write image from it).  A non-constant driver, or a matrix
      // too wide for an i64, must be refused where it is observable -- see the
      // post-loop check -- not defaulted to read-first.
      if (pin_is_const(e.driver)) {
        const auto v = pin_const_value(e.driver);
        mi.fwd_known = v.is_just_i64();
        mi.fwd       = mi.fwd_known ? v.to_just_i64() : 0;
      } else {
        mi.fwd_known = false;
        mi.fwd       = 0;
      }
    } else if (pname == "undef") {
      // ordering="none": the read-during-write window is UNDEFINED. The policy
      // tuple below only knows the two DEFINED answers (write_first from a set
      // `fwd` bit, read_first from a clear one), so a certificate about such a
      // memory asserts a concrete value in a window the emitted RTL leaves x.
      // Refuse it below rather than export a proof of a different design.
      //
      // Tested with is_known_false() rather than the i64 window so a matrix
      // wider than 62 bits still registers as set, and a non-const driver on
      // this comptime pin refuses instead of guessing.
      mi.undef = true;
      if (pin_is_const(e.driver)) {
        mi.undef = !pin_const_value(e.driver).is_known_false();
      }
    } else if (pname == "posclk") {
      mi.posclk = const_pin_int_or(e.driver, 1);  // unused in async emission; tolerate non-const
    } else if (pname == "rdport") {
      mi.ports[port_id].rdport = const_pin_int(ctx, e.driver, node, pname) != 0;
    } else if (pname == "addr") {
      mi.ports[port_id].addr = e.driver;
    } else if (pname == "din") {
      mi.ports[port_id].din = e.driver;
    } else if (pname == "enable") {
      mi.ports[port_id].enable = e.driver;
    } else if (pname == "clock_pin") {
      mi.ports[port_id].clock = e.driver;
    } else if (pname == "init") {
      mi.init_pin = e.driver;
    } else if (pname == "update") {
      mi.update_pin = e.driver;
    } else if (pname == "update_enable") {
      mi.update_enable_pin = e.driver;
    } else if (pname == "reset") {
      mi.bulk_reset_pin = e.driver;
    } else {
      // No silent drop: graph/cell.cpp gives the Memory cell pids 0..15 and the
      // arms above now cover all of them, so reaching here means the cell grew a
      // pin this pass has never seen.
      fatal(ctx,
            "Memory node n_" + std::to_string(mi.nid) + ": unknown Memory pin `" + pname + "` (raw pid " + std::to_string(raw_pid)
                + ") is driven.");
    }
  }

  if (mi.bits == 0 || mi.size == 0) {
    fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " is missing constant bits/size policy.");
  }
  check_width(ctx, node, mi.bits, "memory data");
  mi.addr_width = ceil_log2_u64(mi.size);
  if (mi.addr_width == 0 || mi.addr_width > ctx.max_width) {
    fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " has unsupported address width " + std::to_string(mi.addr_width));
  }
  if (mi.wensize == 0) {
    mi.wensize = 1;
  }
  if (mi.bits % mi.wensize != 0) {
    fatal(ctx,
          "Memory node n_" + std::to_string(mi.nid) + " has bits not divisible by wensize: bits=" + std::to_string(mi.bits)
              + " wensize=" + std::to_string(mi.wensize));
  }

  for (size_t idx = 0; idx < mi.ports.size(); ++idx) {
    auto& p   = mi.ports[idx];
    p.port_id = idx;
    // A resize gap (a port_id that never appeared on any sink) is not a real
    // port; skip it so a sparse port map is not mistaken for a malformed write.
    if (p.addr.is_invalid() && p.din.is_invalid() && p.enable.is_invalid() && p.clock.is_invalid()) {
      continue;
    }
    if (p.rdport) {
      mi.read_ports.push_back(idx);
      if (p.addr.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " read port missing addr.");
      }
      if (p.enable.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " read port missing enable.");
      }
    } else {
      mi.write_ports.push_back(idx);
      if (p.addr.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " write port missing addr.");
      }
      if (p.enable.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " write port missing enable.");
      }
      if (p.din.is_invalid()) {
        fatal(ctx, "Memory node n_" + std::to_string(mi.nid) + " write port missing din.");
      }
    }
  }

  // Read-data output pin id follows the cgen convention (see cgen_verilog.cpp:
  // create_driver_pin(n_wr_ports + n_rd_pos)): dout pid = (total write ports) +
  // (read-port index in port order).  Consumers of a multi-read memory connect
  // to these per-port driver pins; the emitter binds n_<mem>_p<pid> to match.
  for (size_t k = 0; k < mi.read_ports.size(); ++k) {
    mi.ports[mi.read_ports[k]].driver_pid = static_cast<uint32_t>(mi.write_ports.size() + k);
  }

  // ---------------------------------------------------------------------------
  // Initialization / whole-array classification.  Deliberately AFTER the pin
  // walk, the port split and `addr_width`, so every diagnostic below can report
  // the real shape.
  // ---------------------------------------------------------------------------
  if (!mi.update_pin.is_invalid() || !mi.update_enable_pin.is_invalid() || !mi.bulk_reset_pin.is_invalid()) {
    // A WHOLE-ARRAY cell: one bulk next-state bus instead of N per-entry write
    // ports, and `reset` restores `init` at runtime (graph/cell.cpp:234-241).
    // None of that is modeled; `init` is a RUNTIME bus in this shape, not
    // contents, so it must not be mistaken for a ROM table.
    fatal(ctx, memory_policy_summary(mi)
                   + ". this is a WHOLE-ARRAY Memory cell (`update`/`update_enable`/`reset` driven): one bulk "
                     "next-state bus instead of per-entry write ports, and `init` carries the runtime reset-value "
                     "bus rather than contents. pass.lean models per-entry ports only.");
  }

  if (!mi.init_pin.is_invalid()) {
    // STRICT: only an immutable table becomes `memConst`.
    if (!mi.write_ports.empty()) {
      fatal(ctx, memory_policy_summary(mi)
                     + ". `init` is driven on a memory that also has " + std::to_string(mi.write_ports.size())
                     + " write port(s): that is RAM WITH INITIAL CONTENTS, which needs an initial-state constraint on "
                       "mutable memory state, not the immutable-table model. Refusing rather than dropping the "
                       "initial contents.");
    }
    if (!pin_is_const(mi.init_pin)) {
      fatal(ctx, memory_policy_summary(mi)
                     + ". `init` is driven by a NON-CONSTANT pin; contents must be comptime for the immutable-table "
                       "model.");
    }
    if (mi.type == 2) {
      // encode.cpp:3466 models a type-2 array's init as a per-cycle BASE value
      // that writes then override -- combinational-array semantics, not a
      // persistent ROM.  With no write ports the two coincide, but saying so
      // here would bake an unproven coincidence into the model.
      fatal(ctx, memory_policy_summary(mi)
                     + ". `init` on a type=2 COMBINATIONAL array is a per-cycle base value that writes override "
                       "(pass/lec/encode.cpp:3466), which is not the persistent-ROM model. Refusing pending "
                       "combinational-array semantics.");
    }
    if (!(mi.type == 0 || mi.type == 1)) {
      fatal(ctx, memory_policy_summary(mi) + ". `init` on unsupported memory type " + std::to_string(mi.type) + ".");
    }

    const auto iv = pin_const_value(mi.init_pin);
    if (iv.has_unknowns()) {
      fatal(ctx, memory_policy_summary(mi) + ". `init` has X/Z bits; the strict certificate rejects four-valued contents.");
    }

    // Arbitrary-width unpack.  `init` is `size*bits` wide -- 1600 bits for a
    // 64x25 table, 6656 for 256x26 -- so NEVER `to_just_i64()` (it asserts above
    // 62 bits, hlop/dlop.cpp:2667).  Row-major, entry 0 in the low `bits`
    // (graph/cell.cpp:235); same recipe as cgen_verilog.cpp:1430-1433.
    const auto mask = Dlop::get_mask_value(static_cast<int>(mi.bits));
    mi.rom_contents.reserve(static_cast<size_t>(mi.size));
    for (uint64_t i = 0; i < mi.size; ++i) {
      auto entry = iv.sra_op(*Dlop::create_integer(static_cast<int64_t>(i) * static_cast<int64_t>(mi.bits)))->and_op(*mask);
      if (entry->is_known_zero()) {
        mi.rom_contents.emplace_back("0");
      } else if (entry->is_just_i64()) {
        mi.rom_contents.emplace_back(std::to_string(entry->to_just_i64()));
      } else {
        mi.rom_contents.emplace_back(entry->to_decimal_string());
      }
    }

    // Self-check: REPACK every entry and compare against the hydrated constant,
    // truncated to size*bits.  `compileDesign_correct` says the compiled program
    // means what the DesignCert says -- it cannot know whether this C++ unpacked
    // the table correctly, so a wrong extraction would be PROVED CORRECT against
    // the wrong contents.  Checked over every entry, not sampled.
    {
      auto repacked = Dlop::create_integer(0);
      for (uint64_t i = 0; i < mi.size; ++i) {
        auto entry = iv.sra_op(*Dlop::create_integer(static_cast<int64_t>(i) * static_cast<int64_t>(mi.bits)))->and_op(*mask);
        repacked = repacked->or_op(*entry->shl_op(*Dlop::create_integer(static_cast<int64_t>(i) * static_cast<int64_t>(mi.bits))));
      }
      const auto total = Dlop::get_mask_value(static_cast<int>(mi.size * mi.bits));
      const auto want  = iv.and_op(*total);
      if (!repacked->and_op(*total)->same_repr(*want)) {
        fatal(ctx, memory_policy_summary(mi)
                       + ". INTERNAL: ROM contents failed the repack self-check -- the unpacked entries do not "
                         "reassemble into the `init` constant. Refusing rather than emitting a table that would be "
                         "proved correct against the wrong contents.");
      }
    }

    mi.is_rom = true;
  }

  // type 0/2 = async/array (combinational read); type 1 = sync-read (registered
  // read data, modeled with a read-data register field + sram_sync_read_reg_next).
  if (!(mi.type == 0 || mi.type == 1 || mi.type == 2)) {
    fatal(ctx, memory_policy_summary(mi) + ". pass.lean memory supports async/array (type 0/2) and sync-read (type 1) only.");
  }
  if (mi.undef) {
    fatal(ctx,
          memory_policy_summary(mi)
              + ". pass.lean cannot model ordering=\"none\" (the `undef` matrix): sram_1r1w_{read,write}_first only express "
                "the two DEFINED collision answers, so the certificate would assert a value in a window the emitted RTL "
                "leaves x. Use ordering=\"old\"/\"fwd\"/\"program\" to export this design.");
  }
  // The read-during-write policy is only OBSERVABLE when some read can collide
  // with some write.  Where it is observable it must be known exactly: guessing
  // read-first would export a proof about a different design.
  if (!mi.read_ports.empty() && !mi.write_ports.empty()) {
    if (!mi.fwd_known) {
      fatal(ctx,
            memory_policy_summary(mi)
                + ". the `fwd` read-during-write matrix is not a readable constant, and this memory has both read and write "
                  "ports, so the policy is observable. Refusing rather than assuming read-first.");
    }
    const auto cells = static_cast<uint64_t>(mi.read_ports.size()) * static_cast<uint64_t>(mi.write_ports.size());
    if (cells > 62) {
      fatal(ctx,
            memory_policy_summary(mi) + ". the `fwd` matrix needs " + std::to_string(cells)
                + " bits (reads x writes), which does not fit the i64 window used to read it.");
    }
  }
  mi.sync = (mi.type == 1);

  return mi;
}

const Memory_info& memory_info_for(const LeanCtx& ctx, const Node& node) {
  auto it = ctx.memory_info.find(node_id(node));
  if (it == ctx.memory_info.end()) {
    throw Emit_error("internal: memory node n_" + std::to_string(node_id(node)) + " has no Memory_info.");
  }
  return it->second;
}

uint32_t minimal_unsigned_const_width(const Dlop& v) {
  if (!v.is_just_i64()) {
    return std::max<uint32_t>(1, static_cast<uint32_t>(v.get_bits()));
  }
  const int64_t iv = v.to_just_i64();
  if (iv <= 0) {
    return 1;
  }
  auto     uv   = static_cast<uint64_t>(iv);
  uint32_t bits = 0;
  while (uv != 0) {
    ++bits;
    uv >>= 1;
  }
  return std::max<uint32_t>(1, bits);
}

size_t memory_read_ordinal(const Memory_info& mi, size_t port_idx) {
  for (size_t k = 0; k < mi.read_ports.size(); ++k) {
    if (mi.read_ports[k] == port_idx) {
      return k;
    }
  }
  return 0;
}

bool memory_fwd_bit(const Memory_info& mi, size_t r_ord, size_t w_ord) {
  const auto idx = r_ord * mi.write_ports.size() + w_ord;
  if (idx >= 63) {
    return false;  // unreachable: parse refuses a matrix this wide
  }
  return ((mi.fwd >> idx) & 1) != 0;
}
}  // namespace lean_pass
