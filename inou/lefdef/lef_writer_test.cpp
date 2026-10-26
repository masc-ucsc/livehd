// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lef_writer.hpp"

#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::physical {
namespace {
constexpr const char* kTlef = "inou/lefdef/tests/data/sky130hd.tlef";
constexpr const char* kLef  = "inou/lefdef/tests/data/sky130hd.lef";
std::string           temp_lef(const std::string& name, const std::string& text) {
  const auto path = std::filesystem::path(testing::TempDir()) / name;
  std::ofstream(path) << text;
  return path.string();
}
// Every field read_lef models.
void expect_same_macro(const Macro& a, const Macro& b) {
  EXPECT_EQ(a.width, b.width);
  EXPECT_EQ(a.height, b.height);
  EXPECT_EQ(a.origin_x, b.origin_x);
  EXPECT_EQ(a.origin_y, b.origin_y);
  EXPECT_EQ(a.site, b.site);
  EXPECT_EQ(a.klass, b.klass);
  EXPECT_EQ(a.symmetry_x, b.symmetry_x);
  EXPECT_EQ(a.symmetry_y, b.symmetry_y);
  ASSERT_EQ(a.pins.size(), b.pins.size());
  for (const auto& [name, pin] : a.pins) {
    SCOPED_TRACE(name);
    ASSERT_TRUE(b.pins.contains(name));
    EXPECT_EQ(pin.direction, b.pins.at(name).direction);
    EXPECT_EQ(pin.use, b.pins.at(name).use);
    EXPECT_EQ(pin.shapes, b.pins.at(name).shapes);
  }
  EXPECT_EQ(a.obstructions, b.obstructions);
  EXPECT_EQ(a.blocked_layers, b.blocked_layers);
  EXPECT_EQ(a.conservative_obstructions, b.conservative_obstructions);
}
Macro block() {
  Macro m;
  m.klass        = "BLOCK";
  m.width        = 2.5;
  m.height       = 1.25;
  m.origin_x     = -0.005;
  m.origin_y     = 0.01;
  m.symmetry_x   = true;
  m.pins["d[0]"] = {
      "OUTPUT TRISTATE",
      "SIGNAL",
      {{"met2", {0.1, 0.2, 0.24, 1.0}}, {"met1", {-0.05, 0.3, 0.2, 0.44}}}
  };
  m.obstructions = {
      {"met1", {0.5, 0.5, 2.0, 1.0}},
      { "li1",    {0, 0, 2.5, 1.25}},
      {"met1", {0.3, 0.1, 0.4, 0.2}}
  };
  m.blocked_layers = {"met1", "li1"};
  return m;
}
}  // namespace
TEST(LefWriter, RoundTripsLibraryMacrosThroughTheParser) {
  const auto                   library = read_lef({kTlef, kLef});
  std::map<std::string, Macro> written;
  for (const auto& [name, macro] : library.macros) {
    written.emplace("__lhd_rt_" + name, macro);
  }
  written.emplace("__lhd_rt_block", block());
  const auto text = write_lef_macros(written, library.dbu);
  // A cell library must not redefine the technology units or sites.
  EXPECT_EQ(text.find("UNITS"), std::string::npos);
  EXPECT_EQ(text.find("\nSITE "), std::string::npos);
  const auto back = read_lef({kTlef, kLef, temp_lef("roundtrip.lef", text)});
  EXPECT_EQ(back.dbu, library.dbu);
  for (const auto& [name, macro] : written) {
    SCOPED_TRACE(name);
    ASSERT_TRUE(back.macros.contains(name));
    expect_same_macro(macro, back.macros.at(name));
  }
  std::map<std::string, Macro> again;
  for (const auto& [name, macro] : written) {
    again.emplace(name, back.macros.at(name));
  }
  EXPECT_EQ(write_lef_macros(again, library.dbu), text);
}
TEST(LefWriter, PrintsExactDecimalMicrons) {
  Macro m;
  m.klass         = "CORE";
  m.width         = 1.23425;
  m.height        = 0.27;
  m.pins["A"]     = {"INPUT", "SIGNAL", {{"M1", {-0.0025, 0.00025, 0.018, 0.25}}}};
  const auto asap = write_lef_macros(
      {
          {"cell", m}
  },
      4000);
  EXPECT_NE(asap.find("  SIZE 1.23425 BY 0.27 ;\n"), std::string::npos);
  EXPECT_NE(asap.find("  ORIGIN 0 0 ;\n"), std::string::npos);
  EXPECT_NE(asap.find("RECT -0.0025 0.00025 0.018 0.25 ;"), std::string::npos);
  EXPECT_EQ(asap.find("SYMMETRY"), std::string::npos);
  // 0.00025 um is not a DBU at 1000 DBU/um.
  EXPECT_THROW(write_lef_macros(
                   {
                       {"cell", m}
  },
                   1000),
               std::invalid_argument);
  m.pins["A"].shapes.front().rect = {-0.002, 0.001, 0.018, 0.25};
  m.width                         = 6.44;
  const auto hd                   = write_lef_macros(
      {
          {"cell", m}
  },
      1000);
  EXPECT_NE(hd.find("  SIZE 6.44 BY 0.27 ;\n"), std::string::npos);
  EXPECT_NE(hd.find("RECT -0.002 0.001 0.018 0.25 ;"), std::string::npos);
  EXPECT_THROW(write_lef_macros(
                   {
                       {"cell", m}
  },
                   3),
               std::invalid_argument);
}
TEST(LefWriter, RefusesGeometryItCannotWriteExactly) {
  const auto refused = [](auto&& edit) {
    auto m = block();
    edit(m);
    EXPECT_THROW(write_lef_macros(
                     {
                         {"cell", m}
    },
                     1000),
                 std::invalid_argument);
  };
  refused([](Macro& m) { m.pins["d[0]"].shapes.front().rect.xh = 0.2405; });
  refused([](Macro& m) { m.pins["d[0]"].shapes.front().rect.xh = 0.1; });
  refused([](Macro& m) { m.pins["empty"] = {"INPUT", "SIGNAL", {}}; });
  refused([](Macro& m) { m.conservative_obstructions.insert("met1"); });
  refused([](Macro& m) { m.blocked_layers.insert("met3"); });
  refused([](Macro& m) { m.pins["two words"] = m.pins["d[0]"]; });
  refused([](Macro& m) { m.site = "core;"; });
  refused([](Macro& m) { m.height = 0; });
  EXPECT_THROW(write_lef_macros(
                   {
                       {"", block()}
  },
                   1000),
               std::invalid_argument);
}
TEST(LefWriter, DuplicateMacroNameAbortsTheLibraryRead) {
  auto       library = read_lef({kTlef, kLef});
  const auto path    = temp_lef("duplicate.lef",
                             write_lef_macros(
                                 {
                                     {"sky130_fd_sc_hd__inv_1", library.macros.at("sky130_fd_sc_hd__inv_1")}
  },
                                 1000));
  try {
    (void)read_lef({kTlef, kLef, path});
    FAIL() << "duplicate macro accepted";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("duplicate LEF macro: sky130_fd_sc_hd__inv_1"), std::string::npos) << e.what();
  }
}
}  // namespace livehd::physical
