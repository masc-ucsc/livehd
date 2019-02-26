CC=g++
CXXFLAGS=-g -I. -I/usr/lib/jvm/default-java/include -ldl -std=c++11 -I/usr/lib/jvm/java-11-openjdk-amd64/include/linux/
LDFLAGS=-L/usr/lib/jvm/java-11-openjdk-amd64/lib/server/ -ljvm
SRC= hello.o
VPATH=tests

%.o: %.cpp
	$(CC) -c -o $@ $< $(CXXFLAGS)

hello: $(SRC)
	$(CC) -o hello $(SRC) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm $(SRC) hello
