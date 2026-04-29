//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "benchmark/benchmark.h"
#include "eprp_var.hpp"
#include "lgraph/lgraph.hpp"
#include "lnast/lnast_parser.hpp"
#include "lnast/lnast_writer.hpp"
#include "pass/lnast_tolg/lnast_tolg.hpp"

class LnastTestFixture : public benchmark::Fixture {
public:
};

std::unordered_map<std::string, std::shared_ptr<Lnast>> read_lns() {
  std::unordered_map<std::string, std::shared_ptr<Lnast>> lns;
  for (const auto& entry : std::filesystem::directory_iterator("benchmark/ln/")) {
    auto          path = entry.path();
    std::ifstream fs;
    fs.open(path);
    Lnast_parser parser(fs);
    auto         ln  = parser.parse_all();
    lns[path.stem()] = ln;
  }
  return lns;
}

BENCHMARK_F(LnastTestFixture, LNAST_LN)(benchmark::State& st) {
  std::filesystem::create_directory("BM_LNAST_LN");
  auto lns = read_lns();
  for (auto _ : st) {
    for (const auto& p : lns) {
      std::ofstream ofs("BM_LNAST_LN/" + p.first, std::ofstream::out | std::ofstream::trunc);
      Lnast_writer  writer(ofs, p.second);
      writer.write_all();
      ofs.close();
    }
  }
}

BENCHMARK_F(LnastTestFixture, LN_LNAST)(benchmark::State& st) {
  for (auto _ : st) {
    auto lns = read_lns();
  }
}

BENCHMARK_MAIN();
