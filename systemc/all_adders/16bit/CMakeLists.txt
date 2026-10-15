#******************************************************************************
# 16-bit adders: regular (RCA), carry-lookahead (CLA), balanced ternary
# (TER), and SD/TC (PM, PPM). Each .cpp is independently synthesizable --
# run `make rca_16bit_sctool` etc. (or the plain `make rca_16bit` for a
# normal SystemC simulation build) once ICSC is installed.
#******************************************************************************
project(all_adders_16bit)

add_executable(rca_16bit rca_glitch.cpp)
svc_target(rca_16bit)

add_executable(cla_16bit cla_glitch.cpp)
svc_target(cla_16bit)

add_executable(ter_16bit ter_glitch.cpp)
svc_target(ter_16bit)

add_executable(sdtc_pm_16bit sdtc_glitch_pm.cpp)
svc_target(sdtc_pm_16bit)

add_executable(sdtc_ppm_16bit sdtc_glitch_ppm.cpp)
svc_target(sdtc_ppm_16bit)
