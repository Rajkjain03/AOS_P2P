
CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread  

TARGET = tracker

SOURCES = tracker.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)