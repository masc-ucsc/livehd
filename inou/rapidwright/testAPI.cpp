#include <iostream>
// This is the header file created by native-image (Graal)
#include <librapidwright.h>

using namespace std;

int main(int argc, char **argv) {
  // This is some Graal boilerplate code
  graal_isolate_t *isolate = NULL;
  graal_isolatethread_t *thread = NULL;

  if (graal_create_isolate(NULL, &isolate, &thread) != 0) {
    fprintf(stderr, "graal_create_isolate error\n");
    return 1;
  }


  char * designName_ff = const_cast<char*>("HelloCounter");
  char * gName_ff = const_cast<char*>("ff_1");
  char * fileName_ff = const_cast<char*>("ff_1.dcp");

  char * designName_and2 = const_cast<char*>("and_design");
  char * gName_and2 = const_cast<char*>("ff_2");
  char * fileName_and2 = const_cast<char*>("ff_2.dcp");

  int DesignID_ff = RW_Create_Design(thread, designName_ff);
  int DesignID_and2 = RW_Create_Design(thread, designName_and2);

  RW_Create_FF(thread, gName_ff, DesignID_ff);
  RW_Create_FF(thread, gName_and2, DesignID_and2);

  RW_set_IO_Buffer(thread, false, DesignID_ff);
  RW_set_IO_Buffer(thread, false, DesignID_and2);

  RW_place_Design(thread, DesignID_ff);
  RW_route_Design(thread, DesignID_ff);

  RW_write_DCP(thread,fileName_ff, DesignID_ff);
  RW_write_DCP(thread,fileName_and2, DesignID_and2);

  return 0;
}
