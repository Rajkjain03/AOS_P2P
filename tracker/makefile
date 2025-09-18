# Makefile for the tracker component

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread  # <--- This line is changed

# Target executable name
TARGET = tracker

# Source files
SOURCES = tracker.cpp

# Default target
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

# Rule to clean up build files
clean:
	rm -f $(TARGET)