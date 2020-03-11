# Example Makefile to compile a RapidWright shared library for use in C++.
# Author: Chris Lavin (Xilinx Inc.)
# 5/31/2019

# to run from scratch just run 'make' in this directory
# This Makefile should come with the files:
# (1) RapidWrightAPI.java
# (2) RapidWrightExample.cpp

# GRAAL_PATH := $(shell pwd)/graalvm-ce-19.2.1/bin
# RAPIDWRIGHT_PATH := $(shell pwd)/RapidWright
JAR_PATH=$(RAPIDWRIGHT_PATH)/jars
# LiveHD_PATH := $(shell pwd)/livehd/inou/rapidwright

.PHONY: all run run_RapidWrightExample

####################################################################################################

# This target will run all steps in order (installing RapidWright and Graal, updating RapidWright, compiling the .so and the compiling and running the example)
all: testAPI


# # Installs RapidWright with source code
# install_rapidwright:
# 	wget http://www.rapidwright.io/docs/_downloads/rapidwright-installer.jar
# 	PATH="$(GRAAL_PATH):$(PATH)" && java -jar rapidwright-installer.jar -t -k

# # Removes the one file dependency in RapidWright on Jython and removes unnecessary jars
# remove_jython:
# 	rm $(JAR_PATH)/jython-standalone-2.7.0.jar
# 	rm $(JAR_PATH)/jupyter-kernel-jsr223.jar
# 	rm $(JAR_PATH)/jeromq-0.3.6.jar
# 	rm $(JAR_PATH)/junit-4.12.jar
# 	rm $(RAPIDWRIGHT_PATH)/bin/com/xilinx/rapidwright/util/RapidWright.class
# 	rm $(RAPIDWRIGHT_PATH)/src/com/xilinx/rapidwright/util/RapidWright.java

# Adds an example file and compiles it for use with the C++ calls to the RapidWright project/classpath
$(RAPIDWRIGHT_PATH)/bin/com/xilinx/rapidwright/examples/RapidWrightAPI.class: RapidWrightAPI.java
	cp $(shell pwd)/RapidWrightAPI.java $(RAPIDWRIGHT_PATH)/src/com/xilinx/rapidwright/examples
	$(GRAAL_PATH)/javac -cp $(RAPIDWRIGHT_PATH)/bin:$(shell find $(JAR_PATH) -name '*.jar' | grep -Ev 'jython|jupyter|win64|jeromq|json|junit' | tr '\n' ':') $(RAPIDWRIGHT_PATH)/src/com/xilinx/rapidwright/examples/RapidWrightAPI.java -d $(RAPIDWRIGHT_PATH)/bin

# Compiles the shared library (.so) for RapidWright
librapidwright.so: $(RAPIDWRIGHT_PATH)/bin/com/xilinx/rapidwright/examples/RapidWrightAPI.class
	$(GRAAL_PATH)/native-image --no-server -cp $(RAPIDWRIGHT_PATH)/bin:$(shell find $(JAR_PATH) -name '*.jar' | grep -Ev 'jython|jupyter|win64|jeromq|json|junit' | tr '\n' ':') --no-fallback --initialize-at-build-time --shared -H:Name=librapidwright

# Compiles a small C++ example program to test our RapidWright shared library
RapidWrightExample: librapidwright.so RapidWrightExample.cpp
	g++ -o RapidWrightExample RapidWrightExample.cpp -I. -L. -lrapidwright

testAPI: librapidwright.so testAPI.cpp
	g++ -o testAPI testAPI.cpp -I. -L. -lrapidwright

# duplicateFiles:
# 	#rm $(LiveHD_PATH)/librapidwright_dynamic.h #$(LiveHD_PATH)/librapidwright.h $(LiveHD_PATH)/RapidWrightAPI.java $(LiveHD_PATH)/testAPI.cpp $(LiveHD_PATH)/Makefile
# 	cp *.java $(LiveHD_PATH)
# 	cp *.h $(LiveHD_PATH)
# 	cp *.hpp $(LiveHD_PATH)
# 	cp *.cpp $(LiveHD_PATH)
# 	cp Makefile $(LiveHD_PATH)

# Runs the example C++ program, should generte two dcp files separately
run: testAPI
		export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH && export RAPIDWRIGHT_PATH=$(RAPIDWRIGHT_PATH) && ./testAPI

clean:
	rm *.so librapidwright.h graal_isolate.h graal_isolate_dynamic.h librapidwright_dynamic.h
