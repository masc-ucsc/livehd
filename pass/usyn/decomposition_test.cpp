// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "decomposition.hpp"

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
void check_composition(const Truth_table& source, const Decomposition& d) {
  ASSERT_EQ(d.status, Status::feasible);
  std::vector<bool> image(1U << d.top.inputs);
  for (uint32_t x = 0; x < (1U << source.inputs); ++x) {
    uint32_t bound = 0, top = 0;
    for (uint32_t i = 0; i < d.bound.size(); ++i) {
      bound |= ((x >> d.bound[i]) & 1) << i;
    }
    for (uint32_t i = 0; i < d.divisors.size(); ++i) {
      top |= uint32_t{d.divisors[i].get(bound)} << i;
    }
    for (uint32_t i = 0; i < d.free.size(); ++i) {
      top |= ((x >> d.free[i]) & 1) << (d.divisors.size() + i);
    }
    EXPECT_EQ(d.top.get(top), source.get(x)) << x;
    image[top] = true;
  }
  ASSERT_EQ(d.top_care.inputs, d.top.inputs);
  for (uint32_t x = 0; x < image.size(); ++x) {
    EXPECT_EQ(d.top_care.get(x), image[x]) << x;
  }
}
}  // namespace

TEST(Decomposition, ExhaustiveThreeInputFunctionsAndAllProperPartitions) {
  for (uint32_t bits = 0; bits < 256; ++bits) {
    Truth_table table(3);
    table.words[0] = bits;
    for (uint32_t mask = 1; mask < 7; ++mask) {
      Budget     work{100000};
      const auto d = decompose_function(table, mask, 3, work);
      ASSERT_FALSE(work.exhausted);
      if (d.status == Status::feasible) {
        check_composition(table, d);
      } else {
        EXPECT_EQ(d.status, Status::unsupported);
      }
    }
  }
}

TEST(Decomposition, CreatesMultipleCodeBitsAndRejectsTooSmallAnInterface) {
  // Four bound assignments select cofactors 0, r, !r, 1.
  Truth_table table(3);
  for (uint32_t x = 0; x < 8; ++x) {
    const auto b = x & 3;
    table.set(x, b == 3 || (b == 1 && (x & 4)) || (b == 2 && !(x & 4)));
  }
  Budget     work{100000};
  const auto d = decompose_function(table, 3, 3, work);
  ASSERT_EQ(d.divisors.size(), 2U);
  check_composition(table, d);
  EXPECT_EQ(decompose_function(table, 3, 2, work).status, Status::unsupported);
  Budget empty{0};
  EXPECT_EQ(decompose_function(table, 3, 3, empty).status, Status::search_exhausted);
  EXPECT_EQ(decompose_function(table, 8, 3, work).status, Status::invalid);
}

TEST(Decomposition, SixteenInputAnalysisRemainsBounded) {
  Truth_table table(16);
  for (uint32_t x = 0; x < 65536; ++x) {
    table.set(x, (x & 255) == 255 && (x >> 8) != 0);
  }
  Budget     work{2000000};
  const auto d = decompose_function(table, 255, 9, work);
  ASSERT_EQ(d.divisors.size(), 1U);
  check_composition(table, d);
  EXPECT_FALSE(work.exhausted);
}

