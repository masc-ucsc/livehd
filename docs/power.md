# Power modeling

LiveHD is starting to generate power. Use it at your own risk.



# Using bazel_rules_hdl_test

Steps to model power:

1-Get a synthesized netlist and an VCD.

 clone https://github.com/renau/bazel_rules_hdl_test/
```
bazel build //counter:top_gls 
 ./bazel-bin/counter/top_gls
cp -f bazel-out/*/bin/counter/verilog_counter_synth_synth_output.v counter.v
cp -f output.vcd counter.vcd
cp -f bazel-bin/external/com_google_skywater_pdk_sky130_fd_sc_hd/timing/sky130_fd_sc_hd__ff_100C_1v95.lib .
```


2-Run livehd

```
bazel build -c dbg //main:all
./bazel-bin/main/lgshell
````

Sample command line:

1-Read the liberty file
```
livehd> inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib
inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib
reading liberty sky130_fd_sc_hd__ff_100C_1v95.lib
I 34368 22-09-04 11:57:22 celllib.cpp:34] loading celllib "sky130_fd_sc_hd__ff_100C_1v95.lib"
W 34368 22-09-04 11:57:22 celllib.cpp:274] unexpected lut template variable normalized_voltage
I 34368 22-09-04 11:57:22 unit.cpp:244] use celllib time unit 1e-09 s
I 34368 22-09-04 11:57:22 unit.cpp:258] use celllib capacitance unit 1e-12 F
I 34368 22-09-04 11:57:22 unit.cpp:272] use celllib current unit 0.001 A
I 34368 22-09-04 11:57:22 unit.cpp:286] use celllib voltage unit 1 V
I 34368 22-09-04 11:57:22 unit.cpp:300] use celllib resistance unit 1000 Ohm
I 34368 22-09-04 11:57:22 unit.cpp:314] use celllib power unit 1e-09 W
I 34368 22-09-04 11:57:22 celllib.cpp:66] added max celllib "sky130_fd_sc_hd__ff_100C_1v95" [cells:428]
```

2-OPT1 Read verilog file(s) (OPTION 1 using the yosys input)
```
livehd> inou.liveparse files:counter.v |> inou.yosys.tolg |> pass.bitwidth
```

2-OPT2 Read Verilog file(s) using the experimental (has issues pending to fix in lnast2lg) slang backend

```
livehd> inou.verilog files:counter.v |> pass.compiler
```

3-Compute the power (using vcd, or MAX power if no VCD is provided)
```
livehd> lgraph.open name:counter |> pass.bitwidth |> pass.opentimer.power files:sky130_fd_sc_hd__ff_100C_1v95.lib,counter.vcd odir:tmp
```

4-Check the result power trace: (divided in 100 chunks)
```
gnuplot> plot "tmp/counter.vcd_counter.power.trace" using 1:2 with lines
```

## trivial_and2 to generate VCD and power:

1-Read the liberty file (only once is needed)

```
bazel build -c dbg //main:all
./bazel-bin/main/lgshell
livehd> inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib
livehd> exit
```

2-Generate VCD with verilator

```
verilator -O3 --top-module test_and2 ./pass/opentimer/tests/test_and2.v ../synth/bazel_rules_hdl_test/model/sky130_fd_sc_hd.v --cc --trace --exe ./pass/opentimer/tests/dut_test_and2.cpp
make -C ./obj_dir/ -f Vtest_and2.mk
./obj_dir/Vtest_and2
```

This generated output.vcd that has 3 levels of power activity

3-Read the vcd and compute power trace (this using slang, `inou.yosys` should work too)

```
livehd> inou.liveparse files:pass/opentimer/tests/test_and2.v |> inou.verilog |> pass.compiler |> pass.opentimer.power files:output.vcd,sky130_fd_sc_hd__ff_100C_1v95.lib
...
average activity rate 0.45307482200203425
MAX power switch:6.135333863879571e-15 W internal:3.113571942569545e-14 W voltage:1.95 V
```

The MAX power (ignoring VCD) has dynamic switching W and internal power. The
`output.vcd_test_and2.power.trace` file has a power trace for dynamic power.


## Clock Gating (TODO)

CGE:   Clock Gating Efficiency
DACGE: Data Aware Clock Gating Efficiency
FTR:   Flop Toggle Rate

[1] Srinivas, Jithendra, et al. "Clock gating effectiveness metrics: Applications to power optimization." 2009 10th International Symposium on Quality Electronic Design. IEEE, 2009.

