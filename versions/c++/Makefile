# MAKEFILE FOR THE C++ VERSION: MAKE | MAKE WINDOWS (MINGW-W64 ON LINUX) | MAKE CLEAN
CXX ?= c++
MINGW_CXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
SRC = src/server.cpp src/engine.cpp src/sys.cpp
HDR = src/engine.hpp src/json.hpp src/sys.hpp src/embedded.hpp

all: bin/evaluator-cpp

bin/evaluator-cpp: $(SRC) $(HDR)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lpthread

windows: bin/evaluator-cpp.exe

bin/evaluator-cpp.exe: $(SRC) $(HDR)
	mkdir -p bin
	$(MINGW_CXX) $(CXXFLAGS) -o $@ $(SRC) -lws2_32 -lshell32 -static -static-libgcc -static-libstdc++

clean:
	rm -f bin/evaluator-cpp bin/evaluator-cpp.exe

.PHONY: all windows clean
