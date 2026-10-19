CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_ternary

ter_12bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_12bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT TERNARY ADDER SIMULATION (base3)"
	@echo "======================================================="
	./ter_12bit

clean:
	rm -f ter_12bit
