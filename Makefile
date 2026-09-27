CXX ?= g++
CXXFLAGS ?= -O2 -march=native -std=c++17 -Wall -Wextra

.PHONY: all clean
all: pcap2bin exact_hh test_sketches

pcap2bin: pcap2bin.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

exact_hh: exact_hh.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_sketches: test_sketches.cpp sketches.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f pcap2bin exact_hh test_sketches
