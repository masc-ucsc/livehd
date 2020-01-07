
all: vsample

vsample:
	verilator -O3 -CFLAGS "-O3" sample.v sample1.v sample2.v sample3.v --cc --exe dut_sample.cpp
	make -j -C obj_dir -f Vsample.mk Vsample
	cp -f obj_dir/Vsample vsample

vsample_vcd:
	verilator -O3 -CFLAGS "-O3 -DTRACE" sample.v sample1.v sample2.v sample3.v --cc --trace --exe dut_sample.cpp
	make -j -C obj_dir -f Vsample.mk Vsample
	cp -f obj_dir/Vsample vsample_vcd

clean:
	rm -f vsample vsample_vcd
	rm -rf obj_dir
	rm -f output.vcd
