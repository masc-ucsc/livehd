// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_dcert.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lean_export {
namespace {

// One space between fields WITHIN a record, one newline after each record and
// after each section count.  Mirrors `spc` / `chNL` in the Lean encoder; the
// reader skips whitespace before a number, so the framing is advisory for
// every field EXCEPT a clock name, whose separator is checked and whose bytes
// are taken by count.
//
// NO I/O IN A DESTRUCTOR, and the record is assembled before any of it is
// written.
//
// The first version held an `ostream&` and emitted the terminating newline
// from `~Rec`. `publish_group` stages to streams with exceptions ENABLED, so a
// write failure part way through a record threw, and the destructor then wrote
// the newline while that exception was unwinding -- a second throw out of an
// implicitly `noexcept` destructor, which is `std::terminate`. Measured
// against an independently built `streambuf` capped at 12 bytes: SIGABRT
// (exit -6, `terminate called after throwing an instance of
// 'std::__ios_failure'`), so `publish_group`'s catch, rollback and scrub never
// ran -- the process was gone. At a 128-byte cap the same binary exits 0, so a
// green golden-byte fixture says nothing about this path.
//
// Now a record is built in a `std::string` and handed to the stream in one
// `write`, by ordinary control flow. A failing write throws from the call site
// where it can be caught, nothing is destroyed mid-write, and there is no
// second exception to terminate on. `dcert_stream_failure` in
// `emit_dcert_test.cpp` is the permanent control.
class Rec {
public:
  void field(std::string_view s) {
    if (!first_) {
      buf_ += ' ';
    }
    first_ = false;
    buf_ += s;
  }
  void field(uint32_t n) { field(std::to_string(n)); }
  void field(size_t n) { field(std::to_string(n)); }
  // Raw bytes, no separator: a clock name is taken by count, not scanned.
  void raw(std::string_view s) { buf_ += s; }
  // The terminating newline, written where a failure can propagate normally.
  void finish(std::ostream& os) {
    buf_ += '\n';
    os.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
  }

private:
  std::string buf_;
  bool        first_ = true;
};

void section_count(std::ostream& os, size_t n) { os << n << '\n'; }

uint32_t slot(const CertificateIR& c, uint32_t id) {
  auto it = c.slot_of.find(id);
  if (it == c.slot_of.end()) {
    // `index_certificate` assigns every referenced id a dense slot. A missing
    // one is a BUILDER defect, and emitting some placeholder would hand the
    // Lean side a certificate about a different design -- which the round-trip
    // theorem would then faithfully preserve.
    throw std::invalid_argument("dcert: no slot for id " + std::to_string(id));
  }
  return it->second;
}

}  // namespace

std::string canonical_decimal(std::string_view value) {
  if (value.empty()) {
    throw std::invalid_argument("dcert: empty decimal integer");
  }
  bool negative = value.front() == '-';
  std::string_view digits = negative ? value.substr(1) : value;
  if (digits.empty()) {
    throw std::invalid_argument("dcert: decimal integer is a bare sign");
  }
  for (char ch : digits) {
    if (ch < '0' || ch > '9') {
      throw std::invalid_argument("dcert: not a decimal integer: " + std::string(value));
    }
  }
  size_t lead = digits.find_first_not_of('0');
  if (lead == std::string_view::npos) {
    return "0";  // "0", "000" and "-0" all render as Lean's `Int.ofNat 0`.
  }
  digits.remove_prefix(lead);
  return negative ? "-" + std::string(digits) : std::string(digits);
}

uint32_t dcert_opcode(const Op& op) {
  // The numbering is `Compiler.CertIO.opCode`'s, which covers 29 operators.
  // `Operation` names 24 of them: the LGraph front end never builds Sub (2),
  // Div (4), SDiv (6), LT (12) or GT (13), so those codes are reachable only
  // from a hand-written certificate. Listed here as a comment rather than as
  // dead cases, so the switch stays exhaustive over `Operation`.
  switch (op.kind) {
    case Operation::Const     : return 0;
    case Operation::Sum       : return 1;
    case Operation::Mult      : return 3;
    case Operation::UDiv      : return 5;
    case Operation::And       : return 7;
    case Operation::Or        : return 8;
    case Operation::Xor       : return 9;
    case Operation::Ror       : return 10;
    case Operation::Not       : return 11;
    case Operation::ULT       : return 14;
    case Operation::UGT       : return 15;
    case Operation::SLT       : return 16;
    case Operation::SGT       : return 17;
    case Operation::EQ        : return 18;
    case Operation::SHL       : return 19;
    case Operation::SRA       : return 20;
    case Operation::MuxBool   : return 21;
    case Operation::MuxN      : return 22;
    case Operation::Sext      : return 23;
    case Operation::GetMask   : return 24;
    case Operation::SetMask   : return 25;
    case Operation::MemRead   : return 26;
    case Operation::MemWrite  : return 27;
    case Operation::MemWriteBE: return 28;
  }
  throw std::invalid_argument("dcert: unknown certificate operation");
}

