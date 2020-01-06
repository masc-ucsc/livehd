
sample: main.cpp  sample1_stage.cpp  sample2_stage.cpp  sample3_stage.cpp  sample_stage.cpp
	g++ -std=c++17 -O3 -o $@ $?

samplev: sample.v
	vcs -full64 -sverilog sample.v
	./simv

clean:
	@rm -f sample

