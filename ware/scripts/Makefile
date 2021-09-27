#------------------------------------------------------------------------------
#  Makefile for cgen_memory_generate.cpp
#
#  make                makes cgen_memory_generate
#  make clean          removes binary files and verilog files
#------------------------------------------------------------------------------

CC = g++
CFLAGS = -std=c++11 -Wall

cgen_memory_generate : cgen_memory_generate.o 
	$(CC) $(CFLAGS) -o cgen_memory_generate cgen_memory_generate.o 
cgen_memory_generate.o : cgen_memory_generate.cpp 
	$(CC) $(CFLAGS) -c cgen_memory_generate.cpp

clean :
	rm -f cgen_memory_generate cgen_memory_generate.o *.v 
