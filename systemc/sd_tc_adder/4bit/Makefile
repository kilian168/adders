CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

all: run_sdtc

sdtc_4bit: sdtc_glitch.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_4bit sdtc_glitch.cpp $(LDFLAGS)

run_sdtc: sdtc_4bit
	@echo "\n======================================================="
	@echo " STARTING 4-BIT SIGNED-DIGIT / TWO'S-COMPLEMENT ADDER MONTE CARLO SIMULATION"
	@echo "======================================================="
	./sdtc_4bit

clean:
	rm -f sdtc_4bit
