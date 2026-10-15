#******************************************************************************
# 12-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_12bit_sctool` etc. (or the plain `make rca_12bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_12bit)

add_executable(rca_12bit rca_glitch.cpp)
svc_target(rca_12bit)

add_executable(cla_12bit cla_glitch.cpp)
svc_target(cla_12bit)

add_executable(ter_12bit ter_glitch.cpp)
svc_target(ter_12bit)

add_executable(sdtc_pm_12bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_12bit)

add_executable(sdtc_ppm_12bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_12bit)
