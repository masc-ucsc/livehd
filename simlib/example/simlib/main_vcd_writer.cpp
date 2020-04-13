// This file is distributed under the LICENSE.vcd-writer License. See LICENSE for details.
// this file is for reference purposes only.
#include "vcd_writer.cpp"
#include "vcd_utils.cpp"
using namespace vcd;


int main () {
	VCDWriter writer{"dump.vcd", makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now(), "This is the VCD file", "version _simlib_")};
	VarPtr counter_var = writer.register_var("a.b.c", "counter", VariableType::integer, 8);
	for (int timestamp = 0; timestamp < 5; ++timestamp)
	{
		char value = 10 + timestamp * 2;
		writer.change(counter_var, timestamp, utils::vcd_format("%b", value));
	}
  return 0;
}
