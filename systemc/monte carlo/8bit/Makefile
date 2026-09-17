CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_rca run_ternary run_cla run_sdtc_pm run_sdtc_ppm

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

sdtc_pm_8bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_8bit sdtc_glitch_pm.cpp $(LDFLAGS)

run_sdtc_pm: sdtc_pm_8bit
	@echo "\n======================================================="
	@echo " STARTING 8-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PM) SIMULATION"
	@echo "======================================================="
	./sdtc_pm_8bit

sdtc_ppm_8bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_8bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc_ppm: sdtc_ppm_8bit
	@echo "\n======================================================="
	@echo " STARTING 8-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PPM) SIMULATION"
	@echo "======================================================="
	./sdtc_ppm_8bit

clean:
	rm -f rca_8bit ter_8bit cla_8bit sdtc_pm_8bit sdtc_ppm_8bit