TEST(Decomposition, JointCofactorsPreserveBothOutputsAndTheirSharedReachableImage) {
  // Every pair of two-input Boolean functions, lifted over an additional free
  // variable with different polarity/dependence on each output.
  for (uint32_t left = 0; left < 16; ++left) {
    for (uint32_t right = 0; right < 16; ++right) {
      std::array functions{Truth_table(3), Truth_table(3)};
      for (uint32_t x = 0; x < 8; ++x) {
        functions[0].set(x, ((left >> (x & 3)) & 1) && (x & 4));
        functions[1].set(x, ((right >> (x & 3)) & 1) != bool(x & 4));
      }
      Budget     work{100000};
      const auto joint = decompose_pair(functions, 3, 3, work);
      ASSERT_FALSE(work.exhausted);
      if (joint.status == Status::feasible) {
        for (size_t i = 0; i < functions.size(); ++i) {
          check_composition(functions[i], {joint.status, joint.bound, joint.free, joint.divisors, joint.tops[i], joint.top_care});
        }
        if (joint.divisors.size() == 2) {
          for (Joint_code_change change : {
                   Joint_code_change{0, 1},
                   Joint_code_change{1, 0}
          }) {
            const auto recoded = recode_pair(joint, change, work);
            for (size_t i = 0; i < functions.size(); ++i) {
              check_composition(functions[i],
                                {recoded.status, recoded.bound, recoded.free, recoded.divisors, recoded.tops[i], recoded.top_care});
            }
            const auto restored = recode_pair(recoded, change, work);
            EXPECT_EQ(restored.divisors, joint.divisors);
            EXPECT_EQ(restored.tops, joint.tops);
            EXPECT_EQ(restored.top_care, joint.top_care);
          }
        }
      } else {
        EXPECT_EQ(joint.status, Status::unsupported);
      }
    }
  }
}

TEST(Decomposition, RecodingThreeBitPartialImagesPreservesBothFunctionsAndRefusesPartialResults) {
  std::array functions{Truth_table(4), Truth_table(4)};
  for (uint32_t x = 0; x < 16; ++x) {
    const auto cls = (x & 7) % 5;
    functions[0].set(x, (cls >> ((x >> 3) & 1)) & 1);
    functions[1].set(x, cls & 4);
  }
  Budget     work{100000};
  const auto original = decompose_pair(functions, 7, 4, work);
  ASSERT_EQ(original.status, Status::feasible);
  ASSERT_EQ(original.divisors.size(), 3U);
  for (uint32_t target = 0; target < 3; ++target) {
    for (uint32_t source = 0; source < 3; ++source) {
      if (target == source) {
        continue;
      }
      const auto changed = recode_pair(original, {target, source}, work);
      for (size_t i = 0; i < functions.size(); ++i) {
        check_composition(functions[i],
                          {changed.status, changed.bound, changed.free, changed.divisors, changed.tops[i], changed.top_care});
      }
    }
  }
  EXPECT_EQ(recode_pair(original, {0, 0}, work).status, Status::invalid);
  EXPECT_EQ(recode_pair(original, {3, 0}, work).status, Status::invalid);
  Budget short_work{20};
  EXPECT_EQ(recode_pair(original, {0, 1}, short_work).status, Status::search_exhausted);
  for (size_t i = 0; i < functions.size(); ++i) {
    check_composition(functions[i],
                      {original.status, original.bound, original.free, original.divisors, original.tops[i], original.top_care});
  }
}

TEST(Decomposition, JointEncodingCannotMergeClassesDistinguishedOnlyByTheOtherOutput) {
  std::array functions{Truth_table(3), Truth_table(3)};
  for (uint32_t x = 0; x < 8; ++x) {
    functions[0].set(x, (x & 1) && (x & 4));
    functions[1].set(x, (x & 2) && (x & 4));
  }
  Budget work{100000};
  EXPECT_EQ(decompose_function(functions[0], 3, 2, work).divisors.size(), 1U);
  EXPECT_EQ(decompose_function(functions[1], 3, 2, work).divisors.size(), 1U);
  EXPECT_EQ(decompose_pair(functions, 3, 2, work).status, Status::unsupported);
  const auto joint = decompose_pair(functions, 3, 3, work);
  ASSERT_EQ(joint.divisors.size(), 2U);
  for (size_t i = 0; i < functions.size(); ++i) {
    check_composition(functions[i], {joint.status, joint.bound, joint.free, joint.divisors, joint.tops[i], joint.top_care});
  }
  Budget tiny{1};
  EXPECT_EQ(decompose_pair(functions, 3, 3, tiny).status, Status::search_exhausted);
  functions[1] = Truth_table(2);
  EXPECT_EQ(decompose_pair(functions, 3, 3, work).status, Status::invalid);
}
}  // namespace livehd::usyn
