# The verilator testbench flow that lived here was removed together with the
# ware/tests/*_tb.cpp testbenches (commit eb5071007 deleted the testbenches;
# every former target dangled against tests/ files that no longer exist).
# The RTL survives in rtl/ (ware/BUILD exposes rtl/cgen_memory*.v), and the
# cgen memory generator keeps its own flow in scripts/Makefile.

# Keep a harmless default goal: bare `make` used to build+run the benches and
# must not fall through to `clean`.
all:
	@echo "ware verilator testbenches removed; only 'make clean' remains (see header)"

clean:
	rm -rf obj_dir output.vcd a.out wave.fst
