
simlib is the library to perform simulations. Each "stage" in simlib should correspond to a lgraph. 
If there lgraph is very small, it could be "inlined" in the parent lgraph.

Each "stage" consist of a header file (foo_stage.hpp) and a c++ file (foo_stage.cpp). "foo" matches
the lgraph name.

The structure for the header file

```c++
#pragma once

struct Foo_stage {
  // LIST of flops/registers
  UInt<3> foo_register_x; // Example

  // List of memories
  std::array<UInt<32>, 256> memory3;

  // List of stages
  Bar_stage s_bar;

  void reset_cycle();
  void cycle(list_of_inputs...);
};
```

The generated file includes two functions (reset_cycle and cycle). reset_cycle
is called only when the reset for the module is active high. If there are
multiple reset signals, it is called when any reset signal is set. The cycle is
called every cycle.  If the reset cycle is high, reset_cycle is called
immediately after the cycle function.

The structure for the source file:

```c++
#include "livesim_types.hpp"

#include <stdio.h>

#include "foo_stage.hpp"

void Foo_stage::reset_cycle() {
  tmp  = 0; // reset register

	reset_iterator = reset_iterator + 1; // reset state code
	memory[reset_iterator] = 0;

  s_bar.reset_cycle();
}

void Foo_stage::cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d) {

  // Combinational and register updates for the cycle

  comb_val = memory[(tmp&UInt<32>(0xff)).as_single_word()];

  auto s_var_flop_out = s_var.tmp3;
  s_var.cycle(comb_val, tmp, anything_input_needed, any_combination_value);

  tmp = tmp.addw(UInt<32>(7)) + tmp3 + any_combination_value;
}
```

A stage could instantiate other stages. This important to understand how to
handle combinational input/outputs from stages.  The register values are
handled through class variables (E.g: Foo_stage::foo_register_x). Input/output
combinationals are handled with variable input/output.


For a concrete example of "manual" code generation for simlib, check the example/simlib code.

