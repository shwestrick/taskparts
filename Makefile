CXX ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Iinclude
LDFLAGS += -pthread

HEADERS := $(wildcard include/taskparts/*.hpp)

all: fib test_pardo

fib: example/fib.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test_pardo: test/test_pardo.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: test_pardo
	./test_pardo

clean:
	rm -rf fib test_pardo *.dSYM

.PHONY: all test clean
