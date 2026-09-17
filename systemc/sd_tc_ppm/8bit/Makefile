CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_sdtc

sdtc_8bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_8bit sdtc_glitch_ppm.cpp $(LDFLAGS)

run_sdtc: sdtc_8bit
	@echo "\n======================================================="
	@echo " STARTING 8-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_8bit

clean:
	rm -f sdtc_8bit
