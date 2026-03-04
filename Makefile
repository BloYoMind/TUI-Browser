CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
INCLUDES = -Iinclude
LIBS     = -lcurl

SRC  = src/main.cpp
OUT  = main.exe

all: $(OUT)

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) $(LIBS) -o $(OUT)

clean:
	del /Q $(OUT) 2>nul || rm -f $(OUT)

.PHONY: all clean
