# Makefile for the client component

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall

# Target executable name
TARGET = client

# Source files
SOURCES = client.cpp

# Default target
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

# Rule to clean up build files
clean:
	rm -f $(TARGET)