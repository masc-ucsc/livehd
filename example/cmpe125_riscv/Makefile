all: dut

LIB_DEPS=-lrt -lcurl -lcrypto


VERILOG_FILES=./rtl/pipe.sv rtl/decode_block_data.sv   rtl/pyrm_execute_block.sv	rtl/pyrm_write_back_block.sv rtl/fetch_block_icache.sv  rtl/pyrm_decode_block.sv  rtl/pyrm_fetch_block.sv	rtl/write_back_block_dcache.sv

VERILATOR_ARGS=-O3 -I. -Irtl -Icpp/riscv_verify


CFLAGS=-DTRACE=1 -DVL_DEBUG=1 -std=c++11 -O3 $(USR_FLAGS) -DPYR_VERILOG -I. -I.. -L.
CFLAGS+=-I../cpp/riscv_verify $(LIB_DEPS)


CPP_FILES=cpp/dut_main_verilator.cpp



LDFLAGS=-LDFLAGS "$(LIB_DEPS) -L."


dut: $(CPP_FILES) $(VERILOG_FILES) cpp/riscv_verify/libriscv_checker.a
	verilator -I./rtl $(VERILATOR_ARGS) $(LIB_OBJS) --cc --trace $(VERILOG_FILES) --exe $(CPP_FILES) -CFLAGS "$(CFLAGS)" ../cpp/riscv_verify/libriscv_checker.a ../cpp/riscv_verify/riscvdis/disasm.o ../cpp/riscv_verify/riscvdis/regnames.o $(LDFLAGS)
	make -C obj_dir/ -f Vpipe.mk Vpipe
	cp -f obj_dir/Vpipe dut

cpp/riscv_verify/libriscv_checker.a: cpp/riscv_verify/Makefile
	make -C cpp/riscv_verify

clean:
	rm -rf obj_dir output.vcd a.out dut
	make -C cpp/riscv_verify clean


