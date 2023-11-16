# Define the compiler
CXX=clang++

# Define compiler flags, including those from llvm-config
CXXFLAGS=-g -O0 -fuse-ld=lld `llvm-config --cxxflags --ldflags --system-libs --libs core`

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
