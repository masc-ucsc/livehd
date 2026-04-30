How to run:                                        
                                                   
1. Provide the input modules in pass/locator/pre_synth.sh
   in livehd/ run pass/locator/pre_synth.sh   
   This will create verilog file(s) in pre_synth/ from a pyrope file.
  1.1. If there are multiple files, concat them to a single file.
2. cd pass/locator
  2.1. check design name in run.sh (match it to the main verilog file in pre_synth/)
3. ./make_netlist.sh
    Takes verilog file from livehd/pre_synth/ and generates pass/locator/netlist.v

IMP: note that the above 3 steps are merged into pre_synth.sh in form of 3 functions at
the end of pre_synth.sh. Hence, you need not run the above 3 steps one by one.

4. (run sta and get the timing path)(refer to readme_synth.txt)
    FIXME: this has to be scripted in get_nodes.py
5.  cd ../../ (go to livehd/)
   open lgshell
   livehd> inou.yosys.tolg files:pass/locator/netlist.v script:pp.ys top:gcd_3bits
   (pp.ys and sky130.lib are the ones copied from pass/locator/debugged/ to livehd/)


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
   pass/locator/sky130.lib


To remove old data, remove the following:
1. rm -r lgdb**** scalar_tuple* pre_synth******* *****
2. 

================================================================

25April2022

1. some steps for synthesis in ./readme_synth.txt

2. netlist generated from these 2 different steps.
step#1:
  inou/yosys/tests/trivial1.v --> synth netlist.v --> ot_shell
  step#1 : works fine
step#2:
  inou.cgen dumps trivial1.v --> synth netlist.v --> ot_shell
  step#2 : error 
step2 will not work unless we implement a TMAP in lgshell (opentimer only can read trivial netlist verilog style)
{
  If I try with trivial1.v from inou/yosys/tests/, it works fine. (without sdc it says no critical paths).
If I try with :
livehd> inou.yosys.tolg files:inou/yosys/tests/trivial1.v |> pass.bitwidth |> pass.cprop |> pass.bitwidth |> inou.cgen.verilog odir:opentimer_tests
then it gives the error
}

