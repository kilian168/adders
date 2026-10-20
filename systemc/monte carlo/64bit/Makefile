CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_rca run_ternary run_cla run_sdtc_pm run_sdtc_ppm

rca_64bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_64bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_64bit
	@echo "\n======================================================="
	@echo " STARTING 64-BIT RIPPLE CARRY ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./rca_64bit

ter_64bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_64bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_64bit
	@echo "\n======================================================="
	@echo " STARTING 64-DIGIT SIGNED-DIGIT ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./ter_64bit

cla_64bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_64bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_64bit
	@echo "\n======================================================="
	@echo " STARTING 64-BIT CARRY LOOKAHEAD ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./cla_64bit

sdtc_pm_64bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_64bit sdtc_glitch_pm.cpp $(LDFLAGS)

run_sdtc_pm: sdtc_pm_64bit
	@echo "\n======================================================="
	@echo " STARTING 64-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PM) MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_pm_64bit

sdtc_ppm_64bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_64bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc_ppm: sdtc_ppm_64bit
	@echo "\n======================================================="
	@echo " STARTING 64-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PPM) MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_ppm_64bit

clean:
	rm -f rca_64bit ter_64bit cla_64bit sdtc_pm_64bit sdtc_ppm_64bit
