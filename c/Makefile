# Makefile — varianta C.   make  |  make windows (MinGW-w64 pe Linux)  |  make clean
CC ?= cc
MINGW_CC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC = src/server.c src/engine.c src/json.c src/util.c src/runner.c
HDR = src/engine.h src/json.h src/util.h src/runner.h src/embedded.h

all: bin/evaluator-c

bin/evaluator-c: $(SRC) $(HDR)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(SRC) -lpthread -lm

windows: bin/evaluator-c.exe

bin/evaluator-c.exe: $(SRC) $(HDR)
	mkdir -p bin
	$(MINGW_CC) $(CFLAGS) -o $@ $(SRC) -lws2_32 -lshell32 -static

clean:
	rm -f bin/evaluator-c bin/evaluator-c.exe

.PHONY: all windows clean
