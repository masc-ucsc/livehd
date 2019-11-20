#include <iostream>
// This is the header file created by native-image (Graal)
#include <libRW.hpp>
using namespace std;

int main(int argc, char **argv) {

  graalThread* thread = new graalThread();

  char * design_name = const_cast<char*>("myDesign");
  char * ff_1 = const_cast<char*>("ff_1");
  char * ff_2 = const_cast<char*>("ff_2");
  char * and2_name = const_cast<char*>("and2");
  char * fileName = const_cast<char*>("./dcp/and2_ff.dcp");

  int design_ID = thread->create_Design(design_name);
  int ff_1_ID = thread->create_FF(ff_1, design_ID);
  int ff_2_ID = thread->create_FF(ff_2, design_ID);
  int AND2_ID = thread ->create_AND2(and2_name, design_ID);
  cout << "ff_1_ID: " << ff_1_ID << endl;
  cout << "ff_2_ID: " << ff_2_ID << endl;
  cout << "AND2_ID: " << AND2_ID << endl;

  thread -> place_Cell(ff_1_ID, design_ID);
  thread -> place_Cell(ff_2_ID, design_ID);
  bool placed = thread -> place_Cell(AND2_ID, design_ID);
  cout << "Is AND2 is successfully placed? " << placed << endl;
  //thread -> connect_Ports(design_ID, ff_1_ID, ff_2_ID);
  thread -> connect_Ports(design_ID, ff_2_ID, const_cast<char*>("Q"), ff_1_ID, const_cast<char*>("D"));
  thread -> connect_Ports(design_ID, AND2_ID, const_cast<char*>("O"), ff_2_ID, const_cast<char*>("D"));
  //thread->costumRoute(design_ID, ff_1_ID, ff_2_ID);
  //thread->set_IO_Buffer(false, design_ID);

  thread->route_Design(design_ID);

  thread->write_DCP(fileName, design_ID);

  return 0;
}
