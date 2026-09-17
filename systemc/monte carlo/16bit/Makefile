CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_rca run_ternary run_cla run_sdtc_pm run_sdtc_ppm

rca_16bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_16bit rca_glitch.cpp $(LDFLAGS)

run_rca: rca_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-BIT RIPPLE CARRY ADDER SIMULATION"
	@echo "======================================================="
	./rca_16bit

ter_16bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_16bit ter_glitch.cpp $(LDFLAGS)

run_ternary: ter_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-TRIT BALANCED TERNARY ADDER SIMULATION"
	@echo "======================================================="
	./ter_16bit

cla_16bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_16bit cla_glitch.cpp $(LDFLAGS)

run_cla: cla_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-BIT CARRY LOOKAHEAD ADDER SIMULATION"
	@echo "======================================================="
	./cla_16bit

sdtc_pm_16bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_16bit sdtc_glitch_pm.cpp $(LDFLAGS)

run_sdtc_pm: sdtc_pm_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PM) SIMULATION"
	@echo "======================================================="
	./sdtc_pm_16bit

sdtc_ppm_16bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_16bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc_ppm: sdtc_ppm_16bit
	@echo "\n======================================================="
	@echo " STARTING 16-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER (PPM) SIMULATION"
	@echo "======================================================="
	./sdtc_ppm_16bit

clean:
	rm -f rca_16bit ter_16bit cla_16bit sdtc_pm_16bit sdtc_ppm_16bit
