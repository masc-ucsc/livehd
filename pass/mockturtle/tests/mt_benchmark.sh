rm -f ppmt
rm -f ppyosys_pre
rm -f ppyosys_synth

LGSHELL=./bazel-bin/main/lgshell
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1.v;     proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10.v;    proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_100.v;   proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1000.v;  proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_5000.v;  proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_20000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_30000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_40000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_50000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_55000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_60000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_65000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_67500.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_70000.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_72500.v; proc;' 2>>ppyosys_pre>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_75000.v; proc;' 2>>ppyosys_pre>pp

# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1.v;   synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10.v;  synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_100.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_5000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_20000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_30000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_40000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_50000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_60000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_65000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_67500.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_70000.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_72500.v; synth_xilinx;' 2>>ppyosys_synth>pp
# perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_75000.v; synth_xilinx;' 2>>ppyosys_synth>pp

perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1.v;     proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10.v;    proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_100.v;   proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1000.v;  proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_5000.v;  proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_20000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_30000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_40000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_50000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_60000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_65000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_67500.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_70000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_72500.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_75000.v; proc; techmap; abc -lut 4;' 2>>ppyosys_synth>pp

# perf stat ${LGSHELL} "lgraph.open name:mt_xor_1 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_10 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_100 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_1000 > pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_5000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_10000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_20000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_30000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_40000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_50000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_55000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_60000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_65000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_67500 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_70000 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_72500 |> pass.mockturtle" 2>>ppmt>pp
# perf stat ${LGSHELL} "lgraph.open name:mt_xor_75000 |> pass.mockturtle" 2>>ppmt>pp



rm -f yosys_script.*


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_1.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_10.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_100.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_1000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_5000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_10000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_20000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_30000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp



# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_40000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_50000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_55000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_60000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp

# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_65000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_67500.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_70000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_72500.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp


# perf stat yosys -p '
# read_verilog ./pass/mockturtle/tests/xor_75000.v; 
# read_verilog -lib +/xilinx/cells_sim.v; 
# read_verilog -lib +/xilinx/cells_xtra.v;  
# hierarchy -check -auto-top;
# flatten;


# techmap -map +/techmap.v -map +/xilinx/cells_map.v;
# clean;

# abc -luts 2:2,3,6:5[,10,20] [-dff] ;
# clean;
# shregmap -minlen 3 -init -params -enpol any_or_none ;


# techmap -map +/xilinx/lut_map.v -map +/xilinx/ff_map.v -map +/xilinx/cells_map.v;
# shregmap -minlen 3 -init -params -enpol any_or_none
# dffinit -ff FDRE Q INIT -ff FDCE Q INIT -ff FDPE Q INIT -ff FDSE Q INIT -ff FDRE_1 Q INIT -ff FDCE_1 Q INIT -ff FDPE_1 Q INIT -ff FDSE_1 Q INIT
# clean

# hierarchy -check;
# stat -tech xilinx;
# check -noinit;

# ' 2>>ppyosys_tmap>pp






