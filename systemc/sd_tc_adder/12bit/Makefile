CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_sdtc

sdtc_12bit: sdtc_glitch.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_12bit sdtc_glitch.cpp $(LDFLAGS)

run_sdtc: sdtc_12bit
	@echo "\n======================================================="
	@echo " STARTING 12-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_12bit

clean:
	rm -f sdtc_12bit
