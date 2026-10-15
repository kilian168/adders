CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

# Plain-SystemC sanity build only (elaborates + runs each design with no
# stimulus). For the actual SystemC-to-Verilog flow, use CMakeLists.txt
# with ICSC (`cmake . && make rca_32bit_sctool` etc., once ICSC_HOME is set).

all: rca_32bit cla_32bit ter_32bit sdtc_pm_32bit sdtc_ppm_32bit
	./rca_32bit && ./cla_32bit && ./ter_32bit && ./sdtc_pm_32bit && ./sdtc_ppm_32bit

rca_32bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_32bit rca_glitch.cpp $(LDFLAGS)

cla_32bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_32bit cla_glitch.cpp $(LDFLAGS)

ter_32bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_32bit ter_glitch.cpp $(LDFLAGS)

sdtc_pm_32bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_32bit sdtc_glitch_pm.cpp $(LDFLAGS)

sdtc_ppm_32bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_32bit sdtc_glitch_ppm.cpp $(LDFLAGS)

clean:
	rm -f rca_32bit cla_32bit ter_32bit sdtc_pm_32bit sdtc_ppm_32bit
