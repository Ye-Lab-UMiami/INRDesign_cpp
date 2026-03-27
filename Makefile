CXX      = g++
CXXFLAGS = -std=c++17 -O3 -march=native -flto -DNDEBUG
TARGET   = twostage_inr

HEADERS  = stat_math.h twostage_core.h calc_opchar.h
SOURCES  = main.cpp

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

debug: CXXFLAGS = -std=c++17 -g -O0 -fsanitize=address -Wall -Wextra
debug: $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all debug clean
