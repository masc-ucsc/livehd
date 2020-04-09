// This file is distributed under the LICENSE.vcd-writer License. See LICENSE for details.
#include "vcd_writer.cpp"

#include "vcd_utils.cpp"
using namespace vcd;


int main () {	
//	HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now());
	VCDWriter writer{"dump.vcd", makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now())};
	VarPtr counter_var = writer.register_var("a.b.c", "counter", VariableType::integer, 8);
	for (int timestamp = 0; timestamp < 5; ++timestamp)
	{
		char value = 10 + timestamp * 2; 
		writer.change(counter_var, timestamp, utils::format("%b", value));
	}
  return 0;
}
