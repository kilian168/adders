CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

# Plain-SystemC sanity build only (elaborates + runs each design with no
# stimulus). For the actual SystemC-to-Verilog flow, use CMakeLists.txt
# with ICSC (`cmake . && make rca_12bit_sctool` etc., once ICSC_HOME is set).

all: rca_12bit cla_12bit ter_12bit sdtc_pm_12bit sdtc_ppm_12bit
	./rca_12bit && ./cla_12bit && ./ter_12bit && ./sdtc_pm_12bit && ./sdtc_ppm_12bit

rca_12bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_12bit rca_glitch.cpp $(LDFLAGS)

cla_12bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_12bit cla_glitch.cpp $(LDFLAGS)

ter_12bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_12bit ter_glitch.cpp $(LDFLAGS)

sdtc_pm_12bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_12bit sdtc_glitch_pm.cpp $(LDFLAGS)

sdtc_ppm_12bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_12bit sdtc_glitch_ppm.cpp $(LDFLAGS)

clean:
	rm -f rca_12bit cla_12bit ter_12bit sdtc_pm_12bit sdtc_ppm_12bit
