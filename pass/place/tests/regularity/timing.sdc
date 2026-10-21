create_clock -name clock -period 20 [get_ports clock]
set_input_delay -clock clock 0.1 [all_inputs]
set_output_delay -clock clock 0.1 [all_outputs]
