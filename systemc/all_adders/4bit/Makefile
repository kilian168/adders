CXX = g++
CXXFLAGS = -std=c++17 -I/usr/include
LDFLAGS = -L/usr/lib -Wl,-rpath,/usr/lib -lsystemc

# Plain-SystemC sanity build only (elaborates + runs each design with no
# stimulus). For the actual SystemC-to-Verilog flow, use CMakeLists.txt
# with ICSC (`cmake . && make rca_4bit_sctool` etc., once ICSC_HOME is set).

all: rca_4bit cla_4bit ter_4bit sdtc_pm_4bit sdtc_ppm_4bit
	./rca_4bit && ./cla_4bit && ./ter_4bit && ./sdtc_pm_4bit && ./sdtc_ppm_4bit

rca_4bit: rca_glitch.cpp
	$(CXX) $(CXXFLAGS) -o rca_4bit rca_glitch.cpp $(LDFLAGS)

cla_4bit: cla_glitch.cpp
	$(CXX) $(CXXFLAGS) -o cla_4bit cla_glitch.cpp $(LDFLAGS)

ter_4bit: ter_glitch.cpp
	$(CXX) $(CXXFLAGS) -o ter_4bit ter_glitch.cpp $(LDFLAGS)

sdtc_pm_4bit: sdtc_glitch_pm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_pm_4bit sdtc_glitch_pm.cpp $(LDFLAGS)

sdtc_ppm_4bit: sdtc_glitch_ppm.cpp
	$(CXX) $(CXXFLAGS) -o sdtc_ppm_4bit sdtc_glitch_ppm.cpp $(LDFLAGS)

clean:
	rm -f rca_4bit cla_4bit ter_4bit sdtc_pm_4bit sdtc_ppm_4bit
