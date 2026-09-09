// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "worker_pool.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "map/if/if.h"
word* If_DsdManComputeTruth(If_DsdMan_t* manager, int literal, unsigned char* permutation);
}
// clang-format on

namespace {

struct Dsd_deleter {
  void operator()(If_DsdMan_t* manager) const { If_DsdManFree(manager, 0); }
};
using Dsd_manager = std::unique_ptr<If_DsdMan_t, Dsd_deleter>;

constexpr word kAnd3 = 0x8080808080808080ULL;
constexpr word kXor3 = 0x9696969696969696ULL;

TEST(AbcSession, IndependentTruthResults) {
  Dsd_manager                  first(If_DsdManAlloc(6, 0));
  Dsd_manager                  second(If_DsdManAlloc(6, 0));
  std::array<unsigned char, 3> permutation{};
  word                         truth   = kAnd3;
  const int                    literal = If_DsdManCompute(first.get(), &truth, 3, permutation.data(), nullptr);
  word*                        result  = If_DsdManComputeTruth(first.get(), literal, permutation.data());
  ASSERT_EQ(result[0], kAnd3);

  truth = kXor3;
  If_DsdManCompute(second.get(), &truth, 3, permutation.data(), nullptr);
  // A different session must not overwrite the first session's result buffer.
  EXPECT_EQ(result[0], kAnd3);
}

TEST(AbcSession, TruthManagerSurvivesWorkerExit) {
  Dsd_manager          manager;
  livehd::Async_worker worker;
  worker.start([&] {
    manager.reset(If_DsdManAlloc(6, 0));
    std::array<unsigned char, 3> permutation{};
    word                         truth = kAnd3;
    If_DsdManCompute(manager.get(), &truth, 3, permutation.data(), nullptr);
  });
  worker.get();

  for (int i = 0; i < 8; ++i) {
    word       result   = 0;
    const word expected = i % 2 == 0 ? kXor3 : kAnd3;
    worker.start([&] {
      std::array<unsigned char, 3> permutation{};
      word                         truth   = expected;
      const int                    literal = If_DsdManCompute(manager.get(), &truth, 3, permutation.data(), nullptr);
      result                               = If_DsdManComputeTruth(manager.get(), literal, permutation.data())[0];
    });
    worker.get();
    EXPECT_EQ(result, expected);
  }
}

}  // namespace
