How to run:                                        
                                                   
1. in livehd/ run pass/pinpoint_src/pre_synth.sh   
    This will create a verilog file in pre_synth/ from a pyrope file.
2. cd pass/pinpoint_src
  2.1. check design name in run.sh
3. ./make_netlist.sh
    Takes verilog file from livehd/pre_synth/ and generates pass/pinpoint_src/netlist.v
4. get_nodes.py

For first time users: 
You need to get the liberty file for Synth. Complete the following steps before
starting Synth stage.
1. git clone https://github.com/google/skywater-pdk
2. cd skywater-pdk
3. git submodule init libraries/sky130_fd_sc_hs/latest
4. git submodule update
5. make timing
6. copy
   libraries/sky130_fd_sc_hs/latest/timing/sky130_fd_sc_hs__tt_025C_1v50.lib to
   pass/pinpoint_src/sky130.lib
