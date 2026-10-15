CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

# Plain-SystemC sanity build only (elaborates + runs each design with no
# stimulus). For the actual SystemC-to-Verilog flow, use CMakeLists.txt
# with ICSC (`cmake . && make rca_8bit_sctool` etc., once ICSC_HOME is set).

all: rca_8bit cla_8bit ter_8bit sdtc_pm_8bit sdtc_ppm_8bit
	./rca_8bit && ./cla_8bit && ./ter_8bit && ./sdtc_pm_8bit && ./sdtc_ppm_8bit

rca_8bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_8bit rca_glitch.cpp $(LDFLAGS)

cla_8bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_8bit cla_glitch.cpp $(LDFLAGS)

ter_8bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_8bit ter_glitch.cpp $(LDFLAGS)

sdtc_pm_8bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_8bit sdtc_glitch_pm.cpp $(LDFLAGS)

sdtc_ppm_8bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_8bit sdtc_glitch_ppm.cpp $(LDFLAGS)

clean:
	rm -f rca_8bit cla_8bit ter_8bit sdtc_pm_8bit sdtc_ppm_8bit
