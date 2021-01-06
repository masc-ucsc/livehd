#include "floorplanner.hpp"

// test function that floorplans the example floorplan provided at the ArchFP website
void test() {
  geogLayout* dCacheStack = new geogLayout();
  dCacheStack->addComponentCluster(Control, 1, 4, 10., 1., Top);
  dCacheStack->addComponentCluster(L1, 4, 9, 3., 1., Bottom);

  geogLayout* coreCluster = new geogLayout();
  coreCluster->addComponentCluster(ICache, 5, 1, 10., 1., Left);
  coreCluster->addComponent(dCacheStack, 1, Left);
  coreCluster->addComponentCluster(RF, 4, 1, 10., 1., Top);
  coreCluster->addComponentCluster(Core, 16, 3, 2., 1., Bottom);

  geogLayout* L2Stack = new geogLayout();
  L2Stack->addComponentCluster("EBC", 1, 3.166, 3., 1., Top);
  L2Stack->addComponentCluster("C2C", 1, 3.166, 3., 1., Bottom);
  L2Stack->addComponentCluster(MemController, 2, 3.166, 3., 1., TopBottom);
  L2Stack->addComponentCluster("DMA", 2, 3.166, 3., 1., TopBottom);
  L2Stack->addComponentCluster(L2, 4, 9.5, 2., 1., Center);

  geogLayout* chip = new geogLayout();
  chip->addComponent(L2Stack, 1, Left);
  chip->addComponentCluster(L2, 12, 9.5, 2., 1., Left);
  chip->addComponent(coreCluster, 2, TopBottomMirror);

  bool success = chip->layout(AspectRatio, 1.0);
  if (!success)
    cout << "Unable to layout specified CMP configuration.";
  else {
    ostream& HSOut = outputHotSpotHeader("TRIPS.flp");
    chip->outputHotSpotLayout(HSOut);
    outputHotSpotFooter(HSOut);
  }
}