std::string dcert_oparg(const Op& op) {
  switch (op.kind) {
    case Operation::Const     : return canonical_decimal(op.value);
    case Operation::Sum       : return std::to_string(op.parameter);
    case Operation::MemWriteBE: return std::to_string(op.parameter);
    default                   : return "0";
  }
}

void render_dcert(const CertificateIR& d, std::ostream& os) {
  os << "DCERT2\n";

  section_count(os, d.sources.size());
  for (const auto& s : d.sources) {
    Rec r;
    switch (s.kind) {
      case SourceKind::Input:
        r.field(0u);
        r.field(s.ordinal);
        r.field(s.width);
        break;
      case SourceKind::Const:
        r.field(1u);
        r.field(s.width);
        // `implicit_enable` overrides the stored constant with 1 -- the same
        // substitution `emit_design_cert` makes, kept identical here on
        // purpose: the two emitters must describe the SAME design.
        r.field(s.implicit_enable ? std::string("1") : canonical_decimal(s.const_int));
        break;
      case SourceKind::Flop:
        if (s.async_reset) {
          r.field(3u);
          r.field(s.ordinal);
          r.field(s.width);
          r.field(s.reset_input);
          r.field(canonical_decimal(s.reset_value));
          r.field(s.reset_active_low ? 1u : 0u);
        } else {
          r.field(2u);
          r.field(s.ordinal);
          r.field(s.width);
        }
        break;
      case SourceKind::MemImage:
        r.field(4u);
        r.field(s.ordinal);
        r.field(s.addr_w);
        r.field(s.width);
        break;
      case SourceKind::RomConst:
        r.field(5u);
        r.field(s.addr_w);
        r.field(s.width);
        r.field(s.rom_contents.size());
        for (const auto& v : s.rom_contents) {
          r.field(canonical_decimal(v));
        }
        break;
    }
    r.finish(os);
  }

  section_count(os, d.nodes.size());
  for (const auto& n : d.nodes) {
    Rec r;
    r.field(dcert_opcode(n.op));
    r.field(dcert_oparg(n.op));
    r.field(n.width);
    r.field(n.deps.size());
    for (auto dep : n.deps) {
      r.field(slot(d, dep));
    }
    // `origin` is the LGraph id, diagnostic only -- it is the LAST field
    // because the dependency run is variable length.
    r.field(n.id);
    r.finish(os);
  }

  section_count(os, d.outputs.size());
  for (const auto& o : d.outputs) {
    Rec r;
    r.field(slot(d, o.id));
    r.field(o.width);
    r.finish(os);
  }

  section_count(os, d.flops.size());
  for (const auto& f : d.flops) {
    Rec r;
    r.field(f.width);
    r.field(slot(d, f.din));
    // `hasEn`/`hasRst` keep the arity fixed, so the reader never branches on
    // shape; the slot beside an absent flag is 0 and is not read.
    r.field(f.enable ? 1u : 0u);
    r.field(f.enable ? slot(d, *f.enable) : 0u);
    r.field(f.reset_pin ? 1u : 0u);
    r.field(f.reset_pin ? slot(d, *f.reset_pin) : 0u);
    r.field(canonical_decimal(f.reset_value));
    r.field(f.reset_active_low ? 1u : 0u);
    r.field(f.clock);
    r.field(f.async_reset ? 1u : 0u);
    r.finish(os);
  }

  section_count(os, d.memories.size());
  for (const auto& m : d.memories) {
    Rec r;
    r.field(m.addr_w);
    r.field(m.data_w);
    r.field(slot(d, m.next_img));
    r.field(m.clock);
    r.finish(os);
  }

  // LAST, because it is the section DCERT2 ADDED.
  //
  // That is the only sense in which the order is inherited: the records above
  // are NOT byte-identical to DCERT1, because the flop record gained `clock`
  // and `asyncReset` and the memory record gained `clock`. A DCERT1 stream is
  // refused by magic, and would be unreadable here even if it were not.
  section_count(os, d.clocks.size());
  for (const auto& c : d.clocks) {
    // A name is the one field that is NOT whitespace-delimited: its UTF-8 byte
    // count, ONE space, then exactly those bytes, written raw. A name may
    // contain a space, a newline or any other byte, and the reader takes the
    // count without scanning -- so nothing in a name can be mistaken for
    // structure, and nothing is escaped or re-encoded.
    Rec r;
    r.field(c.name.size());
    r.raw(" ");
    r.raw(c.name);
    r.finish(os);
  }
}

std::string render_dcert(const CertificateIR& certificate) {
  std::ostringstream os;
  render_dcert(certificate, os);
  return os.str();
}

void write_dcert(const CertificateIR& certificate, const std::string& path) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os) {
    throw std::runtime_error("dcert: cannot open " + path);
  }
  render_dcert(certificate, os);
  os.flush();
  if (!os) {
    throw std::runtime_error("dcert: write failed for " + path);
  }
}

}  // namespace lean_export
