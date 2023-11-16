# Define the compiler
CXX=clang++

# Define compiler flags, including those from llvm-config
CXXFLAGS=-g -O0 `llvm-config --cxxflags`

# Define the target executable
TARGET=parser

# Define the source file
SRC=parser.cpp

# Default rule for building the executable
all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Clean up the build
clean:
	rm -f $(TARGET)
