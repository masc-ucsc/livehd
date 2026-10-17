// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The C++ side of DCERT2 parity.
//
// Three different things are checked here, and they are NOT the same check:
//
//   1. GOLDEN BYTES.  The exact byte string for a fixture that exercises every
//      source kind, a variable-length dependency run, non-default flop and
//      memory clocks, an asynchronous reset, arbitrary-precision decimal
//      payloads and four clock names -- empty, plain, one containing a space
//      AND a newline, and one multi-byte UTF-8.  A format change that is not
//      deliberate fails here.
//   2. AN INDEPENDENT READER.  A parser written in this file, not sharing a
//      line with the serialiser, reconstructs the record and is compared field
//      by field against the source `CertificateIR`.  A matching bug in one
//      direction would have to be reproduced here to pass.
//   3. REFUSALS.  Malformed decimals and unresolvable ids are errors, not
//      guesses.
//   4. A FAILING STREAM.  Production staging writes to a stream with
//      exceptions enabled, so a write failure part way through a record must
//      PROPAGATE -- not terminate the process. A green golden-byte fixture
//      does not cover this: the same binary that aborts at a 12-byte cap
//      exits 0 at 128.
//
// What is NOT here: agreement with the LEAN reader. That is cross-language and
// lives in `scripts/dcert_parity.sh`, which runs the verified `parseCert` over
// bytes this code produced.
#include "emit_dcert.hpp"

#include <sstream>
#include <streambuf>
#include <stdexcept>

#include "emit_design_cert.hpp"
#include "gtest/gtest.h"

