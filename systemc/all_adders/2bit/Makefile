CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

# Plain-SystemC sanity build only (elaborates + runs each design with no
# stimulus). For the actual SystemC-to-Verilog flow, use CMakeLists.txt
# with ICSC (`cmake . && make rca_2bit_sctool` etc., once ICSC_HOME is set).

all: rca_2bit cla_2bit ter_2bit sdtc_pm_2bit sdtc_ppm_2bit
	./rca_2bit && ./cla_2bit && ./ter_2bit && ./sdtc_pm_2bit && ./sdtc_ppm_2bit

rca_2bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_2bit rca_glitch.cpp $(LDFLAGS)

cla_2bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_2bit cla_glitch.cpp $(LDFLAGS)

ter_2bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_2bit ter_glitch.cpp $(LDFLAGS)

sdtc_pm_2bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_2bit sdtc_glitch_pm.cpp $(LDFLAGS)

sdtc_ppm_2bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_2bit sdtc_glitch_ppm.cpp $(LDFLAGS)

clean:
	rm -f rca_2bit cla_2bit ter_2bit sdtc_pm_2bit sdtc_ppm_2bit
