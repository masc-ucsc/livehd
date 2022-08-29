//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "benchmark/benchmark.h"

#include "lnast/lnast_writer.hpp"
#include "lnast/lnast_parser.hpp"
#include "lnast/lnast_hif_writer.hpp"
#include "lnast/lnast_hif_reader.hpp"

std::shared_ptr<Lnast> read_ln(std::string filename) {
  std::ifstream fs;
  fs.open(filename);
  Lnast_parser parser(fs);
  return parser.parse_all();
}

void write_ln(std::string filename, std::shared_ptr<Lnast> lnast) {
  std::ofstream fs;
  fs.open(filename);
  Lnast_writer writer(fs, lnast);
  writer.write_all();
}

std::shared_ptr<Lnast> read_hif(std::string filename) {
  Lnast_hif_reader reader(filename);
  return reader.read_all();
}

void write_hif(std::string filename, std::shared_ptr<Lnast> lnast) {
  Lnast_hif_writer writer(filename, lnast);
  writer.write_all();
}

static void BM_lnast_hif_write(benchmark::State& state) {
  auto lnast = read_ln("lnast/tests/ln/benchmark.ln");
  for (auto _ : state) {
    write_hif("hif_benchmark", lnast);
  }
}

static void BM_lnast_hif_read(benchmark::State& state) {
  auto lnast = read_ln("lnast/tests/ln/benchmark.ln");
  write_hif("hif_benchmark", lnast);
  for (auto _ : state) {
    auto lnast = read_hif("hif_benchmark");
  }
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

// List of benchmark tests
// TODO: Large LNAST Generator
BENCHMARK(BM_lnast_hif_write);
BENCHMARK(BM_lnast_hif_read);
BENCHMARK(BM_lnast_ln_write);
BENCHMARK(BM_lnast_ln_read);

// Run the benchmark
BENCHMARK_MAIN();
