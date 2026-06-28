CXX = g++
CXXFLAGS = -I$(SYSTEMC_HOME)/include
LDFLAGS = -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc

all: run_rca run_ternary run_cla

rca_2bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_2bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_2bit
	@echo "\n======================================================="
	@echo " STARTING 2-BIT RIPPLE CARRY ADDER SIMULATION"
	@echo "======================================================="
	./rca_2bit

ter_2bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_2bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_2bit
	@echo "\n======================================================="
	@echo " STARTING 2-TRIT BALANCED TERNARY ADDER SIMULATION"
	@echo "======================================================="
	./ter_2bit

cla_2bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_2bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_2bit
	@echo "\n======================================================="
	@echo " STARTING 2-BIT CARRY LOOKAHEAD ADDER SIMULATION"
	@echo "======================================================="
	./cla_2bit

clean:
	rm -f rca_2bit ter_2bit cla_2bit
