CXX=g++
RVEMU_OBJS=$(addprefix riscvemu/,\
cutils.o   fs_net.o	 iomem.o	   riscvemu.o		    virtio.o \
fs.o	    fs_utils.o  riscv_cpu64.o    softfp.o \
fs_disk.o  fs_wget.o	 riscv_machine.o)


RVEMU_LIBS=-lrt -lcurl

#RVVERIFY_OBJS=riscv_verify.o
RVVERIFY_OBJS=riscv_checker.o riscv_verify.o
CFLAGS=-O2 -Wall -Wno-unused-result -I./riscvemu -fPIC -std=c++11

all: build_riscvemu $(RVVERIFY_OBJS) libriscv_checker.a

debug: CFLAGS += -g
debug: all

build_riscvemu:
	make -C riscvemu
	make -C riscvdis

libriscv_checker.a: $(RVVERIFY_OBJS) $(RVEMU_OBJS)
	ar cr libriscv_checker.a $(RVVERIFY_OBJS) $(RVEMU_OBJS)

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $<

clean:
	make -C riscvemu clean
	make -C riscvdis clean
	rm -f *.o libriscv_checker.a

