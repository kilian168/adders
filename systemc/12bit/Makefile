CXX = g++
CXXFLAGS = -I$(SYSTEMC_HOME)/include
LDFLAGS = -L$(SYSTEMC_HOME)/lib-linux64 -lsystemc

all: run_cla

cla_12bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_12bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT CARRY LOOKAHEAD ADDER SIMULATION"
	@echo "======================================================="
	./cla_12bit

clean:
	rm -f cla_12bit
