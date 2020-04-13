CXXFLAGS=-std=c++17 -O3 -I../../ -I../../../lbench/include
CXXFLAGS+=-g
CXXFLAGS+=-DSIMLIB_TRACE
#CXXFLAGS+=-DSIMLIB_VCD

all: sample

SIMLIB_OBJS=vcd_writer.o
GENERATED_OBJS=sample1_stage.o  sample2_stage.o  sample3_stage.o  sample_stage.o

sample: livesim_types.hpp.gch $(SIMLIB_OBJS) $(GENERATED_OBJS) main.o
	$(CXX) $(CXXFLAGS) -o $@ $(SIMLIB_OBJS) $(GENERATED_OBJS) main.o

# simlib objects
vcd_writer.o:../../vcd_writer.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

vcd_utils.o:../../vcd_utils.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

livesim_types.hpp.gch: livesim_types.hpp
	$(CXX) $(CXXFLAGS) -x c++-header -c livesim_types.hpp

clean:
	@rm -f sample
	@rm -f perf.*
	@rm -f *.o *.gch
	@rm -f check_*.ckpt
	@rm -f ckpt_*
	@rm -f *.vcd

