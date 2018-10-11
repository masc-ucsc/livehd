#include "verilated.h"
//#include "verilated_vcd_c.h"

void dut_main();

int main(int argc, char **argv)
{
  Verilated::commandArgs(argc, argv);

  dut_main();
}
