#******************************************************************************
# 8-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_8bit_sctool` etc. (or the plain `make rca_8bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_8bit)

add_executable(rca_8bit rca_glitch.cpp)
svc_target(rca_8bit)

add_executable(cla_8bit cla_glitch.cpp)
svc_target(cla_8bit)

add_executable(ter_8bit ter_glitch.cpp)
svc_target(ter_8bit)

add_executable(sdtc_pm_8bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_8bit)

add_executable(sdtc_ppm_8bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_8bit)
