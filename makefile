.PHONY: test

CXXFLAGS=-g -std=c++1y
#CXX=clang++-3.8

test: tests
	valgrind -q --leak-check=full --show-reachable=yes ./tests

tests.o: internal_ptr.hpp makefile

tests: tests.o
	$(CXX) $(CXXFLAGS) -o $@ $^
