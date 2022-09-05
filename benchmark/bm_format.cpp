//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "benchmark/benchmark.h"

#include "core/lhtree.hpp"

#include "lnast/lnast_writer.hpp"
#include "lnast/lnast_parser.hpp"
#include "lnast/lnast_hif_writer.hpp"
#include "lnast/lnast_hif_reader.hpp"

#include "lgraph/lgraph.hpp"
#include "pass/lnast_tolg/lnast_tolg.hpp"

class LnastTestFixture : public benchmark::Fixture {
public:
};

class LgraphTestFixture : public benchmark::Fixture {
public:
};

std::shared_ptr<Lnast> read_ln(std::string filename) {
  std::ifstream fs;
  fs.open(filename);
  Lnast_parser parser(fs);
  return parser.parse_all();
}

BENCHMARK_F(LnastTestFixture, LNAST_HIF)(benchmark::State& st) {
  auto lnast = read_ln("benchmark/ln/iwls_adder.ln");
  Lnast_hif_writer writer("BM_LNAST_HIF", lnast);
  for (auto _ : st) {
    writer.write_all();
  }
}

BENCHMARK_F(LnastTestFixture, HIF_LNAST)(benchmark::State& st) {
  auto lnast = read_ln("benchmark/ln/iwls_adder.ln");
  Lnast_hif_writer writer("BM_LNAST_HIF", lnast);
  writer.write_all();
  for (auto _ : st) {
    Lnast_hif_reader reader("BM_LNAST_HIF");
    reader.read_all();
  }
}

BENCHMARK_F(LnastTestFixture, LNAST_LN)(benchmark::State& st) {
  auto lnast = read_ln("benchmark/ln/iwls_adder.ln");
  std::ofstream ofs;
  for (auto _ : st) {
    ofs.open("BM_LNAST_LN.ln", std::ofstream::out | std::ofstream::trunc);
    Lnast_writer writer(ofs, lnast);
    writer.write_all();
  }
  ofs.close();
}

BENCHMARK_F(LnastTestFixture, LN_LNAST)(benchmark::State& st) {
  std::ifstream ifs("benchmark/ln/iwls_adder.ln");
  for (auto _ : st) {
    Lnast_parser parser(ifs);
    parser.parse_all();
    ifs.clear();
    ifs.seekg(0);
  }
  ifs.close();
}

/*
BENCHMARK_F(LnastTestFixture, LNAST_FIRRTL)(benchmark::State& st) {
  auto lnast = read_ln("lnast/tests/ln/benchmark.ln");
  for (auto _ : st) {
    
  }
}
*/

BENCHMARK_F(LgraphTestFixture, LGRAPH_HIF)(benchmark::State& st) {
  auto lnast = read_ln("benchmark/ln/iwls_adder.ln");
  auto ln_to_lg = Lnast_tolg("benchmark", "");
  for (auto _ : st) {
    auto lgs = ln_to_lg.do_tolg(lnast, lh::Tree_index::root());
    lgs[0]->save("BM_LGRAPH_HIF");
  }
}

// Run the benchmark
BENCHMARK_MAIN();
