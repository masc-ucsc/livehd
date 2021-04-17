#include "AnnLayout.hpp"
#include "helpers.hpp"
//#include "gmock/gmock.h"
#include "gtest/gtest.h"

class ann_test : public ::testing::Test {
protected:
  // void SetUp() override {};

  // void TearDown() override {};
};

TEST_F(ann_test, basic_test) {
  annLayout* chip = new annLayout(3);
  chip->addComponentCluster("C1", 1, 3, 4., 1.);
  chip->addComponentCluster("C2", 1, 4, 4., 1.);
  chip->addComponentCluster("C3", 1, 2, 4., 1.);

  bool success = chip->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);
}

TEST_F(ann_test, basic_test2) {
  annLayout* chip = new annLayout(5);
  chip->addComponentCluster("C1", 1, 3, 4., 1.);
  chip->addComponentCluster("C2", 1, 4, 4., 1.);
  chip->addComponentCluster("C3", 1, 2, 4., 1.);
  chip->addComponentCluster("C4", 8, 1, 2., 1.);
  chip->addComponentCluster("C5", 2, 2, 4., 1.);

  bool success = chip->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);
}

TEST_F(ann_test, bad_ar_test) {
  annLayout* chip = new annLayout(3);
  chip->addComponentCluster("C1", 1, 3, 4., 1.);
  chip->addComponentCluster("C2", 1, 4, 50., 49.);
  chip->addComponentCluster("C3", 1, 2, 4., 1.);

  bool success = chip->layout(AspectRatio, 1.0); // not possible because of insane C2 AR

  EXPECT_FALSE(success);
}

/*
TEST_F(ann_test, sub_test) {
  annLayout* coreCluster = new annLayout(4);
  coreCluster->addComponentCluster("ICache", 5, 1, 10., 1.);
  coreCluster->addComponent(coreCluster, 1);
  coreCluster->addComponentCluster("RF", 4, 1, 10., 1.);
  coreCluster->addComponentCluster("Core", 16, 3, 2., 1.);

  bool success = coreCluster->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);

  annLayout* chip = new annLayout(3);
  chip->addComponentCluster("C1", 1, 2, 6., 1.);
  chip->addComponentCluster("C2", 1, 4, 4., 1.);
  chip->addComponent(coreCluster, 3);

  success = chip->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);
}

TEST_F(ann_test, archfp_test) {
  annLayout* dCacheStack = new annLayout(2);
  dCacheStack->addComponentCluster("Control", 1, 4, 10., 1.);
  dCacheStack->addComponentCluster("L1", 4, 9, 3., 1.);

  bool success = dCacheStack->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);

  annLayout* coreCluster = new annLayout(4);
  coreCluster->addComponentCluster("ICache", 5, 1, 10., 1.);
  coreCluster->addComponent(dCacheStack, 1);
  coreCluster->addComponentCluster("RF", 4, 1, 10., 1.);
  coreCluster->addComponentCluster("Core", 16, 3, 2., 1.);

  success = coreCluster->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);

  annLayout* L2Stack = new annLayout(5);
  L2Stack->addComponentCluster("EBC", 1, 3.166, 3., 1.);
  L2Stack->addComponentCluster("C2C", 1, 3.166, 3., 1.);
  L2Stack->addComponentCluster("MemController", 2, 3.166, 3., 1.);
  L2Stack->addComponentCluster("DMA", 2, 3.166, 3., 1.);
  L2Stack->addComponentCluster("L2", 4, 9.5, 2., 1.);

  success = L2Stack->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);

  annLayout* chip = new annLayout(3);
  chip->addComponent(L2Stack, 1);
  chip->addComponentCluster("L2", 12, 9.5, 2., 1.);
  chip->addComponent(coreCluster, 2);

  success = chip->layout(AspectRatio, 1.0);
  EXPECT_TRUE(success);
}
*/