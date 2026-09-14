CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_rca run_ternary run_cla

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

cla_4bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_4bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_4bit
	@echo "\n======================================================="
	@echo " STARTING 4-BIT CARRY LOOKAHEAD ADDER SIMULATION"
	@echo "======================================================="
	./cla_4bit

clean:
	rm -f rca_4bit ter_4bit cla_4bit
