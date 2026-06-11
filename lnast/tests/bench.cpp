//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "benchmark/benchmark.h"
#include "lnast/lnast_parser.hpp"
#include "lnast/lnast_writer.hpp"
#include "lnast/tests/ln_test_utils.hpp"

std::shared_ptr<Lnast> read_ln(std::string filename) {
  std::ifstream fs;
  fs.open(filename);
  Ln_test_parser parser(fs);
  return parser.parse_all();
}

void write_ln(std::string filename, std::shared_ptr<Lnast> lnast) {
  std::ofstream fs;
  fs.open(filename);
  Lnast_writer writer(fs, lnast);
  writer.write_all();
}

static void BM_lnast_ln_read(benchmark::State& state) {
  for (auto _ : state) {
    auto lnast = read_ln("lnast/tests/ln/benchmark.ln");
  }
}

static void BM_lnast_ln_write(benchmark::State& state) {
  auto lnast = read_ln("lnast/tests/ln/benchmark.ln");
  for (auto _ : state) {
    write_ln("hif_benchmark.ln", lnast);
  }
}

BENCHMARK(BM_lnast_ln_write);
BENCHMARK(BM_lnast_ln_read);

BENCHMARK_MAIN();
