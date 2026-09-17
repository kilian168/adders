CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc


all: run_rca run_ternary run_cla run_sdtc_pm run_sdtc_ppm

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

sdtc_pm_12bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_12bit sdtc_glitch_pm.cpp $(LDFLAGS)

run_sdtc_pm: sdtc_pm_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PM) SIMULATION"
	@echo "======================================================="
	./sdtc_pm_12bit

sdtc_ppm_12bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_12bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc_ppm: sdtc_ppm_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PPM) SIMULATION"
	@echo "======================================================="
	./sdtc_ppm_12bit

clean:
	rm -f rca_12bit ter_12bit cla_12bit sdtc_pm_12bit sdtc_ppm_12bit
