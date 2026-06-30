CXX = g++
CXXFLAGS = -I$(SYSTEMC_HOME)/include
LDFLAGS = -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc

all: run_rca run_ternary run_cla

rca_12bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_12bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT RIPPLE CARRY ADDER SIMULATION"
	@echo "======================================================="
	./rca_12bit

ter_12bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_12bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-TRIT BALANCED TERNARY ADDER SIMULATION"
	@echo "======================================================="
	./ter_12bit

cla_12bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_12bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT CARRY LOOKAHEAD ADDER SIMULATION"
	@echo "======================================================="
	./cla_12bit

clean:
	rm -f rca_12bit ter_12bit cla_12bit
