CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic

all: activity1

activity1: activity1.cpp sketches.hpp sliding_window.hpp
	$(CXX) $(CXXFLAGS) -o activity1 activity1.cpp

clean:
	rm -f activity1
