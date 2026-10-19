CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_ternary

ter_16bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_16bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-BIT TERNARY ADDER SIMULATION (base3)"
	@echo "======================================================="
	./ter_16bit

clean:
	rm -f ter_16bit
