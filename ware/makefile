
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

add_csa:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module add_csa ./rtl/add_csa.v ./rtl/add_rca.v ./rtl/full_adder.v --exe tests/add_csa_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vadd_csa.mk Vadd_csa

kogg_stone:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module kogg_stone ./rtl/kogg_stone.v ./rtl/pref_op.v ./rtl/diamond.v ./rtl/square_op.v --exe tests/kogg_stone_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vkogg_stone.mk Vkogg_stone

kogg_stone_32:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module kogg_stone_32 ./rtl/kogg_stone_32.v ./rtl/pref_op.v ./rtl/diamond.v ./rtl/square_op.v --exe tests/kogg_stone_32_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vkogg_stone_32.mk Vkogg_stone_32

kogg_stone_64:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module kogg_stone_64 ./rtl/kogg_stone_64.v ./rtl/pref_op.v ./rtl/diamond.v ./rtl/square_op.v --exe tests/kogg_stone_64_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vkogg_stone_64.mk Vkogg_stone_64

kogg_stone_8:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module kogg_stone_8 ./rtl/kogg_stone_8.v ./rtl/pref_op.v ./rtl/diamond.v ./rtl/square_op.v --exe tests/kogg_stone_8_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vkogg_stone_8.mk Vkogg_stone_8

han_carl:
	verilator -sv +incdir+rtl --Wall --cc --trace --top-module han_carl ./rtl/han_carl.v ./rtl/pref_op.v ./rtl/diamond.v ./rtl/square_op.v --exe tests/han_carl_tb.cpp -CFLAGS "-DTRACE=1 -std=c++11"
	make -C obj_dir/ -f Vhan_carl.mk Vhan_carl

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

run8: add_csa
	./obj_dir/Vadd_csa

run9: kogg_stone
	./obj_dir/Vkogg_stone

run10: kogg_stone_32
	./obj_dir/Vkogg_stone_32

run11: kogg_stone_64
	./obj_dir/Vkogg_stone_64

run12: kogg_stone_8
	./obj_dir/Vkogg_stone_8

run13: han_carl
	./obj_dir/Vhan_carl

run: run1 run2 run3 run4 run5 run6 run7 run9 run10 run11 run12 run13

clean:
	rm -rf obj_dir output.vcd a.out

