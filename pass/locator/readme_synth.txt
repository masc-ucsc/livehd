Install steps:

 1-get open_pdk with sky130-pdk

  http://opencircuitdesign.com/open_pdks/

  Copy one of the tech files. E.g: sources/sky130-pdk/libraries/sky130_fd_sc_hs/latest/timing/sky130_fd_sc_hs__tt_025C_1v50.lib
  to sky130.lib

  1.1- to get the lib:(http://opencircuitdesign.com/open_pdks/install.html)
    git clone https://github.com/google/skywater-pdk
    cd skywater-pdk
    git submodule init libraries/sky130_fd_io/latest
    git submodule init libraries/sky130_fd_pr/latest
    git submodule init libraries/sky130_fd_sc_hd/latest
    git submodule init libraries/sky130_fd_sc_hvl/latest
    git submodule init libraries/sky130_fd_sc_hdll/latest
    git submodule init libraries/sky130_fd_sc_hs/latest --> only this is enough for submodule init. Since library files are heavy, so init only those that are required.
    git submodule init libraries/sky130_fd_sc_ms/latest
    git submodule init libraries/sky130_fd_sc_ls/latest
    git submodule init libraries/sky130_fd_sc_lp/latest
    git submodule update
    make timing
    
    livehd$ cp ../skywater-pdk/libraries/sky130_fd_sc_hs/latest/timing/sky130_fd_sc_hs__tt_025C_1v50.lib pass/locator/sky130.lib

 2-Run yosys

   edit run.sh (VERILOG_FILES path)
   source run.sh 
   yosys -c synth.tcl 

   
 3-Run STA

livehd/pass/locator$ ../../../OpenSTA/app/sta
OpenSTA> read_verilog netlist.v 
OpenSTA> read_liberty sky130.lib 
Warning: sky130.lib, line 19 default_operating_condition typ not found.
OpenSTA> current_design arith
OpenSTA> link_design
OpenSTA> report_power
Group                  Internal  Switching    Leakage      Total
                          Power      Power      Power      Power
----------------------------------------------------------------
Sequential             0.00e+00   0.00e+00   0.00e+00   0.00e+00   0.0%
Combinational          4.38e-15   2.69e-16   3.88e-11   3.88e-11 100.0%
Macro                  0.00e+00   0.00e+00   0.00e+00   0.00e+00   0.0%
Pad                    0.00e+00   0.00e+00   0.00e+00   0.00e+00   0.0%
----------------------------------------------------------------
Total                  4.38e-15   2.69e-16   3.88e-11   3.88e-11 100.0%
                           0.0%       0.0%     100.0%
OpenSTA>    


  Timing for unconstrained path:

OpenSTA> report_checks -unconstrained -fields {slew cap input nets fanout} -format full_clock_expanded

  Timing for setup (must have flops)
OpenSTA> report_checks -path_delay max -fields {slew cap input nets fanout} -format full_clock_expanded -group_count 5



  3.1-STA @ opentimer:

    ~$ git clone https://github.com/OpenTimer/OpenTimer.git
    ~$ cd OpenTimer
    ~$ mkdir build
    ~$ cd build
    ~$ cmake ../
    ~$ make 

    ~/livehd/pass/locator$ ../../../OpenTimer/bin/ot-shell
    ot> read_celllib sky130.lib
    ot> read_verilog netlist.v
    ot> read_sdc your_sdc.sdc
    ot> report_timing
