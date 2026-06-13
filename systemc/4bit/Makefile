CXX = g++
CXXFLAGS = -I$(SYSTEMC_HOME)/include
LDFLAGS = -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc

all: run_rca run_ternary

rca_4bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_4bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_4bit
	@echo "\n======================================================="
	@echo " STARTING 4-BIT RIPPLE CARRY ADDER SIMULATION"
	@echo "======================================================="
	./rca_4bit

ter_4bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_4bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_4bit
	@echo "\n======================================================="
	@echo " STARTING 4-TRIT BALANCED TERNARY ADDER SIMULATION"
	@echo "======================================================="
	./ter_4bit

clean:
	rm -f rca_4bit ter_4bit
