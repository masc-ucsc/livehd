
CXXFLAGS=-std=c++11 -O2 -fPIC
riscvdis : main.o disasm.o regnames.o
		g++ -fPIC -std=c++11 -O2 -o riscvdis -fPIC $?

clean:
		rm -f riscvdis *.o
