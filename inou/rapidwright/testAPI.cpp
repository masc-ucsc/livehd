#include <iostream>
// This is the header file created by native-image (Graal)
#include <libRW.hpp>
using namespace std;

int main(int argc, char **argv) {

  graalThread* thread = new graalThread();

  char * design_name = const_cast<char*>("Hello1stDesign");
  char * top_design_name = const_cast<char*>("HelloModuleTop");
  char * ff_name = const_cast<char*>("ff_1");
  char * and2_name = const_cast<char*>("and2");
  char * fileName = const_cast<char*>("./dcp/and2_ff.dcp");

  char * modInst_name = const_cast<char*>("modInst");
  char * mod_fileName = const_cast<char*>("./dcp/and2_ff_module.dcp");

  int design_ID = thread->create_Design(design_name);
  int ff_ID = thread->create_FF(ff_name, design_ID);
  int cell_ID = thread ->create_FF(and2_name, design_ID);


  thread -> place_Cell(cell_ID, design_ID);
  thread -> place_Cell(ff_ID, design_ID);
  thread->costum_Route(design_ID, cell_ID, ff_ID);
  thread->set_IO_Buffer(false, design_ID);
  //thread->route_Design(design_ID);
  thread->write_DCP(fileName, design_ID);

  //create a new top design to test module
  int top_design_ID = thread->create_Design(top_design_name);
  cout << "after design" << endl;
  int module_ID = thread->create_Module(design_ID, top_design_ID);
  cout << "after module" << endl;
  int moduleInst_ID = thread->create_ModuleInst(modInst_name, module_ID, top_design_ID); //need to add module id
  cout << "after moduleInst" << endl;
  thread->write_DCP(mod_fileName, top_design_ID);

  return 0;
}
