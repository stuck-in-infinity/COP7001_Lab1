# Makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
SRC = shell.cpp
BIN = shell

all: $(BIN)

$(BIN): $(SRC)
	
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

clean:
	rm -rf shell

.PHONY: all clean
