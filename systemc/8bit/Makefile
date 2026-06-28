CXX = g++
CXXFLAGS = -I$(SYSTEMC_HOME)/include
LDFLAGS = -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc

all: run_rca run_ternary run_cla

rca_8bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_8bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_8bit
	@echo "\n======================================================="
	@echo " STARTING 8-BIT RIPPLE CARRY ADDER SIMULATION"
	@echo "======================================================="
	./rca_8bit

ter_8bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_8bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_8bit
	@echo "\n======================================================="
	@echo " STARTING 8-TRIT BALANCED TERNARY ADDER SIMULATION"
	@echo "======================================================="
	./ter_8bit

cla_8bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_8bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_8bit
	@echo "\n======================================================="
	@echo " STARTING 8-BIT CARRY LOOKAHEAD ADDER SIMULATION"
	@echo "======================================================="
	./cla_8bit

clean:
	rm -f rca_8bit ter_8bit cla_8bit
