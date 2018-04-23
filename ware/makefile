
all: run

add_rca:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module add_rca ./rtl/add_rca.v ./rtl/full_adder.v --exe tests/add_rca_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vadd_rca.mk Vadd_rca

add_rca_signed:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module add_rca_signed ./rtl/add_rca_signed.v ./rtl/full_adder.v --exe tests/add_rca_tb_signed.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vadd_rca_signed.mk Vadd_rca_signed

shift_barrelfast_sleft:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module shift_barrelfast_sleft ./rtl/shift_barrelfast_sleft.v --exe tests/shift_barrelfast_sleft_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vshift_barrelfast_sleft.mk Vshift_barrelfast_sleft

shift_barrelfast_sright:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module shift_barrelfast_sright ./rtl/shift_barrelfast_sright.v --exe tests/shift_barrelfast_sright_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vshift_barrelfast_sright.mk Vshift_barrelfast_sright

shift_barrelfast_rright:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module shift_barrelfast_rright ./rtl/shift_barrelfast_rright.v --exe tests/shift_barrelfast_rright_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vshift_barrelfast_rright.mk Vshift_barrelfast_rright

shift_barrelfast_rleft:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module shift_barrelfast_rleft ./rtl/shift_barrelfast_rleft.v --exe tests/shift_barrelfast_rleft_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vshift_barrelfast_rleft.mk Vshift_barrelfast_rleft

mult_booth:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module mult_booth ./rtl/mult_booth.v --exe tests/mult_booth_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vmult_booth.mk Vmult_booth

run1: add_rca
	./obj_dir/Vadd_rca

run2: add_rca_signed
	./obj_dir/Vadd_rca_signed

run3: shift_barrelfast_sleft
	./obj_dir/Vshift_barrelfast_sleft

run4: shift_barrelfast_sright
	./obj_dir/Vshift_barrelfast_sright

run5: shift_barrelfast_rright
	./obj_dir/Vshift_barrelfast_rright

run6: shift_barrelfast_rleft
	./obj_dir/Vshift_barrelfast_rleft

run7: mult_booth
	./obj_dir/Vmult_booth

run: run1 run2 run3 run4 run5 run6 run7

clean:
	rm -rf obj_dir output.vcd a.out

