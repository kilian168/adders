CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

# Plain-SystemC sanity build only (elaborates + runs each design with no
# stimulus). For the actual SystemC-to-Verilog flow, use CMakeLists.txt
# with ICSC (`cmake . && make rca_64bit_sctool` etc., once ICSC_HOME is set).

all: rca_64bit cla_64bit ter_64bit sdtc_pm_64bit sdtc_ppm_64bit
	./rca_64bit && ./cla_64bit && ./ter_64bit && ./sdtc_pm_64bit && ./sdtc_ppm_64bit

rca_64bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_64bit rca_glitch.cpp $(LDFLAGS)

cla_64bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_64bit cla_glitch.cpp $(LDFLAGS)

ter_64bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_64bit ter_glitch.cpp $(LDFLAGS)

sdtc_pm_64bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_64bit sdtc_glitch_pm.cpp $(LDFLAGS)

sdtc_ppm_64bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_64bit sdtc_glitch_ppm.cpp $(LDFLAGS)

clean:
	rm -f rca_64bit cla_64bit ter_64bit sdtc_pm_64bit sdtc_ppm_64bit
