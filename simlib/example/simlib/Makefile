CXXFLAGS=-std=c++17 -O3 -I../../ -I../../../lbench/include -DSIMLIB_TRACE
CXXFLAGS+=-g
CXXFLAGS+=-DSIMLIB_VCD
sample: livesim_types.hpp.gch main.o  sample1_stage.o  sample2_stage.o  sample3_stage.o  sample_stage.o ../../vcd_writer.o
	g++ $(CXXFLAGS) -o $@ main.o  sample1_stage.o  sample2_stage.o  sample3_stage.o  sample_stage.o ../../vcd_writer.o

livesim_types.hpp.gch: livesim_types.hpp
	g++ $(CXXFLAGS) -x c++-header -c livesim_types.hpp

clean:
	@rm -f sample
	@rm -f perf.*
	@rm -f *.o *.gch ../../vcd*.o
	@rm -f check_*.ckpt
	@rm -f ckpt_*
	@rm -f *.vcd

