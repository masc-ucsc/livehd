// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "memory_lowering.hpp"

#include <algorithm>

#include "certificate_builder.hpp"

namespace lean_export::detail {
namespace {
uint32_t resize(CertificateBuilder& b, const PinRef& pin, uint32_t width) {
  const auto id = b.dep(pin, width);
  return b.emit({Operation::Or, 0, {}}, width, {id});
}

// Share chains by the set of writes forwarded to each read port. A later
// write wins a collision; the all-writes chain becomes the next image.
uint32_t chain(CertificateBuilder& b, const Memory& m, MemoryLowering& st, const std::vector<size_t>& ords) {
  if (const auto it = st.chains.find(ords); it != st.chains.end()) {
    return it->second;
  }
  auto current = st.image;
  for (auto ordinal : ords) {
    const auto& p      = m.ports.at(m.write_ports.at(ordinal));
    const auto  addr   = resize(b, p.addr, m.addr_width);
    const auto  data   = resize(b, p.din, m.bits);
    const auto  enable = resize(b, p.enable, std::max<uint32_t>(1, m.wensize));
    const Op    op     = m.wensize <= 1 ? Op{Operation::MemWrite, 0, {}} : Op{Operation::MemWriteBE, m.bits / m.wensize, {}};
    current            = b.emit(op, m.bits, {current, addr, data, enable});
  }
  st.chains.emplace(ords, current);
  return current;
}

std::vector<size_t> forwarded_to(const Memory& m, size_t r) {
  std::vector<size_t> forwarded;
  for (size_t w = 0; w < m.write_ports.size(); ++w) {
    // Per-read row lookup.  This was a `>> (r * n_wr + w)` on a uint64, which
    // was undefined once the matrix passed 63 cells -- the very shape a wide
    // memory has.  Out-of-range reads as read-first and never invents a
    // forward; `forwarded` stays in ascending write-ordinal order, so the
    // chain above still lets the LATER write win a collision.
    if (r < m.fwd_rows.size() && w < m.fwd_rows[r].size() && m.fwd_rows[r][w]) {
      forwarded.push_back(w);
    }
  }
  return forwarded;
}
}  // namespace

void lower_memory_reads(CertificateBuilder& b, const Memory& m) {
  auto& st = b.memory_state[m.id];
  Source image;
  image.id           = b.next_id++;
  image.kind         = m.is_rom ? SourceKind::RomConst : SourceKind::MemImage;
  image.width        = m.bits;
  image.addr_w       = m.addr_width;
  image.ordinal      = m.id;  // resolved to runtime ordinal once all memories exist
  image.rom_contents = m.rom_contents;
  b.sources.emplace(image.id, image);
  st.image = image.id;
  for (size_t r = 0; r < m.read_ports.size(); ++r) {
    const auto& p   = m.ports.at(m.read_ports[r]);
    const auto  key = (static_cast<uint64_t>(m.id) << 32) | p.driver_pid;
    if (m.sync) {
      // A REGISTERED read presents last cycle's value, so the only thing it
      // contributes here is the register itself. Its next-state cone -- the
      // address, the enable and the read of the forwarded image -- is built in
      // phase 2, where it may name any node, exactly like a flop's `din`.
      Source reg;
      reg.id    = b.next_id++;
      reg.kind  = SourceKind::Flop;
      reg.width = m.bits;
      b.sources.emplace(reg.id, reg);
      st.read_registers.emplace(r, reg.id);
      b.memory_reads.emplace(key, reg.id);
    } else {
      const auto base   = chain(b, m, st, forwarded_to(m, r));
      const auto addr   = resize(b, p.addr, m.addr_width);
      const auto enable = resize(b, p.enable, b.pin_width(p.enable));
      const auto read   = b.emit({Operation::MemRead, 0, {}}, m.bits, {base, addr, enable});
      b.memory_reads.emplace(key, read);
    }
  }
}

void lower_memory_writes(CertificateBuilder& b, const Memory& m) {
  auto& st = b.memory_state.at(m.id);
  for (size_t r = 0; r < m.read_ports.size(); ++r) {
    const auto it = st.read_registers.find(r);
    if (it == st.read_registers.end()) {
      continue;
    }
    const auto& p      = m.ports.at(m.read_ports[r]);
    const auto  base   = chain(b, m, st, forwarded_to(m, r));
    const auto  addr   = resize(b, p.addr, m.addr_width);
    Source      one;
    one.id              = b.next_id++;
    one.kind            = SourceKind::Const;
    one.width           = 1;
    one.const_int       = "1";
    one.implicit_enable = true;
    b.sources.emplace(one.id, one);
    const auto raw    = b.emit({Operation::MemRead, 0, {}}, m.bits, {base, addr, one.id});
    const auto enable = resize(b, p.enable, b.pin_width(p.enable));
    const auto next   = b.emit({Operation::MuxBool, 0, {}}, m.bits, {enable, it->second, raw});
    FlopDriver f;
    f.origin    = m.id;
    f.read_port = p.port_id;
    f.width     = m.bits;
    f.din       = next;
    // A SYNTHETIC read flop inherits the domain of the port whose registered
    // read it models -- not the memory's, which may differ only because the
    // scan's common-domain check allows one shared answer. It has no reset at
    // all, so it certainly has no ASYNCHRONOUS one; saying so explicitly keeps
    // it from inheriting a default that would make it commit without an edge.
    f.clock       = p.has_domain ? p.clock_ordinal : m.clock_ordinal;
    f.async_reset = false;
    b.read_registers.emplace(it->second, f);
  }
  std::vector<size_t> all;
  for (size_t w = 0; w < m.write_ports.size(); ++w) {
    all.push_back(w);
  }
  const auto next = chain(b, m, st, all);
  if (!m.is_rom) {
    b.memory_drivers.emplace(m.id, MemoryDriver{m.addr_width, m.bits, next, m.id, m.clock_ordinal});
  }
}
}  // namespace lean_export::detail
