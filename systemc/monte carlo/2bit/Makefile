CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc


all: run_rca run_ternary run_cla run_sdtc_pm run_sdtc_ppm

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

sdtc_pm_2bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_2bit sdtc_glitch_pm.cpp $(LDFLAGS)

run_sdtc_pm: sdtc_pm_2bit
	@echo "\n======================================================="
	@echo " STARTING 2-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PM) SIMULATION"
	@echo "======================================================="
	./sdtc_pm_2bit

sdtc_ppm_2bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_2bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc_ppm: sdtc_ppm_2bit
	@echo "\n======================================================="
	@echo " STARTING 2-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PPM) SIMULATION"
	@echo "======================================================="
	./sdtc_ppm_2bit

clean:
	rm -f rca_2bit ter_2bit cla_2bit sdtc_pm_2bit sdtc_ppm_2bit
