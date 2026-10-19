CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_ternary

ter_2bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_2bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_2bit
	@echo "\n======================================================="
	@echo " STARTING 2-BIT TERNARY ADDER SIMULATION (base3)"
	@echo "======================================================="
	./ter_2bit

clean:
	rm -f ter_2bit
