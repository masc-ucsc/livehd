#include <iostream>
// This is the header file created by native-image (Graal)
#include <librapidwright.h>

using namespace std;

int main(int argc, char **argv) {
  // This is some Graal boilerplate code
  graal_isolate_t *      isolate = NULL;
  graal_isolatethread_t *thread  = NULL;

  if (graal_create_isolate(NULL, &isolate, &thread) != 0) {
    fprintf(stderr, "graal_create_isolate error\n");
    return 1;
  }
  // End boilerplate

  // int maxRow = 105;
  // int maxCol = 105;
  string s1      = "myNewDev";
  char * devName = (char *)s1.c_str();
  // char * devName = argv[1];
  string s2       = "and2";
  char * gateName = (char *)s2.c_str();
  // Load the device in RapidWright, the device will be
  // persistent in memory until it is unloaded
  // loadDevice(thread, devName);

  // Get tile names based on row/column indices and print out
  // the tile names for a few tiles
  /*for (int row = 100; row < maxRow; row++){
    for (int col = 100; col < maxCol; col++){
      std::cout << "Tile[" << col << "," << row << "] = \"" <<
        getTileName(thread, devName, row, col) << "\"" << std::endl;
    }
  }*/

  addLogicGate(thread, devName, gateName);

  // Clean up Graal stuff
  /*if (graal_detach_thread(thread) != 0) {
    fprintf(stderr, "graal_detach_thread error\n");
    return 1;
  }*/

  return 0;
}
