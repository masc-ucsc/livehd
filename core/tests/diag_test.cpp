//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "diag.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"

using livehd::diag::Diagnostic;
using livehd::diag::Severity;
using livehd::diag::Sink;
using livehd::diag::Span;

namespace {

Diagnostic make_error() {
  return Diagnostic{.severity = Severity::error,
                    .code     = "range-fit",
                    .category = "type",
                    .pass     = "upass.attributes",
                    .message  = "value -1 does not fit unsigned type u8 [0,255]",
                    .hint     = "use an explicit bit-select e#[0..=7]"};
}

}  // namespace

TEST(diag, emit_counts_and_records) {
  Sink s;
  s.set_jsonl_path("off");  // in-memory only; don't litter the sandbox
  s.emit(make_error());
  EXPECT_TRUE(s.has_errors());
  EXPECT_EQ(s.count(Severity::error), 1u);
  EXPECT_EQ(s.count(Severity::warning), 0u);
  ASSERT_EQ(s.records().size(), 1u);
  EXPECT_EQ(s.records()[0].code, "range-fit");
  EXPECT_EQ(s.records()[0].category, "type");
}

TEST(diag, warning_is_not_an_error) {
  Sink s;
  s.set_jsonl_path("off");
  Diagnostic w{.severity = Severity::warning, .code = "shadow", .category = "name", .pass = "inou.prp", .message = "shadowed name x"};
  s.emit(std::move(w));
  EXPECT_FALSE(s.has_errors());
  EXPECT_EQ(s.count(Severity::warning), 1u);
}

TEST(diag, writes_jsonl_to_file_by_default_path) {
  auto path = testing::TempDir() + "/diag_out.jsonl";
  std::remove(path.c_str());
  {
    Sink s;
    s.set_jsonl_path(path);  // explicit file destination (default would be diagnostics.jsonl)
    s.emit(make_error());
  }  // sink destructed; file already flushed per-line
  std::ifstream f(path);
  ASSERT_TRUE(f.good());
  std::string line;
  std::getline(f, line);
  EXPECT_NE(line.find("\"code\":\"range-fit\""), std::string::npos);
  EXPECT_NE(line.find("\"category\":\"type\""), std::string::npos);
  std::remove(path.c_str());
}

TEST(diag, jsonl_off_suppresses_file) {
  auto path = testing::TempDir() + "/diag_none.jsonl";
  std::remove(path.c_str());
  Sink s;
  s.set_jsonl_path("off");  // no JSON file
  s.set_human_stderr(false);
  s.emit(make_error());
  std::ifstream f(path);
  EXPECT_FALSE(f.good());            // file never created
  EXPECT_EQ(s.records().size(), 1u);  // but still accumulated in memory
}

TEST(diag, text_uses_livehd_prefix) {
  Diagnostic d = make_error();
  d.span       = Span{.file = "pp.prp", .start_line = 10, .start_col = 9};
  auto txt     = livehd::diag::to_text(d);
  // gcc/clang style: livehd:<file>:<line>:<col>:<severity>:<message>
  EXPECT_NE(txt.find("livehd:pp.prp:10:9:error:"), std::string::npos);
  EXPECT_NE(txt.find("livehd:pp.prp:10:9:help:"), std::string::npos);
  EXPECT_EQ(txt.find("error["), std::string::npos);  // old format gone
}

TEST(diag, jsonl_has_core_fields_and_null_span) {
  auto line = livehd::diag::to_jsonl(make_error(), 7);
  EXPECT_NE(line.find("\"schema_version\":1"), std::string::npos);
  EXPECT_NE(line.find("\"severity\":\"error\""), std::string::npos);
  EXPECT_NE(line.find("\"code\":\"range-fit\""), std::string::npos);
  EXPECT_NE(line.find("\"category\":\"type\""), std::string::npos);
  EXPECT_NE(line.find("\"pass\":\"upass.attributes\""), std::string::npos);
  EXPECT_NE(line.find("\"span\":null"), std::string::npos);
  EXPECT_NE(line.find("\"hint\":\"use an explicit bit-select e#[0..=7]\""), std::string::npos);
  EXPECT_NE(line.find("\"seq\":7"), std::string::npos);
  // single line
  EXPECT_EQ(line.find('\n'), std::string::npos);
}

TEST(diag, jsonl_escapes_quotes_and_emits_span_object) {
  Diagnostic d = make_error();
  d.message    = "bad \"quote\" and\nnewline";
  d.span       = Span{.file = "foo.prp", .start_byte = 120, .end_byte = 135, .start_line = 12, .start_col = 7};
  d.see        = {"docs/contracts/typesystem_clean_plan.md#T4"};
  auto line    = livehd::diag::to_jsonl(d, 1);
  EXPECT_NE(line.find("\\\"quote\\\""), std::string::npos);
  EXPECT_NE(line.find("\\n"), std::string::npos);
  EXPECT_NE(line.find("\"span\":{"), std::string::npos);
  EXPECT_NE(line.find("\"file\":\"foo.prp\""), std::string::npos);
  EXPECT_NE(line.find("\"start_byte\":120"), std::string::npos);
  EXPECT_NE(line.find("\"start_line\":12"), std::string::npos);
  EXPECT_NE(line.find("\"see\":["), std::string::npos);
}

TEST(diag, text_degrades_without_span) {
  auto txt = livehd::diag::to_text(make_error());  // no span
  EXPECT_NE(txt.find("livehd:error:"), std::string::npos);
  EXPECT_NE(txt.find("livehd:help:"), std::string::npos);
}

TEST(diag, text_shows_location_with_span) {
  Diagnostic d = make_error();
  d.span       = Span{.file = "foo.prp", .start_line = 12, .start_col = 7};
  auto txt     = livehd::diag::to_text(d);
  EXPECT_NE(txt.find("livehd:foo.prp:12:7:error:"), std::string::npos);
  EXPECT_NE(txt.find("livehd:foo.prp:12:7:help:"), std::string::npos);
}

TEST(diag, dedup_within_step) {
  Sink s;
  s.emit(make_error());
  s.emit(make_error());  // identical (code, span, message) -> suppressed
  EXPECT_EQ(s.records().size(), 1u);

  s.set_step("next");  // new step resets dedup
  s.emit(make_error());
  EXPECT_EQ(s.records().size(), 2u);
}

TEST(diag, fatal_throws) {
  Sink s;
  EXPECT_THROW(s.fatal(make_error()), std::runtime_error);
  EXPECT_TRUE(s.has_errors());
  EXPECT_EQ(s.records().size(), 1u);
}

TEST(diag, global_sink_is_stable) {
  auto& a = livehd::diag::sink();
  auto& b = livehd::diag::sink();
  EXPECT_EQ(&a, &b);
}
