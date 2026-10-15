#******************************************************************************
# 32-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_32bit_sctool` etc. (or the plain `make rca_32bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_32bit)

add_executable(rca_32bit rca_glitch.cpp)
svc_target(rca_32bit)

add_executable(cla_32bit cla_glitch.cpp)
svc_target(cla_32bit)

add_executable(ter_32bit ter_glitch.cpp)
svc_target(ter_32bit)

add_executable(sdtc_pm_32bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_32bit)

add_executable(sdtc_ppm_32bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_32bit)
