#include <iostream>
// This is the header file created by native-image (Graal)
#include <libRW.hpp>
using namespace std;

int main(int argc, char **argv) {

  graalThread* thread = new graalThread();

  char * design_name = const_cast<char*>("HelloCounter");
  char * ff_name = const_cast<char*>("ff_1");
  char * and2_name = const_cast<char*>("and2");
  char * fileName = const_cast<char*>("./dcp/and2_ff.dcp");

  int design_ID = thread->create_Design(design_name);
  int ff_ID = thread->create_FF(ff_name, design_ID);
  int cell_ID = thread ->create_FF(and2_name, design_ID);

  thread -> place_Cell(cell_ID, design_ID);
  thread -> place_Cell(ff_ID, design_ID);
  thread->costumRoute(design_ID, cell_ID, ff_ID);
  thread->set_IO_Buffer(false, design_ID);

  thread->route_Design(design_ID);

  thread->write_DCP(fileName, design_ID);

  return 0;
}
