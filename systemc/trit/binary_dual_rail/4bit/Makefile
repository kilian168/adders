CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_ternary

ter_4bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_4bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_4bit
	@echo "\n======================================================="
	@echo " STARTING 4-BIT TERNARY ADDER SIMULATION (binary_dual_rail)"
	@echo "======================================================="
	./ter_4bit

clean:
	rm -f ter_4bit
