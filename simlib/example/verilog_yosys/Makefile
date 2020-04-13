

sample.cc: sample.v sample1.v sample2.v sample3.v
	yosys -p"read_verilog -sv sample*.v ; write_cxxrtl -O5 sample.cc"

sample: sample.cc main.cpp
	$(CXX) -O3 -o sample -I /usr/local/share/yosys/include/backends/cxxrtl main.cpp

clean:
	@rm -rf sample.cc sample

