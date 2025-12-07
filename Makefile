# Makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
SRC = src/shell.cpp
BIN = shell

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o bin/$(BIN) $(SRC)

clean:
	rm -rf bin

.PHONY: all clean
