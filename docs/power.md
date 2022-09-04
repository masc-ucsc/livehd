# Power modeling

LiveHD is starting to generate power. Use it at your own risk.

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

1- Read liberty file
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

2- Read netlist and vcd:

```
livehd> inou.verilog files:counter.v |> pass.compiler |> pass.opentimer.power files:sky130_fd_sc_hd__ff_100C_1v95.lib,counter.vcd odir:tmp
```

3-Check the result power trace: (divided in 100 chunks)

```
gnuplot> plot "tmp/counter.vcd_counter.power.trace" using 1:2 with lines
```

