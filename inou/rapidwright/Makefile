# Example Makefile to compile a RapidWright shared library for use in C++.
# Author: Chris Lavin (Xilinx Inc.)
# 5/31/2019

# to run from scratch just run 'make' in this directory
# This Makefile should come with the files:
# (1) RapidWrightAPI.java
# (2) testAPI.cpp

GRAAL_PATH := $(shell pwd)/graalvm-ce-19.1.1/bin
RAPIDWRIGHT_PATH := $(shell pwd)/RapidWright
JAR_PATH=$(RAPIDWRIGHT_PATH)/jars

.PHONY: all run run_RapidWrightExample

####################################################################################################

# This target will run all steps in order (installing RapidWright and Graal, updating RapidWright, compiling the .so and the compiling and running the example)
all: testAPI

# Adds an example file and compiles it for use with the C++ calls to the RapidWright project/classpath
$(RAPIDWRIGHT_PATH)/bin/com/xilinx/rapidwright/examples/RapidWrightAPI.class: RapidWrightAPI.java
	cp RapidWrightAPI.java $(RAPIDWRIGHT_PATH)/src/com/xilinx/rapidwright/examples
	$(GRAAL_PATH)/javac -cp $(RAPIDWRIGHT_PATH)/bin:$(shell find $(JAR_PATH) -name '*.jar' | grep -Ev 'jython|jupyter|win64|jeromq|json|junit' | tr '\n' ':') $(RAPIDWRIGHT_PATH)/src/com/xilinx/rapidwright/examples/RapidWrightAPI.java -d $(RAPIDWRIGHT_PATH)/bin

# Compiles the shared library (.so) for RapidWright
librapidwright.so: $(RAPIDWRIGHT_PATH)/bin/com/xilinx/rapidwright/examples/RapidWrightAPI.class
	$(GRAAL_PATH)/native-image --no-server -cp $(RAPIDWRIGHT_PATH)/bin:$(shell find $(JAR_PATH) -name '*.jar' | grep -Ev 'jython|jupyter|win64|jeromq|json|junit' | tr '\n' ':') --no-fallback --initialize-at-build-time --shared -H:Name=librapidwright

# Compiles a small C++ example program to test our RapidWright shared library
RapidWrightExample: librapidwright.so RapidWrightExample.cpp
	g++ -o RapidWrightExample RapidWrightExample.cpp -I. -L. -lrapidwright

testAPI: librapidwright.so testAPI.cpp
	g++ -o testAPI testAPI.cpp -I. -L. -lrapidwright

# Runs the example C++ program, should generte two dcp files separately
run: testAPI
		export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH && export RAPIDWRIGHT_PATH=$(RAPIDWRIGHT_PATH) && ./testAPI
