CXXFLAGS=-std=c++17 -O3 -I../../

sample: livesim_types.hpp.gch main.o  sample1_stage.o  sample2_stage.o  sample3_stage.o  sample_stage.o
	g++ $(CXXFLAGS) -o $@ main.o  sample1_stage.o  sample2_stage.o  sample3_stage.o  sample_stage.o

livesim_types.hpp.gch: livesim_types.hpp
	g++ $(CXXFLAGS) -x c++-header -c livesim_types.hpp

clean:
	@rm -f sample
	@rm -f perf.*
	@rm -f *.o *.gch