namespace lean_export {
namespace {

// A certificate carrying every feature the wire format has to express.
CertificateIR full_fixture() {
  CertificateIR c;

  Source in;
  in.id      = 10;
  in.kind    = SourceKind::Input;
  in.ordinal = 0;
  in.width   = 8;
  c.sources.push_back(in);

  Source k;
  k.id        = 11;
  k.kind      = SourceKind::Const;
  k.width     = 16;
  k.const_int = "-00042";  // normalised to -42
  c.sources.push_back(k);

  Source en;
  en.id              = 12;
  en.kind            = SourceKind::Const;
  en.width           = 1;
  en.const_int       = "99";  // overridden by implicit_enable
  en.implicit_enable = true;
  c.sources.push_back(en);

  Source q;
  q.id      = 13;
  q.kind    = SourceKind::Flop;
  q.ordinal = 1;
  q.width   = 8;
  c.sources.push_back(q);

  Source qa;
  qa.id               = 14;
  qa.kind             = SourceKind::Flop;
  qa.ordinal          = 2;
  qa.width            = 4;
  qa.async_reset      = true;
  qa.reset_input      = 3;
  qa.reset_value      = "-7";
  qa.reset_active_low = true;
  c.sources.push_back(qa);

  Source mi;
  mi.id      = 15;
  mi.kind    = SourceKind::MemImage;
  mi.ordinal = 5;
  mi.addr_w  = 3;
  mi.width   = 16;
  c.sources.push_back(mi);

  Source rom;
  rom.id     = 16;
  rom.kind   = SourceKind::RomConst;
  rom.addr_w = 2;
  rom.width  = 8;
  // Arbitrary precision: the IR carries decimal STRINGS and the format does
  // too, so a 128-bit constant is not a special case.
  rom.rom_contents = {"0", "255", "-1", "0007", "340282366920938463463374607431768211455",
                      "-340282366920938463463374607431768211456"};
  c.sources.push_back(rom);

  CertNode sum;
  sum.id           = 200;
  sum.op.kind      = Operation::Sum;
  sum.op.parameter = 3;
  sum.width        = 8;
  sum.deps         = {10, 11};
  c.nodes.push_back(sum);

  CertNode wbe;
  wbe.id           = 201;
  wbe.op.kind      = Operation::MemWriteBE;
  wbe.op.parameter = 4;
  wbe.width        = 16;
  wbe.deps         = {15, 10, 200};
  c.nodes.push_back(wbe);

  CertNode kn;
  kn.id        = 202;
  kn.op.kind   = Operation::Const;
  kn.op.value  = "-123";
  kn.width     = 12;
  c.nodes.push_back(kn);

  c.outputs.push_back({202, 12});
  c.outputs.push_back({10, 8});

  FlopDriver f0;
  f0.width = 8;
  f0.din   = 200;
  c.flops.push_back(f0);

  FlopDriver f1;
  f1.width            = 4;
  f1.din              = 11;
  f1.enable           = 12;
  f1.reset_pin        = 13;
  f1.reset_value      = "-00005";  // normalised to -5
  f1.reset_active_low = true;
  f1.clock            = 2;
  f1.async_reset      = true;
  c.flops.push_back(f1);

  MemoryDriver m;
  m.addr_w   = 3;
  m.data_w   = 16;
  m.next_img = 201;
  m.clock    = 1;
  c.memories.push_back(m);

  c.clocks = {{"clock"}, {""}, {"two words\nand a newline"}, {"clk_\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}};

  index_certificate(c);
  return c;
}

const char* const kGolden =
    "DCERT2\n"
    "7\n"
    "0 0 8\n"
    "1 16 -42\n"
    "1 1 1\n"
    "2 1 8\n"
    "3 2 4 3 -7 1\n"
    "4 5 3 16\n"
    "5 2 8 6 0 255 -1 7 340282366920938463463374607431768211455 "
    "-340282366920938463463374607431768211456\n"
    "3\n"
    "1 3 8 2 0 1 200\n"
    "28 4 16 3 5 0 7 201\n"
    "0 -123 12 0 202\n"
    "2\n"
    "9 12\n"
    "0 8\n"
    "2\n"
    "8 7 0 0 0 0 0 0 0 0\n"
    "4 1 1 2 1 3 -5 1 2 1\n"
    "1\n"
    "3 16 8 1\n"
    "4\n"
    "5 clock\n"
    "0 \n"
    "23 two words\nand a newline\n"
    "13 clk_\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\n";

// ---------------------------------------------------------------------------
// An INDEPENDENT reader.  Deliberately written against the format description
// rather than against `render_dcert`, so it is a second opinion and not an
// inverse of the same code.
// ---------------------------------------------------------------------------
class Reader {
public:
  explicit Reader(std::string_view text) : text_(text) {}

  void expect_magic() {
    ASSERT_EQ(text_.substr(0, 7), "DCERT2\n");
    at_ = 7;
  }
  // Numbers are whitespace-delimited; a leading '-' is part of the number.
  std::string number() {
    while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n')) {
      ++at_;
    }
    size_t start = at_;
    if (at_ < text_.size() && text_[at_] == '-') {
      ++at_;
    }
    while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
      ++at_;
    }
    EXPECT_GT(at_, start) << "no number at offset " << start;
    return std::string(text_.substr(start, at_ - start));
  }
  uint64_t count() { return std::stoull(number()); }
  // A name is the ONE field taken by count: digits, exactly one space, then
  // that many raw bytes -- no scanning, no unescaping.
  std::string name() {
    size_t n = count();
    EXPECT_LT(at_, text_.size());
    EXPECT_EQ(text_[at_], ' ') << "clock name separator missing";
    ++at_;
    std::string out(text_.substr(at_, n));
    at_ += n;
    return out;
  }
  bool at_end() const { return at_ >= text_.size(); }

private:
  std::string_view text_;
  size_t           at_ = 0;
};

TEST(DcertParity, GoldenBytes) {
  EXPECT_EQ(render_dcert(full_fixture()), kGolden);
}

TEST(DcertParity, IndependentReaderAgreesFieldByField) {
  const CertificateIR c    = full_fixture();
  const std::string   text = render_dcert(c);
  Reader              r(text);
  r.expect_magic();

  ASSERT_EQ(r.count(), c.sources.size());
  for (const auto& s : c.sources) {
    uint64_t tag = r.count();
    switch (s.kind) {
      case SourceKind::Input:
        EXPECT_EQ(tag, 0u);
        EXPECT_EQ(r.count(), s.ordinal);
        EXPECT_EQ(r.count(), s.width);
        break;
      case SourceKind::Const:
        EXPECT_EQ(tag, 1u);
        EXPECT_EQ(r.count(), s.width);
        EXPECT_EQ(r.number(), s.implicit_enable ? "1" : canonical_decimal(s.const_int));
        break;
      case SourceKind::Flop:
        EXPECT_EQ(tag, s.async_reset ? 3u : 2u);
        EXPECT_EQ(r.count(), s.ordinal);
        EXPECT_EQ(r.count(), s.width);
        if (s.async_reset) {
          EXPECT_EQ(r.count(), s.reset_input);
          EXPECT_EQ(r.number(), canonical_decimal(s.reset_value));
          EXPECT_EQ(r.count(), s.reset_active_low ? 1u : 0u);
        }
        break;
      case SourceKind::MemImage:
        EXPECT_EQ(tag, 4u);
        EXPECT_EQ(r.count(), s.ordinal);
        EXPECT_EQ(r.count(), s.addr_w);
        EXPECT_EQ(r.count(), s.width);
        break;
      case SourceKind::RomConst:
        EXPECT_EQ(tag, 5u);
        EXPECT_EQ(r.count(), s.addr_w);
        EXPECT_EQ(r.count(), s.width);
        ASSERT_EQ(r.count(), s.rom_contents.size());
        for (const auto& v : s.rom_contents) {
          EXPECT_EQ(r.number(), canonical_decimal(v));
        }
        break;
    }
  }

  ASSERT_EQ(r.count(), c.nodes.size());
  for (const auto& n : c.nodes) {
    EXPECT_EQ(r.count(), dcert_opcode(n.op));
    EXPECT_EQ(r.number(), dcert_oparg(n.op));
    EXPECT_EQ(r.count(), n.width);
    ASSERT_EQ(r.count(), n.deps.size());
    for (auto dep : n.deps) {
      EXPECT_EQ(r.count(), c.slot_of.at(dep));
    }
    EXPECT_EQ(r.count(), n.id) << "origin is the LGraph id, not the slot";
  }

  ASSERT_EQ(r.count(), c.outputs.size());
  for (const auto& o : c.outputs) {
    EXPECT_EQ(r.count(), c.slot_of.at(o.id));
    EXPECT_EQ(r.count(), o.width);
  }

  ASSERT_EQ(r.count(), c.flops.size());
  for (const auto& f : c.flops) {
    EXPECT_EQ(r.count(), f.width);
    EXPECT_EQ(r.count(), c.slot_of.at(f.din));
    EXPECT_EQ(r.count(), f.enable ? 1u : 0u);
    EXPECT_EQ(r.count(), f.enable ? c.slot_of.at(*f.enable) : 0u);
    EXPECT_EQ(r.count(), f.reset_pin ? 1u : 0u);
    EXPECT_EQ(r.count(), f.reset_pin ? c.slot_of.at(*f.reset_pin) : 0u);
    EXPECT_EQ(r.number(), canonical_decimal(f.reset_value));
    EXPECT_EQ(r.count(), f.reset_active_low ? 1u : 0u);
    EXPECT_EQ(r.count(), f.clock);
    EXPECT_EQ(r.count(), f.async_reset ? 1u : 0u);
  }

  ASSERT_EQ(r.count(), c.memories.size());
  for (const auto& m : c.memories) {
    EXPECT_EQ(r.count(), m.addr_w);
    EXPECT_EQ(r.count(), m.data_w);
    EXPECT_EQ(r.count(), c.slot_of.at(m.next_img));
    EXPECT_EQ(r.count(), m.clock);
  }

  ASSERT_EQ(r.count(), c.clocks.size());
  for (const auto& ck : c.clocks) {
    EXPECT_EQ(r.name(), ck.name);
  }
  // Exactly one trailing newline after the last record, nothing else.
  EXPECT_EQ(text.size(), text.find_last_of('\n') + 1);
}

TEST(DcertParity, ClockNameLengthIsBytesNotCharacters) {
  CertificateIR c;
  c.clocks = {{"clk_\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}};
  index_certificate(c);
  // 4 ASCII + three 3-byte code points.
  EXPECT_NE(render_dcert(c).find("\n13 clk_"), std::string::npos);
}

TEST(DcertParity, ClockMetadataIsCarriedNotDefaulted) {
  CertificateIR c    = full_fixture();
  const std::string a = render_dcert(c);
  c.flops[1].clock    = 0;  // the only change
  const std::string b = render_dcert(c);
  EXPECT_NE(a, b) << "a flop's clock ordinal does not reach the certificate";

  CertificateIR d      = full_fixture();
  d.memories[0].clock  = 0;
  EXPECT_NE(render_dcert(d), a) << "a memory's clock ordinal does not reach the certificate";

  CertificateIR e       = full_fixture();
  e.flops[1].async_reset = false;
  EXPECT_NE(render_dcert(e), a) << "asyncReset does not reach the certificate";

  CertificateIR f = full_fixture();
  f.clocks.push_back({"second"});
  EXPECT_NE(render_dcert(f), a) << "the clock table does not reach the certificate";
}

TEST(DcertParity, DecimalsAreCanonicalisedNotGuessed) {
  EXPECT_EQ(canonical_decimal("0"), "0");
  EXPECT_EQ(canonical_decimal("000"), "0");
  EXPECT_EQ(canonical_decimal("-0"), "0");  // Lean has no negative zero
  EXPECT_EQ(canonical_decimal("-000"), "0");
  EXPECT_EQ(canonical_decimal("007"), "7");
  EXPECT_EQ(canonical_decimal("-007"), "-7");
  EXPECT_EQ(canonical_decimal("340282366920938463463374607431768211455"),
            "340282366920938463463374607431768211455");
  EXPECT_THROW(canonical_decimal(""), std::invalid_argument);
  EXPECT_THROW(canonical_decimal("-"), std::invalid_argument);
  EXPECT_THROW(canonical_decimal("+3"), std::invalid_argument);
  EXPECT_THROW(canonical_decimal("12a"), std::invalid_argument);
  EXPECT_THROW(canonical_decimal("0x10"), std::invalid_argument);
  EXPECT_THROW(canonical_decimal(" 1"), std::invalid_argument);
}

TEST(DcertParity, UnresolvableIdIsRefused) {
  CertificateIR c = full_fixture();
  c.outputs.push_back({9999, 8});  // never declared
  EXPECT_THROW(render_dcert(c), std::invalid_argument);
}

TEST(DcertParity, EmptyCertificateStillStatesEverySection) {
  CertificateIR c;  // default: no records, one clock named "clock"
  index_certificate(c);
  EXPECT_EQ(render_dcert(c), "DCERT2\n0\n0\n0\n0\n0\n1\n5 clock\n");
}

// The two emitters walk the same IR; where they both carry a field, they must
// say the same thing. Checked on the values most likely to drift -- the ones
// DCERT2 added.
TEST(DcertParity, AgreesWithTheLeanLiteralEmitterOnClockMetadata) {
  const CertificateIR c = full_fixture();
  std::ostringstream  literal;
  emit_design_cert("fixture", c, literal);
  const std::string lit = literal.str();

  EXPECT_NE(lit.find("clock := 2, asyncReset := true"), std::string::npos);
  EXPECT_NE(lit.find("clock := 0, asyncReset := false"), std::string::npos);
  EXPECT_NE(lit.find("nextImg := 8, clock := 1"), std::string::npos);
  EXPECT_NE(lit.find("{ name := \"clock\" }"), std::string::npos);
  EXPECT_NE(lit.find("{ name := \"\" }"), std::string::npos);
  // The literal emitter ESCAPES; DCERT2 does not, and carries the raw bytes.
  EXPECT_NE(lit.find("two words\\nand a newline"), std::string::npos);
  EXPECT_NE(render_dcert(c).find("two words\nand a newline"), std::string::npos);
}

// ---------------------------------------------------------------------------
// A stream that fails after a fixed number of bytes, with exceptions enabled --
// the shape `publish_group` stages through.
//
// This is a regression control for a real defect: `Rec` used to emit the
// record's terminating newline from its DESTRUCTOR. When a write threw part
// way through a record, that destructor ran during unwinding and threw again
// out of an implicitly `noexcept` destructor, so the process died with
// SIGABRT and `publish_group`'s catch, rollback and scrub never ran.
// Reproduced independently at a 12-byte cap: exit -6, "terminate called after
// throwing an instance of 'std::__ios_failure'".
// ---------------------------------------------------------------------------
class CappedBuf : public std::streambuf {
public:
  explicit CappedBuf(size_t limit) : limit_(limit) {}
  size_t written() const { return written_; }

protected:
  int overflow(int ch) override {
    if (ch == traits_type::eof()) {
      return traits_type::not_eof(ch);
    }
    if (written_ >= limit_) {
      return traits_type::eof();  // the stream sets badbit and, with
                                  // exceptions enabled, throws
    }
    ++written_;
    return ch;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override {
    std::streamsize k = 0;
    while (k < n) {
      if (overflow(traits_type::to_int_type(s[k])) == traits_type::eof()) {
        break;
      }
      ++k;
    }
    return k;
  }

private:
  size_t limit_   = 0;
  size_t written_ = 0;
};

TEST(DcertStreamFailure, HealthyStreamWritesEverything) {
  const CertificateIR c    = full_fixture();
  const std::string   want = render_dcert(c);
  CappedBuf           buf(want.size() + 1024);
  std::ostream        os(&buf);
  os.exceptions(std::ios::badbit | std::ios::failbit);
  EXPECT_NO_THROW(render_dcert(c, os));
  EXPECT_EQ(buf.written(), want.size());
}

TEST(DcertStreamFailure, MidRecordFailurePropagatesInsteadOfTerminating) {
  const CertificateIR c = full_fixture();
  // 12 bytes is part way through the first source record -- the exact cap the
  // independent control used to reproduce the abort.
  for (size_t limit : {size_t{0}, size_t{7}, size_t{12}, size_t{40}, size_t{200}}) {
    CappedBuf    buf(limit);
    std::ostream os(&buf);
    os.exceptions(std::ios::badbit | std::ios::failbit);
    // The requirement is an ORDINARY exception reaching the caller. If the
    // destructor ever writes again, this test does not fail -- the process
    // aborts and the whole binary does, which is the louder signal.
    EXPECT_THROW(render_dcert(c, os), std::ios_base::failure) << "limit " << limit;
    EXPECT_LE(buf.written(), limit);
  }
}

TEST(DcertStreamFailure, WriteDcertReportsAnUnopenablePath) {
  // A directory is never a writable file; the serialiser says so rather than
  // leaving a caller believing an artifact was published.
  EXPECT_THROW(write_dcert(full_fixture(), "/tmp"), std::runtime_error);
}

}  // namespace
}  // namespace lean_export
