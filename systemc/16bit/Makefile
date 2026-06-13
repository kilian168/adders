CXX = g++
CXXFLAGS = -I$(SYSTEMC_HOME)/include
LDFLAGS = -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc

all: run_rca run_ternary

rca_16bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_16bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-BIT RIPPLE CARRY ADDER SIMULATION"
	@echo "======================================================="
	./rca_16bit

ter_16bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_16bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-TRIT BALANCED TERNARY ADDER SIMULATION"
	@echo "======================================================="
	./ter_16bit

clean:
	rm -f rca_16bit ter_16bit
