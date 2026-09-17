CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_rca run_ternary run_cla run_sdtc_pm run_sdtc_ppm

rca_32bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_32bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_32bit
	@echo "\n======================================================="
	@echo " STARTING 32-BIT RIPPLE CARRY ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./rca_32bit

ter_32bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_32bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_32bit
	@echo "\n======================================================="
	@echo " STARTING 32-TRIT BALANCED TERNARY ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./ter_32bit

cla_32bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_32bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_32bit
	@echo "\n======================================================="
	@echo " STARTING 32-BIT CARRY LOOKAHEAD ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./cla_32bit

sdtc_pm_32bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_32bit sdtc_glitch_pm.cpp $(LDFLAGS)

run_sdtc_pm: sdtc_pm_32bit
	@echo "\n======================================================="
	@echo " STARTING 32-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PM) MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_pm_32bit

sdtc_ppm_32bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_32bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc_ppm: sdtc_ppm_32bit
	@echo "\n======================================================="
	@echo " STARTING 32-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PPM) MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_ppm_32bit

clean:
	rm -f rca_32bit ter_32bit cla_32bit sdtc_pm_32bit sdtc_ppm_32bit
