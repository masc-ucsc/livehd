#include <iostream>
// This is the header file created by native-image (Graal)
#include <libRW.hpp>
using namespace std;

int main(int argc, char **argv) {

  graalThread* thread = new graalThread();

  char * designName_ff = const_cast<char*>("HelloCounter");
  char * gName_ff = const_cast<char*>("ff_1");
  char * fileName_ff = const_cast<char*>("ff_1.dcp");

  int DesignID_ff = thread->create_Design(designName_ff);

  thread->create_FF(gName_ff, DesignID_ff);

  thread->set_IO_Buffer(false, DesignID_ff);

  thread->route_Design(DesignID_ff);

  thread->write_DCP(fileName_ff, DesignID_ff);

  return 0;
}
